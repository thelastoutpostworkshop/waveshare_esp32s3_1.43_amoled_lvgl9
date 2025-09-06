// Tutorial :
// Use board "ESP32 Dev Module" (last tested on v3.3.0)

// The next line instruct LVGL to use lv_conf.h included in this project
#define LV_CONF_INCLUDE_SIMPLE

// Comment the next line if you want to use your own design (ex. from Squareline studio)
// #define USE_BUILT_IN_EXAMPLE

#include <lvgl.h> // Install "lvgl" with the Library Manager (last tested on v9.2.2)
#include "amoled.h"
#include "FT3168.h" // Capacitive Touch functions
#include "qmi8658c.h"
#include "ui.h"

Amoled amoled; // Main object for the display board

#define LVGL_DRAW_BUF_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t)) // LVGL Display buffer size

// LVGL global variables for the display and its buffers
lv_display_t *disp;
lv_color_t *lvgl_buf1 = nullptr;
lv_color_t *lvgl_buf2 = nullptr;

// Global to store the latest sample read the accelerometer and gyroscope (QMI8658)
typedef struct
{
    float ax, ay, az;
    float gx, gy, gz;
    float temp;
} ImuData;
volatile ImuData g_imu;

extern lv_obj_t *uic_bubble;

#ifdef USE_BUILT_IN_EXAMPLE
// Globals variable for the example
static lv_obj_t *chart;
static lv_chart_series_t *ser_ax, *ser_ay, *ser_az;

static lv_obj_t *arc_pitch;
static lv_obj_t *arc_roll;

static lv_obj_t *lbl_axyz;
static lv_obj_t *lbl_gxyz;
static lv_obj_t *lbl_angles;

#define HISTORY_POINTS 60 // samples shown on the chart
#define UI_UPDATE_MS 50
#define RAD2DEG(x) ((x) * (180.0f / 3.1415926f))
#endif

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

    // Create the LVGL input touchpad device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touchpad_read);

    // Register LVGL print function for logging
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    // Create the task to read QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope)
    xTaskCreatePinnedToCore(imu_task, "imu", 4096, NULL, 2, NULL, 1);

#ifdef USE_BUILT_IN_EXAMPLE
    // Launch the UI example
    imu_ui_create();
#else
    // If you want to use a UI created with Squarline Studio, call it here
    // ex.: ui_init();
    ui_init();
    // Periodic timer to update/move the bubble image using latest IMU data
    lv_timer_create(move_bubble, 50, NULL); // ~20 Hz
#endif

}

void loop()
{
    lv_timer_handler(); /* let LVGL do its GUI work */
    delay(5);
}

// Task to read the values of QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope)
static void imu_task(void *arg)
{
    qmi8658_init();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Do not change this delay, it is required by the QMI8658

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

    lv_display_flush_ready(disp);
}

// LVGL display rounder callback for CO5300 (a kind of patch for LVGL 9)
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

