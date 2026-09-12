// ============================================================================
//  MusicScreen.cpp — WAV player app.
// ============================================================================
#include "ui/MusicScreen.hpp"
#include "ui/AppHost.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"
#include "core/Settings.hpp"
#include "hal/Hal.hpp"
#include <SD_MMC.h>
#include <atomic>
#include <cstring>
#include <stdio.h>

namespace ui {

namespace {
    constexpr const char* kTag = "Music";
    constexpr uint32_t kChunkFrames = 1152;  // ~24-144 ms depending on rate
}

// ----------------------------------------------------------------------------
//  Player task
// ----------------------------------------------------------------------------

void MusicScreen::playerTrampoline(void* arg) {
    static_cast<MusicScreen*>(arg)->playerLoop();
}

void MusicScreen::post(Cmd c, uint8_t idx) {
    if (!cmd_q_) return;
    uint8_t msg[2] = {static_cast<uint8_t>(c), idx};
    xQueueSend(cmd_q_, msg, 0);
}

void MusicScreen::playerLoop() {
    for (;;) {
        // ---- wait for a command ------------------------------------------------
        uint8_t msg[2];
        if (xQueueReceive(cmd_q_, msg, portMAX_DELAY) != pdTRUE) continue;
        const Cmd cmd = static_cast<Cmd>(msg[0]);
        int idx = msg[1];

        bool resume = false;
        if (cmd == Cmd::Toggle) {
            if (state_ == 1) {  // playing -> pause
                state_ = 2;
                hal_.audio.paEnable(false);
                continue;
            }
            if (state_ == 2 && current_ >= 0) {  // paused -> resume from pos_
                resume = true;
                idx = current_;
            } else if (current_ >= 0) {
                idx = current_;  // idle -> replay current track from the start
            } else {
                continue;
            }
        } else if (cmd == Cmd::Stop) {
            state_ = 0;
            hal_.audio.paEnable(false);
            continue;
        } else if (cmd == Cmd::Next) {
            idx = (current_ + 1) % static_cast<int>(tracks_.size());
        } else if (cmd == Cmd::Prev) {
            idx = (current_ - 1 + tracks_.size()) % static_cast<int>(tracks_.size());
        }

        // ---- play track idx ------------------------------------------------------
        {
            // tracks_ is loaded once by UiTask before any command can arrive.
            File f = SD_MMC.open(tracks_[idx]);
            hal::WavInfo wav;
            if (!f || !hal::SdHal::instance().parseWav(f, wav)) {
                LOGW(kTag, "cannot open/parse %s", tracks_[idx].c_str());
                f.close();
                state_ = 0;
                continue;
            }

            hal_.audio.setRate(wav.sampleRate);
            total_ = wav.totalSamples;
            rate_ = wav.sampleRate;
            current_ = idx;
            const uint32_t frameBytes = 2 * wav.channels;
            if (resume) {
                if (pos_ >= total_) pos_ = 0;
                f.seek(wav.dataOffset + pos_ * frameBytes);
                LOGI(kTag, "resume %s at frame %lu", tracks_[idx].c_str(),
                     static_cast<unsigned long>(pos_));
            } else {
                pos_ = 0;
            }
            state_ = 1;
            LOGI(kTag, "playing %s (%u Hz, ch%u, %u s)", tracks_[idx].c_str(),
                 static_cast<unsigned>(wav.sampleRate), wav.channels,
                 static_cast<unsigned>(wav.totalSamples / wav.sampleRate));

            static int16_t raw[2 * kChunkFrames];
            static int16_t mono[kChunkFrames];
            hal_.audio.paEnable(true);

            while (state_ == 1) {
                // Serve pause/stop/switch quickly between chunks.
                uint8_t peek[2];
                if (xQueueReceive(cmd_q_, peek, 0) == pdTRUE) {
                    const Cmd c = static_cast<Cmd>(peek[0]);
                    if (c == Cmd::Toggle) {
                        state_ = (state_ == 1) ? 2 : 1;
                        if (state_ == 2) hal_.audio.paEnable(false);
                        continue;
                    }
                    if (c == Cmd::Stop) {
                        state_ = 0;
                        break;
                    }
                    // Next/Prev/Play: end this track and requeue the request —
                    // the outer loop opens the new one cleanly.
                    state_ = 0;
                    uint8_t again[2] = {peek[0], peek[1]};
                    xQueueSend(cmd_q_, again, 0);
                    break;
                }

                const size_t want = kChunkFrames * frameBytes;
                const int got = f.read(reinterpret_cast<uint8_t*>(raw), want);
                if (got <= 0) break;  // end of track

                const size_t frames = got / frameBytes;
                if (wav.channels == 1) {
                    memcpy(mono, raw, frames * 2);
                } else {
                    for (size_t i = 0; i < frames; ++i) {
                        mono[i] = static_cast<int16_t>((raw[2 * i] + raw[2 * i + 1]) / 2);
                    }
                }
                hal_.audio.playPcm(mono, frames);
                pos_ += frames;
            }

            f.close();
            if (state_ == 1) {  // track finished naturally -> auto next
                state_ = 0;
                hal_.audio.paEnable(false);
                if (tracks_.size() > 1) post(Cmd::Next);
            }
        }
    }
}

// ----------------------------------------------------------------------------
//  UI callbacks
// ----------------------------------------------------------------------------

void MusicScreen::play_cb(lv_event_t* e) {
    auto* self = static_cast<MusicScreen*>(lv_event_get_user_data(e));
    if (self) self->post(Cmd::Toggle);
}

void MusicScreen::next_cb(lv_event_t* e) {
    auto* self = static_cast<MusicScreen*>(lv_event_get_user_data(e));
    if (self && !self->tracks_.empty()) self->post(Cmd::Next);
}

void MusicScreen::prev_cb(lv_event_t* e) {
    auto* self = static_cast<MusicScreen*>(lv_event_get_user_data(e));
    if (self && !self->tracks_.empty()) self->post(Cmd::Prev);
}

void MusicScreen::track_cb(lv_event_t* e) {
    auto* self = static_cast<MusicScreen*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!self || !btn) return;
    const auto idx = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn));
    self->post(Cmd::Play, static_cast<uint8_t>(idx));
}

