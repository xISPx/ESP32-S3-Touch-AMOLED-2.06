// ============================================================================
//  ChatScreen.cpp — official-style AI app: face mode (tap-to-talk) + chat.
// ============================================================================
#include "ui/ChatScreen.hpp"
#include "ui/AppHost.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include <cstring>

namespace ui {

namespace {
    constexpr const char* kTag = "Chat";
    constexpr int32_t kBubbleMaxW = 290;

    // lv_buttonmatrix_ctrl_t is a plain enum: C++ needs an explicit cast for
    // the raw width units (1..7 in the low bits, flags above).
    constexpr lv_buttonmatrix_ctrl_t KBC(int w) {
        return static_cast<lv_buttonmatrix_ctrl_t>(w);
    }

    // ---- Russian keyboard maps (mode USER_1 / USER_2) -----------------------
    const char* kRuLowerMap[] = {
        "й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "\n",
        "ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э", "\n",
        "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", ".", LV_SYMBOL_BACKSPACE, "\n",
        "En", "Аа", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_NEW_LINE, "",
    };
    const lv_buttonmatrix_ctrl_t kRuLowerCtrl[] = {
        KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1),
        KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1),
        KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(2),
        KBC(2), KBC(2), KBC(2), KBC(5), KBC(2), KBC(4),
    };
    const char* kRuUpperMap[] = {
        "Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х", "\n",
        "Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э", "\n",
        "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", ",", LV_SYMBOL_BACKSPACE, "\n",
        "En", "Аа", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_NEW_LINE, "",
    };
    const lv_buttonmatrix_ctrl_t kRuUpperCtrl[] = {
        KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1),
        KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1),
        KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(1), KBC(2),
        KBC(2), KBC(2), KBC(2), KBC(5), KBC(2), KBC(4),
    };
}

// ----------------------------------------------------------------------------
//  Callbacks
// ----------------------------------------------------------------------------

void ChatScreen::kb_mode_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    lv_obj_t* kb = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!self || !kb) return;

    const uint32_t btn = lv_keyboard_get_selected_button(kb);
    if (btn == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char* txt = lv_keyboard_get_button_text(kb, btn);
    if (!txt) return;

    if (lv_strcmp(txt, "En") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_event_stop_processing(e);
    } else if (lv_strcmp(txt, "Аа") == 0) {
        const uint32_t mode = lv_keyboard_get_mode(kb);
        lv_keyboard_set_mode(kb, mode == LV_KEYBOARD_MODE_USER_1
                                     ? LV_KEYBOARD_MODE_USER_2
                                     : LV_KEYBOARD_MODE_USER_1);
        lv_event_stop_processing(e);
    }
}

void ChatScreen::ta_focused_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || !self->kb_) return;
    lv_keyboard_set_textarea(self->kb_, self->input_);
    lv_keyboard_set_mode(self->kb_, LV_KEYBOARD_MODE_USER_1);  // RU by default
    lv_obj_remove_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::kb_cancel_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (self && self->kb_) lv_obj_add_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
}

void ChatScreen::ta_ready_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->kb_) lv_obj_add_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    self->send();
}

void ChatScreen::send_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->kb_) lv_obj_add_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    self->send();
}

// ---- push-to-talk (chat mode mic button) ------------------------------------

void ChatScreen::publishVoice(uint8_t kind) {
    core::EventBus::instance().publish(
        core::Event::makeAiText(static_cast<core::AiMsgKind>(kind), ""));
}

void ChatScreen::ptt_pressed_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self || self->pending_row_ || self->listening_) return;
    self->publishVoice(static_cast<uint8_t>(core::AiMsgKind::VoiceStart));
    self->showPending("• Говорите…");
    self->scrollBottom();
}

void ChatScreen::ptt_released_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->publishVoice(static_cast<uint8_t>(core::AiMsgKind::VoiceStop));
    if (self->pending_row_) {
        self->clearPending();
        self->showPending("Распознаю…");
    }
}