// Periodic LVGL timer to move the bubble image based on latest IMU data
void move_bubble(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (!uic_bubble)
        return; // UI not ready yet

    // Ensure our manual positioning takes effect and object is visible/foreground
    static bool bubble_prepared = false;
    if (!bubble_prepared)
    {
        lv_obj_set_align(uic_bubble, LV_ALIGN_TOP_LEFT);
        lv_obj_clear_flag(uic_bubble, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(uic_bubble);
        bubble_prepared = true;
    }

    // Smooth IMU a bit to keep motion calm
    static float sm_ax = 0.0f, sm_ay = 0.0f, sm_az = 0.0f;
    sm_ax = sm_ax + 0.2f * (g_imu.ax - sm_ax);
    sm_ay = sm_ay + 0.2f * (g_imu.ay - sm_ay);
    sm_az = sm_az + 0.2f * (g_imu.az - sm_az);

    // Compute pitch/roll from accel only
    float pitch_deg = (180.0f / 3.1415926f) * atan2f(-sm_ax, sqrtf(sm_ay * sm_ay + sm_az * sm_az));
    float s = (sm_az >= 0.0f) ? 1.0f : -1.0f; // normalize so flat => 0 roll
    float roll_deg = (180.0f / 3.1415926f) * atan2f(sm_ay * s, sm_az * s);
    if (pitch_deg < -90)
        pitch_deg = -90;
    if (pitch_deg > 90)
        pitch_deg = 90;
    if (roll_deg < -180)
        roll_deg = -180;
    if (roll_deg > 180)
        roll_deg = 180;

    // Find parent center
    lv_obj_t *parent = lv_obj_get_parent(uic_bubble);
    int pw = lv_obj_get_width(parent);
    int ph = lv_obj_get_height(parent);
    if (pw <= 0 || ph <= 0)
    {
        // Fallback to display resolution if parent not sized yet
        lv_display_t *d = lv_display_get_default();
        pw = lv_display_get_horizontal_resolution(d);
        ph = lv_display_get_vertical_resolution(d);
    }
    int cx = pw / 2;
    int cy = ph / 2;

    // Bubble size
    int bw = lv_obj_get_width(uic_bubble);
    int bh = lv_obj_get_height(uic_bubble);
    int br = (bw > bh ? bw : bh) / 2; // approx radius

    // Choose a radius that keeps bubble inside the dial (leave small margin)
    int r_max = (pw < ph ? pw : ph) / 2 - br - 10; // 10 px margin from ring
    if (r_max < 0)
        r_max = 0;

    // Pixels per degree so that ~45° reaches near the ring
    float px_per_deg = r_max / 45.0f;

    float dx = roll_deg * px_per_deg;
    float dy = -pitch_deg * px_per_deg;
    float rr = sqrtf(dx * dx + dy * dy);
    if (rr > r_max && rr > 0.0f)
    {
        float k = (float)r_max / rr;
        dx *= k;
        dy *= k;
    }

    // Position the bubble with its center at (cx+dx, cy+dy)
    int x = (int)lrintf((float)cx + dx - bw / 2.0f);
    int y = (int)lrintf((float)cy + dy - bh / 2.0f);
    // Serial.printf("Bubble x=%d,y=%d\n",x,y);
    lv_obj_set_pos(uic_bubble, x, y);
}

#ifdef USE_BUILT_IN_EXAMPLE
// All the functions below are the UI example
//

// Simple tilt (from accel only). For fused angle, feed your AHRS output here.
static void compute_pitch_roll_from_accel(float ax, float ay, float az, float *pitch_deg, float *roll_deg)
{
    // Using common aerospace-ish convention:
    // pitch = atan2(-ax, sqrt(ay^2 + az^2)), roll = atan2(ay, az)
    *pitch_deg = RAD2DEG(atan2f(-ax, sqrtf(ay * ay + az * az)));
    // Normalize roll so that when the device is flat, roll = 0,
    // regardless of whether az is positive or negative.
    float s = (az >= 0.0f) ? 1.0f : -1.0f;
    *roll_deg = RAD2DEG(atan2f(ay * s, az * s));
}

// ---------- Optional smoothing (keeps UI calm) ----------
static inline float ema(float prev, float sample, float alpha)
{
    return prev + alpha * (sample - prev); // alpha ~ 0.1..0.3
}

// ---------- Styles (one-time) ----------
static void apply_chart_style(lv_obj_t *chart)
{
    // dark translucent panel for the chart
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x1d2430), LV_PART_MAIN);
    // Opaque background to avoid temporary layer allocations on transparency
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
    // division lines
    lv_chart_set_div_line_count(chart, 4, 8);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN); // grid
    // series line width
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS); // series
    lv_obj_set_style_pad_all(chart, 6, 0);
}

static void style_arc_gauge(lv_obj_t *arc, lv_color_t color)
{
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2f3747), LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);

    lv_arc_set_bg_angles(arc, 150, 390); // 240° sweep
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

