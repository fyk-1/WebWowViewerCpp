//
// Created by Deamon on 10/6/2023.
//

#include "BindlessTextureHolder.h"

#include <algorithm>

BindlessTextureHolder::BindlessTextureHolder(uint32_t textureCount) :
    m_textureAllocator(OffsetAllocator::Allocator(textureCount, textureCount)){

}

std::shared_ptr<BindlessTexture> BindlessTextureHolder::allocate(const HGSamplableTexture &texture) {
    auto weakTexture = std::weak_ptr(texture);

    auto i = m_textureMap.find(weakTexture);
    if (i != m_textureMap.end()) {
        if (auto shared = i->second.lock()) {
            return shared;
        } else {
            m_textureMap.erase(i);
        }
    }

    OffsetAllocator::Allocation textNum = m_textureAllocator.allocate(1);
    if (textNum.offset == OffsetAllocator::Allocation::NO_SPACE) {
        std::cerr << "Could not allocate bindless texture";
        throw "oops";
    }

    auto l_weak = this->weak_from_this();
    TextureKey textureKey(weakTexture);
    auto bindlessTexture = std::make_shared<BindlessTexture>(textNum.offset, [l_weak, textNum, textureKey]() {
        auto l_this = l_weak.lock();
        if (l_this == nullptr) return;

        auto queueSlot = [l_weak, textNum, textureKey]() {
            auto l_this = l_weak.lock();
            if (l_this == nullptr) return;

            std::lock_guard<std::mutex> lock(l_this->m_freedSlotsMutex);
            l_this->m_freedSlots.push_back({textNum, textureKey});
        };
        //The frames in flight still sample the slot; the device's queue runs the record once they are done
        if (l_this->m_device != nullptr) {
            l_this->m_device->addDeallocationRecord(queueSlot);
        } else {
            queueSlot();
        }
    });

    m_textureMap[weakTexture] = bindlessTexture;

    return bindlessTexture;
}

void BindlessTextureHolder::attach(const std::shared_ptr<IDevice> &device, const std::shared_ptr<GDescriptorSet> &set, int bindPoint, const HGSamplableTexture &placeholder) {
    m_device = device;
    m_set = set;
    m_bindPoint = bindPoint;
    m_placeholder = placeholder;
}

void BindlessTextureHolder::releaseFreedSlots() {
    std::vector<FreedSlot> freedSlots;
    {
        std::lock_guard<std::mutex> lock(m_freedSlotsMutex);
        freedSlots.swap(m_freedSlots);
    }
    if (freedSlots.empty()) return;

    if (m_set != nullptr) {
        //One update's image infos are reserved up to 4096 entries
        constexpr size_t maxWritesPerUpdate = 512;
        for (size_t begin = 0; begin < freedSlots.size(); begin += maxWritesPerUpdate) {
            size_t end = std::min(begin + maxWritesPerUpdate, freedSlots.size());

            auto dsUpdate = m_set->beginUpdate();
            for (size_t i = begin; i < end; i++) {
                dsUpdate.texture(m_bindPoint, m_placeholder, freedSlots[i].allocation.offset);
            }
        }
    }

    for (auto &freed : freedSlots) {
        auto i = m_textureMap.find(freed.textureKey);
        if (i != m_textureMap.end() && i->second.expired()) {
            m_textureMap.erase(i);
        }

        m_textureAllocator.free(freed.allocation);
    }
}
