// ============================================================================
//  SnakeScreen.cpp — snake game (canvas grid, swipe / d-pad steering).
// ============================================================================
#include "ui/SnakeScreen.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"
#include <esp_heap_caps.h>
#include <cstdlib>

namespace ui {

namespace {
    constexpr int8_t kGrid = 20;        // cells per side
    constexpr int8_t kCell = 12;        // px per cell
    constexpr int32_t kSize = kGrid * kCell;  // 240 px canvas

    constexpr uint32_t kColBg    = 0x0D1116;
    constexpr uint32_t kColSnake = 0x81C784;
    constexpr uint32_t kColHead  = 0xB9F6CA;
    constexpr uint32_t kColFood  = 0xFF8A80;
    constexpr uint32_t kColGrid  = 0x11161C;
}

void SnakeScreen::setDir(int8_t dx, int8_t dy) {
    // no 180° turns
    if (dir_x_ == -dx && dir_y_ == -dy) return;
    next_x_ = dx;
    next_y_ = dy;
}

void SnakeScreen::dir_cb(lv_event_t* e) {
    auto* self = static_cast<SnakeScreen*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!self || !self->alive_) return;
    const auto d = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn));
    static const int8_t dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    self->setDir(dirs[d][0], dirs[d][1]);
}

void SnakeScreen::gesture_cb(lv_event_t* e) {
    auto* self = static_cast<SnakeScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    switch (lv_indev_get_gesture_dir(lv_indev_active())) {
        case LV_DIR_TOP:    self->setDir(0, -1); break;
        case LV_DIR_BOTTOM:  self->setDir(0, 1);  break;
        case LV_DIR_LEFT:  self->setDir(-1, 0); break;
        case LV_DIR_RIGHT: self->setDir(1, 0);  break;
        default: break;
    }
}

void SnakeScreen::restart_cb(lv_event_t* e) {
    auto* self = static_cast<SnakeScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->over_label_ ? lv_obj_add_flag(self->over_label_, LV_OBJ_FLAG_HIDDEN)
                      : void();
    self->snake_.assign({Cell{5, 10}, Cell{4, 10}, Cell{3, 10}});
    self->dir_x_ = 1; self->dir_y_ = 0;
    self->next_x_ = 1; self->next_y_ = 0;
    self->score_ = 0;
    self->alive_ = true;
    self->running_ = true;
    lv_label_set_text_fmt(self->score_label_, "%d", self->score_);
    self->redraw();
}

void SnakeScreen::spawnFood() {
    for (int tries = 0; tries < 400; ++tries) {
        Cell c{static_cast<int8_t>(rand() % kGrid),
               static_cast<int8_t>(rand() % kGrid)};
        bool busy = false;
        for (auto& s : snake_)
            if (s.x == c.x && s.y == c.y) { busy = true; break; }
        if (!busy) { food_ = c; return; }
    }
}

void SnakeScreen::redraw() {
    for (int8_t y = 0; y < kGrid; ++y)
        for (int8_t x = 0; x < kGrid; ++x)
            for (int8_t py = 0; py < kCell; ++py)
                for (int8_t px = 0; px < kCell; ++px)
                    lv_canvas_set_px(canvas_, x * kCell + px, y * kCell + py,
                                     lv_color_hex(((x + y) & 1) ? kColBg : kColGrid),
                                     LV_OPA_COVER);
    for (size_t i = 0; i < snake_.size(); ++i) {
        const uint32_t col = (i == 0) ? kColHead : kColSnake;
        for (int8_t py = 0; py < kCell; ++py)
            for (int8_t px = 0; px < kCell; ++px)
                lv_canvas_set_px(canvas_, snake_[i].x * kCell + px,
                                 snake_[i].y * kCell + py,
                                 lv_color_hex(col), LV_OPA_COVER);
    }
    for (int8_t py = 0; py < kCell; ++py)
        for (int8_t px = 0; px < kCell; ++px)
            lv_canvas_set_px(canvas_, food_.x * kCell + px, food_.y * kCell + py,
                             lv_color_hex(kColFood), LV_OPA_COVER);
}

