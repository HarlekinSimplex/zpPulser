/*****************************************************************
*
*
*
*
*
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_console.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif.h"
#include "argtable3/argtable3.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
//#include "esp_system.h"
//#include "esp_event.h"
//#include "driver/gpio.h"
//#include "nvs.h"

#include "sdkconfig.h"

#include "zp_nvs.h"
#include "zp_ota.h"
static const char *TAG = "zp_ota" ;

#if CONFIG_STORE_HISTORY
	#include "linenoise/linenoise.h"
	#define HISTORY_PATH CONFIG_HISTORY_MOUNT_PATH CONFIG_HISTORY_FILENAME
#endif

#define WIFI_SSID_MAX_LENGTH 32
#define WIFI_PASSWORD_MAX_LENGTH 64
#define MAX_DOWNLOAD_DOTS 64

#ifdef CONFIG_OTA_CONNECT_IPV6
#define MAX_IP6_ADDRS_PER_NETIF (5)
#define NR_OF_IP_ADDRESSES_TO_WAIT_FOR (s_active_interfaces*2)

#if defined(CONFIG_OTA_CONNECT_IPV6_PREF_LOCAL_LINK)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_LINK_LOCAL
#elif defined(CONFIG_OTA_CONNECT_IPV6_PREF_GLOBAL)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_GLOBAL
#elif defined(CONFIG_OTA_CONNECT_IPV6_PREF_SITE_LOCAL)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_SITE_LOCAL
#elif defined(CONFIG_OTA_CONNECT_IPV6_PREF_UNIQUE_LOCAL)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_UNIQUE_LOCAL
#endif // if-elif CONFIG_OTA_CONNECT_IPV6_PREF_...

#else
#define NR_OF_IP_ADDRESSES_TO_WAIT_FOR (s_active_interfaces)
#endif

#define OTA_DO_CONNECT CONFIG_OTA_CONNECT_WIFI || CONFIG_OTA_CONNECT_ETHERNET

static int s_active_interfaces = 0;
static xSemaphoreHandle s_semph_get_ip_addrs;
static esp_netif_t *s_ota_esp_netif = NULL;

#ifdef CONFIG_OTA_CONNECT_IPV6
static esp_ip6_addr_t s_ipv6_addr;

/* types of ipv6 addresses to be displayed on ipv6 events */
static const char *s_ipv6_addr_types[] = {
    "ESP_IP6_ADDR_IS_UNKNOWN",
    "ESP_IP6_ADDR_IS_GLOBAL",
    "ESP_IP6_ADDR_IS_LINK_LOCAL",
    "ESP_IP6_ADDR_IS_SITE_LOCAL",
    "ESP_IP6_ADDR_IS_UNIQUE_LOCAL",
    "ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6"
};
#endif

#ifdef CONFIG_OTA_CONNECT_IPV6
#define MAX_IP6_ADDRS_PER_NETIF (5)
#define NR_OF_IP_ADDRESSES_TO_WAIT_FOR (s_active_interfaces*2)

#if defined(CONFIG_OTA_CONNECT_IPV6_PREF_LOCAL_LINK)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_LINK_LOCAL
#elif defined(CONFIG_OTA_CONNECT_IPV6_PREF_GLOBAL)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_GLOBAL
#elif defined(CONFIG_OTA_CONNECT_IPV6_PREF_SITE_LOCAL)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_SITE_LOCAL
#elif defined(CONFIG_OTA_CONNECT_IPV6_PREF_UNIQUE_LOCAL)
#define OTA_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_UNIQUE_LOCAL
#endif // if-elif CONFIG_OTA_CONNECT_IPV6_PREF_...

#else
#define NR_OF_IP_ADDRESSES_TO_WAIT_FOR (s_active_interfaces)
#endif

#define OTA_DO_CONNECT CONFIG_OTA_CONNECT_WIFI || CONFIG_OTA_CONNECT_ETHERNET

static xSemaphoreHandle s_semph_get_ip_addrs;

#ifdef CONFIG_OTA_CONNECT_IPV6
static esp_ip6_addr_t s_ipv6_addr;
#endif

#if CONFIG_OTA_CONNECT_WIFI
static esp_netif_t *wifi_start(void);
static void wifi_stop(void);
#endif
#if CONFIG_OTA_CONNECT_ETHERNET
static esp_netif_t *eth_start(void);
static void eth_stop(void);
#endif

#if CONFIG_OTA_CONNECT_WIFI
static esp_netif_t *wifi_start(void);
static void wifi_stop(void);
#endif
#if CONFIG_OTA_CONNECT_ETHERNET
static esp_netif_t *eth_start(void);
static void eth_stop(void);
#endif

