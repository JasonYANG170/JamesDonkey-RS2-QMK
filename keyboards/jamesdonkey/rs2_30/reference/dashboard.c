// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "rs2_30.h"
#include "bluetooth/ch582f_ajazz.h"
#include "rtc/rtc.h"
#include "media/media.h"
#include "qp_surface.h"
#include "fonts/ak650_sc_16.qff.h"

#include <stdio.h>
#include <string.h>

#define KNOB_HOLD_MS 450
#define BOOT_HOLD_MS 1200
#define MENU_IDLE_MS 20000
#define LEGACY_PREFERENCES_SIGNATURE 0x414B6500u
#define PREFERENCES_SIGNATURE 0x414C0000u
#define PREFERENCES_MASK 0x1FFFu
#define LANGUAGE_SHIFT 4
#define APPEARANCE_SHIFT 5
#define BACKLIGHT_SHIFT 7
#define SLEEP_SHIFT 10
#define KNOB_ROW 0
#define KNOB_COL 17

enum setting {
    LIGHTING,
    EFFECT,
    BRIGHTNESS,
    COLOR,
    SPEED,
    ACCENT,
    APPEARANCE,
    BACKLIGHT,
    SCREEN_TIMEOUT,
    LANGUAGE,
    DEVELOPER,
    EXIT,
    MENU_COUNT,
};

enum view { HOME, MENU, EDIT };

typedef struct { const char *en, *zh; } translation_t;

static const translation_t settings[] = {
    {"Lighting", "灯光"}, {"Effect", "灯效"}, {"RGB Bright", "灯光亮度"}, {"Color", "颜色"},
    {"Speed", "速度"}, {"Accent", "主题色"}, {"Theme", "界面主题"}, {"Backlight", "屏幕亮度"},
    {"Timeout", "熄屏时间"}, {"Language", "语言"}, {"Developer", "开发模式"}, {"Exit", "退出"},
};
static const struct {
    uint8_t mode;
    translation_t name;
} effects[] = {
    {RGB_MATRIX_SOLID_COLOR, {"Solid", "常亮"}},
    {RGB_MATRIX_BREATHING, {"Breathe", "呼吸"}},
    {RGB_MATRIX_CYCLE_ALL, {"Cycle", "变色"}},
    {RGB_MATRIX_CYCLE_LEFT_RIGHT, {"Wave", "彩虹波"}},
    {RGB_MATRIX_CYCLE_PINWHEEL, {"Pinwheel", "旋转"}},
    {RGB_MATRIX_JELLYBEAN_RAINDROPS, {"Raindrops", "雨滴"}},
    {RGB_MATRIX_TYPING_HEATMAP, {"Heatmap", "热力"}},
};
static const struct {
    uint8_t hue, saturation;
    translation_t name;
} colors[] = {
    {0, 0, {"White", "白色"}}, {4, 215, {"Coral", "珊瑚"}}, {31, 255, {"Amber", "琥珀"}},
    {108, 188, {"Mint", "薄荷"}}, {136, 222, {"Ice", "冰蓝"}}, {169, 250, {"Blue", "深蓝"}},
    {203, 190, {"Lilac", "丁香"}},
};

enum appearance { DARK, LIGHT, SAND };
static const translation_t appearances[] = {
    [DARK] = {"Dark", "深色"}, [LIGHT] = {"Light", "浅色"}, [SAND] = {"Sand", "暖沙"},
};

static const struct {
    uint8_t hue, saturation;
    translation_t label;
} accents[] = {
    {135, 199, {"Ice", "冰蓝"}}, {29, 205, {"Amber", "琥珀"}},
    {0, 206, {"Red", "红色"}}, {14, 225, {"Orange", "橙色"}},
    {43, 210, {"Gold", "金黄"}}, {61, 197, {"Lime", "青柠"}},
    {88, 210, {"Green", "草绿"}}, {102, 180, {"Mint", "薄荷"}},
    {120, 190, {"Teal", "青绿"}}, {152, 205, {"Sky", "天蓝"}},
    {170, 220, {"Blue", "宝蓝"}}, {188, 186, {"Indigo", "靛蓝"}},
    {203, 189, {"Lilac", "丁香"}}, {219, 195, {"Purple", "紫色"}},
    {236, 183, {"Rose", "玫红"}}, {250, 125, {"Coral", "珊瑚"}},
};

static const struct {
    uint32_t milliseconds;
    translation_t label;
} screen_timeouts[] = {
    {30u * 1000u, {"30 sec", "30 秒"}},
    {60u * 1000u, {"1 min", "1 分钟"}},
    {3u * 60u * 1000u, {"3 min", "3 分钟"}},
    {5u * 60u * 1000u, {"5 min", "5 分钟"}},
    {10u * 60u * 1000u, {"10 min", "10 分钟"}},
    {0, {"Always on", "常亮"}},
};

