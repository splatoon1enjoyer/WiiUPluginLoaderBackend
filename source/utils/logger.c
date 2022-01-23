#ifdef DEBUG
#include <stdint.h>
#include <whb/log_udp.h>
#include <whb/log_cafe.h>
#include <whb/log_module.h>


uint32_t moduleLogInit = false;
uint32_t cafeLogInit = false;
uint32_t udpLogInit = false;
#endif // DEBUG

void initLogging() {
#ifdef DEBUG
    if (!(moduleLogInit = WHBLogModuleInit())) {
        cafeLogInit = WHBLogCafeInit();
        udpLogInit = WHBLogUdpInit();
    }
#endif // DEBUG
}

void deinitLogging() {
#ifdef DEBUG
    if (moduleLogInit) {
        WHBLogMffoduleDeinit();
        moduleLogInit = false;
    }
    if (cafeLogInit) {
        WHBLogCafeDeinit();
        cafeLogInit = false;
    }
    if (udpLogInit) {
        WHBLogUdpDeinit();
        udpLogInit = false;
    }
#endif // DEBUG
}