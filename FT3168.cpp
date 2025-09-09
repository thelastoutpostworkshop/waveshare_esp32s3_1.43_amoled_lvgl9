#include "board_config.h"
#include "FT3168.h"
#include "esp_err.h"

void Touch_Init(void)
{
  i2c_config_t conf = 
  {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = PIN_NUM_TOUCH_SDA,         // Configure the GPIO of the SDA
    .scl_io_num = PIN_NUM_TOUCH_SCL,         // Configure GPIO for SCL
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master = {.clk_speed = I2C_FREQUENCY,},  // Select a frequency for the project
    .clk_flags = 0,          // Optionally, use the I2C SCLK SRC FLAG * flag to select the I2C source clock
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode,0,0,0));

  uint8_t data = 0x00;
  I2C_writr_buff(I2C_ADDR_FT3168,0x00,&data,1); //Switch to normal mode

}
uint8_t getTouch(uint16_t *x,uint16_t *y)
{
  uint8_t data;
  uint8_t buf[4];
  I2C_read_buff(I2C_ADDR_FT3168,0x02,&data,1);
  if(data)
  {
    I2C_read_buff(I2C_ADDR_FT3168,0x03,buf,4);
    *x = (((uint16_t)buf[0] & 0x0f)<<8) | (uint16_t)buf[1];
    *y = (((uint16_t)buf[2] & 0x0f)<<8) | (uint16_t)buf[3];
    if(*x > DISPLAY_WIDTH)
    *x = DISPLAY_WIDTH;
    if(*y > DISPLAY_HEIGHT)
    *y = DISPLAY_HEIGHT;
    return 1;
  }
  return 0;
}