static enum view view = HOME;
static uint8_t selected;
static bool redraw = true;
static bool preferences_loaded;
static uint8_t accent_color;
static bool english;
static uint8_t appearance;
static uint8_t backlight = DISPLAY_BACKLIGHT_LEVELS;
static uint8_t screen_timeout = 2; // 3 minutes, matching the previous default.
static bool preferences_pending;
static uint32_t activity;
static uint32_t knob_started;
static uint16_t knob_tap;
static bool knob_handled;
static bool knob_held;

static const char *translated(translation_t value) { return english ? value.en : value.zh; }
static const char *tr(const char *en, const char *zh) { return translated((translation_t){en, zh}); }

uint32_t display_idle_timeout_ms(void) {
    return screen_timeouts[screen_timeout].milliseconds;
}

static uint8_t next_index(uint8_t current, uint8_t count, bool forward) {
    return (current + count + (forward ? 1 : -1)) % count;
}

static int8_t effect_index(void) {
    for (uint8_t i = 0; i < ARRAY_SIZE(effects); ++i) {
        if (effects[i].mode == rgb_matrix_get_mode()) return i;
    }
    return -1;
}

static int8_t color_index(void) {
    hsv_t hsv = rgb_matrix_get_hsv();
    for (uint8_t i = 0; i < ARRAY_SIZE(colors); ++i) {
        if (colors[i].saturation == hsv.s && (!hsv.s || colors[i].hue == hsv.h)) return i;
    }
    return -1;
}

static uint8_t next_choice(int8_t current, uint8_t count, bool forward) {
    if (current < 0) return forward ? 0 : count - 1;
    return next_index(current, count, forward);
}

static uint8_t adjusted(uint8_t value, uint8_t max, bool increase) {
    int16_t step = (max + 9) / 10;
    int16_t result = value + (increase ? step : -step);
    if (result <= 0) return 0;
    if (result >= max) return max;
    return result;
}

static void turn(bool clockwise) {
    activity = timer_read32();
    redraw = true;
    if (view == MENU) {
        selected = next_index(selected, MENU_COUNT, clockwise);
        return;
    }

    switch (selected) {
        case EFFECT:
            rgb_matrix_mode_noeeprom(effects[next_choice(effect_index(), ARRAY_SIZE(effects), clockwise)].mode);
            break;
        case BRIGHTNESS: {
            hsv_t hsv = rgb_matrix_get_hsv();
            rgb_matrix_sethsv_noeeprom(hsv.h, hsv.s, adjusted(hsv.v, RGB_MATRIX_MAXIMUM_BRIGHTNESS, clockwise));
            break;
        }
        case COLOR: {
            uint8_t i = next_choice(color_index(), ARRAY_SIZE(colors), clockwise);
            rgb_matrix_sethsv_noeeprom(colors[i].hue, colors[i].saturation, rgb_matrix_get_val());
            break;
        }
        case SPEED:
            rgb_matrix_set_speed_noeeprom(adjusted(rgb_matrix_get_speed(), UINT8_MAX, clockwise));
            break;
        case ACCENT:
            accent_color = next_index(accent_color, ARRAY_SIZE(accents), clockwise);
            preferences_pending = true;
            return;
        case APPEARANCE:
            appearance = next_index(appearance, ARRAY_SIZE(appearances), clockwise);
            preferences_pending = true;
            return;
        case BACKLIGHT:
            if (clockwise && backlight < DISPLAY_BACKLIGHT_LEVELS) {
                ++backlight;
            } else if (!clockwise && backlight > 1) {
                --backlight;
            } else {
                return;
            }
            display_set_backlight_level(backlight);
            preferences_pending = true;
            return;
        case SCREEN_TIMEOUT:
            screen_timeout = next_index(screen_timeout, ARRAY_SIZE(screen_timeouts), clockwise);
            preferences_pending = true;
            return;
        case LANGUAGE:
            english = !english;
            preferences_pending = true;
            return;
        default:
            return;
    }
    rgb_settle_arm();
}

static void click(void) {
    activity = timer_read32();
    redraw = true;
    if (view == EDIT) {
        view = MENU;
    } else if (selected == EXIT) {
        view = HOME;
    } else if (selected == LIGHTING) {
        rgb_matrix_toggle_noeeprom();
        rgb_settle_arm();
    } else {
        view = EDIT;
    }
}

static bool confirming_boot(void) {
    return view == EDIT && selected == DEVELOPER;
}

static bool knob_hold_elapsed(void) {
    return timer_elapsed32(knob_started) >= (confirming_boot() ? BOOT_HOLD_MS : KNOB_HOLD_MS);
}

