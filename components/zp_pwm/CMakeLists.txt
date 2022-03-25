idf_component_register(
	SRCS "zp_pwm.c"
	INCLUDE_DIRS .
	REQUIRES console nvs_flash zp_nvs app_update
)