#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#include "esp_efuse.h"
#endif

#if CONFIG_OTA_CONNECT_WIFI
#include "esp_wifi.h"
#endif

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
// extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

const char *update_ssid = CONFIG_OTA_WIFI_SSID ;
const char *update_passwd = CONFIG_OTA_WIFI_PASSWORD ;
const char *update_url = CONFIG_OTA_FIRMWARE_UPGRADE_URL ;
const char *update_version = NULL ;

#define MAX_DOWNLOAD_URL_LEN 128
static char download_url[MAX_DOWNLOAD_URL_LEN] ;

#define OTA_URL_SIZE 256

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

#ifndef CONFIG_OTA_SKIP_VERSION_CHECK
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
    	ESP_LOGE(TAG, "Current running version [%s] is the same as a new. We will not continue the update.", running_app_info.version);
        return ESP_FAIL;
    }
#endif

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /**
     * Secure version check from firmware image header prevents subsequent download and flash write of
     * entire firmware image. However this is optional because it is also taken care in API
     * esp_https_ota_finish at the end of OTA update procedure.
     */
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();
    if (new_app_info->secure_version < hw_sec_version) {
        ESP_LOGW(TAG, "New firmware security version is less than eFuse programmed, %d < %d", new_app_info->secure_version, hw_sec_version);
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}


void start_ota_update(void *pvParameter)
{
	// Log function name
	ESP_LOGI(TAG, "start_ota_update()");

	// Add version extension if set otherwise just copy url
	if(update_version != NULL){
		snprintf(download_url, MAX_DOWNLOAD_URL_LEN, "%s.%s", update_url, update_version) ;
	} else {
		snprintf(download_url, MAX_DOWNLOAD_URL_LEN, "%s", update_url) ;
	}

    ESP_LOGI(TAG, "OTA update task started");
    ESP_LOGI(TAG, "Downloading Image: %s", download_url);

    printf("Downloading Image: %s\n", download_url);

    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config = {
        .url = download_url,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = CONFIG_OTA_OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
    };

#ifdef CONFIG_OTA_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        OTA_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_OTA_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = _http_client_init_cb, // Register a callback to be invoked after esp_http_client is initialized
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        vTaskDelete(NULL);
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        goto ota_end;
    }

    unsigned short dl_dot = 0 ;
    while (1) {
    	int bytes_read;

    	err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        bytes_read = esp_https_ota_get_image_len_read(https_ota_handle) ;
        ESP_LOGD(TAG, "Image bytes read: %d", bytes_read);
        // printf("Image bytes downloaded: %d\n", bytes_read);

        // Log a dot to show download progress
    	printf(".") ;
    	dl_dot++ ;
    	if(dl_dot > MAX_DOWNLOAD_DOTS ){
    		dl_dot = 0 ;
    		printf("\n") ;
    	}
    }
    // Log completion
    printf("\nDownload completed\n") ;

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
    } else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {

			#if CONFIG_STORE_HISTORY
				// Clear NVS.
				printf("Erase Command History\n");
				// Remove history file from filesystem
				remove(HISTORY_PATH) ;
			#endif

            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            printf("ESP_HTTPS_OTA upgrade successful.\n");
            printf("Rebooting ...\n\n");

            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
//            vTaskDelete(NULL);
        }
    }

ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
//    vTaskDelete(NULL);
}


/******************************************************************************************
 * ota_updatee
 * -------------------------------------
 * Update the zpPulser firmware
 *
*/

static struct {
    struct arg_str *ssid;
    struct arg_str *url;
    struct arg_str *version;
    struct arg_str *passwd;
    struct arg_end *end;
} ota_update_args;


/**
 * @brief Checks the netif description if it contains specified prefix.
 * All netifs created withing common connect component are prefixed with the module TAG,
 * so it returns true if the specified netif is owned by this module
 */
static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

/* set up connection, Wi-Fi and/or Ethernet */
static void start(void)
{

#if CONFIG_OTA_CONNECT_WIFI
    s_ota_esp_netif = wifi_start();
    s_active_interfaces++;
#endif

#if CONFIG_OTA_CONNECT_ETHERNET
    s_ota_esp_netif = eth_start();
    s_active_interfaces++;
#endif

#if CONFIG_OTA_CONNECT_WIFI && CONFIG_OTA_CONNECT_ETHERNET
    /* if both intefaces at once, clear out to indicate that multiple netifs are active */
    s_ota_esp_netif = NULL;
#endif

#if OTA_DO_CONNECT
    /* create semaphore if at least one interface is active */
    s_semph_get_ip_addrs = xSemaphoreCreateCounting(NR_OF_IP_ADDRESSES_TO_WAIT_FOR, 0);
#endif

}

