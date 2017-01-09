/*
// Copyright(c) 2016 Amlogic Corporation
*/

#ifndef PRIMARY_DEVICE_H
#define PRIMARY_DEVICE_H

#include <PhysicalDevice.h>

namespace android {
namespace amlogic {


class PrimaryDevice : public PhysicalDevice {
public:
    PrimaryDevice(Hwcomposer& hwc);
    virtual ~PrimaryDevice();
public:
    virtual bool initialize();
    virtual void deinitialize();

private:
    static void hotplugInEventListener(void *data);
    static void hotplugOutEventListener(void *data);
    void hotplugListener(bool connected);
};

}
}

#endif /* PRIMARY_DEVICE_H */