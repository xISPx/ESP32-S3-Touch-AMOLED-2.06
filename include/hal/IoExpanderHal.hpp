// ============================================================================
//  IoExpanderHal.hpp — XL9555 16-bit I/O expander.
//  On this board the side PWR button is wired to expander input EXIO6
//  (reads HIGH while pressed); the expander is optional — when absent the
//  module degrades and the watch falls back to the BOOT button only.
// ============================================================================
#pragma once

#include <cstdint>
#include "core/Error.hpp"
#include "ExtensionIOXL9555.hpp"

namespace hal {

class IoExpanderHal {
public:
    IoExpanderHal() = default;
    ~IoExpanderHal() = default;
    IoExpanderHal(const IoExpanderHal&) = delete;
    IoExpanderHal& operator=(const IoExpanderHal&) = delete;

    core::Status init();

    // True while the physical PWR button is held (needs a working expander).
    bool powerButtonPressed();

private:
    ExtensionIOXL9555 io_;
    bool ready_ = false;
};

}  // namespace hal
