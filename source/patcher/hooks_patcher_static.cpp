#include "hooks_patcher_static.h"
#include <coreinit/core.h>
#include <coreinit/messagequeue.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

#include "../globals.h"
#include "../hooks.h"

uint8_t vpadPressCooldown  = 0xFF;
bool configMenuOpened      = false;
bool wantsToOpenConfigMenu = false;

DECL_FUNCTION(void, GX2SwapScanBuffers, void) {
    real_GX2SwapScanBuffers();

    if (wantsToOpenConfigMenu && !configMenuOpened) {
        configMenuOpened = true;
        ConfigUtils::openConfigMenu();
        configMenuOpened      = false;
        wantsToOpenConfigMenu = false;
    }
}

DECL_FUNCTION(void, GX2SetTVBuffer, void *buffer, uint32_t buffer_size, int32_t tv_render_mode, GX2SurfaceFormat format, GX2BufferingMode buffering_mode) {
    gStoredTVBuffer.buffer         = buffer;
    gStoredTVBuffer.buffer_size    = buffer_size;
    gStoredTVBuffer.mode           = tv_render_mode;
    gStoredTVBuffer.surface_format = format;
    gStoredTVBuffer.buffering_mode = buffering_mode;

    return real_GX2SetTVBuffer(buffer, buffer_size, tv_render_mode, format, buffering_mode);
}

DECL_FUNCTION(void, GX2SetDRCBuffer, void *buffer, uint32_t buffer_size, uint32_t drc_mode, GX2SurfaceFormat surface_format, GX2BufferingMode buffering_mode) {
    gStoredDRCBuffer.buffer         = buffer;
    gStoredDRCBuffer.buffer_size    = buffer_size;
    gStoredDRCBuffer.mode           = drc_mode;
    gStoredDRCBuffer.surface_format = surface_format;
    gStoredDRCBuffer.buffering_mode = buffering_mode;

    return real_GX2SetDRCBuffer(buffer, buffer_size, drc_mode, surface_format, buffering_mode);
}

static uint32_t lastData0 = 0;

DECL_FUNCTION(uint32_t, OSReceiveMessage, OSMessageQueue *queue, OSMessage *message, uint32_t flags) {
    uint32_t res = real_OSReceiveMessage(queue, message, flags);
    if (queue == OSGetSystemMessageQueue()) {
        if (message != nullptr && res) {
            if (lastData0 != message->args[0]) {
                if (message->args[0] == 0xFACEF000) {
                    CallHook(gLoadedPlugins, WUPS_LOADER_HOOK_ACQUIRED_FOREGROUND);
                } else if (message->args[0] == 0xD1E0D1E0) {
                    // Implemented via WUMS Hook
                }
            }
            lastData0 = message->args[0];
        }
    }
    return res;
}

DECL_FUNCTION(void, OSReleaseForeground) {
    if (OSGetCoreId() == 1) {
        CallHook(gLoadedPlugins, WUPS_LOADER_HOOK_RELEASE_FOREGROUND);
    }
    real_OSReleaseForeground();
}

DECL_FUNCTION(int32_t, VPADRead, int32_t chan, VPADStatus *buffer, uint32_t buffer_size, int32_t *error) {
    int32_t result = real_VPADRead(chan, buffer, buffer_size, error);

    if (result > 0 && (buffer[0].hold == (VPAD_BUTTON_L | VPAD_BUTTON_DOWN | VPAD_BUTTON_MINUS)) && vpadPressCooldown == 0 && !configMenuOpened) {
        wantsToOpenConfigMenu = true;
        vpadPressCooldown     = 0x3C;
    }

    if (vpadPressCooldown > 0) {
        vpadPressCooldown--;
    }
    return result;
}

DECL_FUNCTION(void, WPADRead, WPADChan chan, WPADStatusProController *data) {
    real_WPADRead(chan, data);

    if (!configMenuOpened && data[0].err == 0) {
        if (data[0].extensionType == WPAD_EXT_CORE || data[0].extensionType == WPAD_EXT_NUNCHUK) {
            // button data is in the first 2 bytes for wiimotes
            if (((uint16_t *) data)[0] == (WPAD_BUTTON_B | WPAD_BUTTON_DOWN | WPAD_BUTTON_MINUS)) {
                wantsToOpenConfigMenu = true;
            }
        } else {
            // TODO does this work for classic controllers?
            if (data[0].buttons == (WPAD_CLASSIC_BUTTON_L | WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_MINUS)) {
                wantsToOpenConfigMenu = true;
            }
        }
    }
}

// clang-format off
#define KiReport ((void(*)(const char *, ...)) 0xfff0ad0c)
// clang-format on

