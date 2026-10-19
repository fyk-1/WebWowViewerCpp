//
// Created by deamon on 06.09.17.
//

#ifndef WEBWOWVIEWERCPP_REQUESTPROCESSOR_H
#define WEBWOWVIEWERCPP_REQUESTPROCESSOR_H

#include "../include/sharedFile.h"
#include "../include/iostuff.h"
#include "../include/fileRank.h"
#include "../renderer/frame/prodConsumerChain/ProdConsumerIOConnector.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <list>
#include <vector>
#include <atomic>
#include <forward_list>
#include <unordered_set>

#define CHECK_FOR_DUPLICATES 0

struct IterateFilesRequest {
    IterateFilesRequest(std::unique_lock <std::mutex> &a,
                        std::function<bool (int fileDataId, const std::string &fileName)> b,
                        std::function<void (int fileDataId, const HFileContent &fileData)> c) : m(std::move(a)){
        process = b;
        callback = c;
    }

    std::unique_lock <std::mutex> m;
    std::function<bool (int fileDataId, const std::string &fileName)> process;
    std::function<void (int fileDataId, const HFileContent &fileData)> callback;
};

class RequestProcessor : public IFileRequest {
protected:
    RequestProcessor() : toBeProcessed(0){
    }
    virtual ~RequestProcessor() {
    };

protected:
    virtual void processFileRequest(CacheHolderType holderType, const std::weak_ptr<PersistentFile> &s_file) = 0;
public:
    // Every loader thread calls this on a new processor: the first makes the queue they share, and the rest find it made.
    void initThreadQueue(bool &isTerminating) {
        std::lock_guard<std::mutex> lock(m_initMutex);
        if (m_threaded) return;
        m_terminating = &isTerminating;
        m_threaded = true;
    }

    void requestFile(CacheHolderType holderType, std::weak_ptr<PersistentFile> s_file) override;
private:
    class RequestStruct {
    public:
        CacheHolderType holderType;
        std::weak_ptr<PersistentFile> s_file;
        std::chrono::steady_clock::time_point queued;
        std::shared_ptr<FileRank> rank;
    };

    class ResultStruct {
    public:
        ResultStruct(){};
        ResultStruct (const ResultStruct &old_obj) {
            holderType = old_obj.holderType;
            buffer = old_obj.buffer;
        }
        CacheHolderType holderType;
        HFileContent buffer = nullptr;
    };

    // The pending requests, in no order: the loader takes the nearest one (popNearest).
    std::vector<RequestStruct> m_requestQueue;
    std::mutex m_requestQueueMutex;
    std::condition_variable m_requestQueueSignal;
    bool popNearest(RequestStruct &request);
    std::list<ResultStruct> m_resultQueue;

#if CHECK_FOR_DUPLICATES == 1
    std::unordered_set<std::string> currentlyProcessingFnames;
#endif

    bool m_threaded = false;
    bool dumb_boolean_always_false = false;
    bool *m_terminating = &dumb_boolean_always_false;
    std::mutex m_initMutex;
    std::mutex m_iterateMutex;
    // The time processResult spent parsing the file this thread is processing, read back by processRequests.
    static thread_local std::chrono::steady_clock::duration m_parseTime;

    void processTimed(const RequestStruct &request);

    std::unique_ptr<IterateFilesRequest> m_iterateFilesRequest;
public:
    void processRequests(int limit);

    bool completedAllJobs() {
        std::lock_guard<std::mutex> lock(m_requestQueueMutex);
        return (m_requestQueue.empty()) && (m_resultQueue.empty()) && (toBeProcessed == 0);
    };
    void iterateAllFiles( std::unique_ptr<IterateFilesRequest> &iterateFilesRequest );

protected:
    // Where the read of the file this thread is processing spent its time, set by a processor that can tell; the rest is lookup.
    struct ReadSplit {
        std::chrono::steady_clock::duration overrides{};
        std::chrono::steady_clock::duration open{};
        std::chrono::steady_clock::duration read{};
        std::chrono::steady_clock::duration inflate{};
    };
    static thread_local ReadSplit m_readSplit;

    std::atomic<int> toBeProcessed;
    void processResult(const std::shared_ptr<PersistentFile> &s_file, const HFileContent &content);
    virtual void iterateFilesInternal(
        std::function<bool (int fileDataId, const std::string &fileName)> &process,
        std::function<void (int fileDataId, const HFileContent &fileData)> &callback) {};
};

typedef std::shared_ptr<RequestProcessor> HRequestProcessor;

#endif //WEBWOWVIEWERCPP_REQUESTPROCESSOR_H
