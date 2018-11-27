/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <BasicTypes.h>
#include <MesonLog.h>
#include <DebugHelper.h>
#include <unistd.h>
#include <HwcConfig.h>

#include "MesonHwc2Defs.h"
#include "MesonHwc2.h"
#include "VirtualDisplay.h"

#include <thread>
#include <mutex>
#include <condition_variable>

#define CHECK_DISPLAY_VALID(display)    \
    if (isDisplayValid(display) == false) { \
        MESON_LOGE("(%s) met invalid display id (%d)", \
            __func__, (int)display); \
        return HWC2_ERROR_BAD_DISPLAY; \
    }

#define GET_HWC_DISPLAY(display)    \
    std::shared_ptr<Hwc2Display> hwcDisplay; \
    std::map<hwc2_display_t, std::shared_ptr<Hwc2Display>>::iterator it = \
            mDisplays.find(display); \
    if (it != mDisplays.end()) { \
        hwcDisplay = it->second; \
    }  else {\
        MESON_LOGE("(%s) met invalid display id (%d)",\
            __func__, (int)display); \
        return HWC2_ERROR_BAD_DISPLAY; \
    }

#define GET_HWC_LAYER(display, id)    \
    std::shared_ptr<Hwc2Layer> hwcLayer = display->getLayerById(id); \
    if (hwcLayer.get() == NULL) { \
        MESON_LOGE("(%s) met invalid layer id (%d) in display (%p)",\
            __func__, (int)id, display.get()); \
        return HWC2_ERROR_BAD_LAYER; \
    }


/************************************************************
*                        Hal Interface
************************************************************/
void MesonHwc2::dump(uint32_t* outSize, char* outBuffer) {
    if (outBuffer == NULL) {
        *outSize = 8192;
        return ;
    }

    String8 dumpstr;
    DebugHelper::getInstance().resolveCmd();

    dumpstr.append("\nMesonHwc2 state:\n");

    if (DebugHelper::getInstance().dumpDetailInfo()) {
        HwcConfig::dump(dumpstr);
        HwDisplayManager::getInstance().dump(dumpstr);
    }

    // dump composer status
    std::map<hwc2_display_t, std::shared_ptr<Hwc2Display>>::iterator it;
    for (it = mDisplays.begin(); it != mDisplays.end(); it++) {
        it->second->dump(dumpstr);
    }

    DebugHelper::getInstance().dump(dumpstr);
    dumpstr.append("\n");

    strncpy(outBuffer, dumpstr.string(), dumpstr.size() > 8192 ? 8191 : dumpstr.size());
}

void MesonHwc2::getCapabilities(uint32_t* outCount,
    int32_t* outCapabilities) {
    *outCount = 1;
    if (outCapabilities) {
        *outCount = 1;
        outCapabilities[0] = HWC2_CAPABILITY_SIDEBAND_STREAM;
    }
}

