#include "amoled.h"

static const panel_lcd_init_cmd_t sh8601_lcd_init_cmds[] =
    {
        {0x11, (uint8_t[]){0x00}, 0, 120},
        {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
        // {0x35, (uint8_t[]){0x00}, 1, 0},
        {0x53, (uint8_t[]){0x20}, 1, 10},
        {0x51, (uint8_t[]){0x00}, 1, 10},
        {0x29, (uint8_t[]){0x00}, 0, 10},
        {0x51, (uint8_t[]){0xFF}, 1, 0},
        //{0x36, (uint8_t []){0x80}, 1, 0},
        {0x34, (uint8_t[]){}, 0, 0}, // TE OFF

};
static const panel_lcd_init_cmd_t co5300_lcd_init_cmds[] =
    {
        {0x11, (uint8_t[]){0x00}, 0, 80},
        {0xC4, (uint8_t[]){0x80}, 1, 0},
        //{0x44, (uint8_t []){0x01, 0xD1}, 2, 0},
        // {0x35, (uint8_t []){0x00}, 1, 0},//TE ON
        {0x34, (uint8_t[]){}, 0, 0}, // TE OFF
        {0x53, (uint8_t[]){0x20}, 1, 1},
        {0x63, (uint8_t[]){0xFF}, 1, 1},
        {0x51, (uint8_t[]){0x00}, 1, 1},
        {0x29, (uint8_t[]){0x00}, 0, 10},
        {0x51, (uint8_t[]){0xFF}, 1, 0},
        // {0x36, (uint8_t []){0x60}, 1, 0},
};

// Convert byte to big endian
static inline uint16_t toBE565(uint16_t c)
{
  return (uint16_t)((c << 8) | (c >> 8)); // byte-swap
}

Amoled::Amoled() {};
Amoled::~Amoled()
{
  if (lineBuffer)
  {
    free(lineBuffer);
    lineBuffer = nullptr;
    lineBufferSize = 0;
  }
}
bool Amoled::begin()
{
  controller_id = read_lcd_id();
  const spi_bus_config_t buscfg = AMOLED_PANEL_BUS_QSPI_CONFIG(PIN_NUM_LCD_PCLK,
                                                               PIN_NUM_LCD_DATA0,
                                                               PIN_NUM_LCD_DATA1,
                                                               PIN_NUM_LCD_DATA2,
                                                               PIN_NUM_LCD_DATA3,
                                                               TRANSFER_SIZE);
  if (spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK)
    return false;
  esp_lcd_panel_io_handle_t io_handle = NULL;

  const esp_lcd_panel_io_spi_config_t io_config = AMOLED_PANEL_IO_QSPI_CONFIG(PIN_NUM_LCD_CS, NULL, NULL);
  sh8601_vendor_config_t vendor_config =
      {
          .flags =
              {
                  .use_qspi_interface = 1,
              },
      };
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle) != ESP_OK)
    return false;
  const esp_lcd_panel_dev_config_t panel_config =
      {
          .reset_gpio_num = PIN_NUM_LCD_RST,
          .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
          .bits_per_pixel = LCD_BIT_PER_PIXEL,
          .vendor_config = &vendor_config,
      };
  vendor_config.init_cmds = (controller_id == SH8601_ID) ? sh8601_lcd_init_cmds : co5300_lcd_init_cmds;
  vendor_config.init_cmds_size = (controller_id == SH8601_ID) ? sizeof(sh8601_lcd_init_cmds) / sizeof(sh8601_lcd_init_cmds[0]) : sizeof(co5300_lcd_init_cmds) / sizeof(co5300_lcd_init_cmds[0]);
  if (esp_amoled_new_panel(io_handle, &panel_config, &panel_handle))
    return false;
  if (esp_lcd_panel_reset(panel_handle))
    return false;
  if (esp_lcd_panel_init(panel_handle))
    return false;
  if (esp_lcd_panel_disp_on_off(panel_handle, true))
    return false;
  // reserve a single reusable scanline buffer equal to panel width
  if (!reserveLineBuffer())
    return false;
  return true;
}

