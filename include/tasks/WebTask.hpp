// ============================================================================
//  WebTask.hpp — remote access over Wi-Fi (Arduino WebServer, port 80):
//
//    GET  /                 embedded dark page: 7 tabs (status/display/sound/
//                           wifi/ai/time/files), per-section save buttons
//    GET  /values           JSON snapshot of all non-secret settings (+tz list)
//    GET  /status           JSON telemetry (battery, net, heap, steps, time, sd)
//    GET  /files            JSON listing of /pictures and /music on the SD card
//    GET  /scan[?start=1]   Wi-Fi scan state machine (async driver scan)
//    POST /settings         apply parameters (empty secrets keep current)
//    POST /upload?dir=...   multipart upload (BMP -> /pictures, WAV -> /music)
//    POST /delete           remove one file from /pictures or /music
//    POST /reboot           restart the watch (response flushes first)
//
//  Runs its own task (internal stack, NVS writes allowed); SD_MMC access is
//  serialized by the SDMMC host driver.  Serves in both STA and AP modes.
// ============================================================================
#pragma once

#include "core/TaskBase.hpp"
#include "hal/Hal.hpp"

namespace tasks {

class WebTask final : public core::TaskBase {
public:
    explicit WebTask(hal::Hal& hal) : hal_(hal) {}
    hal::Hal& hal() { return hal_; }

private:
    void run() override;

    hal::Hal& hal_;
    bool server_up_ = false;
};

}  // namespace tasks
