//
// Created by Deamon on 12/6/2023.
//

#include "EntityActorsFactory.h"
#include "../../../gapi/interface/IDevice.h"
#include "../../../gapi/interface/FrameContext.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <list>

struct EntityDeallocationRecord {
    unsigned int frameNumberToDoAt;
    std::function<void()> callback;
};

std::mutex m_listOfEntityDeallocatorsAccessMtx;
std::list<EntityDeallocationRecord> listOfEntityDeallocators;

void addEntityDeallocationRecord(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_listOfEntityDeallocatorsAccessMtx);
    EntityDeallocationRecord dr;
    dr.frameNumberToDoAt = FrameContext::getCurrentProcessingFrameNumber() + IDevice::MAX_FRAMES_IN_FLIGHT+1;
    dr.callback = callback;
    listOfEntityDeallocators.push_back(dr);
};

/*
robots@Impl: Entity deallocation is paced. A tile that goes frees thousands of objects at once, and running every record in
one frame was a hitch of its own, so a frame drains from the front until kDeallocationBudget has passed and carries the rest.
The records are in frame order, which makes a partial drain safe: what is left is never due before what ran. A frame always
drains one record, so the queue cannot stall. Shutdown (forceDealloc) and the window after a map change drain every due record.

The window is wall clock because the frame number is per thread. Two seconds is long enough for the render thread to let go of
the old map's last plan and for the records its destruction queues to come due (MAX_FRAMES_IN_FLIGHT + 1 frames); a window that
long at a loading frame rate still ends, where a flag cleared on first sight would be cleared before the old map was gone.
kDeallocationBudget is 1 ms: a freed record is cheap, so a millisecond frees many, while a backlog of thousands spreads over a
few dozen frames and not into one; it is a fraction of a 16.7 ms frame the render leaves untouched.
*/
static constexpr auto kDeallocationBudget = std::chrono::microseconds(1000);
static constexpr auto kFullDeallocationWindow = std::chrono::seconds(2);

static std::atomic<long long> fullDeallocationUntilNs{0};

static long long steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void requestFullDeallocation() {
    fullDeallocationUntilNs.store(steadyNowNs() +
        std::chrono::duration_cast<std::chrono::nanoseconds>(kFullDeallocationWindow).count());
}

bool fullDeallocationRequested() {
    return steadyNowNs() < fullDeallocationUntilNs.load();
}

bool deallocationBudgetSpent(std::chrono::steady_clock::time_point start, size_t drained) {
    return drained >= 1 && (std::chrono::steady_clock::now() - start) >= kDeallocationBudget;
}

void executeEntityDeallocators(bool forceDealloc) {
    const bool unbudgeted = forceDealloc || fullDeallocationRequested();
    const auto start = std::chrono::steady_clock::now();
    size_t drained = 0;

    for (;;) {
        EntityDeallocationRecord record;
        {
            std::lock_guard<std::mutex> lock(m_listOfEntityDeallocatorsAccessMtx);
            if (listOfEntityDeallocators.empty()) return;
            if (!forceDealloc &&
                listOfEntityDeallocators.front().frameNumberToDoAt > FrameContext::getCurrentProcessingFrameNumber()) return;

            record = std::move(listOfEntityDeallocators.front());
            listOfEntityDeallocators.pop_front();
        }

        // The callback runs without the lock held
        if (record.callback)
            record.callback();
        drained++;

        if (!unbudgeted && deallocationBudgetSpent(start, drained)) return;
    }
}