static void hold(void) {
    if (confirming_boot()) {
        reset_keyboard();
        return;
    }
    activity = timer_read32();
    redraw = true;
    if (view == HOME) {
        display_set_power(true);
        selected = LIGHTING;
        view = MENU;
    } else {
        view = view == EDIT ? MENU : HOME;
    }
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    keyevent_t event = record->event;
    if (IS_ENCODEREVENT(event) && view != HOME) {
        if (event.pressed) turn(event.type == ENCODER_CW_EVENT);
        return false;
    }
    if (!IS_KEYEVENT(event) || event.key.row != KNOB_ROW || event.key.col != KNOB_COL) return true;

    if (event.pressed) {
        // Keep a VIA reassigned click functional on the home screen. The
        // default Fn + knob (RS2_TFT) is also an entry point for Settings.
        if (view == HOME && keycode != KC_MUTE && keycode != RS2_TFT) return true;
        knob_handled = true;
        knob_held = false;
        knob_tap = keycode;
        knob_started = activity = timer_read32();
    } else {
        if (!knob_handled) return true;
        knob_handled = false;
        if (knob_held) return false;
        if (knob_hold_elapsed()) {
            hold();
        } else if (view != HOME) {
            click();
        } else if (knob_tap == RS2_TFT) {
            rs2_screen_toggle();
            redraw = true;
        } else {
            tap_code16(KC_MUTE);
        }
    }
    return false;
}

void housekeeping_task_user(void) {
    if (!preferences_loaded) {
        uint32_t saved = eeconfig_read_user();
        bool current_format = (saved & ~PREFERENCES_MASK) == PREFERENCES_SIGNATURE;
        if (current_format) {
            accent_color = saved & 15u;
            english = saved & (1u << LANGUAGE_SHIFT);
            uint8_t theme = (saved >> APPEARANCE_SHIFT) & 3u;
            if (theme < ARRAY_SIZE(appearances)) appearance = theme;
            uint8_t level = (saved >> BACKLIGHT_SHIFT) & 7u;
            if (level >= 1 && level <= DISPLAY_BACKLIGHT_LEVELS) backlight = level;
            uint8_t timeout = (saved & PREFERENCES_MASK) >> SLEEP_SHIFT;
            if (timeout < ARRAY_SIZE(screen_timeouts)) screen_timeout = timeout;
        } else if ((saved & ~0x3Fu) == LEGACY_PREFERENCES_SIGNATURE) {
            accent_color = saved & 1u;
            english = saved & 2u;
            appearance = saved & 4u ? LIGHT : DARK;
            uint8_t level = (saved >> 3) & 7u;
            if (level >= 1 && level <= DISPLAY_BACKLIGHT_LEVELS) backlight = level;
        }
        display_set_backlight_level(backlight);
        preferences_loaded = true;
        redraw = true;
    }
    if (knob_handled && !knob_held && knob_hold_elapsed()) {
        knob_held = true;
        hold();
    }
    if (view != HOME && timer_elapsed32(activity) >= MENU_IDLE_MS) {
        view = HOME;
        redraw = true;
    }
    if (preferences_pending && timer_elapsed32(activity) >= 900) {
        preferences_pending = false;
        eeconfig_update_user(PREFERENCES_SIGNATURE | accent_color | (english << LANGUAGE_SHIFT) |
                             (appearance << APPEARANCE_SHIFT) |
                             (backlight << BACKLIGHT_SHIFT) | (screen_timeout << SLEEP_SHIFT));
    }
}

typedef struct { uint8_t h, s, v; } ink_t;
typedef struct {
    ink_t background, card, rule, foreground, muted;
    uint8_t accent_value, selection_saturation, selection_value;
} palette_t;
typedef struct {
    painter_device_t panel, tile;
    painter_font_handle_t large, small;
    const palette_t *palette;
    ink_t accent, selected;
} canvas_t;

#define SCREEN_WIDTH RS2_PANEL_WIDTH
#define TILE_HEIGHT 34
#define SPI_SLICE_ROWS 2
#define HEADER_HEIGHT 28
#define MENU_ROW_HEIGHT 25
#define MENU_ROWS 3
#define HERO_Y HEADER_HEIGHT
#define DETAIL_Y (HERO_Y + TILE_HEIGHT)
#define STATUS_Y 84
#define FOOTER_Y 110
#define CONTENT_RIGHT (SCREEN_WIDTH - 9)

