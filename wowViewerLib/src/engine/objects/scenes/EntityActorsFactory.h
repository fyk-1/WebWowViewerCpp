//
// Created by Deamon on 12/6/2023.
//

#ifndef AWEBWOWVIEWERCPP_ENTITYACTORSFACTORY_H
#define AWEBWOWVIEWERCPP_ENTITYACTORSFACTORY_H

#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <typeinfo>
#include <functional>
#include <iostream>
#include <chrono>
#include <mutex>
#include <tuple>
#include <cstring>
#include <type_traits>
#include <cassert>
#include "../../../../3rdparty/OffsetAllocator/offsetAllocator.hpp"


void addEntityDeallocationRecord(std::function<void()> callback);
void executeEntityDeallocators(bool forceDealloc);
// A map change: for a short window the deallocators drain every due record per frame, not a budget's worth.
void requestFullDeallocation();
bool fullDeallocationRequested();
// Whether a drain that has run `drained` records since `start` has used its per-frame budget. One record always runs.
bool deallocationBudgetSpent(std::chrono::steady_clock::time_point start, size_t drained);


// The most blocks or pages one storage holds. The tables of them are allocated once at this size and never move, since
// readers on the cull and render threads index them without the factory's lock while createObject grows them: a growing
// std::vector reallocated under such a reader, which then read a moved-from block and got null or garbage back.
static constexpr int EntityMaxBlocks = 4096;

// Block-based memory pool for MainClass (supports placement new/delete)
template<uint16_t MemoryBlockSize, typename T>
class MainClassBlockPool {
    static constexpr int BlockSizeInBytes = MemoryBlockSize * sizeof(T);
    std::unique_ptr<std::unique_ptr<uint8_t[]>[]> objectPools = std::make_unique<std::unique_ptr<uint8_t[]>[]>(EntityMaxBlocks);
    // Published after the block is in place, so a reader that sees the count sees the block.
    std::atomic<int> blockCount = 0;
public:
    inline T * getPtrFromOffset(int offset) const {
        auto blockIndex = offset / MemoryBlockSize;
        auto blockOffset = (offset % MemoryBlockSize);
        auto byteOffset = blockOffset * sizeof(T);

        if (blockIndex < 0 || blockIndex >= blockCount.load(std::memory_order_acquire))
            return nullptr;

        return (T *)&objectPools[blockIndex][byteOffset];
    }
    void expand() {
        int count = blockCount.load(std::memory_order_relaxed);
        if (count >= EntityMaxBlocks) {
            std::cerr << "EntityFactory: more than " << EntityMaxBlocks << " blocks of " << typeid(T).name() << std::endl;
            std::abort();
        }
        objectPools[count] = std::make_unique<uint8_t[]>(BlockSizeInBytes);
        blockCount.store(count + 1, std::memory_order_release);
    }
};

// Paged storage for auxiliary component types
// Each page is a fixed-size array. Pointers into pages remain stable on growth, and so does the table of pages.
// copyToBuffer assembles pages into a contiguous destination (e.g. GPU staging buffer).
template<uint16_t PageSize, typename T>
class PagedComponentStorage {
    std::unique_ptr<std::unique_ptr<std::array<T, PageSize>>[]> m_pages = std::make_unique<std::unique_ptr<std::array<T, PageSize>>[]>(EntityMaxBlocks);
    std::atomic<int> m_pageCount = 0;
    size_t m_totalSize = 0;

    // Span of ids written through getPtrMutable since the last take/clear: min in the low 32 bits,
    // max in the high 32. One word, so a take can never see one end of a span without the other.
    static constexpr uint64_t EMPTY_DIRTY_RANGE = 0x00000000FFFFFFFFull;
    std::atomic<uint64_t> m_dirtyRange = EMPTY_DIRTY_RANGE;

    void markDirty(uint32_t id) {
        uint64_t cur = m_dirtyRange.load(std::memory_order_relaxed);
        while (true) {
            uint32_t curMin = (uint32_t) cur;
            uint32_t curMax = (uint32_t) (cur >> 32);
            if (id >= curMin && id <= curMax) return;

            uint64_t next = (uint64_t) (std::min)(curMin, id) | ((uint64_t) (std::max)(curMax, id) << 32);
            if (m_dirtyRange.compare_exchange_weak(cur, next, std::memory_order_relaxed)) return;
        }
    }
public:
    void expand() {
        int count = m_pageCount.load(std::memory_order_relaxed);
        if (count >= EntityMaxBlocks) {
            std::cerr << "EntityFactory: more than " << EntityMaxBlocks << " pages of " << typeid(T).name() << std::endl;
            std::abort();
        }
        m_pages[count] = std::make_unique<std::array<T, PageSize>>();
        m_pageCount.store(count + 1, std::memory_order_release);
        m_totalSize += PageSize;
    }

