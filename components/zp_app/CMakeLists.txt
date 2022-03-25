idf_component_register(
	SRCS "zp_app.c"
	INCLUDE_DIRS .
	REQUIRES console nvs_flash fatfs vfs zp_nvs
)

