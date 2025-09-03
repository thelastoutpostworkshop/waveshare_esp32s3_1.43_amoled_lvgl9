// Tutorial : https://youtu.be/jYcxUgxz9ks
// Use board "ESP32 Dev Module" (last tested on v3.2.0)

#define LV_CONF_INCLUDE_SIMPLE // Use the lv_conf.h included in this project, to configure see https://docs.lvgl.io/master/get-started/platforms/arduino.html

#include <lvgl.h> // Install "lvgl" with the Library Manager (last tested on v9.2.2)
#include "ui.h"
#include "amoled.h"
#include "FT3168.h" // Capacitive Touch functions
#define DRAW_BUF_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

Amoled amoled; // Main object for the display
lv_display_t *disp;
lv_color_t *buf1 = nullptr; // lv_color_t matches LV_COLOR_DEPTH
lv_color_t *buf2 = nullptr;

void setup()
{
    Serial.begin(115200);
    delay(4000); // Give time to the serial port to show initial messages printed on the serial port upon reset

    // Display initialization
    if (!amoled.begin())
    {
        Serial.println("Display initialization failed!");
        while (true)
        {
            /* no need to continue */
        }
    }
    // init LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);

    buf1 = (lv_color_t *)heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    if (!buf1)
    {
        Serial.println("LVGL buffer 1 allocate failed!");
        while (true)
        {
        }
    }

    buf2 = (lv_color_t *)heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    if (!buf2)
    {
        Serial.println("LVGL buffer 2 allocate failed!");
        while (true)
        {
        }
    }
    disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // register print function for debugging
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif
    ui_init();
}

void loop()
{
    lv_task_handler(); /* let LVGL do its GUI work */
    delay(5);
}

// LVGL calls this function to print log information
void my_print(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    Serial.println(buf);
    Serial.flush();
}

// LVGL calls this function to retrieve elapsed time
uint32_t millis_cb(void)
{
    return millis();
}

// LVGL calls this function when a rendered image needs to copied to the display
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    amoled.drawArea(area->x1, area->y1, area->x2, area->y2,(uint16_t*) px_map);

    lv_disp_flush_ready(disp);
}
