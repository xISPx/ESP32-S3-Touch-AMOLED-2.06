// ============================================================================
//  ViewerScreen.cpp — BMP viewer (24/32-bit uncompressed), scaled to fit.
// ============================================================================
#include "ui/ViewerScreen.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <cstring>

namespace ui {

namespace {
    constexpr const char* kTag = "Viewer";

    bool endsWithBmp(const String& name) {
        const char* p = name.c_str();
        const size_t n = strlen(p);
        return n > 4 && strcasecmp(p + n - 4, ".bmp") == 0;
    }
}

void ViewerScreen::gesture_cb(lv_event_t* e) {
    auto* self = static_cast<ViewerScreen*>(lv_event_get_user_data(e));
    if (!self || self->files_.empty()) return;
    switch (lv_indev_get_gesture_dir(lv_indev_active())) {
        case LV_DIR_LEFT:  self->loadImage((self->current_ + 1) % self->files_.size()); break;
        case LV_DIR_RIGHT: self->loadImage((self->current_ - 1 + self->files_.size()) %
                                           self->files_.size()); break;
        default: return;
    }
}

void ViewerScreen::reloadList() {
    files_.clear();
    const char* dirs[] = {"/pictures", "/"};
    for (const char* dir : dirs) {
        File root = SD_MMC.open(dir);
        if (!root || !root.isDirectory()) continue;
        File f;
        while ((f = root.openNextFile())) {
            if (!f.isDirectory() && endsWithBmp(f.name())) {
                String path = String(dir) + String(dir[1] == '\0' ? "" : "/");
                files_.push_back(path + f.name());
            }
            f.close();
        }
        root.close();
    }
}

void ViewerScreen::showStatus(const char* text) {
    lv_label_set_text(status_, text);
    lv_obj_remove_flag(status_, LV_OBJ_FLAG_HIDDEN);
}

// Decode an uncompressed BMP (24/32 bpp) into the PSRAM RGB565 frame,
// nearest-neighbour scaled to fit the 410x502 screen.
bool ViewerScreen::loadImage(int16_t index) {
    if (index < 0 || index >= static_cast<int16_t>(files_.size())) return false;

    File f = SD_MMC.open(files_[index]);
    if (!f) { showStatus("Файл не открылся"); return false; }

    uint8_t hdr[54];
    if (f.read(hdr, 54) != 54 || memcmp(hdr, "BM", 2) != 0) {
        f.close(); showStatus("Не BMP-файл"); return false;
    }
    const uint32_t dataOff = static_cast<uint32_t>(hdr[10]) |
                             (static_cast<uint32_t>(hdr[11]) << 8) |
                             (static_cast<uint32_t>(hdr[12]) << 16) |
                             (static_cast<uint32_t>(hdr[13]) << 24);
    const int32_t width = static_cast<int32_t>(hdr[18]) |
                          (static_cast<int32_t>(hdr[19]) << 8) |
                          (static_cast<int32_t>(hdr[20]) << 16) |
                          (static_cast<int32_t>(hdr[21]) << 24);
    const int32_t heightRaw = static_cast<int32_t>(hdr[22]) |
                              (static_cast<int32_t>(hdr[23]) << 8) |
                              (static_cast<int32_t>(hdr[24]) << 16) |
                              (static_cast<int32_t>(hdr[25]) << 24);
    const uint16_t bpp = static_cast<uint16_t>(hdr[28]) |
                         (static_cast<uint16_t>(hdr[29]) << 8);
    const uint32_t compression = static_cast<uint32_t>(hdr[30]);

    const bool bottomUp = heightRaw > 0;
    const int32_t h = bottomUp ? heightRaw : -heightRaw;
    if (width <= 0 || h <= 0 || (bpp != 24 && bpp != 32) || compression != 0) {
        f.close(); showStatus("Формат не поддержан\n(нужен BMP 24/32 бита)"); return false;
    }

    f.seek(dataOff);

    // scale factor: fit into the screen
    const float scale = (width > cfg::kLcdWidth || h > cfg::kLcdHeight)
                            ? (cfg::kLcdWidth > cfg::kLcdHeight)
                                  ? 1.0f * cfg::kLcdHeight / h : 1.0f * cfg::kLcdWidth / width
                            : 1.0f;
    int32_t dstW = static_cast<int32_t>(width * scale);
    int32_t dstH = static_cast<int32_t>(h * scale);
    if (dstW > cfg::kLcdWidth) { dstW = cfg::kLcdWidth; }
    if (dstH > cfg::kLcdHeight) { dstH = cfg::kLcdHeight; }
    const int32_t offX = (cfg::kLcdWidth - dstW) / 2;
    const int32_t offY = (cfg::kLcdHeight - dstH) / 2;

    // black backdrop
    for (int32_t y = 0; y < cfg::kLcdHeight; ++y)
        for (int32_t x = 0; x < cfg::kLcdWidth; ++x)
            pixels_[(y * cfg::kLcdWidth) + x] = 0;

    const uint32_t rowBytes = ((width * (bpp / 8) + 3) / 4) * 4;
    uint8_t* row = static_cast<uint8_t*>(malloc(rowBytes));
    if (!row) { f.close(); showStatus("Не хватило памяти"); return false; }

    for (int32_t sy = 0; sy < h; ++sy) {
        const int32_t srcRow = bottomUp ? (h - 1 - sy) : sy;
        if (f.read(row, rowBytes) != rowBytes) break;
        const int32_t dy = offY + static_cast<int32_t>(sy * scale);
        if (dy < 0 || dy >= cfg::kLcdHeight) continue;
        for (int32_t dxPix = 0; dxPix < dstW; ++dxPix) {
            const int32_t sx = static_cast<int32_t>(dxPix / scale);
            if (sx >= width) continue;
            const uint8_t* px = row + sx * (bpp / 8);
            const uint8_t b = px[0], g = px[1], r = px[2];
            pixels_[(dy * cfg::kLcdWidth) + offX + dxPix] =
                static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    free(row);
    f.close();

    // hand the frame to LVGL
    img_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    img_dsc_.header.w = cfg::kLcdWidth;
    img_dsc_.header.h = cfg::kLcdHeight;
    img_dsc_.data = reinterpret_cast<const uint8_t*>(pixels_);
    img_dsc_.data_size = cfg::kLcdWidth * cfg::kLcdHeight * 2;
    lv_img_set_src(img_, &img_dsc_);

    current_ = index;
    const char* base = strrchr(files_[index].c_str(), '/');
    lv_label_set_text(name_label_, base ? base + 1 : files_[index].c_str());
    lv_label_set_text_fmt(index_label_, "%d / %u", index + 1,
                          static_cast<unsigned>(files_.size()));
    lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
    LOGI(kTag, "shown %s (%dx%d)", files_[index].c_str(), width, h);
    return true;
}

void ViewerScreen::show() {
    if (!list_loaded_) {
        list_loaded_ = true;
        reloadList();
    }
    if (files_.empty()) {
        showStatus("Нет BMP-картинок\nв папке /pictures на карте");
        return;
    }
    if (current_ < 0) loadImage(0);
}

void ViewerScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());
    lv_obj_set_style_pad_all(content, 0, 0);

    pixels_ = static_cast<uint8_t*>(
        heap_caps_malloc(cfg::kLcdWidth * cfg::kLcdHeight * 2, MALLOC_CAP_SPIRAM));
    if (!pixels_) {
        LOGE(kTag, "frame buffer alloc failed");
        return;
    }

    img_ = lv_img_create(content);
    lv_obj_set_size(img_, cfg::kLcdWidth, cfg::kLcdHeight - 84);
    lv_obj_align(img_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(img_, 0, 0);
    lv_obj_add_event_cb(img_, gesture_cb, LV_EVENT_GESTURE, this);

    name_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(name_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(name_label_, lv_color_white(), 0);
    lv_label_set_text(name_label_, "");
    lv_obj_align(name_label_, LV_ALIGN_BOTTOM_LEFT, 14, -14);

    index_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(index_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(index_label_, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(index_label_, "");
    lv_obj_align(index_label_, LV_ALIGN_BOTTOM_RIGHT, -14, -14);

    status_ = lv_label_create(root_);
    lv_obj_set_style_text_font(status_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(status_, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(status_);
    lv_label_set_text(status_, "Картинки не загружены");
}

}  // namespace ui
