idf_component_register(
	SRCS "zp_nvs.c"
	INCLUDE_DIRS "."
	REQUIRES console nvs_flash zp_pwm zp_app
)
