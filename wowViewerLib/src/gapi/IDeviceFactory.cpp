//
// Created by Deamon on 9/4/2018.
//


#include "IDeviceFactory.h"

#ifndef SKIP_VULKAN
#include "vulkan/GDeviceVulkan.h"
#endif

HGDevice IDeviceFactory::createDevice(std::string gapiName, void * data) {
#ifndef SKIP_VULKAN
    if (gapiName == "vulkan") {
        auto deviceVlk = std::make_shared<GDeviceVLK>((vkCallInitCallback *) data);
        deviceVlk->initialize();
        return deviceVlk;
    } else
#endif
    {}

    return nullptr;
}