// Reserve a two line buffer, co5300 needs a bitmap height that is greater than 1
bool Amoled::reserveLineBuffer()
{
  if (lineBuffer)
    return true;

  const int push_w_max = (DISPLAY_WIDTH & 1) ? (DISPLAY_WIDTH + 1) : DISPLAY_WIDTH; // even
  const int cap_elems = push_w_max * 2;                                             // 2 rows

  lineBuffer = (uint16_t *)heap_caps_malloc(
      cap_elems * sizeof(uint16_t),
      MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!lineBuffer)
    return false;

  lineBufferSize = cap_elems; // capacity in elements
  return true;
}
bool Amoled::drawBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h)
{
  if (!panel_handle || !bitmap || w <= 0 || h <= 0)
    return false;

  // clip destination rect to screen
  int dx = x, dy = y, cw = w, ch = h;
  if (dx < 0)
  {
    cw += dx;
    dx = 0;
  }
  if (dy < 0)
  {
    ch += dy;
    dy = 0;
  }
  if (dx + cw > DISPLAY_WIDTH)
    cw = DISPLAY_WIDTH - dx;
  if (dy + ch > DISPLAY_HEIGHT)
    ch = DISPLAY_HEIGHT - dy;
  if (cw <= 0 || ch <= 0)
    return true; // nothing visible; treat as success

  return pushToPanel(dx, dy, bitmap, w, h);
}

bool Amoled::drawArea(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t *bitmap)
{
  const int offsetx1 = (controller_id == SH8601_ID) ? x1 : x1 + 0x06;
  const int offsetx2 = (controller_id == SH8601_ID) ? x2 : x2 + 0x06;
  const int offsety1 = y1;
  const int offsety2 = y2;
  return esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2+1, y2+1, bitmap) == ESP_OK;
};

bool Amoled::pushToPanel(int x, int y, const uint16_t *buf, int w, int h)
{
  const int x1 = (controller_id == SH8601_ID) ? x : x + 0x06;
  const int x2 = (controller_id == SH8601_ID) ? x + w : x + w + 0x06;
  const int y1 = y;
  const int y2 = y + h; // end-exclusive
  return esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, buf) == ESP_OK;
}

bool Amoled::invertColor(bool invertColor)
{
  return esp_lcd_panel_invert_color(panel_handle, invertColor) == ESP_OK;
}

uint8_t Amoled::ID()
{
  return controller_id;
}

char *Amoled::name()
{
  switch (ID())
  {
  case SH8601_ID:
    return SH8601_NAME;
  case CO5300_ID:
    return CO5300_NAME;
  default:
    return "Unknown";
  }
}

bool Amoled::fillScreen(uint16_t color565)
{
  return fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color565);
}

bool Amoled::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color565)
{
  if (!panel_handle || w <= 0 || h <= 0)
    return false;

  // Clip
  int xs = x, ys = y, xe = x + w, ye = y + h;
  if (xs < 0)
    xs = 0;
  if (ys < 0)
    ys = 0;
  if (xe > DISPLAY_WIDTH)
    xe = DISPLAY_WIDTH;
  if (ye > DISPLAY_HEIGHT)
    ye = DISPLAY_HEIGHT;
  int cw = xe - xs, ch = ye - ys;
  if (cw <= 0 || ch <= 0)
    return true;

  // Even width for the panel, and always push 2 rows
  const bool pad_col = (cw & 1);
  const int push_w = cw + (pad_col ? 1 : 0);
  const int need = push_w * 2; // elements for 2 rows

  const uint16_t be = toBE565(color565);

  // Build 2 rows in the reusable buffer
  for (int col = 0; col < cw; ++col)
  {
    lineBuffer[col] = be;
    lineBuffer[push_w + col] = be;
  }
  if (pad_col)
  { // duplicate last column
    lineBuffer[push_w - 1] = be;
    lineBuffer[push_w + push_w - 1] = be;
  }

  // Push in 2-row slices (height=2 is required by this panel quirk)
  for (int row = 0; row < ch; row += 2)
  {
    int y_push = ys + row;
    if ((ch & 1) && row == ch - 1)
    { // last single line -> shift up to keep height=2 on-screen
      if (y_push > ys)
        y_push -= 1;
    }
    if (!pushToPanel(xs, y_push, lineBuffer, push_w, 2))
      return false;
  }
  return true;
}
