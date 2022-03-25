/*****************************************************************
*
*
*
*
*
*/

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register Pulser functions
esp_err_t initialize_pwm(int force_flag) ;
esp_err_t register_pwm(void) ;
esp_err_t pwm_set_gpio_factory_defaults(int force_flag) ;
esp_err_t pwm_set_timer_factory_defaults(int force_flag) ;

#ifdef __cplusplus
}
#endif
