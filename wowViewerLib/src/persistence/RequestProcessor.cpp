#include <mutex>
#include <vector>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
#include "RequestProcessor.h"

std::mutex setProcessingMtx;     // mutex for critical section

namespace {
/*
robots@Impl: A line on cout, at most once a second while files are processed, beside Map's post-load line: how many files the
loader finished, the longest the queue was, how long a file waited from request to its turn (p50 and max), how long the loader
was busy, in all and per kind with the count, and where the time went: the override check, the lookup of the file's archive
and offset (what the read took beyond the other parts), opening the archive, reading the bytes, inflating them and parsing. A
request whose owner was freed before its turn is dropped unread and counted apart. Busy near the whole second means the one
loader thread is the limit; a long wait with a short busy means the files were requested late. Lookup, inflate and parse are
CPU; open and read are the file system. Busy sums every loader thread, so with two it can pass the second. The threads share
the tally under its mutex.
*/
struct LoaderTally {
    static constexpr const char *kindNames[] = {
        "m2", "skin", "anim", "bone", "skel", "wmo", "wmo group", "adt", "wdt", "wdt light", "wdl", "blp", "db2"};
    static constexpr size_t kindCount = std::size(kindNames);

    struct Kind {
        int files = 0;
        std::chrono::steady_clock::duration overrides{};
        std::chrono::steady_clock::duration lookup{};
        std::chrono::steady_clock::duration open{};
        std::chrono::steady_clock::duration read{};
        std::chrono::steady_clock::duration inflate{};
        std::chrono::steady_clock::duration parse{};

        std::chrono::steady_clock::duration busy() const { return overrides + lookup + open + read + inflate + parse; }

        void add(const Kind &other) {
            files += other.files;
            overrides += other.overrides;
            lookup += other.lookup;
            open += other.open;
            read += other.read;
            inflate += other.inflate;
            parse += other.parse;
        }

        void print(std::ostream &line) const {
            line << "overrides " << ms(overrides) << ", lookup " << ms(lookup) << ", open " << ms(open) << ", read "
                 << ms(read) << ", inflate " << ms(inflate) << ", parse " << ms(parse) << " ms";
        }
    };

    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    std::array<Kind, kindCount> kinds{};
    std::vector<float> waitsMs;
    int dropped = 0;
    int mostQueued = 0;

    static double ms(std::chrono::steady_clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    }

    void note(CacheHolderType holderType, std::chrono::steady_clock::duration wait, const Kind &file, int queued) {
        kinds[std::min<size_t>(static_cast<size_t>(holderType), kindCount - 1)].add(file);
        waitsMs.push_back(static_cast<float>(ms(wait)));
        mostQueued = std::max(mostQueued, queued);
        flush();
    }

    void noteDropped(int queued) {
        dropped++;
        mostQueued = std::max(mostQueued, queued);
        flush();
    }

    void flush() {
        const auto now = std::chrono::steady_clock::now();
        if (now - since < std::chrono::seconds(1)) return;
        if (!waitsMs.empty() || dropped > 0) {
            Kind all;
            for (auto const &kind : kinds) all.add(kind);

            float p50 = 0, most = 0;
            if (!waitsMs.empty()) {
                most = *std::max_element(waitsMs.begin(), waitsMs.end());
                auto mid = waitsMs.begin() + waitsMs.size() / 2;
                std::nth_element(waitsMs.begin(), mid, waitsMs.end());
                p50 = *mid;
            }

            std::ostringstream line;
            line.setf(std::ios::fixed);
            line.precision(0);
            line << "loader: " << waitsMs.size() << " files over " << std::setprecision(2)
                 << std::chrono::duration<double>(now - since).count() << std::setprecision(0) << " s, queue up to "
                 << mostQueued << ", wait p50 " << p50 << " ms max " << most << " ms, busy " << ms(all.busy()) << " ms (";
            all.print(line);
            line << ")";
            const char *separator = "; ";
            for (size_t i = 0; i < kindCount; i++) {
                auto const &kind = kinds[i];
                if (kind.files == 0) continue;
                line << separator << kindNames[i] << " " << kind.files << " (";
                kind.print(line);
                line << ")";
                separator = ", ";
            }
            line << "; " << dropped << " dropped";
            std::cout << line.str() << std::endl;
        }
        *this = LoaderTally();
    }
};
LoaderTally g_loaderTally;
std::mutex g_loaderTallyMutex;
}