static uint16_t tile_pixels[SCREEN_WIDTH * TILE_HEIGHT];
static struct {
    uint8_t top, height, sent;
} tile_transfer;
static const palette_t palettes[] = {
    [DARK] = {
        .background = {158, 157, 25}, .card = {156, 149, 45}, .rule = {153, 132, 76},
        .foreground = {145, 16, 252}, .muted = {150, 69, 174},
        .accent_value = 255, .selection_saturation = 170, .selection_value = 79,
    },
    [LIGHT] = {
        .background = {148, 8, 250}, .card = {145, 24, 232}, .rule = {153, 34, 175},
        .foreground = {158, 145, 47}, .muted = {154, 93, 112},
        .accent_value = 120, .selection_saturation = 61, .selection_value = 249,
    },
    [SAND] = {
        .background = {26, 38, 231}, .card = {29, 28, 249}, .rule = {23, 76, 166},
        .foreground = {21, 126, 53}, .muted = {25, 119, 108},
        .accent_value = 118, .selection_saturation = 82, .selection_value = 245,
    },
};

// 12 × 12 monochrome symbols, tinted by the active screen palette.
static const uint16_t icon_usb[] = {0x060, 0x0f0, 0x060, 0x060, 0x266, 0x266, 0x3e6, 0x07e, 0x060, 0x0f0, 0x0f0, 0x000};
static const uint16_t icon_bt[] = {0x040, 0x060, 0x250, 0x148, 0x0d0, 0x060, 0x0d0, 0x148, 0x250, 0x060, 0x040, 0x000};
static const uint16_t icon_radio[] = {0x000, 0x1f8, 0x204, 0x402, 0x0f0, 0x108, 0x000, 0x060, 0x090, 0x060, 0x060, 0x000};
static const uint16_t icon_windows[] = {0x000, 0x79e, 0x79e, 0x79e, 0x79e, 0x000, 0x79e, 0x79e, 0x79e, 0x79e, 0x000, 0x000};
static const uint16_t icon_mac[] = {0x018, 0x030, 0x020, 0x39c, 0x7fe, 0x7fc, 0x7f8, 0x7fc, 0x7fe, 0x3fc, 0x1d8, 0x000};
static const uint16_t icon_caps[] = {0x060, 0x0f0, 0x1f8, 0x3fc, 0x0f0, 0x0f0, 0x0f0, 0x000, 0x3fc, 0x3fc, 0x000, 0x000};
static const uint16_t icon_light[] = {0x060, 0x462, 0x204, 0x0f0, 0x1f8, 0xdfb, 0xdfb, 0x1f8, 0x0f0, 0x204, 0x462, 0x060};
static const uint16_t icon_lock[] = {0x0f0, 0x198, 0x108, 0x108, 0x3fc, 0x3fc, 0x39c, 0x39c, 0x39c, 0x3fc, 0x3fc, 0x000};

static void box(const canvas_t *ui, uint8_t left, uint8_t top, uint8_t right, uint8_t bottom, ink_t fill) {
    qp_rect(ui->tile, left, top, right, bottom, fill.h, fill.s, fill.v, true);
}

enum { ROUND_TOP = 1, ROUND_BOTTOM = 2, ROUND_ALL = ROUND_TOP | ROUND_BOTTOM };

static void rounded_box(const canvas_t *ui, uint8_t l, uint8_t t, uint8_t r, uint8_t b, uint8_t corners, ink_t fill) {
    int top = t, bottom = b;
    if (corners & ROUND_TOP) {
        box(ui, l + 3, t, r - 3, t, fill);
        box(ui, l + 1, t + 1, r - 1, t + 2, fill);
        top += 3;
    }
    if (corners & ROUND_BOTTOM) {
        box(ui, l + 3, b, r - 3, b, fill);
        box(ui, l + 1, b - 2, r - 1, b - 1, fill);
        bottom -= 3;
    }
    if (top <= bottom) box(ui, l, top, r, bottom, fill);
}

static void draw_icon(const canvas_t *ui, uint8_t x, uint8_t y, const uint16_t *icon, ink_t color) {
    for (uint8_t row = 0; row < 12; ++row) {
        for (uint8_t col = 0; col < 12;) {
            uint8_t start = col;
            while (col < 12 && (icon[row] & (0x800u >> col))) ++col;
            if (col > start) box(ui, x + start, y + row, x + col - 1, y + row, color);
            else ++col;
        }
    }
}

static void text(const canvas_t *ui, painter_font_handle_t font, uint8_t x, uint8_t y, const char *s, ink_t fg, ink_t bg) {
    qp_drawtext_recolor(ui->tile, x, y, font, s, fg.h, fg.s, fg.v, bg.h, bg.s, bg.v);
}

static void center(const canvas_t *ui, painter_font_handle_t font, uint8_t y, const char *s, ink_t fg, ink_t bg) {
    uint16_t width = qp_textwidth(font, s);
    if (width <= SCREEN_WIDTH) text(ui, font, (SCREEN_WIDTH - width) / 2, y, s, fg, bg);
}

static void clear_tile(const canvas_t *ui, uint8_t height) {
    box(ui, 0, 0, SCREEN_WIDTH - 1, height - 1, ui->palette->background);
}

