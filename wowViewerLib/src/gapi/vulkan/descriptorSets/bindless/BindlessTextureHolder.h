//
// Created by Deamon on 10/6/2023.
//

#ifndef AWEBWOWVIEWERCPP_BINDLESSTEXTUREHOLDER_H
#define AWEBWOWVIEWERCPP_BINDLESSTEXTUREHOLDER_H


#include <cstdint>
#include <mutex>
#include <vector>
#include "../../../interface/IDevice.h"
#include "../../../../../3rdparty/OffsetAllocator/offsetAllocator.hpp"
#include "../../../../include/custom_container_key.h"
#include "../GDescriptorSet.h"
#include "../../../../renderer/mapScene/materials/BindlessTexture.h"
#include "../../../../robin_hood.h"

class BindlessTextureHolder : public std::enable_shared_from_this<BindlessTextureHolder> {
public:
    BindlessTextureHolder(uint32_t textureCount);

    std::shared_ptr<BindlessTexture> allocate(const HGSamplableTexture& texture);

    /// Names the set and bind point that hold this holder's slots. The set's record keeps a texture alive
    /// until its slot is rewritten, so a freed slot is rewritten with `placeholder`, once the frames in flight
    /// when it was freed are done: the free waits in `device`'s deallocation queue.
    void attach(const std::shared_ptr<IDevice> &device, const std::shared_ptr<GDescriptorSet> &set, int bindPoint, const HGSamplableTexture &placeholder);

    /// Rewrites every slot freed since the last call with the placeholder, then returns it to the allocator.
    /// Runs on the thread that calls `allocate`.
    void releaseFreedSlots();

private:
    typedef wtf::KeyContainer<std::weak_ptr<HGSamplableTexture::element_type>> TextureKey;

    struct FreedSlot {
        OffsetAllocator::Allocation allocation;
        TextureKey textureKey;
    };

    OffsetAllocator::Allocator m_textureAllocator;

    //Texture -> Bindless in array
    robin_hood::unordered_flat_map<TextureKey, std::weak_ptr<BindlessTexture>> m_textureMap;

    std::shared_ptr<IDevice> m_device;
    std::shared_ptr<GDescriptorSet> m_set;
    int m_bindPoint = 0;
    HGSamplableTexture m_placeholder;

    std::mutex m_freedSlotsMutex;
    std::vector<FreedSlot> m_freedSlots;
};


#endif //AWEBWOWVIEWERCPP_BINDLESSTEXTUREHOLDER_H