    inline T * getPtrMutable(int offset) {
        auto pageIndex = offset / PageSize;
        auto pageOffset = offset % PageSize;
        if (pageIndex < 0 || pageIndex >= m_pageCount.load(std::memory_order_acquire))
            return nullptr;
        markDirty((uint32_t) offset);
        return &(*m_pages[pageIndex])[pageOffset];
    }

    inline const T * getPtrConst(int offset) const {
        auto pageIndex = offset / PageSize;
        auto pageOffset = offset % PageSize;
        if (pageIndex < 0 || pageIndex >= m_pageCount.load(std::memory_order_acquire))
            return nullptr;
        return &(*m_pages[pageIndex])[pageOffset];
    }

    bool isDirty() const { return m_dirtyRange.load(std::memory_order_relaxed) != EMPTY_DIRTY_RANGE; }
    void clearDirty() { m_dirtyRange.store(EMPTY_DIRTY_RANGE, std::memory_order_relaxed); }

    // Returns the inclusive span of ids written since the last take/clear, and resets it.
    // False when nothing was written.
    bool takeDirtyRange(uint32_t &minId, uint32_t &maxId) {
        uint64_t range = m_dirtyRange.exchange(EMPTY_DIRTY_RANGE, std::memory_order_acq_rel);
        if (range == EMPTY_DIRTY_RANGE) return false;
        minId = (uint32_t) range;
        maxId = (uint32_t) (range >> 32);
        return true;
    }

    // Copy `count` elements starting at id `first` into a contiguous destination buffer
    template<typename U>
    void copyRangeToBuffer(U *dest, size_t first, size_t count) const {
        static_assert(std::is_same_v<T, U>, "Type mismatch in copyRangeToBuffer");
        assert(first + count <= m_totalSize);
        int pageCount = m_pageCount.load(std::memory_order_acquire);
        while (count > 0) {
            size_t pageIndex = first / PageSize;
            if (pageIndex >= (size_t) pageCount) break;

            size_t pageOffset = first % PageSize;
            size_t toCopy = std::min<size_t>(count, PageSize - pageOffset);
            std::memcpy(dest, m_pages[pageIndex]->data() + pageOffset, toCopy * sizeof(T));
            dest += toCopy;
            first += toCopy;
            count -= toCopy;
        }
    }

    // Copy all pages into a contiguous destination buffer with type checking
    template<typename U>
    void copyToBuffer(U *dest, size_t count) const {
        static_assert(std::is_same_v<T, U>, "Type mismatch in copyToBuffer");
        assert(count <= m_totalSize);
        size_t remaining = count;
        int pageCount = m_pageCount.load(std::memory_order_acquire);
        for (int i = 0; i < pageCount; i++) {
            auto &page = m_pages[i];
            size_t toCopy = std::min<size_t>(remaining, (size_t)PageSize);
            if (toCopy == 0) break;
            std::memcpy(dest, page->data(), toCopy * sizeof(T));
            dest += toCopy;
            remaining -= toCopy;
        }
    }

    size_t size() const { return m_totalSize; }

    using value_type = T;
};


template<uint16_t MemoryBlockSize, typename ObjIdType, typename MainClass, typename... Types>
class EntityFactory : public std::enable_shared_from_this<EntityFactory<MemoryBlockSize, ObjIdType, MainClass, Types...>> {
    static_assert(sizeof(ObjIdType) == sizeof(uintptr_t));

    static constexpr int ComponentAmount = sizeof...(Types);

    MainClassBlockPool<MemoryBlockSize, MainClass> m_mainPool;
    std::tuple<PagedComponentStorage<MemoryBlockSize, Types>...> m_componentStorages;

    void expandAll() {
        m_mainPool.expand();
        if constexpr (ComponentAmount > 0) {
            std::apply([](auto &... storage) {
                (storage.expand(), ...);
            }, m_componentStorages);
        }
    }

public:
    explicit EntityFactory() {
        expandAll();
    };