#pragma GCC push_options
#pragma GCC optimize("O0")

DECL_FUNCTION(uint32_t, SC17_FindClosestSymbol,
              uint32_t addr,
              uint32_t *outDistance,
              char *symbolNameBuffer,
              uint32_t symbolNameBufferLength,
              char *moduleNameBuffer,
              uint32_t moduleNameBufferLength) {
    for (auto &plugin : gLoadedPlugins) {
        auto sectionInfoOpt = plugin->getPluginInformation()->getSectionInfo(".text");
        if (!sectionInfoOpt) {
            continue;
        }

        auto &sectionInfo = sectionInfoOpt.value();

        if (!sectionInfo->isInSection(addr)) {
            continue;
        }

        strncpy(moduleNameBuffer, plugin->getMetaInformation()->getName().c_str(), moduleNameBufferLength - 1);
        auto functionSymbolDataOpt = plugin->getPluginInformation()->getNearestFunctionSymbolData(addr);
        if (functionSymbolDataOpt) {
            auto &functionSymbolData = functionSymbolDataOpt.value();

            strncpy(symbolNameBuffer, functionSymbolData->getName().c_str(), moduleNameBufferLength);
            if (outDistance) {
                *outDistance = addr - (uint32_t) functionSymbolData->getAddress();
            }
            return 0;
        }

        strncpy(symbolNameBuffer, ".text", symbolNameBufferLength);

        if (outDistance) {
            *outDistance = addr - (uint32_t) sectionInfo->getAddress();
        }

        return 0;
    }

    return real_SC17_FindClosestSymbol(addr, outDistance, symbolNameBuffer, symbolNameBufferLength, moduleNameBuffer, moduleNameBufferLength);
}

DECL_FUNCTION(uint32_t, KiGetAppSymbolName, uint32_t addr, char *buffer, int32_t bufSize) {
    for (auto &plugin : gLoadedPlugins) {
        auto sectionInfoOpt = plugin->getPluginInformation()->getSectionInfo(".text");
        if (!sectionInfoOpt) {
            continue;
        }

        auto &sectionInfo = sectionInfoOpt.value();

        if (!sectionInfo->isInSection(addr)) {
            continue;
        }

        auto pluginNameLen        = strlen(plugin->getMetaInformation()->getName().c_str());
        int32_t spaceLeftInBuffer = (int32_t) bufSize - (int32_t) pluginNameLen - 1;
        if (spaceLeftInBuffer < 0) {
            spaceLeftInBuffer = 0;
        }
        strncpy(buffer, plugin->getMetaInformation()->getName().c_str(), bufSize - 1);

        auto functionSymbolDataOpt = plugin->getPluginInformation()->getNearestFunctionSymbolData(addr);
        if (functionSymbolDataOpt) {
            auto &functionSymbolData  = functionSymbolDataOpt.value();
            buffer[pluginNameLen]     = '|';
            buffer[pluginNameLen + 1] = '\0';
            strncpy(buffer + pluginNameLen + 1, functionSymbolData->getName().c_str(), spaceLeftInBuffer - 1);
        }

        return 0;
    }

    return real_KiGetAppSymbolName(addr, buffer, bufSize);
}

#pragma GCC pop_options

function_replacement_data_t method_hooks_static[] __attribute__((section(".data"))) = {
        REPLACE_FUNCTION(GX2SwapScanBuffers, LIBRARY_GX2, GX2SwapScanBuffers),
        REPLACE_FUNCTION(GX2SetTVBuffer, LIBRARY_GX2, GX2SetTVBuffer),
        REPLACE_FUNCTION(GX2SetDRCBuffer, LIBRARY_GX2, GX2SetDRCBuffer),
        REPLACE_FUNCTION(OSReceiveMessage, LIBRARY_COREINIT, OSReceiveMessage),
        REPLACE_FUNCTION(OSReleaseForeground, LIBRARY_COREINIT, OSReleaseForeground),
        REPLACE_FUNCTION(VPADRead, LIBRARY_VPAD, VPADRead),
        REPLACE_FUNCTION(WPADRead, LIBRARY_PADSCORE, WPADRead),
        REPLACE_FUNCTION_VIA_ADDRESS(SC17_FindClosestSymbol, 0xfff10218, 0xfff10218),
        REPLACE_FUNCTION_VIA_ADDRESS(KiGetAppSymbolName, 0xfff0e3a0, 0xfff0e3a0),

};

uint32_t method_hooks_static_size __attribute__((section(".data"))) = sizeof(method_hooks_static) / sizeof(function_replacement_data_t);