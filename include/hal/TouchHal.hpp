// ============================================================================
//  TouchHal.hpp — FocalTech FT3168 capacitive touch (SensorLib TouchDrv).
// ============================================================================
#pragma once

#include <cstdint>
#include "core/Error.hpp"
#include "TouchDrvFT6X36.hpp"

namespace hal {

class TouchHal {
public:
    TouchHal() = default;
    ~TouchHal() = default;
    TouchHal(const TouchHal&) = delete;
    TouchHal& operator=(const TouchHal&) = delete;

    core::Status init();

    // Reads the first finger position.  Returns false when nothing is pressed
    // or when the bus failed; coordinates are only valid on true.
    bool readPoint(int16_t& x, int16_t& y);

private:
    TouchDrvFT6X36 touch_;
};

}  // namespace hal