// The tile stays in RAM until its final slice is sent. Only finished pixels
// reach the panel, and the keyboard task runs between separate SPI slices.
static void stage_tile(uint8_t y, uint8_t height) {
    tile_transfer.top = y;
    tile_transfer.height = height;
    tile_transfer.sent = 0;
}

static void bar(const canvas_t *ui, uint8_t y, uint8_t percent, ink_t fill) {
    const uint8_t left = 14, right = SCREEN_WIDTH - 15;
    box(ui, left, y, right, y + 3, ui->palette->rule);
    uint8_t filled = percent * (right - left + 1) / 100;
    if (filled) box(ui, left, y, left + filled - 1, y + 3, fill);
}

typedef struct {
    uint8_t connection, slot, battery, flags;
    char title[8], date[8];
} home_t;

enum { MAC = 1, CAPS = 2, WIN_LOCK = 4, RGB_ON = 8, NOW_PLAYING = 16, PLAYING = 32, LINKED = 64 };

static home_t home_state(bool mac, uint8_t connection, bool media) {
    bool wireless_link = connection && ch582_is_connected();
    home_t home = {
        .connection = connection,
        .slot = connection == 1 && wireless_link ? ch582_get_slot() : 0,
        .battery = ch582_get_battery(),
        .flags = (mac ? MAC : 0) | (host_keyboard_led_state().caps_lock ? CAPS : 0) |
                 (keymap_config.no_gui ? WIN_LOCK : 0) |
                 (rgb_matrix_is_enabled() && !rgb_matrix_get_suspend_state() ? RGB_ON : 0) |
                 (media ? NOW_PLAYING : 0) | (media && media_playing() ? PLAYING : 0) |
                 (!connection || wireless_link ? LINKED : 0),
        .title = "RS2",
    };
    rtc_time_t time;
    if (rtc_get_time(&time)) {
        snprintf(home.title, sizeof(home.title), "%02u:%02u", time.hours, time.minutes);
        snprintf(home.date, sizeof(home.date), "%02u/%02u", time.month, time.day);
    }
    return home;
}

static void draw_header(canvas_t *ui, const home_t *home) {
    clear_tile(ui, HEADER_HEIGHT);
    if (view == HOME) {
        const uint16_t *link = home->connection == 1 ? icon_bt : home->connection == 2 ? icon_radio : icon_usb;
        bool show_slot = home->slot >= 1 && home->slot <= 3;
        ink_t link_color = home->flags & LINKED ? ui->accent : ui->palette->muted;
        rounded_box(ui, 8, 2, show_slot ? 49 : 34, 23, ROUND_ALL, ui->palette->card);
        draw_icon(ui, 15, 7, link, link_color);
        if (show_slot) {
            char slot[] = {'0' + home->slot, 0};
            text(ui, ui->small, 34, 2, slot, link_color, ui->palette->card);
        }
        rounded_box(ui, CONTENT_RIGHT - 26, 2, CONTENT_RIGHT, 23, ROUND_ALL, ui->palette->card);
        draw_icon(ui, CONTENT_RIGHT - 19, 7, home->flags & MAC ? icon_mac : icon_windows, ui->palette->foreground);
    } else {
        const char *title = view == MENU ? tr("SETTINGS", "设置") : translated(settings[selected]);
        text(ui, ui->small, 8, 2, title, ui->palette->foreground, ui->palette->background);
    }
    box(ui, 8, 25, CONTENT_RIGHT, 25, ui->palette->rule);
    box(ui, 8, 25, 36, 26, ui->accent);
    stage_tile(0, HEADER_HEIGHT);
}

static void draw_footer(canvas_t *ui, uint8_t y, const char *hint) {
    uint8_t height = RS2_PANEL_HEIGHT - y;
    clear_tile(ui, height);
    center(ui, ui->small, 0, hint, ui->palette->muted, ui->palette->background);
    stage_tile(y, height);
}

static void draw_media_line(const canvas_t *ui, uint8_t index) {
    char line[21];
    snprintf(line, sizeof(line), "%.20s", media_line(index));
    center(ui, ui->small, index ? 0 : 8, line, index ? ui->palette->muted : ui->palette->foreground, ui->palette->card);
}

enum { BAND_HEADER = 1, BAND_FIRST = 2, BAND_SECOND = 4, BAND_THIRD = 8, BAND_FOOTER = 16, BAND_PROGRESS = 32 };
#define PAGE_BANDS (BAND_HEADER | BAND_FIRST | BAND_SECOND | BAND_THIRD | BAND_FOOTER)
#define MENU_ITEM_BANDS (BAND_FIRST | BAND_SECOND | BAND_THIRD)