int32_t MesonHwc2::getClientTargetSupport(hwc2_display_t display,
    uint32_t width __unused, uint32_t height __unused, int32_t format, int32_t dataspace) {
    GET_HWC_DISPLAY(display);
    if (format == HAL_PIXEL_FORMAT_RGBA_8888 &&
        dataspace == HAL_DATASPACE_UNKNOWN) {
        return HWC2_ERROR_NONE;
    }

    MESON_LOGE("getClientTargetSupport failed: format (%d), dataspace (%d)",
        format, dataspace);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t MesonHwc2::registerCallback(int32_t descriptor,
    hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer) {
    hwc2_error_t ret = HWC2_ERROR_NONE;

    switch (descriptor) {
        case HWC2_CALLBACK_HOTPLUG:
        /* For android:
        When primary display is hdmi, we should always return connected event
        to surfaceflinger, or surfaceflinger will not boot and wait
        connected event.
        */
            mHotplugFn = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
            mHotplugData = callbackData;

            /*always to call hotplug for primary display,
        * for android always think primary is always connected.
        */
            if (mHotplugFn) {
                mHotplugFn(mHotplugData, HWC_DISPLAY_PRIMARY, true);
            }
            break;
        case HWC2_CALLBACK_REFRESH:
            mRefreshFn = reinterpret_cast<HWC2_PFN_REFRESH>(pointer);
            mRefreshData = callbackData;
            break;
        case HWC2_CALLBACK_VSYNC:
            mVsyncFn = reinterpret_cast<HWC2_PFN_VSYNC>(pointer);
            mVsyncData = callbackData;
            break;

        default:
            MESON_LOGE("register unknown callback (%d)", descriptor);
            ret = HWC2_ERROR_UNSUPPORTED;
            break;
    }

    return ret;
}

int32_t MesonHwc2::getDisplayName(hwc2_display_t display,
    uint32_t* outSize, char* outName) {
    GET_HWC_DISPLAY(display);
    const char * name = hwcDisplay->getName();
    if (name == NULL) {
        MESON_LOGE("getDisplayName (%d) failed", (int)display);
    } else {
        *outSize = strlen(name) + 1;
        if (outName) {
            strcpy(outName, name);
        }
    }
    return HWC2_ERROR_NONE;
}

int32_t  MesonHwc2::getDisplayType(hwc2_display_t display,
    int32_t* outType) {
    GET_HWC_DISPLAY(display);

    if (display < HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
        *outType = HWC2_DISPLAY_TYPE_PHYSICAL;
    } else {
        *outType = HWC2_DISPLAY_TYPE_VIRTUAL;
    }

    return HWC2_ERROR_NONE;
}

int32_t MesonHwc2::getDozeSupport(hwc2_display_t display,
    int32_t* outSupport) {
    /*No doze support now.*/
    CHECK_DISPLAY_VALID(display);
    *outSupport = 0;
    return HWC2_ERROR_NONE;
}

int32_t  MesonHwc2::getColorModes(hwc2_display_t display,
    uint32_t* outNumModes, int32_t*  outModes) {
    CHECK_DISPLAY_VALID(display);
    /*Only support native color mode.*/
    *outNumModes = 1;
    if (outModes) {
         outModes[0] = HAL_COLOR_MODE_NATIVE;
     }
    return HWC2_ERROR_NONE;
}

int32_t MesonHwc2::setColorMode(hwc2_display_t display, int32_t mode __unused) {
    CHECK_DISPLAY_VALID(display);
     /*Only support native color mode, nothing to do now.*/
     return HWC2_ERROR_NONE;
}

int32_t MesonHwc2::setColorTransform(hwc2_display_t display,
    const float* matrix, int32_t hint) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->setColorTransform(matrix, (android_color_transform_t)hint);
}

int32_t MesonHwc2::getHdrCapabilities(hwc2_display_t display,
    uint32_t* outNumTypes, int32_t* outTypes, float* outMaxLuminance,
    float* outMaxAverageLuminance, float* outMinLuminance) {
    GET_HWC_DISPLAY(display);
    const drm_hdr_capabilities_t * caps = hwcDisplay->getHdrCapabilities();
    if (caps) {
        bool getInfo = false;
        if (outTypes)
            getInfo = true;

        if (caps->DolbyVisionSupported) {
            *outNumTypes = *outNumTypes + 1;
            if (getInfo) {
                *outTypes++ = HAL_HDR_DOLBY_VISION;
            }
        }
        if (caps->HLGSupported) {
            *outNumTypes = *outNumTypes + 1;
            if (getInfo) {
                *outTypes++ = HAL_HDR_HLG;
            }
        }
        if (caps->HDR10Supported) {
            *outNumTypes = *outNumTypes + 1;
            if (getInfo) {
                *outTypes++ = HAL_HDR_HDR10;
            }
        }

        if (getInfo) {
            *outMaxLuminance = caps->maxLuminance;
            *outMaxAverageLuminance = caps->avgLuminance;
            *outMinLuminance = caps->minLuminance;
        }
    } else {
        *outNumTypes = 0;
    }

    return HWC2_ERROR_NONE;
}

int32_t MesonHwc2::setPowerMode(hwc2_display_t display,
    int32_t mode) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->setPowerMode((hwc2_power_mode_t)mode);
}