// ---- official face mode: tap = ToggleChatState -------------------------------

void ChatScreen::face_tap_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->speaking_spinner_ &&
        !lv_obj_has_flag(self->speaking_spinner_, LV_OBJ_FLAG_HIDDEN)) {
        return;  // wait until the assistant finishes talking
    }
    if (!self->listening_) {
        self->listening_ = true;
        self->publishVoice(static_cast<uint8_t>(core::AiMsgKind::VoiceStart));
        self->setStatus("Слушаю… говорите", 0x7ED957);
        lv_obj_set_style_border_color(self->face_btn_,
                                      lv_color_hex(0x7ED957), 0);
    } else {
        self->listening_ = false;
        self->publishVoice(static_cast<uint8_t>(core::AiMsgKind::VoiceStop));
        self->setStatus("Думаю…", 0x90A4AE);
        lv_obj_set_style_border_color(self->face_btn_,
                                      lv_color_hex(0x2E3B49), 0);
    }
}

void ChatScreen::mode_toggle_cb(lv_event_t* e) {
    auto* self = static_cast<ChatScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->setMode(!self->face_mode_);
}

// ----------------------------------------------------------------------------
//  Mode / status / face
// ----------------------------------------------------------------------------

void ChatScreen::setMode(bool faceMode) {
    face_mode_ = faceMode;
    // Never leave a dangling voice session when switching views.
    if (listening_) {
        listening_ = false;
        publishVoice(static_cast<uint8_t>(core::AiMsgKind::VoiceStop));
    }
    if (kb_) lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);

    if (faceMode) {
        lv_obj_add_flag(messages_, LV_OBJ_FLAG_HIDDEN);
        if (input_row_) lv_obj_add_flag(input_row_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
        setStatus("Нажмите, чтобы говорить", 0x90A4AE);
    } else {
        lv_obj_add_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(messages_, LV_OBJ_FLAG_HIDDEN);
        if (input_row_) lv_obj_remove_flag(input_row_, LV_OBJ_FLAG_HIDDEN);
        scrollBottom();
    }
}

void ChatScreen::setStatus(const char* text, uint32_t color) {
    if (!status_label_) return;
    lv_label_set_text(status_label_, text);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(color), 0);
}

// Official llm emotions → a face glyph (FontAwesome 4.7).
void ChatScreen::setEmotion(const char* emo) {
    const char* glyph;
    uint32_t color;
    if (strstr("happy laughing loving cool relaxed delicious winking", emo)) {
        glyph = "\xEF\x84\x98";  // smile-o
        color = 0xFFD54F;
    } else if (strstr("sad angry crying sleepy embarrassed confused", emo)) {
        glyph = "\xEF\x84\x99";  // frown-o
        color = 0xE57373;
    } else if (strstr("neutral surprised shocked thinking zany", emo)) {
        glyph = "\xEF\x84\x9A";  // meh-o
        color = 0x90A4AE;
    } else {
        return;  // unknown emotion — keep the current face
    }
    if (emotion_label_) {
        lv_label_set_text(emotion_label_, glyph);
        lv_obj_set_style_text_color(emotion_label_, lv_color_hex(color), 0);
    }
    if (face_glyph_) {
        lv_label_set_text(face_glyph_, glyph);
        lv_obj_set_style_text_color(face_glyph_, lv_color_hex(color), 0);
    }
}

// ----------------------------------------------------------------------------
//  Sending / receiving
// ----------------------------------------------------------------------------

void ChatScreen::send() {
    const char* text = lv_textarea_get_text(input_);
    if (!text || text[0] == '\0') return;
    if (pending_row_) return;  // one request at a time

    addBubble(text, 0);
    core::EventBus::instance().publish(
        core::Event::makeAiText(core::AiMsgKind::Query, text));
    lv_textarea_set_text(input_, "");
    showPending();
    scrollBottom();
}

