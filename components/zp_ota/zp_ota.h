/*****************************************************************
*
*
*
*
*
*/

#pragma once

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

void register_ota(void) ;

esp_err_t ota_connect(void);

/**
 * Counterpart to ota_connect, de-initializes Wi-Fi or Ethernet
 */
esp_err_t ota_disconnect(void);

/**
 * @brief Returns esp-netif pointer created by ota_connect()
 *
 * @note If multiple interfaces active at once, this API return NULL
 * In that case the get_ota_netif_from_desc() should be used
 * to get esp-netif pointer based on interface description
 */
esp_netif_t *get_ota_netif(void);

/**
 * @brief Returns esp-netif pointer created by ota_connect() described by
 * the supplied desc field
 *
 * @param desc Textual interface of created network interface, for example "sta"
 * indicate default WiFi station, "eth" default Ethernet interface.
 *
 */
esp_netif_t *get_ota_netif_from_desc(const char *desc);

#ifdef __cplusplus
}
#endif