void MusicScreen::volume_cb(lv_event_t* e) {
    auto* self = static_cast<MusicScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        const uint8_t v = static_cast<uint8_t>(lv_slider_get_value(self->volume_slider_));
        self->hal_.audio.setVolume(v);
        core::Settings::instance().d().volume = v;
        core::Settings::instance().save();  // UiTask: internal stack, NVS safe
    }
}

// ----------------------------------------------------------------------------
//  UI refresh / list
// ----------------------------------------------------------------------------

void MusicScreen::refreshUi() {
    if (lv_screen_active() != root_) return;

    const uint8_t st = state_;
    if (play_label_) {
        lv_label_set_text(play_label_,
                          st == 1 ? "\xEF\x81\x8C"   // pause
                                  : "\xEF\x81\x8B"); // play
    }
    const uint32_t pos = pos_, total = total_;
    if (bar_ && total) {
        lv_bar_set_value(bar_, static_cast<int32_t>((pos * 100UL) / total),
                         LV_ANIM_OFF);
    }
    if (time_label_) {
        const uint32_t rate = rate_;
        char buf[24];
        if (total && rate) {
            snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu",
                     static_cast<unsigned long>(pos / rate / 60),
                     static_cast<unsigned long>((pos / rate) % 60),
                     static_cast<unsigned long>(total / rate / 60),
                     static_cast<unsigned long>((total / rate) % 60));
        } else {
            snprintf(buf, sizeof(buf), "--:-- / --:--");
        }
        lv_label_set_text(time_label_, buf);
    }
    if (title_label_ && current_ >= 0 && current_ < static_cast<int>(tracks_.size())) {
        const char* base = tracks_[current_].c_str();
        const char* slash = strrchr(base, '/');
        lv_label_set_text(title_label_, slash ? slash + 1 : base);
    }
}

void MusicScreen::rebuildList() {
    if (!list_) return;
    lv_obj_clean(list_);
    if (tracks_.empty()) {
        sd_status_ = lv_label_create(list_);
        lv_obj_set_style_text_font(sd_status_, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(sd_status_, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(sd_status_,
                          tracks_loaded_
                              ? "Нет WAV-файлов (16 бит PCM)\nна карте в / или /music"
                              : "TF-карта не найдена");
        lv_label_set_long_mode(sd_status_, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(sd_status_, lv_pct(95));
        return;
    }

    for (size_t i = 0; i < tracks_.size(); ++i) {
        lv_obj_t* btn = lv_btn_create(list_);
        lv_obj_set_size(btn, lv_pct(100), 56);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x141A21), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x241A2E), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(btn, track_cb, LV_EVENT_CLICKED, this);

        lv_obj_t* ic = lv_label_create(btn);
        lv_obj_set_style_text_font(ic, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(accent()), 0);
        lv_label_set_text(ic, "\xEF\x80\x81");  // FA audio
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);

        const char* base = tracks_[i].c_str();
        const char* slash = strrchr(base, '/');
        lv_obj_t* name = lv_label_create(btn);
        lv_obj_set_style_text_font(name, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, slash ? slash + 1 : base);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, cfg::kLcdWidth - 140);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 46, 0);
    }
}

// ----------------------------------------------------------------------------
//  Build / lifecycle
// ----------------------------------------------------------------------------

void MusicScreen::onAiText(const core::Event& e) {
    // The assistant owns the speaker: stop music when it starts talking or
    // when a PTT session begins.
    const auto kind = static_cast<core::AiMsgKind>(e.ai.kind);
    if ((kind == core::AiMsgKind::Speaking && e.ai.text[0] == '1') ||
        kind == core::AiMsgKind::VoiceStart) {
        post(Cmd::Stop);
    }
}

void MusicScreen::show() {
    if (!tracks_loaded_) {
        tracks_loaded_ = true;
        tracks_ = hal::SdHal::instance().listTracks();
        rebuildList();
    }
}

void MusicScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 10, 0);

    // ---- now playing card ------------------------------------------------------
    lv_obj_t* card = lv_obj_create(content);
    lv_obj_set_size(card, lv_pct(100), 116);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141A21), 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    title_label_ = lv_label_create(card);
    lv_obj_set_style_text_font(title_label_, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_white(), 0);
    lv_label_set_text(title_label_, "—");
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(title_label_, cfg::kLcdWidth - 80);
    lv_obj_align(title_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    time_label_ = lv_label_create(card);
    lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_hex(0x8FA3B5), 0);
    lv_label_set_text(time_label_, "--:-- / --:--");
    lv_obj_align(time_label_, LV_ALIGN_TOP_RIGHT, 0, 40);

    bar_ = lv_bar_create(card);
    lv_obj_set_size(bar_, cfg::kLcdWidth - 80, 10);
    lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(0x2E3B49), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(accent()), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_, 5, LV_PART_INDICATOR);

    // ---- transport controls ------------------------------------------------------
    lv_obj_t* controls = lv_obj_create(content);
    lv_obj_set_size(controls, lv_pct(100), 92);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_clear_flag(controls, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* prev = lv_btn_create(controls);
    lv_obj_set_size(prev, 76, 76);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 44, 0);
    lv_obj_set_style_bg_color(prev, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_radius(prev, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(prev, prev_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* pl = lv_label_create(prev);
    lv_obj_set_style_text_font(pl, &ui_font_ru_20, 0);
    lv_label_set_text(pl, "\xEF\x81\x88");  // prev
    lv_obj_center(pl);

    lv_obj_t* play = lv_btn_create(controls);
    lv_obj_set_size(play, 88, 88);
    lv_obj_align(play, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(play, lv_color_hex(0x6A1B9A), 0);
    lv_obj_set_style_radius(play, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(play, play_cb, LV_EVENT_CLICKED, this);
    play_label_ = lv_label_create(play);
    lv_obj_set_style_text_font(play_label_, &ui_font_ru_28, 0);
    lv_label_set_text(play_label_, "\xEF\x81\x8B");  // play
    lv_obj_center(play_label_);

    lv_obj_t* next = lv_btn_create(controls);
    lv_obj_set_size(next, 76, 76);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, -44, 0);
    lv_obj_set_style_bg_color(next, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_radius(next, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(next, next_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* nl = lv_label_create(next);
    lv_obj_set_style_text_font(nl, &ui_font_ru_20, 0);
    lv_label_set_text(nl, "\xEF\x81\x91");  // next
    lv_obj_center(nl);

    // ---- volume -------------------------------------------------------------------
    lv_obj_t* volRow = lv_obj_create(content);
    lv_obj_set_size(volRow, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(volRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volRow, 0, 0);
    lv_obj_set_style_pad_all(volRow, 0, 0);
    lv_obj_clear_flag(volRow, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* volIcon = lv_label_create(volRow);
    lv_obj_set_style_text_font(volIcon, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(volIcon, lv_color_hex(0x8FA3B5), 0);
    lv_label_set_text(volIcon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_align(volIcon, LV_ALIGN_LEFT_MID, 8, 0);

    volume_slider_ = lv_slider_create(volRow);
    lv_obj_set_size(volume_slider_, cfg::kLcdWidth - 90, 16);
    lv_obj_align(volume_slider_, LV_ALIGN_LEFT_MID, 46, 0);
    lv_slider_set_range(volume_slider_, 0, 100);
    lv_slider_set_value(volume_slider_, hal_.audio.volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume_slider_, lv_color_hex(0x2E3B49), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider_, lv_color_hex(accent()), LV_PART_INDICATOR);
    lv_obj_add_event_cb(volume_slider_, volume_cb, LV_EVENT_ALL, this);

    // ---- track list ------------------------------------------------------------------
    list_ = lv_obj_create(content);
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_width(list_, lv_pct(100));
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_set_style_pad_row(list_, 8, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    // ---- player task -------------------------------------------------------------------
    cmd_q_ = xQueueCreate(4, 2);
    if (cmd_q_) {
        // PSRAM stack: keeps the scarce internal SRAM for the Wi-Fi/TLS stack.
        music_stack_ = static_cast<StackType_t*>(
            heap_caps_malloc(10 * 1024, MALLOC_CAP_SPIRAM));
        music_tcb_ = static_cast<StaticTask_t*>(
            heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL));
        if (music_stack_ && music_tcb_) {
            xTaskCreateStaticPinnedToCore(playerTrampoline, "music",
                                          10 * 1024 / sizeof(StackType_t), this,
                                          1, music_stack_, music_tcb_, 0);
        } else {
            LOGE(kTag, "player task buffers allocation failed");
        }
    }

    ui_timer_ = lv_timer_create([](lv_timer_t* t) {
        static_cast<MusicScreen*>(lv_timer_get_user_data(t))->refreshUi();
    }, 500, this);
}

}  // namespace ui