static void draw_home(canvas_t *ui, const home_t *home, uint8_t progress, uint8_t band) {
    bool playing = home->flags & NOW_PLAYING;
    if (band == BAND_HEADER) draw_header(ui, home);
    if (band == BAND_FIRST) {
        clear_tile(ui, TILE_HEIGHT);
        rounded_box(ui, 8, 2, CONTENT_RIGHT, TILE_HEIGHT - 1, ROUND_TOP, ui->palette->card);
        if (playing) draw_media_line(ui, 0);
        else center(ui, ui->large, 1, home->title, ui->palette->foreground, ui->palette->card);
        stage_tile(HERO_Y, TILE_HEIGHT);
    }
    if (band == BAND_SECOND) {
        clear_tile(ui, STATUS_Y - DETAIL_Y);
        rounded_box(ui, 8, 0, CONTENT_RIGHT, STATUS_Y - DETAIL_Y - 1, ROUND_BOTTOM, ui->palette->card);
        if (playing) draw_media_line(ui, 1);
        else if (home->date[0]) center(ui, ui->small, 0, home->date, ui->palette->muted, ui->palette->card);
        else box(ui, SCREEN_WIDTH / 2 - 10, 9, SCREEN_WIDTH / 2 + 9, 10, ui->accent);
        stage_tile(DETAIL_Y, STATUS_Y - DETAIL_Y);
    }
    if (band == BAND_PROGRESS) {
        // Keep the moving progress pixels off the metadata text strip.
        clear_tile(ui, 3);
        rounded_box(ui, 8, 0, CONTENT_RIGHT, 2, ROUND_BOTTOM, ui->palette->card);
        if (progress != UINT8_MAX) {
            const uint8_t filled = progress * (SCREEN_WIDTH - 28) / 100;
            box(ui, 14, 0, SCREEN_WIDTH - 15, 2, ui->palette->rule);
            if (filled) box(ui, 14, 0, 13 + filled, 2, home->flags & PLAYING ? ui->accent : ui->palette->muted);
        }
        stage_tile(STATUS_Y - 3, 3);
    }
    if (band == BAND_THIRD) {
        clear_tile(ui, FOOTER_Y - STATUS_Y);
        rounded_box(ui, 8, 2, 56, 19, ROUND_ALL, ui->palette->card);
        rounded_box(ui, 64, 2, 112, 19, ROUND_ALL, ui->palette->card);
        rounded_box(ui, 120, 2, 168, 19, ROUND_ALL, ui->palette->card);
        draw_icon(ui, 26, 5, icon_caps, home->flags & CAPS ? ui->accent : ui->palette->muted);
        draw_icon(ui, 82, 5, icon_light, home->flags & RGB_ON ? ui->accent : ui->palette->muted);
        draw_icon(ui, 138, 5, icon_lock, home->flags & WIN_LOCK ? ui->accent : ui->palette->muted);
        if (home->battery <= 100) {
            box(ui, 207, 5, 229, 13, ui->palette->muted);
            box(ui, 208, 6, 228, 12, ui->palette->background);
            box(ui, 230, 7, 231, 11, ui->palette->muted);
            uint8_t fill = home->battery * 21 / 100;
            if (fill) box(ui, 208, 6, 207 + fill, 12, ui->accent);
        }
        stage_tile(STATUS_Y, FOOTER_Y - STATUS_Y);
    }
    if (band == BAND_FOOTER) draw_footer(ui, FOOTER_Y, tr("HOLD KNOB", "长按旋钮设置"));
}

static uint8_t first_menu_item(uint8_t focus) {
    return MIN(focus / MENU_ROWS * MENU_ROWS, MENU_COUNT - MENU_ROWS);
}

static void draw_menu(canvas_t *ui, uint8_t band, bool lighting) {
    if (band == BAND_HEADER) {
        draw_header(ui, NULL);
        return;
    }
    if (band == BAND_FOOTER) {
        draw_footer(ui, HEADER_HEIGHT + MENU_ROWS * MENU_ROW_HEIGHT, tr("PRESS SELECT", "按下选择"));
        return;
    }

    uint8_t row = band == BAND_FIRST ? 0 : band == BAND_SECOND ? 1 : 2;
    uint8_t item = first_menu_item(selected) + row;
    bool focused = item == selected;
    clear_tile(ui, MENU_ROW_HEIGHT);
    ink_t fill = focused ? ui->selected : ui->palette->card;
    rounded_box(ui, 8, 1, CONTENT_RIGHT, 23, ROUND_ALL, fill);
    if (focused) box(ui, 11, 5, 12, 18, ui->accent);
    text(ui, ui->small, 17, 1, translated(settings[item]), focused ? ui->palette->foreground : ui->palette->muted, fill);
    if (item == LIGHTING) box(ui, CONTENT_RIGHT - 9, 9, CONTENT_RIGHT - 5, 13, lighting ? ui->accent : ui->palette->rule);
    stage_tile(HEADER_HEIGHT + row * MENU_ROW_HEIGHT, MENU_ROW_HEIGHT);
}