void ChatScreen::onAiText(const core::Event& e) {
    switch (static_cast<core::AiMsgKind>(e.ai.kind)) {
        case core::AiMsgKind::Query:
        case core::AiMsgKind::VoiceStart:
        case core::AiMsgKind::VoiceStop:
            return;  // our own echoes — bubbles were added at send/press time

        case core::AiMsgKind::Emotion:
            setEmotion(e.ai.text);
            return;

        case core::AiMsgKind::Speaking:
            if (speaking_spinner_) {
                if (e.ai.text[0] == '1') {
                    lv_obj_remove_flag(speaking_spinner_, LV_OBJ_FLAG_HIDDEN);
                    if (face_mode_) setStatus("Говорит…", 0x4FC3F7);
                } else {
                    lv_obj_add_flag(speaking_spinner_, LV_OBJ_FLAG_HIDDEN);
                    if (face_mode_) setStatus("Нажмите, чтобы говорить", 0x90A4AE);
                    listening_ = false;
                    if (face_btn_)
                        lv_obj_set_style_border_color(face_btn_,
                                                      lv_color_hex(0x2E3B49), 0);
                }
            }
            return;

        case core::AiMsgKind::Status:
            if (face_mode_) setStatus(e.ai.text, 0x4FC3F7);
            return;

        case core::AiMsgKind::Stt:
            // Official "stt": what the server heard from the microphone.
            addBubble(e.ai.text, 0);
            if (face_mode_) {
                char line[128];
                snprintf(line, sizeof(line), "Вы сказали: %s", e.ai.text);
                setStatus(line, 0x90A4AE);
            } else {
                showPending();
            }
            break;

        case core::AiMsgKind::Info:
            addBubble(e.ai.text, 3);
            if (face_mode_) setStatus(e.ai.text, 0x90A4AE);
            break;

        case core::AiMsgKind::Reply:
            clearPending();
            addBubble(e.ai.text, 1);
            if (face_mode_) setStatus(e.ai.text, 0xE3F2FD);
            break;

        case core::AiMsgKind::Error:
            clearPending();
            addBubble(e.ai.text, 2);
            listening_ = false;
            if (face_mode_) setStatus(e.ai.text, 0xE57373);
            if (face_btn_)
                lv_obj_set_style_border_color(face_btn_,
                                              lv_color_hex(0x2E3B49), 0);
            break;
    }
    scrollBottom();
}

