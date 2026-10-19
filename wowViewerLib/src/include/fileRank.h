#ifndef WOWVIEWERLIB_FILERANK_H
#define WOWVIEWERLIB_FILERANK_H

#include <atomic>
#include <cstdint>
#include <cstring>

/*
robots@Impl: A requested file's place in the loader queue: the squared distance in yards from the camera to the nearest thing
that wants it, lower first, which RequestProcessor reads each time it picks the next file. Map starts a new epoch once a frame
(nextEpoch). Within one epoch a file keeps the nearest distance any owner gave it, and the first rank of a new epoch replaces the
old one, so a file whose owners moved away falls back. A file nobody ranks keeps 0 and goes first, as every file did when the
queue ran in order of request: the map's own files, the sky, the liquids.

An owner ranks the files it asks for by holding a FileRankScope with its distance while it asks: the cache ranks every file it
hands out under one, new or already there, so a texture takes the rank of the model, WMO group or tile it is for. Owners whose
files are still pending rank them again each frame, so the queue follows the camera.
*/
class FileRank {
public:
    static void nextEpoch() { s_epoch.fetch_add(1, std::memory_order_relaxed); }

    void rank(float distanceSq) {
        if (!(distanceSq > 0)) distanceSq = 0;
        const uint64_t epoch = s_epoch.load(std::memory_order_relaxed);
        const uint32_t distanceBits = bits(distanceSq);
        const uint64_t wanted = (epoch << 32) | distanceBits;
        uint64_t current = m_packed.load(std::memory_order_relaxed);
        while (true) {
            const uint64_t currentEpoch = current >> 32;
            if (currentEpoch > epoch) return;
            if (currentEpoch == epoch && static_cast<uint32_t>(current) <= distanceBits) return;
            if (m_packed.compare_exchange_weak(current, wanted, std::memory_order_relaxed)) return;
        }
    }

    float value() const {
        const uint32_t distanceBits = static_cast<uint32_t>(m_packed.load(std::memory_order_relaxed));
        float distanceSq;
        std::memcpy(&distanceSq, &distanceBits, sizeof(distanceSq));
        return distanceSq;
    }

private:
    // A float of 0 or more orders as its bits do, so the packed word compares distances as integers
    static uint32_t bits(float value) {
        uint32_t result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    inline static std::atomic<uint64_t> s_epoch{1};
    std::atomic<uint64_t> m_packed{0};
};

class FileRankScope {
public:
    explicit FileRankScope(float distanceSq) : m_previous(t_distanceSq) { t_distanceSq = distanceSq < 0 ? 0 : distanceSq; }
    ~FileRankScope() { t_distanceSq = m_previous; }
    FileRankScope(const FileRankScope &) = delete;
    FileRankScope &operator=(const FileRankScope &) = delete;

    // Ranks a file the cache hands out under this thread's innermost scope, if there is one.
    static void apply(FileRank &rank) {
        if (t_distanceSq >= 0) rank.rank(t_distanceSq);
    }

private:
    float m_previous;
    inline static thread_local float t_distanceSq = -1;
};

#endif //WOWVIEWERLIB_FILERANK_H