static int16_t edit_percent(void) {
    switch (selected) {
        case BACKLIGHT: return 100u * backlight / DISPLAY_BACKLIGHT_LEVELS;
        case BRIGHTNESS: return 100u * rgb_matrix_get_val() / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
        case SPEED: return 100u * rgb_matrix_get_speed() / UINT8_MAX;
        default: return -1;
    }
}

static void draw_edit(canvas_t *ui, uint8_t band) {
    if (band == BAND_HEADER) {
        draw_header(ui, NULL);
        return;
    }
    if (band == BAND_FOOTER) {
        draw_footer(ui, FOOTER_Y, confirming_boot() ? tr("HOLD FLASH", "长按确认") : tr("PRESS DONE", "按下完成"));
        return;
    }
    if (band == BAND_THIRD) {
        clear_tile(ui, FOOTER_Y - STATUS_Y);
        stage_tile(STATUS_Y, FOOTER_Y - STATUS_Y);
        return;
    }

    int16_t percent = edit_percent();
    if (band == BAND_SECOND) {
        clear_tile(ui, STATUS_Y - DETAIL_Y);
        rounded_box(ui, 8, 0, CONTENT_RIGHT, 18, ROUND_BOTTOM, ui->palette->card);
        if (percent >= 0) bar(ui, 9, percent, ui->accent);
        if (selected == COLOR) {
            hsv_t hsv = rgb_matrix_get_hsv();
            rounded_box(ui, SCREEN_WIDTH / 2 - 27, 4, SCREEN_WIDTH / 2 + 26, 12, ROUND_ALL, (ink_t){hsv.h, hsv.s, 235});
        } else if (selected == ACCENT) {
            rounded_box(ui, SCREEN_WIDTH / 2 - 27, 4, SCREEN_WIDTH / 2 + 26, 12, ROUND_ALL,
                        (ink_t){accents[accent_color].hue, accents[accent_color].saturation, 240});
        } else if (selected == DEVELOPER) {
            center(ui, ui->small, 0, tr("TAP BACK", "短按返回"), ui->palette->muted, ui->palette->card);
        }
        stage_tile(DETAIL_Y, STATUS_Y - DETAIL_Y);
        return;
    }

    clear_tile(ui, TILE_HEIGHT);
    rounded_box(ui, 8, 2, CONTENT_RIGHT, TILE_HEIGHT - 1, ROUND_TOP, ui->palette->card);
    if (percent >= 0) {
        char value[8];
        snprintf(value, sizeof(value), "%u%%", (unsigned)percent);
        center(ui, ui->large, 1, value, ui->palette->foreground, ui->palette->card);
    } else if (selected == ACCENT) {
        center(ui, ui->small, 9, translated(accents[accent_color].label), ui->palette->foreground, ui->palette->card);
    } else if (selected == APPEARANCE) {
        center(ui, ui->small, 9, translated(appearances[appearance]), ui->palette->foreground, ui->palette->card);
    } else if (selected == SCREEN_TIMEOUT) {
        center(ui, ui->small, 9, translated(screen_timeouts[screen_timeout].label), ui->palette->foreground, ui->palette->card);
    } else if (selected == LANGUAGE) {
        center(ui, ui->small, 9, tr("English", "简体中文"), ui->palette->foreground, ui->palette->card);
    } else if (selected == DEVELOPER) {
        center(ui, ui->small, 9, tr("Bootloader", "进入刷机"), ui->palette->foreground, ui->palette->card);
    } else {
        int8_t index = selected == EFFECT ? effect_index() : color_index();
        const char *label = index < 0 ? tr("Custom", "自定义") :
                            translated(selected == EFFECT ? effects[index].name : colors[index].name);
        center(ui, ui->small, 9, label, ui->palette->foreground, ui->palette->card);
    }
    stage_tile(HERO_Y, TILE_HEIGHT);
}

static struct {
    canvas_t ui;
    home_t home;
    enum view view;
    uint8_t pending, progress, selected, accent, appearance;
    bool english, lighting, retry;
} refresh;

static uint8_t home_changes(const home_t *home, uint8_t progress, bool media_changed) {
    const home_t *last = &refresh.home;
    uint8_t flags = home->flags ^ last->flags;
    bool playing = home->flags & NOW_PLAYING;
    uint8_t bands = 0;
    if (flags & (MAC | LINKED) || home->connection != last->connection || home->slot != last->slot) bands |= BAND_HEADER;
    if (flags & NOW_PLAYING || (playing ? media_changed : strcmp(home->title, last->title))) bands |= BAND_FIRST;
    if (flags & NOW_PLAYING || (playing ? media_changed : strcmp(home->date, last->date))) bands |= BAND_SECOND;
    if (playing && ((bands & BAND_SECOND) || (flags & PLAYING) || progress != refresh.progress)) bands |= BAND_PROGRESS;
    if (flags & (CAPS | WIN_LOCK | RGB_ON) || home->battery != last->battery) bands |= BAND_THIRD;
    return bands;
}

