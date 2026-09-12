// ============================================================================
//  ViewerScreen.hpp — picture viewer: BMP files from /pictures on the TF card,
//  decoded into a PSRAM RGB565 buffer and scaled to fit the rounded screen.
//  Swipe left/right — previous/next picture.
// ============================================================================
#pragma once

#include <vector>
#include "ui/App.hpp"
#include "hal/SdHal.hpp"

namespace ui {

class ViewerScreen final : public App {
public:
    void create(AppHost* host) override;
    void show() override;

    const char* title() const override { return "Картинки"; }
    const char* icon() const override { return LV_SYMBOL_IMAGE; }
    uint32_t accent() const override { return 0xCE93D8; }

private:
    static void gesture_cb(lv_event_t* e);

    void reloadList();
    bool loadImage(int16_t index);
    void showStatus(const char* text);

    lv_obj_t* img_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* index_label_ = nullptr;

    std::vector<String> files_;
    bool list_loaded_ = false;
    int16_t current_ = -1;
    uint8_t* pixels_ = nullptr;   // PSRAM RGB565 frame (kLcdWidth x kLcdHeight)
    lv_img_dsc_t img_dsc_ = {};
};

}  // namespace ui
