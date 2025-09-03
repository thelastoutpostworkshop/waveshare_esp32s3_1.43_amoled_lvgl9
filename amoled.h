// Main class for the display controller
//
#ifndef AMOLED_H
#define AMOLED_H

#include "Arduino.h"
#include "low_level_amoled.h"
#include "board_config.h"
// #include "FT3168.h"
#include "read_lcd_id_bsp.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"

// Human-readable RGB565 colors (programmer shortcuts)
enum Color565 : uint16_t
{
    AMOLED_COLOR_BLACK = 0x0000,
    AMOLED_COLOR_WHITE = 0xFFFF,
    AMOLED_COLOR_RED = 0xF800,
    AMOLED_COLOR_LIME = 0x07E0, // bright green
    AMOLED_COLOR_GREEN = 0x07E0,
    AMOLED_COLOR_BLUE = 0x001F,
    AMOLED_COLOR_CYAN = 0x07FF,
    AMOLED_COLOR_AQUA = 0x07FF,
    AMOLED_COLOR_MAGENTA = 0xF81F,
    AMOLED_COLOR_FUCHSIA = 0xF81F,
    AMOLED_COLOR_YELLOW = 0xFFE0,
    AMOLED_COLOR_ORANGE = 0xFD20,
    AMOLED_COLOR_PURPLE = 0x8010,
    AMOLED_COLOR_VIOLET = 0x801F,
    AMOLED_COLOR_PINK = 0xF97F,
    AMOLED_COLOR_GRAY = 0x8410,
    AMOLED_COLOR_GREY = 0x8410,
    AMOLED_COLOR_SILVER = 0xC618,
    AMOLED_COLOR_MAROON = 0x8000,
    AMOLED_COLOR_OLIVE = 0x8400,
    AMOLED_COLOR_NAVY = 0x0010,
    AMOLED_COLOR_TEAL = 0x0410,
    AMOLED_COLOR_BROWN = 0xA145,
    AMOLED_COLOR_GOLD = 0xFEA0,
    AMOLED_COLOR_SKYBLUE = 0x867D,
};

class Amoled
{
private:
    uint8_t controller_id = 0x00;
    esp_lcd_panel_handle_t panel_handle = NULL;
    uint16_t *lineBuffer; // 2 rows
    int lineBufferSize = 0;
    inline bool pushToPanel(int x, int y, const uint16_t *buf, int w, int h);
    bool reserveLineBuffer();

public:
    Amoled();
    ~Amoled();
    uint8_t ID();
    char *name();
    bool begin();
    bool drawBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h);
    bool drawArea(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t *bitmap);
    bool fillScreen(uint16_t color565);
    bool fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color565);
    bool fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color565 color)
    {
        return fillRect(x, y, w, h, static_cast<uint16_t>(color));
    }
    bool invertColor(bool invert);
};

#endif