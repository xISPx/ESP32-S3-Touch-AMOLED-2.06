// ============================================================================
//  UiModel.hpp — latest state snapshot handed to the UI layer.
//
//  The UI does not keep scattered copies of task data: producers publish
//  Event messages, UiTask folds them into this small POD snapshot, and
//  screens re-render from it.  Access is confined to UiTask, so no lock is
//  required (single-writer/single-reader in one task).
// ============================================================================
#pragma once

#include <cstdint>

namespace ui {

struct UiModel {
    // Battery (from Event::Battery)
    uint8_t  battery_percent = 0;
    uint16_t battery_mv = 0;
    uint16_t vbus_mv = 0;
    int8_t   temperature_c = 0;
    bool     charging = false;
    bool     battery_present = false;

    // Network / time
    bool     wifi_connected = false;
    int8_t   rssi = 0;
    bool     time_synced = false;
    bool     rtc_valid = false;

    // Activity (from Event::StepCount / ImuSample)
    uint32_t steps = 0;
    int16_t  ax_mg = 0, ay_mg = 0, az_mg = 0;  // milli-g
    int16_t  gx_md = 0, gy_md = 0, gz_md = 0;  // milli-dps

    // Buttons
    uint32_t last_button_id = 0;
};

}  // namespace ui