/* tear down connection, release resources */
static void stop(void)
{
#if CONFIG_OTA_CONNECT_WIFI
    wifi_stop();
    s_active_interfaces--;
#endif

#if CONFIG_OTA_CONNECT_ETHERNET
    eth_stop();
    s_active_interfaces--;
#endif
}

#if OTA_DO_CONNECT
static esp_ip4_addr_t s_ip_addr;

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!is_our_netif(TAG, event->esp_netif)) {
        ESP_LOGW(TAG, "Got IPv4 from another interface \"%s\": ignored", esp_netif_get_desc(event->esp_netif));
        return;
    }
    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
    xSemaphoreGive(s_semph_get_ip_addrs);
}
#endif

#ifdef CONFIG_OTA_CONNECT_IPV6

static void on_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    if (!is_our_netif(TAG, event->esp_netif)) {
        ESP_LOGW(TAG, "Got IPv6 from another netif: ignored");
        return;
    }
    esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
    ESP_LOGI(TAG, "Got IPv6 event: Interface \"%s\" address: " IPV6STR ", type: %s", esp_netif_get_desc(event->esp_netif),
             IPV62STR(event->ip6_info.ip), s_ipv6_addr_types[ipv6_type]);
    if (ipv6_type == OTA_CONNECT_PREFERRED_IPV6_TYPE) {
        memcpy(&s_ipv6_addr, &event->ip6_info.ip, sizeof(s_ipv6_addr));
        xSemaphoreGive(s_semph_get_ip_addrs);
    }
}

#endif // CONFIG_OTA_CONNECT_IPV6

esp_err_t ota_connect(void)
{
#if OTA_DO_CONNECT
    if (s_semph_get_ip_addrs != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    start();
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&stop));
    ESP_LOGI(TAG, "Waiting for IP(s)");
    for (int i = 0; i < NR_OF_IP_ADDRESSES_TO_WAIT_FOR; ++i) {
        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
    }
    // iterate over active interfaces, and print out IPs of "our" netifs
    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip;
    for (int i = 0; i < esp_netif_get_nr_of_ifs(); ++i) {
        netif = esp_netif_next(netif);
        if (is_our_netif(TAG, netif)) {
            ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
            ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));

            ESP_LOGI(TAG, "- IPv4 address: " IPSTR, IP2STR(&ip.ip));
#ifdef CONFIG_OTA_CONNECT_IPV6
            esp_ip6_addr_t ip6[MAX_IP6_ADDRS_PER_NETIF];
            int ip6_addrs = esp_netif_get_all_ip6(netif, ip6);
            for (int j = 0; j < ip6_addrs; ++j) {
                esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&(ip6[j]));
                ESP_LOGI(TAG, "- IPv6 address: " IPV6STR ", type: %s", IPV62STR(ip6[j]), s_ipv6_addr_types[ipv6_type]);
            }
#endif

        }
    }
    return ESP_OK;
}

esp_err_t ota_disconnect(void)
{
    if (s_semph_get_ip_addrs == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    vSemaphoreDelete(s_semph_get_ip_addrs);
    s_semph_get_ip_addrs = NULL;
    stop();
    ESP_ERROR_CHECK(esp_unregister_shutdown_handler(&stop));
    return ESP_OK;
}

#ifdef CONFIG_OTA_CONNECT_WIFI

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

#ifdef CONFIG_OTA_CONNECT_IPV6

static void on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    esp_netif_create_ip6_linklocal(esp_netif);
}

#endif // CONFIG_OTA_CONNECT_IPV6

static esp_netif_t *wifi_start(void)
{
    char *desc;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Prefix the interface description with the module TAG
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio = 128;
    esp_netif_t *netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    free(desc);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
#ifdef CONFIG_OTA_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_OTA_WIFI_SSID,
            .password = CONFIG_OTA_WIFI_PASSWORD,
        },
    };

    // Set command parameters given to SSID and Password
    strcpy((char *)&wifi_config.sta.ssid, update_ssid) ;
    strcpy((char *)&wifi_config.sta.password, update_passwd) ;

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    return netif;
}

static void wifi_stop(void)
{
    esp_netif_t *wifi_netif = get_ota_netif_from_desc("sta");
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
#ifdef CONFIG_OTA_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect));
#endif
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(wifi_netif));
    esp_netif_destroy(wifi_netif);
    s_ota_esp_netif = NULL;
}
#endif // CONFIG_OTA_CONNECT_WIFI

