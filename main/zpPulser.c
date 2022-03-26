/*****************************************************************
*
*
*
*
*
*/

#include <stdio.h>
#include "esp_log.h"
#include "zp_app.h"
#include "zp_nvs.h"
#include "zp_pwm.h"
#include "zp_ota.h"
#include "zp_sys.h"

#include "zpPulser.h"
static const char* TAG = "zpPulser";


#ifdef CONFIG_ESP_CONSOLE_USB_CDC
#error This software is incompatible with USB CDC console.
#endif // CONFIG_ESP_CONSOLE_USB_CDC

void app_main(void)
{
	int force_flag = false ;  // Set to true if using factory defaults should be forced

	// Initialize component zp_nvs
	initialize_nvs(force_flag) ;
    register_nvs() ;
	ESP_LOGI(TAG, "zp_nvs initialization completed");

    // Initialize component zp_ota
    register_ota();
	ESP_LOGI(TAG, "zp_ota initialization completed");

	// Initialize component zp_sys
    register_sys();
	ESP_LOGI(TAG, "zp_sys initialization completed");

	// Initialize component zp_pwm
	initialize_pwm(force_flag) ;
    register_pwm();
	ESP_LOGI(TAG, "zp_pwm initialization completed");

	// Initialize component zp_app
    initialize_app(force_flag) ;
    register_app();
	ESP_LOGI(TAG, "zp_app initialization completed");

	// Initialize component zp_app
    xTaskCreate((TaskFunction_t)&start_app, "start_app", 1024 * 8, NULL, 5, NULL);
	ESP_LOGI(TAG, "Application task started");
}
