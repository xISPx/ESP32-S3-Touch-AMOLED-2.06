// ============================================================================
//  UiTask.cpp — LVGL event loop + app host.
// ============================================================================
#include "tasks/UiTask.hpp"
#include "tasks/PowerTask.hpp"  // ButtonId enums shared with PowerTask
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include "core/PwrState.hpp"
#include "core/Settings.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <sys/time.h>
#include <time.h>

namespace tasks {

namespace {
    constexpr const char* kTag = "Ui";

    // ---- LVGL display driver glue (Waveshare direct-render path) -----------
    //
    // Vendor pattern (06_LVGL_Arduino_v9): in DIRECT render mode the flush
    // callback only signals readiness — LVGL renders into the shared
    // full-frame buffer, and the task loop pushes the whole frame to the
    // CO5300 after lv_timer_handler() on every iteration.
    // Power saving: LVGL renders only invalidated areas, so flush_cb fires
    // only when something actually changed.  The task loop pushes the frame
    // to the panel only when this flag is set — idle screens cost zero QSPI.
    void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
        LV_UNUSED(area);
        LV_UNUSED(px_map);
        *static_cast<bool*>(lv_display_get_user_data(disp)) = true;
        lv_display_flush_ready(disp);
    }

    void rounder_cb(lv_event_t* e) {
        // CO5300 QSPI windowing: keep odd-sized areas aligned like the vendor demo.
        lv_area_t* area = static_cast<lv_area_t*>(lv_event_get_param(e));
        area->x1 &= ~0x1;
        area->y1 &= ~0x1;
        area->x2 |= 0x1;
        area->y2 |= 0x1;
    }

    void read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
        auto* self = static_cast<UiTask*>(lv_indev_get_user_data(indev));
        if (!self) return;
        int16_t x = 0, y = 0;
        if (self->hal().touch.readPoint(x, y)) {
            self->noteTouch();
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = x;
            data->point.y = y;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }

    void drain_timer_cb(lv_timer_t* timer) {
        static_cast<UiTask*>(lv_timer_get_user_data(timer))->onDrainTimer();
    }
    void clock_timer_cb(lv_timer_t* timer) {
        static_cast<UiTask*>(lv_timer_get_user_data(timer))->onClockTimer();
    }
}

bool UiTask::initLvgl() {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    disp_ = lv_display_create(cfg::kLcdWidth, cfg::kLcdHeight);
    if (!disp_) return false;

    // Dark theme with the Cyrillic-capable font as the default: dropdown /
    // roller lists and any unstyled widget render readable out of the box.
    lv_theme_default_init(disp_, lv_palette_main(LV_PALETTE_BLUE_GREY),
                          lv_palette_main(LV_PALETTE_AMBER), true,
                          &ui_font_ru_16);

    // Full-frame buffer: internal RAM first, then anything (falls to PSRAM) —
    // same allocation ladder as the vendor demo.  402 KB never fits internal,
    // so it lands in PSRAM, staged into an internal DMA buffer on transfer.
    const size_t buf_bytes = cfg::kLcdWidth * cfg::kLcdHeight * sizeof(uint16_t);
    draw_buf_ = static_cast<uint16_t*>(
        heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!draw_buf_) {
        draw_buf_ = static_cast<uint16_t*>(
            heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!draw_buf_) {
        LOGE(kTag, "draw buffer (%u bytes) allocation failed",
             static_cast<unsigned>(buf_bytes));
        return false;
    }
    memset(draw_buf_, 0, buf_bytes);  // first frame is a defined black screen

    lv_display_set_buffers(disp_, draw_buf_, nullptr, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_user_data(disp_, &frame_dirty_);
    lv_display_set_flush_cb(disp_, flush_cb);
    lv_display_add_event_cb(disp_, rounder_cb, LV_EVENT_INVALIDATE_AREA, this);

    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, read_cb);
    lv_indev_set_user_data(indev_, this);

    return true;
}

bool UiTask::createApps() {
    bool ok = true;
    ok &= host_.add(&watchface_);   // slot 0
    ok &= host_.add(&menu_);        // slot 1
    ok &= host_.add(&status_);
    ok &= host_.add(&fitness_);
    ok &= host_.add(&stopwatch_);
    ok &= host_.add(&chat_);
    ok &= host_.add(&music_);
    ok &= host_.add(&snake_);
    ok &= host_.add(&game2048_);
    ok &= host_.add(&viewer_);
    ok &= host_.add(&settings_);
    if (!ok) {
        LOGE(kTag, "app registry full — some apps are missing from the menu");
    }
    host_.build();
    return true;
}

// ----------------------------------------------------------------------------
//  Power-on animation: spinner ring + fading logo, then fade to the watchface.
// ----------------------------------------------------------------------------

namespace {
    void boot_opa_cb(void* var, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(var), v, 0);
    }
}

