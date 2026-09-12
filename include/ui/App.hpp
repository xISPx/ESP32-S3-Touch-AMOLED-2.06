// ============================================================================
//  App.hpp — base class for every watch application.
//
//  An App owns exactly one LVGL screen (root_).  AppHost instantiates,
//  navigates between them and feeds them the shared UiModel snapshot.
//  All lv_*() calls happen in UiTask, so no locking is required.
// ============================================================================
#pragma once

#include <cstdint>
#include "lvgl.h"
#include "ui/UiModel.hpp"
#include "core/Event.hpp"

namespace ui {

class AppHost;

class App {
public:
    virtual ~App() = default;

    // Menu metadata (apps with an empty title are not shown in the launcher).
    virtual const char* title() const { return ""; }
    virtual const char* icon() const { return LV_SYMBOL_VIDEO; }
    virtual uint32_t accent() const { return 0x4FC3F7; }  // icon tint, RGB888

    // Build the screen.  host_ is set before the call.
    virtual void create(AppHost* host) {
        host_ = host;
        root_ = nullptr;
    }

    // Lifecycle: becoming / leaving the active screen.
    virtual void show() {}
    virtual void hide() {}

    // Data-driven refresh.  Called for the active app on every model change;
    // ChatScreen also receives AI messages while hidden (it buffers them).
    virtual void onModel(const UiModel& m) { LV_UNUSED(m); }

    // AI assistant traffic (user query echo / reply / error).  Only the chat
    // app reacts; everything else ignores it.
    virtual void onAiText(const core::Event& e) { LV_UNUSED(e); }

    // Back navigation (header chevron + swipe-right).  Apps with internal
    // sub-pages override it to unwind their own stack first.
    virtual void onBack();

    lv_obj_t* root() const { return root_; }

    // Set to false before createChrome() to omit the floating back button
    // (apps that provide their own navigation, e.g. SettingsScreen).
    bool chrome_back_ = true;

protected:
    // Shared chrome: black screen + header (back chevron, title).  Returns
    // the content container below the header (fills the rest of the screen).
    lv_obj_t* createChrome(const char* titleText);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* chrome_title_ = nullptr;  // header title (SettingsScreen retitles pages)
    AppHost* host_ = nullptr;
};

}  // namespace ui