int32_t MesonHwc2::setVsyncEnable(hwc2_display_t display,
    int32_t enabled) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->setVsyncEnable((hwc2_vsync_t)enabled);
}

int32_t MesonHwc2::getActiveConfig(hwc2_display_t display,
    hwc2_config_t* outConfig) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getActiveConfig(outConfig);
}

int32_t  MesonHwc2::getDisplayConfigs(hwc2_display_t display,
            uint32_t* outNumConfigs, hwc2_config_t* outConfigs) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getDisplayConfigs(outNumConfigs, outConfigs);
}

int32_t MesonHwc2::setActiveConfig(hwc2_display_t display,
        hwc2_config_t config) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->setActiveConfig(config);
}

int32_t  MesonHwc2::getDisplayAttribute(hwc2_display_t display,
    hwc2_config_t config, int32_t attribute, int32_t* outValue) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getDisplayAttribute(config, attribute, outValue);
}

/*************Virtual display api below*************/
int32_t MesonHwc2::createVirtualDisplay(uint32_t width, uint32_t height,
    int32_t* format, hwc2_display_t* outDisplay) {
    hwc2_display_t id = getVirtualDisplayId();
    std::shared_ptr<VirtualDisplay> disp = std::make_shared<VirtualDisplay>(width, height);
    disp->initialize();
    disp->setFormat(format);
    mDisplays.emplace(id, disp);

    *outDisplay = id;
    return HWC2_ERROR_NONE;
}

int32_t MesonHwc2::destroyVirtualDisplay(hwc2_display_t display) {
    CHECK_DISPLAY_VALID(display);
    mDisplays.erase(display);
    freeVirtualDisplayId(display);
    return HWC2_ERROR_NONE;
}

int32_t MesonHwc2::setOutputBuffer(hwc2_display_t display,
    buffer_handle_t buffer, int32_t releaseFence) {
    GET_HWC_DISPLAY(display);

    VirtualDisplay * disp = (VirtualDisplay *)hwcDisplay.get();
    return disp->setOutputBuffer(buffer, releaseFence);
}

uint32_t MesonHwc2::getMaxVirtualDisplayCount() {
    return MESON_VIRTUAL_DISPLAY_MAX_COUNT;
}

/*************Compose api below*************/
int32_t  MesonHwc2::acceptDisplayChanges(hwc2_display_t display) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->acceptDisplayChanges();
}

int32_t MesonHwc2::getChangedCompositionTypes( hwc2_display_t display,
    uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t*  outTypes) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getChangedCompositionTypes(outNumElements,
        outLayers, outTypes);
}

int32_t MesonHwc2::getDisplayRequests(hwc2_display_t display,
    int32_t* outDisplayRequests, uint32_t* outNumElements,
    hwc2_layer_t* outLayers, int32_t* outLayerRequests) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getDisplayRequests(outDisplayRequests, outNumElements,
        outLayers, outLayerRequests);
}

int32_t MesonHwc2::setClientTarget(hwc2_display_t display,
    buffer_handle_t target, int32_t acquireFence,
    int32_t dataspace, hwc_region_t damage) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->setClientTarget(target, acquireFence,
        dataspace, damage);
}

int32_t MesonHwc2::getReleaseFences(hwc2_display_t display,
    uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outFences) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getReleaseFences(outNumElements,
        outLayers, outFences);
}

int32_t MesonHwc2::validateDisplay(hwc2_display_t display,
    uint32_t* outNumTypes, uint32_t* outNumRequests) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->validateDisplay(outNumTypes,
        outNumRequests);
}

int32_t MesonHwc2::presentDisplay(hwc2_display_t display,
    int32_t* outPresentFence) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->presentDisplay(outPresentFence);
}

/*************Layer api below*************/
int32_t MesonHwc2::createLayer(hwc2_display_t display, hwc2_layer_t* outLayer) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->createLayer(outLayer);
}

int32_t MesonHwc2::destroyLayer(hwc2_display_t display, hwc2_layer_t layer) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->destroyLayer(layer);
}

