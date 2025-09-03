#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Types of display controller
#define SH8601_ID 0x86
#define CO5300_ID 0xff

#define CO5300_NAME "CO5300"
#define SH8601_NAME "SH8601"

// Display config
#define DISPLAY_WIDTH   466
#define DISPLAY_HEIGHT  466
#define LCD_BIT_PER_PIXEL 16

// Display controller pins
#define PIN_NUM_LCD_CS     9
#define PIN_NUM_LCD_PCLK   10 
#define PIN_NUM_LCD_DATA0  11
#define PIN_NUM_LCD_DATA1  12
#define PIN_NUM_LCD_DATA2  13
#define PIN_NUM_LCD_DATA3  14
#define PIN_NUM_LCD_RST    21

// Touch pins & configuration
#define I2C_ADDR_FT3168 0x38
#define PIN_NUM_TOUCH_SCL 48
#define PIN_NUM_TOUCH_SDA 47
#define I2C_PORT I2C_NUM_0
#define I2C_FREQUENCY (300 * 1000)

// SD Card pins
#define SD_CS 38
#define SD_MOSI 39
#define SD_MISO 40
#define SD_CLK 41

// SPI Config
#define LCD_HOST SPI2_HOST          // SPI peripheral
#define TRANSFER_SIZE 4092          // Controls the largest DMA-able chunk the SPI driver will handle
#define BUS_SPEED 80 * 1000 * 1000  // SPI Bus speed
#define TRANSFER_QUEUE_DEPTH 32     // It’s the SPI device’s queue size; you can raise it until you run out of RAM

#endif