    template<typename... _Args>
    std::shared_ptr<MainClass> createObject(_Args&&... __args) {
        OffsetAllocator::Allocation offsetData;
        MainClass * entity = nullptr;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            offsetData = allocator.allocate(1);
            if (offsetData.offset == OffsetAllocator::Allocation::NO_SPACE) {
                allocator.growSize(MemoryBlockSize);
                expandAll();

                offsetData = allocator.allocate(1);
            }
            auto ptr = m_mainPool.getPtrFromOffset(offsetData.offset);

            entity = new(ptr) MainClass(std::forward<decltype(__args)>(__args)...);
            entity->setId((ObjIdType)offsetData.offset);
        }

        auto l_this = this->shared_from_this();
        return std::shared_ptr<MainClass>(entity, [offsetData, l_this](MainClass *ls) -> void {
            addEntityDeallocationRecord([offsetData, l_this]() {
                l_this->deallocate(offsetData);
            });
        });
    }

    // Access MainClass (I == 0) or auxiliary component (I >= 1)
    // For I >= 1, marks the component storage as dirty
    template<int I>
    inline auto getObjectById(ObjIdType id) {
        int index = (int)id;
        if constexpr (I == 0) {
            if (index < 0) return (MainClass *)nullptr;
            return m_mainPool.getPtrFromOffset(index);
        } else {
            static_assert(I <= ComponentAmount, "Component index out of range");
            using CompType = typename std::tuple_element_t<I - 1, std::tuple<PagedComponentStorage<MemoryBlockSize, Types>...>>::value_type;
            if (index < 0) return (CompType *)nullptr;
            return std::get<I - 1>(m_componentStorages).getPtrMutable(index);
        }
    }

    // Read-only access to auxiliary component (I >= 1), does NOT mark dirty
    template<int I>
    inline auto getObjectByIdConst(ObjIdType id) const {
        static_assert(I > 0 && I <= ComponentAmount, "Use getObjectById<0> for MainClass, or index out of range");
        using CompType = typename std::tuple_element_t<I - 1, std::tuple<PagedComponentStorage<MemoryBlockSize, Types>...>>::value_type;
        int index = (int)id;
        if (index < 0) return (const CompType *)nullptr;
        return std::get<I - 1>(m_componentStorages).getPtrConst(index);
    }

    // Check if any element of component type I (1-based) was modified since last clear
    template<int I>
    bool isComponentDirty() const {
        static_assert(I > 0 && I <= ComponentAmount, "Component index out of range");
        return std::get<I - 1>(m_componentStorages).isDirty();
    }

    // Take (and reset) the inclusive span of ids of component type I (1-based) written through
    // getObjectById<I> since the last take/clear. False when nothing was written.
    template<int I>
    bool takeComponentDirtyRange(uint32_t &minId, uint32_t &maxId) {
        static_assert(I > 0 && I <= ComponentAmount, "Component index out of range");
        return std::get<I - 1>(m_componentStorages).takeDirtyRange(minId, maxId);
    }

    // Clear the dirty flag for component type I (1-based)
    template<int I>
    void clearComponentDirty() {
        static_assert(I > 0 && I <= ComponentAmount, "Component index out of range");
        std::get<I - 1>(m_componentStorages).clearDirty();
    }

    // Copy contiguous component data to a destination buffer with type checking
    // I is 1-based (same as getObjectById indexing)
    template<int I, typename U>
    void copyComponentToBuffer(U *dest, size_t count) const {
        static_assert(I > 0 && I <= ComponentAmount, "Component index out of range");
        std::get<I - 1>(m_componentStorages).copyToBuffer(dest, count);
    }

    // Copy `count` slots of component I starting at slot `first` to a destination buffer with type checking
    template<int I, typename U>
    void copyComponentRangeToBuffer(U *dest, size_t first, size_t count) const {
        static_assert(I > 0 && I <= ComponentAmount, "Component index out of range");
        std::get<I - 1>(m_componentStorages).copyRangeToBuffer(dest, first, count);
    }

    // Get total capacity of component storage (number of slots, including dead ones)
    template<int I>
    size_t getComponentCapacity() const {
        static_assert(I > 0 && I <= ComponentAmount, "Component index out of range");
        return std::get<I - 1>(m_componentStorages).size();
    }

private:
    void deallocate(const OffsetAllocator::Allocation &alloc) {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto *ptr = m_mainPool.getPtrFromOffset(alloc.offset);

        if (!ptr) {
            std::cout << "Dealloc PTR is null for " << typeid(MainClass).name() << std::endl;
        }

        ptr->~MainClass();
        allocator.free(alloc);
    }

private:
    std::mutex m_mutex;

    OffsetAllocator::Allocator allocator = OffsetAllocator::Allocator(MemoryBlockSize);
};


#endif //AWEBWOWVIEWERCPP_ENTITYACTORSFACTORY_H