thread_local std::chrono::steady_clock::duration RequestProcessor::m_parseTime{};
thread_local RequestProcessor::ReadSplit RequestProcessor::m_readSplit{};

//1. Add request to FIFO
//2. Get filename from FIFO
//3. Add result to ResultFIFO
//4. Get ready results from FIFO
void
RequestProcessor::requestFile(CacheHolderType holderType, std::weak_ptr<PersistentFile> w_file) {
#if CHECK_FOR_DUPLICATES == 1
    std::unique_lock<std::mutex> setLck (setProcessingMtx,std::defer_lock);
    setLck.lock();

    auto s_file = w_file.lock();

    if (currentlyProcessingFnames.count(fileName) > 0) {
        uint32_t fileDataId = 0;
        if (fileName.find("File") == 0) {
            std::stringstream ss;
            std::string fileDataIdHex = fileName.substr(4, fileName.find(".") - 4);

            ss << std::hex << fileDataIdHex;
            ss >> fileDataId;
        }
        std::cout << "RequestProcessor::addRequest : duplicate detected for fileName = " << fileName
                  << " " << ((fileDataId > 0) ? ("(fileDataId = "+std::to_string(fileDataId)+")"): "")
                  << " holderTypeReq = " << (int)holderType
                  <<std::endl;
        return;
    }
    currentlyProcessingFnames.insert(fileName);

    setLck.unlock();
#endif

    std::shared_ptr<FileRank> rank;
    if (auto s_file = w_file.lock()) rank = s_file->rank();
    {
        std::lock_guard<std::mutex> lock(m_requestQueueMutex);
        m_requestQueue.push_back({holderType, w_file, std::chrono::steady_clock::now(), rank});
    }
    m_requestQueueSignal.notify_one();
    toBeProcessed++;
}
void RequestProcessor::iterateAllFiles( std::unique_ptr<IterateFilesRequest> &iterateFilesRequest ) {
    {
        std::lock_guard<std::mutex> lock(m_iterateMutex);
        m_iterateFilesRequest = std::move(iterateFilesRequest);
    }

    {
        std::lock_guard<std::mutex> lock(m_requestQueueMutex);
        m_requestQueue.push_back({});
    }
    m_requestQueueSignal.notify_one();
}

void RequestProcessor::processRequests (int limit) {
    using namespace std::chrono_literals;
    // critical section (exclusive access to std::cout signaled by locking lck):
    std::unique_lock<std::mutex> setLck (setProcessingMtx,std::defer_lock);

    //Taken under the lock, so one loader thread runs it
    std::unique_ptr<IterateFilesRequest> iterateFilesRequest;
    {
        std::lock_guard<std::mutex> lock(m_iterateMutex);
        iterateFilesRequest = std::move(m_iterateFilesRequest);
    }
    if (iterateFilesRequest != nullptr) {
        iterateFilesInternal(iterateFilesRequest->process, iterateFilesRequest->callback);
    }

    if (m_threaded) {
        {
            std::unique_lock<std::mutex> lock(m_requestQueueMutex);
            m_requestQueueSignal.wait_for(lock, 20ms, [&]() { return !m_requestQueue.empty() || *m_terminating; });
        }
        RequestStruct it;
        while (popNearest(it)) {
            this->processTimed(it);

#if CHECK_FOR_DUPLICATES == 1
            setLck.lock();
            currentlyProcessingFnames.erase(it.fileName);
            setLck.unlock();
#endif
        }
        //The wait above wakes at least every 20 ms, so a second that ends idle still prints
        std::lock_guard<std::mutex> lock(g_loaderTallyMutex);
        g_loaderTally.flush();
    } else {
        RequestStruct it;
        for (int processed = 0; processed < limit && popNearest(it); processed++) {
            this->processTimed(it);
#if CHECK_FOR_DUPLICATES == 1
            setLck.lock();
            currentlyProcessingFnames.erase(it.fileName);
            setLck.unlock();
#endif
        }
    }
}

