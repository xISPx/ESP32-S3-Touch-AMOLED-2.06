// ============================================================================
//  DisplayHal.cpp — CO5300 AMOLED bring-up (vendor demo constructor values).
// ============================================================================
#include "hal/DisplayHal.hpp"
#include "core/Logger.hpp"

namespace hal {

namespace {
    constexpr const char* kTag = "Display";
}

core::Status DisplayHal::init() {
    bus_.reset(new Arduino_ESP32QSPI(
        cfg::kLcdCs, cfg::kLcdSclk,
        cfg::kLcdSdio0, cfg::kLcdSdio1, cfg::kLcdSdio2, cfg::kLcdSdio3));

    // NOTE: registry GFX 1.6.0 added a `bool ips` slot as the 4th argument —
    // the vendor-bundled build does not have it.  Passing the vendor demo's
    // 9-argument form verbatim shifts every argument (ips = 410 -> true,
    // w = 502, h = 22), which renders as a pink/garbled panel.  The panel is
    // not IPS-inverted: pass false explicitly.
    gfx_.reset(new Arduino_CO5300(
        bus_.get(), cfg::kLcdReset, cfg::kLcdRotation,
        false /* ips */,
        cfg::kLcdWidth, cfg::kLcdHeight,
        cfg::kLcdColOffset /* col_offset1 */, 0 /* row_offset1 */,
        0 /* col_offset2 */, 0 /* row_offset2 */));

    if (!gfx_->begin()) {
        LOGE(kTag, "CO5300 init failed (QSPI probe error)");
        return core::Status::fail(core::ErrorCode::DisplayInit);
    }

    gfx_->fillScreen(RGB565_BLACK);
    gfx_->setBrightness(brightness_);
    LOGI(kTag, "CO5300 %ux%u ready, brightness=%u",
         static_cast<unsigned>(cfg::kLcdWidth), static_cast<unsigned>(cfg::kLcdHeight),
         static_cast<unsigned>(brightness_));
    return core::Status::ok(core::Unit{});
}

void DisplayHal::setBrightness(uint8_t level) {
    brightness_ = level;
    if (gfx_) gfx_->setBrightness(level);
}

}  // namespace hal
