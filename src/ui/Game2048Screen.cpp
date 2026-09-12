// ============================================================================
//  Game2048Screen.cpp — 2048: swipe to slide, merge equal tiles.
// ============================================================================
#include "ui/Game2048Screen.hpp"
#include "config/config.hpp"
#include <cstdlib>

namespace ui {

namespace {
    constexpr int32_t kTile = 78;
    constexpr int32_t kGap = 8;

    uint32_t tileColor(uint16_t v) {
        switch (v) {
            case 2:    return 0x37474F;
            case 4:    return 0x455A64;
            case 8:    return 0x1565C0;
            case 16:   return 0x0288D1;
            case 32:   return 0x00897B;
            case 64:   return 0x2E7D32;
            case 128:  return 0xF9A825;
            case 256:  return 0xF57F17;
            case 512:  return 0xEF6C00;
            case 1024: return 0xD84315;
            default:   return 0xC62828;
        }
    }
}

void Game2048Screen::spawn() {
    for (int tries = 0; tries < 50; ++tries) {
        const int8_t x = rand() % 4, y = rand() % 4;
        if (grid_[y][x] == 0) {
            grid_[y][x] = (rand() % 10 == 0) ? 4 : 2;
            return;
        }
    }
}

void Game2048Screen::render() {
    for (int8_t y = 0; y < 4; ++y)
        for (int8_t x = 0; x < 4; ++x) {
            lv_obj_t* t = tiles_[y][x];
            const uint16_t v = grid_[y][x];
            if (v == 0) {
                lv_obj_set_style_bg_color(t, lv_color_hex(0x10161C), 0);
                lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
                lv_label_set_text(lv_obj_get_child(t, 0), "");
            } else {
                lv_obj_set_style_bg_color(t, lv_color_hex(tileColor(v)), 0);
                lv_label_set_text_fmt(lv_obj_get_child(t, 0), "%u",
                                      static_cast<unsigned>(v));
            }
        }
    lv_label_set_text_fmt(score_label_, "Счёт: %u",
                          static_cast<unsigned>(score_));
}

bool Game2048Screen::move(int8_t dx, int8_t dy) {
    bool changed = false;

    auto slideLine = [&](uint16_t* line, int8_t n) {
        // compact non-zero values to the front, merge equal neighbours
        int8_t out = 0;
        for (int8_t i = 0; i < n; ++i) {
            if (!line[i]) continue;
            if (out > 0 && line[out - 1] == line[i] && line[out - 1] < 4096) {
                line[out - 1] *= 2;
                score_ += line[out - 1];
                line[i] = 0;
                changed = true;
            } else {
                if (out != i) { line[out] = line[i]; line[i] = 0; changed = true; }
                ++out;
            }
        }
    };

    for (int8_t k = 0; k < 4; ++k) {
        if (dx == 1) {          // right
            uint16_t line[4] = {grid_[k][3], grid_[k][2], grid_[k][1], grid_[k][0]};
            slideLine(line, 4);
            grid_[k][3] = line[0]; grid_[k][2] = line[1];
            grid_[k][1] = line[2]; grid_[k][0] = line[3];
        } else if (dx == -1) {  // left
            uint16_t line[4] = {grid_[k][0], grid_[k][1], grid_[k][2], grid_[k][3]};
            slideLine(line, 4);
            grid_[k][0] = line[0]; grid_[k][1] = line[1];
            grid_[k][2] = line[2]; grid_[k][3] = line[3];
        } else if (dy == 1) {   // down
            uint16_t line[4] = {grid_[3][k], grid_[2][k], grid_[1][k], grid_[0][k]};
            slideLine(line, 4);
            grid_[3][k] = line[0]; grid_[2][k] = line[1];
            grid_[1][k] = line[2]; grid_[0][k] = line[3];
        } else if (dy == -1) {  // up
            uint16_t line[4] = {grid_[0][k], grid_[1][k], grid_[2][k], grid_[3][k]};
            slideLine(line, 4);
            grid_[0][k] = line[0]; grid_[1][k] = line[1];
            grid_[2][k] = line[2]; grid_[3][k] = line[3];
        }
    }
    return changed;
}

void Game2048Screen::gameOver() {
    over_ = true;
    lv_obj_remove_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
}

void Game2048Screen::newGame() {
    for (int8_t y = 0; y < 4; ++y)
        for (int8_t x = 0; x < 4; ++x) grid_[y][x] = 0;
    score_ = 0;
    over_ = false;
    lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
    spawn();
    spawn();
    render();
}

void Game2048Screen::gesture_cb(lv_event_t* e) {
    auto* self = static_cast<Game2048Screen*>(lv_event_get_user_data(e));
    if (!self || self->over_) return;
    bool moved = false;
    switch (lv_indev_get_gesture_dir(lv_indev_active())) {
        case LV_DIR_LEFT:  moved = self->move(-1, 0); break;
        case LV_DIR_RIGHT: moved = self->move(1, 0);  break;
        case LV_DIR_TOP:    moved = self->move(0, -1); break;
        case LV_DIR_BOTTOM:  moved = self->move(0, 1);  break;
        default: return;
    }
    if (moved) {
        self->spawn();
        self->render();
        // game over: no free cells and no mergeable neighbours
        bool canMove = false;
        for (int8_t y = 0; y < 4 && !canMove; ++y)
            for (int8_t x = 0; x < 4 && !canMove; ++x) {
                if (!self->grid_[y][x]) canMove = true;
                if (x < 3 && self->grid_[y][x] == self->grid_[y][x + 1]) canMove = true;
                if (y < 3 && self->grid_[y][x] == self->grid_[y + 1][x]) canMove = true;
            }
        if (!canMove) self->gameOver();
    }
}

void Game2048Screen::new_game_cb(lv_event_t* e) {
    auto* self = static_cast<Game2048Screen*>(lv_event_get_user_data(e));
    if (self) self->newGame();
}

void Game2048Screen::show() {
    render();
}

void Game2048Screen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 10, 0);

    lv_obj_t* top = lv_obj_create(content);
    lv_obj_set_size(top, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    score_label_ = lv_label_create(top);
    lv_obj_set_style_text_font(score_label_, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(score_label_, lv_color_white(), 0);
    lv_label_set_text(score_label_, "Счёт: 0");
    lv_obj_align(score_label_, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* reset = lv_btn_create(top);
    lv_obj_set_size(reset, 118, 40);
    lv_obj_align(reset, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(reset, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_radius(reset, 14, 0);
    lv_obj_set_style_shadow_width(reset, 0, 0);
    lv_obj_add_event_cb(reset, new_game_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* rl = lv_label_create(reset);
    lv_obj_set_style_text_font(rl, &ui_font_ru_16, 0);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Заново");
    lv_obj_center(rl);

    board_ = lv_obj_create(content);
    lv_obj_set_size(board_, 4 * kTile + 5 * kGap, 4 * kTile + 5 * kGap);
    lv_obj_set_style_bg_color(board_, lv_color_hex(0x141A21), 0);
    lv_obj_set_style_radius(board_, 20, 0);
    lv_obj_set_style_border_width(board_, 0, 0);
    lv_obj_set_style_pad_all(board_, kGap, 0);
    lv_obj_clear_flag(board_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(board_, gesture_cb, LV_EVENT_GESTURE, this);

    for (int8_t y = 0; y < 4; ++y)
        for (int8_t x = 0; x < 4; ++x) {
            lv_obj_t* t = lv_obj_create(board_);
            lv_obj_set_size(t, kTile, kTile);
            lv_obj_set_pos(t, x * (kTile + kGap), y * (kTile + kGap));
            lv_obj_set_style_radius(t, 14, 0);
            lv_obj_set_style_border_width(t, 0, 0);
            lv_obj_clear_flag(t, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_t* l = lv_label_create(t);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            lv_obj_center(l);
            tiles_[y][x] = t;
        }

    over_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(over_label_, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(over_label_, lv_color_hex(0xFF8A80), 0);
    lv_label_set_text(over_label_, "Ходов нет — «Заново»");
    lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);

    newGame();
}

}  // namespace ui