int32_t MesonHwc2::setCursorPosition(hwc2_display_t display,
    hwc2_layer_t layer, int32_t x, int32_t y) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->setCursorPosition(layer, x, y);
}

int32_t MesonHwc2::setLayerBuffer(hwc2_display_t display, hwc2_layer_t layer,
    buffer_handle_t buffer, int32_t acquireFence) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setBuffer(buffer, acquireFence);
}

int32_t MesonHwc2::setLayerSurfaceDamage(hwc2_display_t display,
    hwc2_layer_t layer, hwc_region_t damage) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setSurfaceDamage(damage);
}

int32_t MesonHwc2::setLayerBlendMode(hwc2_display_t display,
    hwc2_layer_t layer, int32_t mode) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setBlendMode((hwc2_blend_mode_t)mode);
}

int32_t MesonHwc2::setLayerColor(hwc2_display_t display,
    hwc2_layer_t layer, hwc_color_t color) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setColor(color);
}

int32_t MesonHwc2::setLayerCompositionType(hwc2_display_t display,
    hwc2_layer_t layer, int32_t type) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setCompositionType((hwc2_composition_t)type);
}

int32_t MesonHwc2::setLayerDataspace(hwc2_display_t display,
    hwc2_layer_t layer, int32_t dataspace) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setDataspace((android_dataspace_t)dataspace);
}

int32_t MesonHwc2::setLayerDisplayFrame(hwc2_display_t display,
    hwc2_layer_t layer, hwc_rect_t frame) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setDisplayFrame(frame);
}

int32_t MesonHwc2::setLayerPlaneAlpha(hwc2_display_t display,
    hwc2_layer_t layer, float alpha) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setPlaneAlpha(alpha);
}

int32_t MesonHwc2::setLayerSidebandStream(hwc2_display_t display,
    hwc2_layer_t layer, const native_handle_t* stream) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setSidebandStream(stream);
}

int32_t MesonHwc2::setLayerSourceCrop(hwc2_display_t display,
    hwc2_layer_t layer, hwc_frect_t crop) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setSourceCrop(crop);
}

int32_t MesonHwc2::setLayerTransform(hwc2_display_t display,
    hwc2_layer_t layer, int32_t transform) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setTransform((hwc_transform_t)transform);
}

int32_t MesonHwc2::setLayerVisibleRegion(hwc2_display_t display,
    hwc2_layer_t layer, hwc_region_t visible) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setVisibleRegion(visible);
}

int32_t  MesonHwc2::setLayerZorder(hwc2_display_t display,
    hwc2_layer_t layer, uint32_t z) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setZorder(z);
}

#ifdef HWC_HDR_METADATA_SUPPORT
int32_t MesonHwc2::setLayerPerFrameMetadata(
        hwc2_display_t display, hwc2_layer_t layer,
        uint32_t numElements, const int32_t* /*hw2_per_frame_metadata_key_t*/ keys,
        const float* metadata) {
    GET_HWC_DISPLAY(display);
    GET_HWC_LAYER(hwcDisplay, layer);
    return hwcLayer->setPerFrameMetadata(numElements, keys, metadata);
}

int32_t MesonHwc2::getPerFrameMetadataKeys(
        hwc2_display_t display,
        uint32_t* outNumKeys,
        int32_t* /*hwc2_per_frame_metadata_key_t*/ outKeys) {
    GET_HWC_DISPLAY(display);
    return hwcDisplay->getFrameMetadataKeys(outNumKeys, outKeys);
}
#endif


/**********************Internal Implement********************/

class MesonHwc2Observer : public Hwc2DisplayObserver {
public:
    MesonHwc2Observer(hwc2_display_t display, MesonHwc2 * hwc) {
        mDispId = display;
        mHwc = hwc;
    }
    ~MesonHwc2Observer() {
        MESON_LOGD("MesonHwc2Observer disp(%d) destruct.", (int)mDispId);
    }

    void refresh() {
        MESON_LOGD("Display (%d) ask for refresh.", (int)mDispId);
        mHwc->refresh(mDispId);
    }

    void onVsync(int64_t timestamp) {
        mHwc->onVsync(mDispId, timestamp);
    }