void SnakeScreen::tick() {
    if (!running_ || !alive_) return;

    dir_x_ = next_x_;
    dir_y_ = next_y_;
    Cell head{static_cast<int8_t>(snake_[0].x + dir_x_),
              static_cast<int8_t>(snake_[0].y + dir_y_)};

    // wall collision
    if (head.x < 0 || head.y < 0 || head.x >= kGrid || head.y >= kGrid) {
        gameOver();
        return;
    }
    // self collision (tail will move away unless we grow — simple check)
    for (size_t i = 0; i + 1 < snake_.size(); ++i)
        if (snake_[i].x == head.x && snake_[i].y == head.y) {
            gameOver();
            return;
        }

    const Cell oldTail = snake_.back();
    const Cell oldFood = food_;
    bool grew = false;

    snake_.insert(snake_.begin(), head);
    if (head.x == food_.x && head.y == food_.y) {
        grew = true;
        score_ += 10;
        lv_label_set_text_fmt(score_label_, "%d", score_);
        spawnFood();
        if (snake_.size() % 5 == 0 && period_ > 90) {
            period_ -= 10;
            lv_timer_set_period(tick_, period_);
        }
    } else {
        snake_.pop_back();
    }

    // incremental draw: clear old tail + old food, draw head + new food
    auto paint = [&](Cell c, uint32_t col) {
        for (int8_t py = 0; py < kCell; ++py)
            for (int8_t px = 0; px < kCell; ++px)
                lv_canvas_set_px(canvas_, c.x * kCell + px, c.y * kCell + py,
                                 lv_color_hex(col), LV_OPA_COVER);
    };
    const auto checker = [&](Cell c) {
        paint(c, ((c.x + c.y) & 1) ? kColBg : kColGrid);
    };
    if (!grew) checker(oldTail);
    if (grew || oldFood.x != food_.x || oldFood.y != food_.y) checker(oldFood);
    // old head becomes body
    if (snake_.size() > 1) paint(snake_[1], kColSnake);
    paint(head, kColHead);
    paint(food_, kColFood);
}

void SnakeScreen::gameOver() {
    alive_ = false;
    running_ = false;
    lv_label_set_text(over_label_, "Игра окончена — тап для рестарта");
    lv_obj_remove_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
}

void SnakeScreen::tick_cb(lv_timer_t* t) {
    static_cast<SnakeScreen*>(lv_timer_get_user_data(t))->tick();
}

void SnakeScreen::show() {
    if (!running_ && alive_) {
        running_ = true;
    }
}

void SnakeScreen::hide() {
    running_ = false;  // pause while hidden; state is kept
}

void SnakeScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 8, 0);

    buf_ = static_cast<uint16_t*>(
        heap_caps_malloc(kSize * kSize * sizeof(uint16_t), MALLOC_CAP_SPIRAM));

    canvas_ = lv_canvas_create(content);
    lv_canvas_set_buffer(canvas_, buf_, kSize, kSize, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas_, kSize, kSize);
    lv_obj_set_style_border_width(canvas_, 2, 0);
    lv_obj_set_style_border_color(canvas_, lv_color_hex(0x2E3B49), 0);
    lv_obj_add_event_cb(canvas_, gesture_cb, LV_EVENT_GESTURE, this);
    lv_canvas_fill_bg(canvas_, lv_color_hex(kColBg), LV_OPA_COVER);

    score_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(score_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(score_label_, lv_color_white(), 0);
    lv_label_set_text(score_label_, "0");

    // d-pad
    lv_obj_t* pad = lv_obj_create(content);
    lv_obj_set_size(pad, 190, 150);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);

    const struct { int8_t dx, dy; int32_t x, y; const char* icon; } btns[4] = {
        {0, -1, 66, 0,   LV_SYMBOL_UP},
        {-1, 0, 0, 52,   LV_SYMBOL_LEFT},
        {1, 0, 132, 52,  LV_SYMBOL_RIGHT},
        {0, 1, 66, 104,  LV_SYMBOL_DOWN},
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* b = lv_btn_create(pad);
        lv_obj_set_size(b, 58, 46);
        lv_obj_set_pos(b, btns[i].x, btns[i].y);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1B242E), 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_user_data(b, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(b, dir_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text(l, btns[i].icon);
        lv_obj_center(l);
    }

    over_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(over_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(over_label_, lv_color_hex(0xFF8A80), 0);
    lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(over_label_, restart_cb, LV_EVENT_CLICKED, this);
    lv_label_set_text(over_label_, "Игра окончена — тап для рестарта");

    snake_.assign({Cell{5, 10}, Cell{4, 10}, Cell{3, 10}});
    spawnFood();
    redraw();

    tick_ = lv_timer_create(tick_cb, 160, this);
}

}  // namespace ui
