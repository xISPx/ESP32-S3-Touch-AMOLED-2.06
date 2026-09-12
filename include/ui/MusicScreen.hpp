// ============================================================================
//  MusicScreen.hpp — music player: WAV playback from the TF card through the
//  ES8311 speaker.
//
//  A dedicated FreeRTOS task streams the file (chunked, pause/stop between
//  chunks) into AudioHal, which serializes I2S access against the AI task
//  and re-clocks the bus to the file's sample rate.  When the assistant
//  starts talking (AiText/Speaking "1") or a PTT session begins, the music
//  stops — AI owns the speaker first.
// ============================================================================
#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "ui/App.hpp"
#include "hal/Hal.hpp"
#include "hal/SdHal.hpp"

namespace ui {

class MusicScreen final : public App {
public:
    explicit MusicScreen(hal::Hal& hal) : hal_(hal) {}
    void create(AppHost* host) override;
    void show() override;
    void onAiText(const core::Event& e) override;   // stop on Speaking/VoiceStart

    const char* title() const override { return "Музыка"; }
    const char* icon() const override { return "\xEF\x80\x81"; }  // FA audio
    uint32_t accent() const override { return 0xBA68C8; }

private:
    enum class Cmd : uint8_t { Stop = 0, Play, Toggle, Next, Prev };

    static void playerTrampoline(void* arg);
    void playerLoop();
    void post(Cmd c, uint8_t idx = 0);

    // UI actions (UiTask context)
    static void play_cb(lv_event_t* e);
    static void next_cb(lv_event_t* e);
    static void prev_cb(lv_event_t* e);
    static void track_cb(lv_event_t* e);
    static void volume_cb(lv_event_t* e);
    void refreshUi();          // 500 ms timer, active screen only
    void rebuildList();

    hal::Hal& hal_;

    // ---- UI objects ----------------------------------------------------------
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* play_label_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* sd_status_ = nullptr;
    lv_timer_t* ui_timer_ = nullptr;

    // ---- player state (player task writes, UI reads) --------------------------
    QueueHandle_t cmd_q_ = nullptr;      // {Cmd, idx}
    StackType_t* music_stack_ = nullptr; // PSRAM-backed task stack
    StaticTask_t* music_tcb_ = nullptr;
    std::atomic<uint8_t> state_{0};      // 0 idle, 1 playing, 2 paused
    std::atomic<uint32_t> pos_{0};       // played mono frames
    std::atomic<uint32_t> total_{0};
    std::atomic<uint32_t> rate_{0};
    std::atomic<int> current_{-1};
    std::vector<String> tracks_;         // UiTask-only (loaded once, then read-only)
    bool tracks_loaded_ = false;
};

}  // namespace ui