/*
robots@Impl: The next request is the one whose file ranks nearest (fileRank.h), the earlier asked on a tie, read at the moment
it is taken, so a file ranks by where the camera is now and not where it was when asked. The pick is a scan of the pending
requests under the lock; they run to a few hundred through a jump.
*/
bool RequestProcessor::popNearest(RequestStruct &request) {
    std::lock_guard<std::mutex> lock(m_requestQueueMutex);
    if (m_requestQueue.empty()) return false;

    auto rankOf = [](const RequestStruct &candidate) { return candidate.rank ? candidate.rank->value() : 0.0f; };
    size_t best = 0;
    float bestRank = rankOf(m_requestQueue[0]);
    for (size_t i = 1; i < m_requestQueue.size(); i++) {
        const float rank = rankOf(m_requestQueue[i]);
        if (rank < bestRank || (rank == bestRank && m_requestQueue[i].queued < m_requestQueue[best].queued)) {
            best = i;
            bestRank = rank;
        }
    }

    request = std::move(m_requestQueue[best]);
    if (best + 1 != m_requestQueue.size()) m_requestQueue[best] = std::move(m_requestQueue.back());
    m_requestQueue.pop_back();
    return true;
}

void RequestProcessor::processTimed(const RequestStruct &request) {
    //The request iterateAllFiles queues carries no file and no time
    if (request.queued == std::chrono::steady_clock::time_point{}) {
        this->processFileRequest(request.holderType, request.s_file);
        return;
    }

    const int queued = toBeProcessed.load();
    TracyPlot("Loader queue", static_cast<int64_t>(queued));
    if (request.s_file.expired()) {
        this->processFileRequest(request.holderType, request.s_file);
        std::lock_guard<std::mutex> lock(g_loaderTallyMutex);
        g_loaderTally.noteDropped(queued);
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    m_parseTime = {};
    m_readSplit = {};
    this->processFileRequest(request.holderType, request.s_file);
    const auto spent = std::chrono::steady_clock::now() - start;

    LoaderTally::Kind file;
    file.files = 1;
    file.overrides = m_readSplit.overrides;
    file.open = m_readSplit.open;
    file.read = m_readSplit.read;
    file.inflate = m_readSplit.inflate;
    file.parse = m_parseTime;
    file.lookup = spent - file.busy();

    const auto wait = start - request.queued;
    const double waitMs = LoaderTally::ms(wait);
    TracyPlot("Loader wait ms", waitMs);
    std::lock_guard<std::mutex> lock(g_loaderTallyMutex);
    g_loaderTally.note(request.holderType, wait, file, queued);
}

void
RequestProcessor::processResult(const std::shared_ptr<PersistentFile> &s_file, const HFileContent &content) {
    if (s_file->getStatus() == FileStatus::FSLoaded) {
        std::cout << "sharedPtr->getStatus == FileStatus::FSLoaded " << s_file->getFileNameOrDataId() << std::endl;
    } if (s_file->getStatus() == FileStatus::FSRejected) {
        std::cout << "sharedPtr->getStatus == FileStatus::FSRejected" << s_file->getFileNameOrDataId() << std::endl;
    } else {
        const auto start = std::chrono::steady_clock::now();
        s_file->process(content);
        m_parseTime = std::chrono::steady_clock::now() - start;
    }
}