void ChatScreen::addBubble(const char* text, int style) {
    static constexpr uint32_t kBg[] = {0x1E4A8A, 0x242D38, 0x6A1F1F, 0x3A4653};

    // Bound the history: the LVGL pool is small (64 KB, shared with every
    // screen), so drop the oldest rows beyond 24 messages.
    while (lv_obj_get_child_count(messages_) > 1 + 24) {
        lv_obj_del(lv_obj_get_child(messages_, 1));  // [0] is the hint label
    }

    lv_obj_t* row = lv_obj_create(messages_);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* bubble = lv_obj_create(row);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(kBg[style]), 0);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_clear_flag(bubble, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_max_width(bubble, kBubbleMaxW, 0);

    lv_obj_t* lbl = lv_label_create(bubble);
    lv_obj_set_style_text_font(lbl, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, kBubbleMaxW - 20);

    lv_obj_align(bubble, style == 0 ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_update_layout(bubble);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
}

void ChatScreen::showPending(const char* text) {
    if (pending_row_) return;
    addBubble(text, 1);
    // The bubble we just added is the last row of the message list.
    pending_row_ = lv_obj_get_child(messages_, lv_obj_get_child_count(messages_) - 1);
}

void ChatScreen::clearPending() {
    if (!pending_row_) return;
    lv_obj_del(pending_row_);
    pending_row_ = nullptr;
}

void ChatScreen::scrollBottom() {
    if (messages_ && !lv_obj_has_flag(messages_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_scroll_to_y(messages_, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

// ----------------------------------------------------------------------------
//  Build
// ----------------------------------------------------------------------------

void ChatScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 6, 0);

    // ---- header: emotion face + speaking spinner + mode toggle ----------------
    emotion_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(emotion_label_, &ui_font_ru_28, 0);
    lv_obj_set_style_text_color(emotion_label_, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(emotion_label_, "\xEF\x84\x9A");  // meh-o, neutral
    lv_obj_align(emotion_label_, LV_ALIGN_TOP_RIGHT, -148, 12);

    speaking_spinner_ = lv_spinner_create(root_);
    lv_obj_set_size(speaking_spinner_, 34, 34);
    lv_obj_align(speaking_spinner_, LV_ALIGN_TOP_RIGHT, -92, 12);
    lv_obj_set_style_arc_color(speaking_spinner_, lv_color_hex(0x4FC3F7),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(speaking_spinner_, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(speaking_spinner_, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(speaking_spinner_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* toggle = lv_btn_create(root_);
    lv_obj_set_size(toggle, 56, 44);
    lv_obj_align(toggle, LV_ALIGN_TOP_RIGHT, -24, 10);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_radius(toggle, 12, 0);
    lv_obj_add_event_cb(toggle, mode_toggle_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* tl = lv_label_create(toggle);
    lv_obj_set_style_text_font(tl, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(tl, lv_color_hex(0x64B5F6), 0);
    lv_label_set_text(tl, "\xEF\x82\x86");
    lv_obj_center(tl);

    // ---- face mode (official app look) -----------------------------------------
    face_container_ = lv_obj_create(content);
    lv_obj_set_flex_grow(face_container_, 1);
    lv_obj_set_width(face_container_, lv_pct(100));
    lv_obj_set_style_bg_opa(face_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_container_, 0, 0);
    lv_obj_set_style_pad_all(face_container_, 0, 0);
    lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_SCROLLABLE);

    face_btn_ = lv_btn_create(face_container_);
    lv_obj_set_size(face_btn_, 230, 230);
    lv_obj_align(face_btn_, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_color(face_btn_, lv_color_hex(0x141A21), 0);
    lv_obj_set_style_border_width(face_btn_, 3, 0);
    lv_obj_set_style_border_color(face_btn_, lv_color_hex(0x2E3B49), 0);
    lv_obj_set_style_radius(face_btn_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(face_btn_, 0, 0);
    lv_obj_add_event_cb(face_btn_, face_tap_cb, LV_EVENT_CLICKED, this);

    face_glyph_ = lv_label_create(face_btn_);
    lv_obj_set_style_text_font(face_glyph_, &ui_font_ru_28, 0);
    lv_obj_set_style_text_color(face_glyph_, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_style_transform_scale(face_glyph_, 1152, 0);  // ~4.5x → 126 px
    lv_label_set_text(face_glyph_, "\xEF\x84\x9A");
    lv_obj_center(face_glyph_);

    status_label_ = lv_label_create(face_container_);
    lv_obj_set_style_text_font(status_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(status_label_, "Нажмите, чтобы говорить");
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label_, cfg::kLcdWidth - 80);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 286);

    // ---- message list (chat mode) -----------------------------------------------
    messages_ = lv_obj_create(content);
    lv_obj_set_flex_grow(messages_, 1);
    lv_obj_set_width(messages_, lv_pct(100));
    lv_obj_set_style_bg_opa(messages_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(messages_, 0, 0);
    lv_obj_set_style_pad_all(messages_, 4, 0);
    lv_obj_set_style_pad_row(messages_, 6, 0);
    lv_obj_set_flex_flow(messages_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(messages_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(messages_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* hint = lv_label_create(messages_);
    lv_obj_set_style_text_font(hint, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(hint,
        "Текст или удерживайте микрофон для голоса.\n"
        "ИИ настраивается в Настройках → ИИ-ассистент.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(95));

    // ---- input row (chat mode) -----------------------------------------------------
    input_row_ = lv_obj_create(content);
    lv_obj_set_size(input_row_, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(input_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row_, 0, 0);
    lv_obj_set_style_pad_all(input_row_, 0, 0);
    lv_obj_clear_flag(input_row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(input_row_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* pttBtn = lv_btn_create(input_row_);
    lv_obj_set_size(pttBtn, 54, 54);
    lv_obj_align(pttBtn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(pttBtn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_bg_color(pttBtn, lv_color_hex(0x4CAF50), LV_STATE_PRESSED);
    lv_obj_set_style_radius(pttBtn, 14, 0);
    lv_obj_add_event_cb(pttBtn, ptt_pressed_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(pttBtn, ptt_released_cb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(pttBtn, ptt_released_cb, LV_EVENT_PRESS_LOST, this);

    lv_obj_t* pttLbl = lv_label_create(pttBtn);
    lv_obj_set_style_text_font(pttLbl, &ui_font_ru_20, 0);
    lv_label_set_text(pttLbl, "\xEF\x84\xB0");  // FA microphone
    lv_obj_center(pttLbl);

    input_ = lv_textarea_create(input_row_);
    lv_obj_set_size(input_, 244, 54);
    lv_obj_align(input_, LV_ALIGN_LEFT_MID, 62, 0);
    lv_textarea_set_one_line(input_, true);
    lv_textarea_set_placeholder_text(input_, "Сообщение…");
    lv_obj_set_style_text_font(input_, &ui_font_ru_16, 0);
    lv_obj_set_style_bg_color(input_, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_border_color(input_, lv_color_hex(0x2E3B49), 0);
    lv_obj_set_style_radius(input_, 14, 0);
    lv_obj_add_event_cb(input_, ta_focused_cb, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(input_, ta_ready_cb, LV_EVENT_READY, this);

    lv_obj_t* sendBtn = lv_btn_create(input_row_);
    lv_obj_set_size(sendBtn, 60, 54);
    lv_obj_align(sendBtn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sendBtn, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_radius(sendBtn, 14, 0);
    lv_obj_add_event_cb(sendBtn, send_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* sendLbl = lv_label_create(sendBtn);
    lv_obj_set_style_text_font(sendLbl, &ui_font_ru_28, 0);
    lv_label_set_text(sendLbl, "\xEF\x87\x98");  // FA paper-plane
    lv_obj_center(sendLbl);

    // ---- keyboard (floating rounded card, edge-safe; hidden until input tap) ----
    kb_ = lv_keyboard_create(root_);
    lv_obj_set_size(kb_, cfg::kLcdWidth - 40, 196);
    lv_obj_align(kb_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(kb_, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_radius(kb_, 20, 0);
    lv_obj_set_style_border_color(kb_, lv_color_hex(0x2E3B49), 0);
    lv_obj_set_style_border_width(kb_, 1, 0);
    lv_obj_set_style_pad_all(kb_, 6, 0);
    lv_obj_set_style_pad_row(kb_, 5, 0);
    lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(kb_, &ui_font_ru_16, 0);
    lv_keyboard_set_map(kb_, LV_KEYBOARD_MODE_USER_1, kRuLowerMap, kRuLowerCtrl);
    lv_keyboard_set_map(kb_, LV_KEYBOARD_MODE_USER_2, kRuUpperMap, kRuUpperCtrl);
    lv_keyboard_set_mode(kb_, LV_KEYBOARD_MODE_USER_1);
    lv_obj_add_event_cb(kb_, kb_mode_cb, static_cast<lv_event_code_t>(
                                             LV_EVENT_VALUE_CHANGED |
                                             LV_EVENT_PREPROCESS),
                        this);
    lv_obj_add_event_cb(kb_, ta_ready_cb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(kb_, kb_cancel_cb, LV_EVENT_CANCEL, this);
}

}  // namespace ui
