// ============================================================================
//  SnakeScreen.hpp — classic snake on a canvas grid.
//
//  Board is a small LVGL canvas (PSRAM-backed buffer, 24 px cells).  The
//  snake is steered by swipes on the board or the round d-pad buttons below.
// ============================================================================
#pragma once

#include <vector>
#include "ui/App.hpp"

namespace ui {

class SnakeScreen final : public App {
public:
    void create(AppHost* host) override;
    void show() override;
    void hide() override;

    const char* title() const override { return "Змейка"; }
    const char* icon() const override { return "\xEF\x80\x8B"; }  // FA list
    uint32_t accent() const override { return 0x81C784; }

private:
    struct Cell { int8_t x, y; };

    static void tick_cb(lv_timer_t* t);
    static void dir_cb(lv_event_t* e);      // d-pad buttons
    static void gesture_cb(lv_event_t* e);  // swipe on the board
    static void restart_cb(lv_event_t* e);

    void setDir(int8_t dx, int8_t dy);
    void tick();
    void redraw();
    void drawCell(int8_t x, int8_t y, uint32_t color);
    void spawnFood();
    void gameOver();

    lv_obj_t* canvas_ = nullptr;
    lv_obj_t* score_label_ = nullptr;
    lv_obj_t* over_label_ = nullptr;
    lv_timer_t* tick_ = nullptr;

    uint16_t* buf_ = nullptr;           // canvas buffer (PSRAM)
    std::vector<Cell> snake_;
    Cell food_{0, 0};
    int8_t dir_x_ = 1, dir_y_ = 0;
    int8_t next_x_ = 1, next_y_ = 0;
    int16_t score_ = 0;
    bool alive_ = true;
    bool running_ = false;
    int32_t period_ = 160;
};

}  // namespace ui
