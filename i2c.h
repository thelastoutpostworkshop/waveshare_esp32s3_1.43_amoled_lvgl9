// I2C functions declarations used by the touch controller (FT3168) and 6-axis controller (QMI8658C)
//
#ifndef I2C_H
#define I2C_H
#include "board_config.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif 

uint8_t I2C_writr_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);
uint8_t I2C_read_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);
uint8_t I2C_master_write_read_device(uint8_t addr,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen);

#ifdef __cplusplus
}
#endif
#endif