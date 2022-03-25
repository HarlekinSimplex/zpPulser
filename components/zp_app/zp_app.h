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

esp_err_t app_set_history_factory_defaults(int force_flag) ;
esp_err_t initialize_app(int force_flag) ;
void register_app(void) ;
void start_app(void) ;

#ifdef __cplusplus
}
#endif