// ---------- Build UI ----------
void imu_ui_create(void)
{
    // Fixed pixel layout for a 466x466 circular display.
    // Chosen constants keep all widgets inside the visible circle,
    // using the inscribed 320x320 square centered on screen (left/top=73).
    lv_obj_t *root = lv_scr_act();

    // Title
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "IMU (QMI8658)");
    // lv_obj_set_style_text_font(title, lv_theme_get_font_large(root), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 78);

    // Chart (300x90), centered horizontally under the title
    chart = lv_chart_create(root);
    lv_obj_set_size(chart, 300, 90);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 102);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HISTORY_POINTS);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -2 * 1000, 2 * 1000);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, HISTORY_POINTS - 1);
    apply_chart_style(chart);

    // Series
    ser_ax = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_ay = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    ser_az = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // Legend directly under the chart (300x20)
    lv_obj_t *legend = lv_obj_create(root);
    lv_obj_set_size(legend, 300, 20);
    lv_obj_align(legend, LV_ALIGN_TOP_MID, 0, 198);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER);

    auto make_leg = [](lv_obj_t *parent, const char *txt, lv_color_t col)
    {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(row, 0, 0);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_bg_color(dot, col, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, txt);
        return row;
    };
    make_leg(legend, "Ax", lv_palette_main(LV_PALETTE_RED));
    make_leg(legend, "Ay", lv_palette_main(LV_PALETTE_GREEN));
    make_leg(legend, "Az", lv_palette_main(LV_PALETTE_BLUE));

    // Gauges: two 100x100 arcs centered horizontally at y~268
    // Pitch
    arc_pitch = lv_arc_create(root);
    lv_obj_set_size(arc_pitch, 100, 100);
    style_arc_gauge(arc_pitch, lv_palette_main(LV_PALETTE_CYAN));
    lv_arc_set_range(arc_pitch, -90, 90);
    lv_obj_align(arc_pitch, LV_ALIGN_CENTER, -80, 35);
    lv_obj_t *pitch_text = lv_label_create(arc_pitch);
    lv_label_set_text(pitch_text, "0°");
    lv_obj_center(pitch_text);
    lv_obj_t *lblP = lv_label_create(root);
    lv_label_set_text(lblP, "Pitch");
    lv_obj_align_to(lblP, arc_pitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Roll
    arc_roll = lv_arc_create(root);
    lv_obj_set_size(arc_roll, 100, 100);
    style_arc_gauge(arc_roll, lv_palette_main(LV_PALETTE_INDIGO));
    lv_arc_set_range(arc_roll, -180, 180);
    lv_obj_align(arc_roll, LV_ALIGN_CENTER, 80, 35);

    lv_obj_t *roll_text = lv_label_create(arc_roll);
    lv_label_set_text(roll_text, "0°");
    lv_obj_center(roll_text);
    lv_obj_t *lblR = lv_label_create(root);
    lv_label_set_text(lblR, "Roll");
    lv_obj_align_to(lblR, arc_roll, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Footer readouts at the bottom inside the safe square
    lbl_axyz = lv_label_create(root);
    lv_label_set_text(lbl_axyz, "Accel: X 0.000  Y 0.000  Z 0.000 g");
    lv_obj_align(lbl_axyz, LV_ALIGN_TOP_LEFT, 73, 327);

    lbl_gxyz = lv_label_create(root);
    lv_label_set_text(lbl_gxyz, "Gyro:  X 0.0  Y 0.0  Z 0.0 dps");
    lv_obj_align(lbl_gxyz, LV_ALIGN_TOP_LEFT, 73, 347);

    lbl_angles = lv_label_create(root);
    lv_label_set_text(lbl_angles, "Pitch 0.0°  Roll 0.0°");
    lv_obj_align(lbl_angles, LV_ALIGN_TOP_LEFT, 73, 367);

    // UI refresh timer
    lv_timer_create(
        [](lv_timer_t *t)
        {
            LV_UNUSED(t);
            // (Optional) smooth values a bit for the arcs
            static float sm_ax = 0, sm_ay = 0, sm_az = 0;
            sm_ax = ema(sm_ax, g_imu.ax, 0.2f);
            sm_ay = ema(sm_ay, g_imu.ay, 0.2f);
            sm_az = ema(sm_az, g_imu.az, 0.2f);

            // Chart gets raw (or smoothed) values
            lv_chart_set_next_value(chart, ser_ax, (int32_t)(sm_ax * 1000.0f));
            lv_chart_set_next_value(chart, ser_ay, (int32_t)(sm_ay * 1000.0f));
            lv_chart_set_next_value(chart, ser_az, (int32_t)(sm_az * 1000.0f));

            // Tilt from accel (use your fused output if you have it)
            float pitch_deg = (180.0f / 3.1415926f) * atan2f(-sm_ax, sqrtf(sm_ay * sm_ay + sm_az * sm_az));
            // Same normalization as compute_pitch_roll_from_accel(): ensure roll=0 when flat
            float s = (sm_az >= 0.0f) ? 1.0f : -1.0f;
            float roll_deg = (180.0f / 3.1415926f) * atan2f(sm_ay * s, sm_az * s);
            if (pitch_deg < -90)
                pitch_deg = -90;
            if (pitch_deg > 90)
                pitch_deg = 90;
            // Allow full range for roll
            if (roll_deg < -180)
                roll_deg = -180;
            if (roll_deg > 180)
                roll_deg = 180;

            lv_arc_set_value(arc_pitch, (int32_t)pitch_deg);
            lv_arc_set_value(arc_roll, (int32_t)roll_deg);

            // Update numeric readouts
            lv_label_set_text_fmt(lbl_axyz, "Accel: X %.3f  Y %.3f  Z %.3f g", g_imu.ax, g_imu.ay, g_imu.az);
            lv_label_set_text_fmt(lbl_gxyz, "Gyro:  X %.1f  Y %.1f  Z %.1f dps", g_imu.gx, g_imu.gy, g_imu.gz);
            lv_label_set_text_fmt(lbl_angles, "Pitch %.1f°  Roll %.1f°  T %.1fC", pitch_deg, roll_deg, g_imu.temp);

            // update the numbers inside arcs
            lv_obj_t *pitch_text = lv_obj_get_child(arc_pitch, 0);
            lv_obj_t *roll_text = lv_obj_get_child(arc_roll, 0);
            if (pitch_text)
                lv_label_set_text_fmt(pitch_text, "%.0f°", pitch_deg);
            if (roll_text)
                lv_label_set_text_fmt(roll_text, "%.0f°", roll_deg);
        },
        UI_UPDATE_MS, NULL);
}

#endif
