idf_component_register(
	SRCS "zpPulser.c"
	INCLUDE_DIRS "."
	REQUIRES console nvs_flash zp_app zp_nvs zp_ota zp_pwm zp_sys
)
                
                    