#ifdef CONFIG_OTA_CONNECT_ETHERNET

#ifdef CONFIG_OTA_CONNECT_IPV6

/** Event handler for Ethernet events */
static void on_eth_event(void *esp_netif, esp_event_base_t event_base,
                         int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Up");
        esp_netif_create_ip6_linklocal(esp_netif);
        break;
    default:
        break;
    }
}

#endif // CONFIG_OTA_CONNECT_IPV6

static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static void *s_eth_glue = NULL;

static esp_netif_t *eth_start(void)
{
    char *desc;
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    // Prefix the interface description with the module TAG
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio = 64;
    esp_netif_config_t netif_config = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    esp_netif_t *netif = esp_netif_new(&netif_config);
    assert(netif);
    free(desc);
    // Set default handlers to process TCP/IP stuffs
    ESP_ERROR_CHECK(esp_eth_set_default_handlers(netif));
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, NULL));
#ifdef CONFIG_OTA_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_event, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_OTA_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_OTA_ETH_PHY_RST_GPIO;
#if CONFIG_OTA_USE_INTERNAL_ETHERNET
    mac_config.smi_mdc_gpio_num = CONFIG_OTA_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_OTA_ETH_MDIO_GPIO;
    s_mac = esp_eth_mac_new_esp32(&mac_config);
#if CONFIG_OTA_ETH_PHY_IP101
    s_phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_OTA_ETH_PHY_RTL8201
    s_phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_OTA_ETH_PHY_LAN8720
    s_phy = esp_eth_phy_new_lan8720(&phy_config);
#elif CONFIG_OTA_ETH_PHY_DP83848
    s_phy = esp_eth_phy_new_dp83848(&phy_config);
#endif
#elif CONFIG_ETH_USE_SPI_ETHERNET
    gpio_install_isr_service(0);
    spi_device_handle_t spi_handle = NULL;
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_OTA_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_OTA_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_OTA_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_OTA_ETH_SPI_HOST, &buscfg, 1));
#if CONFIG_OTA_USE_DM9051
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_OTA_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_OTA_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_OTA_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* dm9051 ethernet driver is based on spi driver */
    eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(spi_handle);
    dm9051_config.int_gpio_num = CONFIG_OTA_ETH_SPI_INT_GPIO;
    s_mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
    s_phy = esp_eth_phy_new_dm9051(&phy_config);
#elif CONFIG_OTA_USE_W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
        .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
        .mode = 0,
        .clock_speed_hz = CONFIG_OTA_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_OTA_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_OTA_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* w5500 ethernet driver is based on spi driver */
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    w5500_config.int_gpio_num = CONFIG_OTA_ETH_SPI_INT_GPIO;
    s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    s_phy = esp_eth_phy_new_w5500(&phy_config);
#endif
#elif CONFIG_OTA_USE_OPENETH
    phy_config.autonego_timeout_ms = 100;
    s_mac = esp_eth_mac_new_openeth(&mac_config);
    s_phy = esp_eth_phy_new_dp83848(&phy_config);
#endif

    // Install Ethernet driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));
