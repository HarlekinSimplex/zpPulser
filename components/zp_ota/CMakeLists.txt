idf_component_register(
	SRCS "zp_ota.c"
	INCLUDE_DIRS "."
	REQUIRES app_update esp_https_ota console nvs_flash zp_nvs zp_app
	EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
)
                   