void UiTask::createBootScreen() {
    boot_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(boot_screen_, lv_color_black(), 0);
    lv_obj_clear_flag(boot_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // spinning arc
    boot_arc_ = lv_arc_create(boot_screen_);
    lv_obj_set_size(boot_arc_, 150, 150);
    lv_obj_align(boot_arc_, LV_ALIGN_CENTER, 0, -46);
    lv_obj_remove_style(boot_arc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(boot_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(boot_arc_, 0, 360);
    lv_arc_set_angles(boot_arc_, 0, 110);
    lv_obj_set_style_arc_color(boot_arc_, lv_color_hex(0x1B242E), LV_PART_MAIN);
    lv_obj_set_style_arc_color(boot_arc_, lv_color_hex(0x4FC3F7), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(boot_arc_, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(boot_arc_, 8, LV_PART_INDICATOR);
    lv_arc_set_value(boot_arc_, 0);

    lv_anim_t spin;
    lv_anim_init(&spin);
    lv_anim_set_var(&spin, boot_arc_);
    lv_anim_set_exec_cb(&spin, (lv_anim_exec_xcb_t)lv_arc_set_rotation);
    lv_anim_set_values(&spin, 0, 359);
    lv_anim_set_duration(&spin, 1100);
    lv_anim_set_path_cb(&spin, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&spin, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&spin);

    // logo (fades in)
    boot_logo_ = lv_label_create(boot_screen_);
    lv_obj_set_style_text_font(boot_logo_, &ui_font_ru_28, 0);
    lv_obj_set_style_text_color(boot_logo_, lv_color_white(), 0);
    lv_label_set_text(boot_logo_, "Smartwatch");
    lv_obj_align(boot_logo_, LV_ALIGN_CENTER, 0, 66);
    lv_obj_set_style_opa(boot_logo_, LV_OPA_TRANSP, 0);

    boot_sub_ = lv_label_create(boot_screen_);
    lv_obj_set_style_text_font(boot_sub_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(boot_sub_, lv_color_hex(0x8FA3B5), 0);
    lv_label_set_text(boot_sub_, "ESP32-S3  AMOLED 2.06");
    lv_obj_align(boot_sub_, LV_ALIGN_CENTER, 0, 108);
    lv_obj_set_style_opa(boot_sub_, LV_OPA_TRANSP, 0);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_exec_cb(&fade, boot_opa_cb);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade, 600);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);

    lv_anim_set_var(&fade, boot_logo_);
    lv_anim_set_delay(&fade, 200);
    lv_anim_start(&fade);

    lv_anim_set_var(&fade, boot_sub_);
    lv_anim_set_delay(&fade, 550);
    lv_anim_start(&fade);

    lv_screen_load_anim(boot_screen_, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // hand over to the watchface
    lv_timer_t* done = lv_timer_create(finish_boot_cb, 2100, this);
    lv_timer_set_repeat_count(done, 1);
}

void UiTask::finish_boot_cb(lv_timer_t* timer) {
    auto* self = static_cast<UiTask*>(lv_timer_get_user_data(timer));
    if (!self) return;
    // stop the boot animations before the objects go away
    lv_anim_delete(self->boot_arc_, nullptr);
    lv_anim_delete(self->boot_logo_, nullptr);
    lv_anim_delete(self->boot_sub_, nullptr);
    if (ui::App* wf = self->host_.app(0)) {
        lv_screen_load_anim(wf->root(), LV_SCR_LOAD_ANIM_FADE_IN, 450, 0, true);
    }
    self->boot_screen_ = nullptr;
}

void UiTask::onDrainTimer() {
    // Event-driven redraws only: a full-frame AMOLED push costs ~20 ms of
    // QSPI time, so the screens are touched strictly when data changes.
    bool telemetry_dirty = false;
    bool steps_dirty = false;
    bool imu_dirty = false;
    bool model_dirty = false;

    core::Event e;
    while (waitFor(e, 0)) {
        model_dirty = true;
        switch (e.type) {
            case core::EventType::Battery:
                model_.battery_percent = e.battery.percent;
                model_.battery_mv = e.battery.voltage_mv;
                model_.vbus_mv = e.battery.vbus_mv;
                model_.temperature_c = e.battery.temperature_c;
                model_.charging = e.battery.charging;
                model_.battery_present = e.battery.present;
                telemetry_dirty = true;
                break;

            case core::EventType::Button:
                if (shutting_down_) break;  // ignore input during power-off
                model_.last_button_id = e.button.button_id;
                if (e.button.pressed) {
                    if (e.button.button_id == kBtnBoot) {
                        // BOOT: watchface -> launcher; anywhere else -> back.
                        if (host_.current() == 0) host_.open(1);
                        else host_.back();
                    } else if (e.button.button_id == kBtnPwr) {
                        // Tap: wake from off, otherwise toggle idle brightness.
                        // (Long hold = power off, PowerTask → Shutdown event.)
                        if (screen_off_) {
                            noteTouch();
                        } else {
                            dimmed_ = !dimmed_;
                            hal_.display.setBrightness(
                                dimmed_ ? cfg::kBrightnessIdle
                                        : core::Settings::instance().d().brightness);
                        }
                    }
                }
                break;

            case core::EventType::TimeSync:
                model_.time_synced = e.time.synced;
                if (e.time.synced) {
                    hal_.rtc.storeSystemClockToRtc();
                }
                telemetry_dirty = true;
                break;

            case core::EventType::NetStatus:
                model_.wifi_connected = e.net.connected;
                model_.rssi = e.net.rssi;
                telemetry_dirty = true;
                break;

            case core::EventType::StepCount:
                model_.steps = e.steps.steps;
                steps_dirty = true;
                break;

            case core::EventType::ImuSample:
                model_.ax_mg = e.imu.ax; model_.ay_mg = e.imu.ay; model_.az_mg = e.imu.az;
                model_.gx_md = e.imu.gx; model_.gy_md = e.imu.gy; model_.gz_md = e.imu.gz;
                imu_dirty = true;
                break;

            case core::EventType::SettingsChanged:
                // NVS writes happen here (internal stack): PSRAM-stack tasks
                // request persistence via the kSettingsPersist flag.
                if (e.settings.mask & core::kSettingsPersist) {
                    core::Settings::instance().save();
                }
                // Brightness lives in settings; display applies it live.
                if (e.settings.mask & core::kSettingsDisplay) {
                    if (!dimmed_ && !screen_off_) {
                        hal_.display.setBrightness(
                            core::Settings::instance().d().brightness);
                    }
                }
                break;

            case core::EventType::AiText:
                host_.dispatchAi(e);  // chat app buffers even while hidden
                break;

            case core::EventType::Wake:
                noteTouch();  // shake wakes the dimmed screen
                break;

            case core::EventType::Shutdown:
                showShutdownScreen();  // PowerTask cuts power ~2 s later
                break;
        }
    }

    if (model_dirty) host_.onModel(model_);
    LV_UNUSED(telemetry_dirty);
    LV_UNUSED(steps_dirty);
    LV_UNUSED(imu_dirty);
}

void UiTask::onClockTimer() {
    if (shutting_down_) return;  // freeze UI state during power-off
    if (host_.current() == 0) watchface_.onModel(model_);  // 1 Hz clock text
    updateIdleDim();
}

void UiTask::showShutdownScreen() {
    if (shutting_down_) return;
    shutting_down_ = true;

    // The panel may be in deep sleep (screen-off stage): wake it and go to
    // full brightness so the user actually sees the screen during the 2 s
    // grace window before the PMU cuts the power.
    if (screen_off_) {
        screen_off_ = false;
        hal().display.gfx()->displayOn();
        frame_dirty_ = true;
    }
    dimmed_ = false;
    hal().display.setBrightness(cfg::kBrightnessFull);
    core::pwr::uiAwake.store(true);

    shutdown_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(shutdown_screen_, lv_color_black(), 0);
    lv_obj_clear_flag(shutdown_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon = lv_label_create(shutdown_screen_);
    lv_obj_set_style_text_font(icon, &ui_font_ru_28, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(icon, LV_SYMBOL_POWER);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t* label = lv_label_create(shutdown_screen_);
    lv_obj_set_style_text_font(label, &ui_font_ru_28, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "Выключение…");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t* hint = lv_label_create(shutdown_screen_);
    lv_obj_set_style_text_font(hint, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(hint, "Для включения зажмите кнопку PWR");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 60);

    lv_screen_load_anim(shutdown_screen_, LV_SCR_LOAD_ANIM_FADE_IN, 350, 0,
                        false);
    LOGI("Ui", "shutdown screen shown");
}

void UiTask::noteTouch() {
    last_touch_ms_ = millis();
    core::pwr::uiAwake.store(true);
    const bool was_dim = dimmed_;
    const bool was_off = screen_off_;
    dimmed_ = false;
    screen_off_ = false;
    if (was_off) {
        // Sleep-out resets the panel; restore brightness and repaint fully.
        hal().display.gfx()->displayOn();
        hal().display.setBrightness(core::Settings::instance().d().brightness);
        hal().display.gfx()->draw16bitRGBBitmap(
            0, 0, draw_buf_, cfg::kLcdWidth, cfg::kLcdHeight);
        frame_dirty_ = false;
    } else if (was_dim) {
        hal().display.setBrightness(core::Settings::instance().d().brightness);
    }
}

void UiTask::updateIdleDim() {
    if (shutting_down_ || screen_off_) return;
    const uint16_t timeout_s = core::Settings::instance().d().dimTimeoutS;
    if (timeout_s == 0) return;  // "never dim"
    const uint32_t idle = millis() - last_touch_ms_;
    if (!dimmed_ && idle > timeout_s * 1000UL) {
        dimmed_ = true;
        hal().display.setBrightness(cfg::kBrightnessIdle);
    } else if (dimmed_ && idle > timeout_s * 1000UL + cfg::kScreenOffAfterMs) {
        // Deep stage: panel sleep — the display draws nothing and the touch
        // loop slows down until touch / shake / PWR wake it again.
        dimmed_ = false;
        screen_off_ = true;
        core::pwr::uiAwake.store(false);
        hal().display.gfx()->displayOff();
    }
}

void UiTask::run() {
    if (!initLvgl()) {
        LOGE(kTag, "LVGL init failed — UI task exits");
        core::SystemHealth::mark(core::ModuleBit::Display);
        vTaskSuspend(nullptr);  // stay parked; watchdog will report
    }
    if (!createApps()) {
        LOGE(kTag, "app creation failed");
    }
    LOGI(kTag, "apps built — boot screen");
    createBootScreen();  // power-on animation, then fade to the watchface

    core::EventBus::instance().subscribe(mailbox());

    // Persistent display preferences.
    hal_.display.setBrightness(core::Settings::instance().d().brightness);

    drain_timer_ = lv_timer_create(drain_timer_cb, cfg::kLvglTimerTickMs, this);
    clock_timer_ = lv_timer_create(clock_timer_cb, 1000, this);

    LOGI(kTag, "UI loop running");
    while (true) {
        lv_timer_handler();
        // Push the frame to the panel only when LVGL actually rendered
        // something (flush_cb sets the flag).  With the screen asleep the
        // loop idles at 30 ms — touch still polls via lv_timer_handler and
        // noteTouch() wakes the panel from the read callback instantly.
        if (frame_dirty_) {
            frame_dirty_ = false;
            if (!screen_off_) {
                hal_.display.gfx()->draw16bitRGBBitmap(
                    0, 0, draw_buf_, cfg::kLcdWidth, cfg::kLcdHeight);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(screen_off_ ? 30 : 5));
    }
}

}  // namespace tasks
