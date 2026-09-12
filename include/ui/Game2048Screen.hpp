// ============================================================================
//  Game2048Screen.hpp — 2048 with swipe controls on a 4×4 tile board.
// ============================================================================
#pragma once

#include <cstdint>
#include "ui/App.hpp"

namespace ui {

class Game2048Screen final : public App {
public:
    void create(AppHost* host) override;
    void show() override;

    const char* title() const override { return "2048"; }
    const char* icon() const override { return "\xEF\x80\xA7"; }  // FA grid-ish
    uint32_t accent() const override { return 0xFFB74D; }

private:
    static void gesture_cb(lv_event_t* e);
    static void new_game_cb(lv_event_t* e);

    void newGame();
    void spawn();
    void render();
    bool move(int8_t dx, int8_t dy);  // returns true when the board changed
    void gameOver();

    lv_obj_t* board_ = nullptr;      // 4×4 tile container
    lv_obj_t* tiles_[4][4] = {};
    lv_obj_t* score_label_ = nullptr;
    lv_obj_t* over_label_ = nullptr;
    uint16_t grid_[4][4] = {};
    uint32_t score_ = 0;
    bool over_ = false;
};

}  // namespace ui