#if !CONFIG_OTA_USE_INTERNAL_ETHERNET
    /* The SPI Ethernet module might doesn't have a burned factory MAC address, we cat to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56
    }));
#endif
    // combine driver with netif
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    esp_netif_attach(netif, s_eth_glue);
    esp_eth_start(s_eth_handle);
    return netif;
}

static void eth_stop(void)
{
    esp_netif_t *eth_netif = get_ota_netif_from_desc("eth");
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip));
#ifdef CONFIG_OTA_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
    ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_event));
#endif
    ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
    ESP_ERROR_CHECK(esp_eth_clear_default_handlers(eth_netif));
    ESP_ERROR_CHECK(esp_eth_driver_uninstall(s_eth_handle));
    ESP_ERROR_CHECK(s_phy->del(s_phy));
    ESP_ERROR_CHECK(s_mac->del(s_mac));

    esp_netif_destroy(eth_netif);
    s_ota_esp_netif = NULL;
}

#endif // CONFIG_OTA_CONNECT_ETHERNET

esp_netif_t *get_ota_netif(void)
{
    return s_ota_esp_netif;
}

esp_netif_t *get_ota_netif_from_desc(const char *desc)
{
    esp_netif_t *netif = NULL;
    char *expected_desc;
    asprintf(&expected_desc, "%s: %s", TAG, desc);
    while ((netif = esp_netif_next(netif)) != NULL) {
        if (strcmp(esp_netif_get_desc(netif), expected_desc) == 0) {
            free(expected_desc);
            return netif;
        }
    }
    free(expected_desc);
    return netif;
}

static int do_ota_update(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ota_update_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ota_update_args.end, argv[0]);
        return 0;
    }

    /* Check "-s --ssid" option */
    if (ota_update_args.ssid->count) {
        update_ssid = ota_update_args.ssid->sval[0];
    }
    /* Check "-p --passwd" option */
    if (ota_update_args.passwd->count) {
    	update_passwd = ota_update_args.passwd->sval[0];
    }
    /* Check "-u --url" option */
    if (ota_update_args.url->count) {
        update_url = ota_update_args.url->sval[0];
    }
    /* Check "-v --version" option */
    if (ota_update_args.version->count) {
        update_version = ota_update_args.version->sval[0];
    }

    // Check lenght of SSID and Password
    if(strlen(update_ssid) > WIFI_SSID_MAX_LENGTH-1){
        ESP_LOGE(TAG, "OTA Update WiFi SSID to long. Maximum %d allowed.", WIFI_SSID_MAX_LENGTH-1);
        return ESP_ERR_INVALID_ARG ;
    }
    if(strlen(update_passwd) > WIFI_PASSWORD_MAX_LENGTH-1){
        ESP_LOGE(TAG, "OTA Update WiFi Password to long. Maximum %d allowed.", WIFI_PASSWORD_MAX_LENGTH-1);
        return ESP_ERR_INVALID_ARG ;
    }

    // Log parsed parameters
    ESP_LOGI(TAG, "OTA update parameters set");
    ESP_LOGI(TAG, "SSID: %s", update_ssid);
    ESP_LOGI(TAG, "PASSWD: %s", update_passwd);
    ESP_LOGI(TAG, "URL: %s", update_url);
    ESP_LOGI(TAG, "VERSION: %s", update_version == NULL ? "none" : update_version);

    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
    */

    //    ESP_ERROR_CHECK(OTA_connect());
    ESP_ERROR_CHECK(ota_connect());


#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK)
    /**
     * We are treating successful WiFi connection as a checkpoint to cancel rollback
     * process and mark newly updated firmware image as active. For production cases,
     * please tune the checkpoint behavior per end application requirement.
     */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App is valid, rollback cancelled successfully");
            } else {
                ESP_LOGE(TAG, "Failed to cancel rollback");
            }
        }
    }
#endif

#if CONFIG_OTA_CONNECT_WIFI
    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    esp_wifi_set_ps(WIFI_PS_NONE);
#endif // CONFIG_OTA_CONNECT_WIFI

    printf("Connection to WiFi AP successful\n");
    printf("Waiting for download ...\n");

    xTaskCreate(&start_ota_update, "start_ota_update", 1024 * 8, NULL, 5, NULL);
//    start_ota_update(NULL) ;

    return ESP_OK ;
}

static void register_ota_ota_update(void)
{
	ota_update_args.ssid = arg_str0("s", "ssid", "<ssid>", "WiFi AP SSID");
	ota_update_args.url = arg_str0("u", "url", "<url>", "Download URL");
	ota_update_args.version = arg_str0("v", "version", "<version>", "Version of Firmware to download");
	ota_update_args.passwd = arg_str1(NULL, NULL, "<passwd>", "WiFi AP Password");
	ota_update_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "ota_update",
        .help = "Update the device firmware using WiFi",
        .hint = NULL,
        .func = &do_ota_update,
		.argtable = &ota_update_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/******************************************************************************************
 * ota_switch_boot_partition
 * -------------------------------------
 * Switch boot priority to the next (update) partition
 * Actually this is the firmware before update
 *
*/

static esp_err_t do_ota_switch_boot_partition(int argc, char **argv)
{
	const esp_partition_t *boot_partition ;

	// Get next ota partition
	boot_partition = esp_ota_get_next_update_partition(NULL);

	// Set partition as next boot partition
    esp_err_t esp_err = esp_ota_set_boot_partition(boot_partition);
    if (esp_err != ESP_OK) goto Error ;

    // Reboot
    esp_restart() ;

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_ota_switch_boot_partition(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ota_switch_boot_partition",
        .help = "Activate and reboot the other partition",
        .hint = NULL,
        .func = &do_ota_switch_boot_partition,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

void register_ota(void)
{
    register_ota_ota_update();
    register_ota_switch_boot_partition();
}
