// Tutorial :
// Use board "ESP32 Dev Module" (last tested on v3.3.0)

// The next line instruct LVGL to use lv_conf.h included in this project
#define LV_CONF_INCLUDE_SIMPLE 

#include <lvgl.h> // Install "lvgl" with the Library Manager (last tested on v9.2.2)
#include "amoled.h"
#include "FT3168.h" // Capacitive Touch functions
#include "qmi8658c.h"

Amoled amoled; // Main object for the display board

#define LVGL_DRAW_BUF_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t))    // LVGL Display buffer size

// LVGL global variables for the display and its buffers
lv_display_t *disp;
lv_color_t *lvgl_buf1 = nullptr; 
lv_color_t *lvgl_buf2 = nullptr;

#define HISTORY_POINTS 60 // samples shown on the chart
#define UI_UPDATE_MS 50   // ~20 Hz UI update
#define RAD2DEG(x) ((x) * (180.0f / 3.1415926f))

// Global to store the latest sample read the accelerometer and gyroscope (QMI8658)
typedef struct
{
    float ax, ay, az;
    float gx, gy, gz;
    float temp;
} ImuData;
volatile ImuData g_imu; 

// --- Globals (widget handles) ---
static lv_obj_t *chart;
static lv_chart_series_t *ser_ax, *ser_ay, *ser_az;

static lv_obj_t *arc_pitch;
static lv_obj_t *arc_roll;

static lv_obj_t *lbl_axyz;
static lv_obj_t *lbl_gxyz;
static lv_obj_t *lbl_angles;

void setup()
{
    Serial.begin(115200);

    // Optional: Give time to the serial port to show initial messages printed on the serial port upon reset
    delay(4000); 
    
    // Initialize the touch screen
    Touch_Init(); 

    // Display initialization
    if (!amoled.begin())
    {
        Serial.println("Display initialization failed!");
        while (true)
        {
            /* no need to continue */
        }
    }

    // LVGL initialization
    lv_init();
    lv_tick_set_cb(millis_cb);

    // LVGL Buffers allocation for the display
    lvgl_buf1 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lvgl_buf1)
    {
        Serial.println("LVGL buffer 1 allocate failed!");
        while (true)
        {
        }
    }
    lvgl_buf2 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lvgl_buf2)
    {
        Serial.println("LVGL buffer 2 allocate failed!");
        while (true)
        {
        }
    }

    // Create the LVGL display
    disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, lvgl_buf1, lvgl_buf2, LVGL_DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // Create input touchpad device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touchpad_read);

    // register print function for debugging
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif
    // Create the task to read QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope)
    xTaskCreatePinnedToCore(imu_task, "imu", 4096, NULL, 2, NULL, 1);
    imu_ui_create();
}

void loop()
{
    lv_timer_handler(); /* let LVGL do its GUI work */
    delay(5);
}

static void imu_task(void *arg)
{
    qmi8658_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;)
    {
        float acc[3], gyro[3];
        float temp = 0;
        qmi8658_read_xyz(acc, gyro);
        temp = qmi8658_readTemp();
        g_imu.ax = acc[0];
        g_imu.ay = acc[1];
        g_imu.az = acc[2];
        g_imu.gx = gyro[0];
        g_imu.gy = gyro[1];
        g_imu.gz = gyro[2];
        g_imu.temp = temp;

        vTaskDelay(pdMS_TO_TICKS(100)); // 100 Hz sampling (adjust as you like)
    }
}

// Simple tilt (from accel only). For fused angle, feed your AHRS output here.
static void compute_pitch_roll_from_accel(float ax, float ay, float az, float *pitch_deg, float *roll_deg)
{
    // Using common aerospace-ish convention:
    // pitch = atan2(-ax, sqrt(ay^2 + az^2)), roll = atan2(ay, az)
    *pitch_deg = RAD2DEG(atan2f(-ax, sqrtf(ay * ay + az * az)));
    *roll_deg = RAD2DEG(atan2f(ay, az));
}

