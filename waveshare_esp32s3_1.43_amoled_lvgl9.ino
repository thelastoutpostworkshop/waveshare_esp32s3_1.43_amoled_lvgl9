// Waveshare ESP32-S3-Touch-AMOLED-1.43 Surface Level (LVGL9)
// Tutorial :
// Use board "Waveshare ESP32-S3-Touch-AMOLED-1.43" (last tested on v3.3.0)

// The next line instruct LVGL to use lv_conf.h included in this project
// Keep it this way, everything is configured correctly
#define LV_CONF_INCLUDE_SIMPLE

// Comment the next line if you want to use your own design (ex. from Squareline studio) and disable the Surface Level Example 
#define USE_BUILT_IN_SURFACE_LEVEL_EXAMPLE

#include <lvgl.h> // Install "lvgl" with the Library Manager (last tested on v9.2.2)
#include "amoled.h"
#include "FT3168.h"   // Capacitive Touch functions
#include "qmi8658c.h" // QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope) functions
#include "ui.h"

Amoled amoled; // Main object for the display board

// LVGL Display buffer size
#define LVGL_DRAW_BUF_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t))

// LVGL global variables for the display and its buffers
lv_display_t *disp;
lv_color_t *lvgl_buf1 = nullptr;
lv_color_t *lvgl_buf2 = nullptr;

#ifdef USE_BUILT_IN_SURFACE_LEVEL_EXAMPLE
// Globals for the surface level example
#define TARGET_THRESHOLD_PX 20 // Target threshold in pixels when the bubble is almost level (turn the target red)
#define READ_SAMPLE_INTERVAL_MS 50     // Interval in ms to read a sample from the QMI8658
#define MOVE_BUBBLE_INTERVAL_MS 100     // Interval in ms to adjust bubble movement in the UI

// Global to store the latest sample read the accelerometer and gyroscope (QMI8658)
typedef struct
{
    float ax, ay, az;
    float gx, gy, gz;
    float temp;
} ImuData;
volatile ImuData g_imu;

extern lv_obj_t *uic_bubble;     // The bubble to move on the screen to show the level
extern lv_obj_t *uic_target_on;  // Target image to show when bubble is almost "level"
extern lv_obj_t *uic_target_off; // Target image to show otherwise
extern lv_obj_t *uic_Label_x;    // Label for the y position of the bubble
extern lv_obj_t *uic_Label_y;    // Label for the x position of the bubble
#endif

void setup()
{
    Serial.begin(115200);

    // Optional: Give time to the serial port to show initial messages printed on the serial monitor upon reset
    delay(4000);

    // Initialize the touch screen
    Serial.println("Touche screen initialization");
    Touch_Init();

    // Initialize QMI8658 6-axis IMU
    Serial.println("QMI8658 6-axis IMU initialization");
    qmi8658_init();
    delay(1000); // Do not change this delay, it is required by the QMI8658

    // Display initialization
    Serial.println("Amoled display initialization");
    if (!amoled.begin())
    {
        Serial.println("Display initialization failed!");
        while (true)
        {
            /* no need to continue */
        }
    }
    Serial.printf("Display controller name is %s (id=%d)\n", amoled.name(), amoled.ID());

    // LVGL initialization
    Serial.println("LVGL initialization");
    lv_init();
    lv_tick_set_cb(millis_cb);

    // LVGL Buffers allocation for the display
    Serial.println("LVGL buffers allocation");
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

#ifdef USE_BUILT_IN_SURFACE_LEVEL_EXAMPLE
    // Launch the UI example
    ui_init();
    // Create the task to read QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope)
    xTaskCreatePinnedToCore(imu_task, "imu", 4096, NULL, 2, NULL, 0);
    // Periodic timer to update/move the bubble image using latest IMU data
    lv_timer_create(move_bubble, MOVE_BUBBLE_INTERVAL_MS, NULL); // ~20 Hz
#else
    // If you want to use a UI created with Squarline Studio, call it here
    ui_init();
#endif
}

void loop()
{
    lv_timer_handler(); /* let LVGL do its GUI work */
    delay(5);
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

#ifdef USE_BUILT_IN_SURFACE_LEVEL_EXAMPLE
// Below are all the functions need by the Surface Level example

// Task to read the values of QMI8658 6-axis IMU (3-axis accelerometer and 3-axis gyroscope)
static void imu_task(void *arg)
{
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

        vTaskDelay(pdMS_TO_TICKS(READ_SAMPLE_INTERVAL_MS));
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

    // If your board axes are rotated relative to the screen,
    // swap or invert here to match your expected motion.
    const bool swap_axes = true;  // true: use roll for vertical, pitch for horizontal
    const bool inv_pitch = false; // flip vertical if needed
    const bool inv_roll = false;  // flip horizontal if needed

    float used_pitch = swap_axes ? roll_deg : pitch_deg;
    float used_roll = swap_axes ? pitch_deg : roll_deg;
    if (inv_pitch)
        used_pitch = -used_pitch;
    if (inv_roll)
        used_roll = -used_roll;

    float dx = used_roll * px_per_deg;   // right = +
    float dy = -used_pitch * px_per_deg; // up = +pitch
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
    lv_label_set_text_fmt(uic_Label_x, "%.1f°", used_roll);
    lv_label_set_text_fmt(uic_Label_y, "%.1f°", used_pitch);
    // Serial.printf("Bubble x=%d,y=%d\n",x,y);
    lv_obj_set_pos(uic_bubble, x, y);

    // Toggle target images depending on proximity to center
    const int target_threshold_px = TARGET_THRESHOLD_PX; // when to considered "level"
    if (uic_target_on && uic_target_off)
    {
        if (rr <= target_threshold_px)
        {
            lv_obj_clear_flag(uic_target_on, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(uic_target_off, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(uic_target_off, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(uic_target_on, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

#endif