    void onHotplug(bool connected) {
        mHwc->onHotplug(mDispId, connected);
    }

protected:
    hwc2_display_t mDispId;
    MesonHwc2 * mHwc;
};

MesonHwc2::MesonHwc2() {
    mVirtualDisplayIds = 0;
    mHotplugFn = NULL;
    mHotplugData = NULL;
    mRefreshFn = NULL;
    mRefreshData = NULL;
    mVsyncFn = NULL;
    mVsyncData = NULL;
    initialize();
}

MesonHwc2::~MesonHwc2() {
    mDisplays.clear();
}

void MesonHwc2::refresh(hwc2_display_t  display) {
    if (mRefreshFn) {
        mRefreshFn(mRefreshData, display);
        return;
    }

    MESON_LOGE("No refresh callback registered.");
}

void MesonHwc2::onVsync(hwc2_display_t display, int64_t timestamp) {
    if (mVsyncFn) {
        mVsyncFn(mVsyncData, display, timestamp);
        return;
    }

    MESON_LOGE("No vsync callback registered.");
}

void MesonHwc2::onHotplug(hwc2_display_t display, bool connected) {
    if (display == HWC_DISPLAY_PRIMARY && !HwcConfig::primaryHotplugEnabled()) {
        MESON_LOGD("Primary display not support hotplug.");
        return;
    }

    if (mHotplugFn) {
        MESON_LOGD("On hotplug, Fn: %p, Data: %p, display: %d(%d), connected: %d",
                mHotplugFn, mHotplugData, (int)display, HWC_DISPLAY_PRIMARY, connected);
        mHotplugFn(mHotplugData, display, connected);
    } else {
        MESON_LOGE("No hotplug callback registered.");
    }
}

int32_t MesonHwc2::initialize() {
    uint32_t hwNum, hwcIdx;
    hw_display_id hwId;

    if (HwDisplayManager::getInstance().getHwDisplayIds(&hwNum, NULL) != 0)
        return HWC2_ERROR_NO_RESOURCES;

    hw_display_id * hwIds = new hw_display_id[hwNum];
    if (HwDisplayManager::getInstance().getHwDisplayIds(&hwNum, hwIds) != 0)
        return HWC2_ERROR_NO_RESOURCES;

    /*TODO: how to confirm which hw display is primary display? */
    if (hwNum > HWC_NUM_PHYSICAL_DISPLAY_TYPES)
        hwNum = HWC_NUM_PHYSICAL_DISPLAY_TYPES;

    for (hwcIdx = 0; hwcIdx < hwNum; ++hwcIdx) {
        std::shared_ptr<Hwc2DisplayObserver> displayObserver =
                std::make_shared<MesonHwc2Observer>(hwcIdx, this);
        hwId = hwIds[hwcIdx];

        std::shared_ptr<Hwc2Display> disp =
            std::make_shared<Hwc2Display>(hwId, displayObserver);
        disp->initialize();
        mDisplays.emplace(hwcIdx, disp);
    }

    return HWC2_ERROR_NONE;
}

bool MesonHwc2::isDisplayValid(hwc2_display_t display) {
    std::map<hwc2_display_t, std::shared_ptr<Hwc2Display>>::iterator it =
        mDisplays.find(display);

    if (it != mDisplays.end())
        return true;

    return false;
}

uint32_t MesonHwc2::getVirtualDisplayId() {
    uint32_t idx;
    for (idx = MESON_VIRTUAL_DISPLAY_ID_START; idx < 31; idx ++) {
        if ((mVirtualDisplayIds & (1 << idx)) == 0) {
            mVirtualDisplayIds |= (1 << idx);
            MESON_LOGD("getVirtualDisplayId (%d) to (%x)", idx, mVirtualDisplayIds);
            return idx;
        }
    }

    MESON_LOGE("All virtual display id consumed.\n");
    return 0;
}

void MesonHwc2::freeVirtualDisplayId(uint32_t id) {
    mVirtualDisplayIds &= ~(1 << id);
    MESON_LOGD("freeVirtualDisplayId (%d) to (%x)", id, mVirtualDisplayIds);
}

