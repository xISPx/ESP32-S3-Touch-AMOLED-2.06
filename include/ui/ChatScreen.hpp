// ============================================================================
//  ChatScreen.hpp — AI assistant app, two modes like the official client:
//
//  * FACE (default): full-screen assistant face (official llm emotions),
//    status line and tap-to-talk — the tap starts/stops a voice session
//    exactly like ToggleChatState in xiaozhi-esp32;
//  * CHAT: bubbles + text input + push-to-talk button + RU/EN keyboard.
//
//  The header face and speaking spinner stay visible in both modes.
//  Queries go to AiTask through the EventBus (AiText events), replies come
//  back as Reply/Stt/Error/Info/Emotion/Speaking — even while hidden.
// ============================================================================
#pragma once

#include "ui/App.hpp"

namespace ui {

class ChatScreen final : public App {
public:
    void create(AppHost* host) override;
    void onAiText(const core::Event& e) override;
    void show() override { scrollBottom(); }

    const char* title() const override { return "ИИ-ассистент"; }
    const char* icon() const override { return "\xEF\x82\x86"; }  // FA comments
    uint32_t accent() const override { return 0x64B5F6; }

private:
    static void send_cb(lv_event_t* e);
    static void ta_focused_cb(lv_event_t* e);
    static void ta_ready_cb(lv_event_t* e);
    static void ptt_pressed_cb(lv_event_t* e);   // push-to-talk: mic button
    static void ptt_released_cb(lv_event_t* e);
    static void face_tap_cb(lv_event_t* e);      // official toggle-chat-state
    static void mode_toggle_cb(lv_event_t* e);
    static void kb_mode_cb(lv_event_t* e);    // PREPROCESS: "En"/"Аа" mode keys
    static void kb_cancel_cb(lv_event_t* e);  // keyboard-icon button hides kb

    void send();
    void publishVoice(uint8_t kind);
    void setMode(bool faceMode);
    void setStatus(const char* text, uint32_t color);
    void setEmotion(const char* emo);
    void addBubble(const char* text, int style);  // 0 user, 1 ai, 2 err, 3 info
    void showPending(const char* text = "Думаю…");
    void clearPending();
    void scrollBottom();

    // chat mode widgets
    lv_obj_t* messages_ = nullptr;
    lv_obj_t* input_ = nullptr;
    lv_obj_t* input_row_ = nullptr;
    lv_obj_t* kb_ = nullptr;
    lv_obj_t* pending_row_ = nullptr;  // "Думаю…" bubble while a reply is in flight

    // face mode widgets (official app look)
    lv_obj_t* face_container_ = nullptr;
    lv_obj_t* face_btn_ = nullptr;
    lv_obj_t* face_glyph_ = nullptr;
    lv_obj_t* status_label_ = nullptr;

    // shared
    lv_obj_t* emotion_label_ = nullptr;   // official llm emotion face (header)
    lv_obj_t* speaking_spinner_ = nullptr;
    bool face_mode_ = true;
    bool listening_ = false;              // voice capture in progress (UI side)
};

}  // namespace ui