static void ui_tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    const float ax = g_imu.ax, ay = g_imu.ay, az = g_imu.az;
    const float gx = g_imu.gx, gy = g_imu.gy, gz = g_imu.gz;
    const float temp = g_imu.temp;

    lv_chart_set_next_value(chart, ser_ax, (int32_t)(ax * 1000.0f));
    lv_chart_set_next_value(chart, ser_ay, (int32_t)(ay * 1000.0f));
    lv_chart_set_next_value(chart, ser_az, (int32_t)(az * 1000.0f));

    float pitch_deg, roll_deg;
    compute_pitch_roll_from_accel(ax, ay, az, &pitch_deg, &roll_deg);
    if (pitch_deg < -90)
        pitch_deg = -90;
    if (pitch_deg > 90)
        pitch_deg = 90;
    if (roll_deg < -90)
        roll_deg = -90;
    if (roll_deg > 90)
        roll_deg = 90;

    lv_arc_set_value(arc_pitch, (int32_t)pitch_deg);
    lv_arc_set_value(arc_roll, (int32_t)roll_deg);

    lv_label_set_text_fmt(lbl_axyz, "Accel: X %.3f  Y %.3f  Z %.3f g", ax, ay, az);
    lv_label_set_text_fmt(lbl_gxyz, "Gyro:  X %.1f  Y %.1f  Z %.1f dps", gx, gy, gz);
    lv_label_set_text_fmt(lbl_angles, "Pitch %.1f°  Roll %.1f°  T %.1fC", pitch_deg, roll_deg, temp);
}

// --- UI builder ---
void imu_ui_create(void)
{
    lv_obj_t *root = lv_scr_act();

    // Title
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "IMU (QMI8658)");
    lv_obj_set_style_text_font(title, lv_theme_get_font_large(root), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // CHART (top)
    chart = lv_chart_create(root);
    lv_obj_set_size(chart, 300, 140);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 36);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HISTORY_POINTS);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    // Optional axes ranges for accel in g (adjust to your scale)
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -2 * 1000, 2 * 1000); // scaled by 1000 for precision
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, HISTORY_POINTS - 1);

    // Three series for Accel X/Y/Z (use default theme colors)
    ser_ax = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_ay = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    ser_az = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // ARCS (middle) - Pitch & Roll, range -90..+90 deg
    arc_pitch = lv_arc_create(root);
    lv_obj_set_size(arc_pitch, 110, 110);
    lv_obj_align(arc_pitch, LV_ALIGN_LEFT_MID, 24, 10);
    lv_arc_set_range(arc_pitch, -90, 90);
    lv_arc_set_bg_angles(arc_pitch, 150, 390); // a nice 240° sweep
    lv_obj_remove_flag(arc_pitch, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lblP = lv_label_create(root);
    lv_label_set_text(lblP, "Pitch");
    lv_obj_align_to(lblP, arc_pitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    arc_roll = lv_arc_create(root);
    lv_obj_set_size(arc_roll, 110, 110);
    lv_obj_align(arc_roll, LV_ALIGN_RIGHT_MID, -24, 10);
    lv_arc_set_range(arc_roll, -90, 90);
    lv_arc_set_bg_angles(arc_roll, 150, 390);
    lv_obj_remove_flag(arc_roll, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lblR = lv_label_create(root);
    lv_label_set_text(lblR, "Roll");
    lv_obj_align_to(lblR, arc_roll, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    // LABELS (bottom)
    lbl_axyz = lv_label_create(root);
    lv_obj_align(lbl_axyz, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    lv_label_set_text(lbl_axyz, "Accel: X 0.000  Y 0.000  Z 0.000 g");

    lbl_gxyz = lv_label_create(root);
    lv_obj_align(lbl_gxyz, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(lbl_gxyz, "Gyro:  X 0.0  Y 0.0  Z 0.0 dps");

    lbl_angles = lv_label_create(root);
    lv_obj_align(lbl_angles, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    lv_label_set_text(lbl_angles, "Pitch 0.0°  Roll 0.0°");

    // Timer to refresh UI
    lv_timer_create(ui_tick_cb, UI_UPDATE_MS, NULL);
}

// LVGL calls this function to read the touchpad
void lvgl_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t tp_x = 0, tp_y = 0;
    uint8_t win = getTouch(&tp_x, &tp_y);
    if (win)
    {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
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
    // Serial.printf("x1=%d, y1=%d, x2=%d, y2=%d\n",area->x1, area->y1, area->x2, area->y2);
    amoled.drawArea(area->x1, area->y1, area->x2, area->y2, (uint16_t *)px_map);

    lv_disp_flush_ready(disp);
}

// LVGL display rounder callback for CO5300
static void rounder_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_INVALIDATE_AREA)
    {
        lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
        if (area)
        {
            // Round coordinates for CO5300 display optimization
            area->x1 = (area->x1) & ~1; // Round down to even
            area->x2 = (area->x2) | 1;  // Round up to odd
            area->y1 = (area->y1) & ~1; // Round down to even
            area->y2 = (area->y2) | 1;  // Round up to odd
        }
    }
}
