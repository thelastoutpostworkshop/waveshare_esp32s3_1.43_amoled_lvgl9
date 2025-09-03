/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
*/
#include <stdlib.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"

#include "low_level_amoled.h"

#define LCD_OPCODE_WRITE_CMD        (0x02ULL)
#define LCD_OPCODE_READ_CMD         (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR      (0x32ULL)

static const char *TAG = "amoled_sh8601_co5300";

static esp_err_t amoled_deallocate(esp_lcd_panel_t *panel);
static esp_err_t amoled_reset(esp_lcd_panel_t *panel);
static esp_err_t amoled_init(esp_lcd_panel_t *panel);
static esp_err_t amoled_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t amoled_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t amoled_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t amoled_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t amoled_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t amoled_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save surrent value of LCD_CMD_COLMOD register
    const panel_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface: 1;
        unsigned int reset_level: 1;
    } flags;
} amoled_panel_t;

esp_err_t esp_amoled_new_panel(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    amoled_panel_t *panel = NULL;
    panel = calloc(1, sizeof(amoled_panel_t));
    ESP_GOTO_ON_FALSE(panel, ESP_ERR_NO_MEM, err, TAG, "no mem for sh8601 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        panel->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        panel->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color element order");
        break;
    }

    uint8_t fb_bits_per_pixel = 0;
    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        panel->colmod_val = 0x55;
        fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        panel->colmod_val = 0x66;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        fb_bits_per_pixel = 18;
        break;
    case 24: // RGB888
        panel->colmod_val = 0x77;
        fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    panel->io = io;
    panel->reset_gpio_num = panel_dev_config->reset_gpio_num;
    panel->fb_bits_per_pixel = fb_bits_per_pixel;
    sh8601_vendor_config_t *vendor_config = (sh8601_vendor_config_t *)panel_dev_config->vendor_config;
    if (vendor_config) {
        panel->init_cmds = vendor_config->init_cmds;
        panel->init_cmds_size = vendor_config->init_cmds_size;
        panel->flags.use_qspi_interface = vendor_config->flags.use_qspi_interface;
    }
    panel->flags.reset_level = panel_dev_config->flags.reset_active_high;
    panel->base.del = amoled_deallocate;
    panel->base.reset = amoled_reset;
    panel->base.init = amoled_init;
    panel->base.draw_bitmap = amoled_draw_bitmap;
    panel->base.invert_color = amoled_invert_color;
    panel->base.set_gap = amoled_set_gap;
    panel->base.mirror = amoled_mirror;
    panel->base.swap_xy = amoled_swap_xy;
    panel->base.disp_on_off = amoled_disp_on_off;
    *ret_panel = &(panel->base);
    ESP_LOGD(TAG, "new sh8601 panel @%p", panel);

    //ESP_LOGI(TAG, "LCD panel create success, version: %d.%d.%d", ESP_LCD_SH8601_VER_MAJOR, ESP_LCD_SH8601_VER_MINOR,
             //ESP_LCD_SH8601_VER_PATCH);

    return ESP_OK;

err:
    if (panel) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(panel);
    }
    return ret;
}

static esp_err_t tx_param(amoled_panel_t *panel, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (panel->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(amoled_panel_t *panel, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (panel->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

static esp_err_t amoled_deallocate(esp_lcd_panel_t *lcd_panel)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);

    if (panel->reset_gpio_num >= 0) {
        gpio_reset_pin(panel->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del sh8601 panel @%p", panel);
    free(panel);
    return ESP_OK;
}

static esp_err_t amoled_reset(esp_lcd_panel_t *lcd_panel)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    esp_lcd_panel_io_handle_t io = panel->io;

    // Perform hardware reset
    if (panel->reset_gpio_num >= 0) {
        gpio_set_level(panel->reset_gpio_num, panel->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(panel->reset_gpio_num, !panel->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(150));
    } else { // Perform software reset
        ESP_RETURN_ON_ERROR(tx_param(panel, io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    return ESP_OK;
}

static const panel_lcd_init_cmd_t vendor_specific_init_default[] = {
//  {cmd, { data }, data_size, delay_ms}
    {0x44, (uint8_t []){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t []){0x00}, 0, 0},
    {0x53, (uint8_t []){0x20}, 1, 25},
};

static esp_err_t amoled_init(esp_lcd_panel_t *lcd_panel)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    esp_lcd_panel_io_handle_t io = panel->io;
    const panel_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    bool is_cmd_overwritten = false;

    ESP_RETURN_ON_ERROR(tx_param(panel, io, LCD_CMD_MADCTL, (uint8_t[]) {
        panel->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(panel, io, LCD_CMD_COLMOD, (uint8_t[]) {
        panel->colmod_val,
    }, 1), TAG, "send command failed");

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    if (panel->init_cmds) {
        init_cmds = panel->init_cmds;
        init_cmds_size = panel->init_cmds_size;
    } else {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(panel_lcd_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        switch (init_cmds[i].cmd) {
        case LCD_CMD_MADCTL:
            is_cmd_overwritten = true;
            panel->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        case LCD_CMD_COLMOD:
            is_cmd_overwritten = true;
            panel->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        default:
            is_cmd_overwritten = false;
            break;
        }

        if (is_cmd_overwritten) {
            ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
        }

        ESP_RETURN_ON_ERROR(tx_param(panel, io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG,
                            "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t amoled_draw_bitmap(esp_lcd_panel_t *lcd_panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = panel->io;

    x_start += panel->x_gap ;
    x_end += panel->x_gap ;
    y_start += panel->y_gap;
    y_end += panel->y_gap;

    // define an area of frame memory where MCU can access
    ESP_RETURN_ON_ERROR(tx_param(panel, io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(panel, io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * panel->fb_bits_per_pixel / 8;
    tx_color(panel, io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t amoled_invert_color(esp_lcd_panel_t *lcd_panel, bool invert_color_data)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    esp_lcd_panel_io_handle_t io = panel->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(tx_param(panel, io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t amoled_mirror(esp_lcd_panel_t *lcd_panel, bool mirror_x, bool mirror_y)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    esp_lcd_panel_io_handle_t io = panel->io;
    esp_err_t ret = ESP_OK;

    if (mirror_x) {
        panel->madctl_val |= BIT(6);
    } else {
        panel->madctl_val &= ~BIT(6);
    }
    if (mirror_y) {
        ESP_LOGE(TAG, "mirror_y is not supported by this panel");
        ret = ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(tx_param(panel, io, LCD_CMD_MADCTL, (uint8_t[]) {
        panel->madctl_val
    }, 1), TAG, "send command failed");
    return ret;
}

static esp_err_t amoled_swap_xy(esp_lcd_panel_t *lcd_panel, bool swap_axes)
{
    ESP_LOGE(TAG, "swap_xy is not supported by this panel");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t amoled_set_gap(esp_lcd_panel_t *lcd_panel, int x_gap, int y_gap)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    panel->x_gap = x_gap;
    panel->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t amoled_disp_on_off(esp_lcd_panel_t *lcd_panel, bool on_off)
{
    amoled_panel_t *panel = __containerof(lcd_panel, amoled_panel_t, base);
    esp_lcd_panel_io_handle_t io = panel->io;
    int command = 0;

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(tx_param(panel, io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}
