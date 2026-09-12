// ============================================================================
//  AppMenuScreen.hpp — smartwatch-style launcher: scrollable icon grid.
// ============================================================================
#pragma once

#include "ui/App.hpp"

namespace ui {

class AppMenuScreen final : public App {
public:
    void create(AppHost* host) override;

    // Not reachable "back" — the host turns swipe-right into the watchface.
    void show() override {}

private:
    static void open_app_cb(lv_event_t* e);
};

}  // namespace ui