static uint8_t menu_changes(bool lighting) {
    uint8_t first = first_menu_item(selected);
    if (first != first_menu_item(refresh.selected)) return MENU_ITEM_BANDS;

    uint8_t bands = 0;
    if (selected != refresh.selected) {
        bands = (BAND_FIRST << (selected - first)) | (BAND_FIRST << (refresh.selected - first));
    }
    if (first == LIGHTING && lighting != refresh.lighting) bands |= BAND_FIRST;
    return bands;
}

void rs2_render_dashboard(painter_device_t panel, painter_font_handle_t large, painter_font_handle_t small,
                                   bool mac, uint8_t connection, bool media, bool force) {
    static painter_device_t surface;
    static painter_font_handle_t chinese;
    if (!surface) {
        surface = qp_make_rgb565_surface(SCREEN_WIDTH, TILE_HEIGHT, tile_pixels);
        if (!surface || !qp_init(surface, QP_ROTATION_0)) {
            surface = NULL;
            return;
        }
        chinese = qp_load_font_mem(font_ak650_sc_16);
        force = true;
    }
    if (!large || !small || !chinese) return;

    const palette_t *palette = &palettes[appearance];
    uint8_t hue = accents[accent_color].hue;
    refresh.ui = (canvas_t){
        .panel = panel, .tile = surface, .large = large, .small = english ? small : chinese,
        .palette = palette,
        .accent = {hue, accents[accent_color].saturation, palette->accent_value},
        .selected = {hue, palette->selection_saturation, palette->selection_value},
    };
    bool all = force || refresh.retry || refresh.view != view || refresh.english != english ||
               refresh.accent != accent_color || refresh.appearance != appearance ||
               (view == EDIT && refresh.selected != selected);
    if (all) {
        refresh.pending = PAGE_BANDS;
        tile_transfer.height = 0;
    }
    bool lighting = rgb_matrix_is_enabled();
    if (view == MENU) {
        if (!all) refresh.pending |= menu_changes(lighting);
    } else if (view == EDIT) {
        if (!all && redraw) {
            refresh.pending |= BAND_FIRST;
            if (edit_percent() >= 0 || selected == COLOR || selected == ACCENT) refresh.pending |= BAND_SECOND;
        }
    } else {
        home_t home = home_state(mac, connection, media);
        bool media_changed = media_take_dirty();
        uint8_t progress = UINT8_MAX;
        if (media) {
            uint32_t duration = media_duration_ms();
            uint32_t elapsed = media_elapsed_ms();
            if (duration) progress = elapsed >= duration ? 100 : (uint64_t)elapsed * 100 / duration;
        }
        refresh.pending |= all ? (media ? BAND_PROGRESS : 0) : home_changes(&home, progress, media_changed);
        refresh.home = home;
        refresh.progress = progress;
    }
    refresh.retry = false;
    redraw = false;
    refresh.view = view;
    refresh.selected = selected;
    refresh.lighting = lighting;
    refresh.english = english;
    refresh.accent = accent_color;
    refresh.appearance = appearance;
}

void rs2_send_dashboard_slice(void) {
    if (redraw || !refresh.ui.tile) return;

    if (tile_transfer.height) {
        uint8_t rows = MIN(SPI_SLICE_ROWS, tile_transfer.height - tile_transfer.sent);
        uint8_t y = tile_transfer.top + tile_transfer.sent;
        painter_device_t panel = refresh.ui.panel;
        if (!rs2_screen_blit_rows(panel, y, rows, tile_pixels + SCREEN_WIDTH * tile_transfer.sent)) {
            refresh.retry = true;
            refresh.pending = 0;
            tile_transfer.height = 0;
            return;
        }
        tile_transfer.sent += rows;
        if (tile_transfer.sent == tile_transfer.height) tile_transfer.height = 0;
        return;
    }
    if (!refresh.pending) return;
    uint8_t pending = refresh.pending;
    if (refresh.view == MENU && (pending & MENU_ITEM_BANDS)) pending &= MENU_ITEM_BANDS;
    uint8_t band = pending & -pending;
    refresh.pending &= ~band;
    switch (refresh.view) {
        case HOME: draw_home(&refresh.ui, &refresh.home, refresh.progress, band); break;
        case MENU: draw_menu(&refresh.ui, band, refresh.lighting); break;
        case EDIT: draw_edit(&refresh.ui, band); break;
    }
}

bool rs2_dashboard_needs_redraw(void) {
    return redraw;
}

bool rs2_dashboard_pending(void) {
    return !refresh.ui.tile || refresh.pending || tile_transfer.height || refresh.retry || redraw;
}
