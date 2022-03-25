/*****************************************************************
*
*
*
*
*
*/

#pragma once

#include "esp_err.h"
#include "zp_pwm.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register NVS functions
void register_nvs(void);

uint8_t option_get_u8(const char* option_key) ;
uint16_t option_get_u16(const char* option_key) ;
uint32_t option_get_u32(const char* option_key) ;
float option_get_float(const char* option_key) ;

esp_err_t initialize_nvs(int force_flag);

#ifdef __cplusplus
}
#endif
