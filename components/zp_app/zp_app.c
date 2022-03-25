/*****************************************************************
*
*
*
*
*
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_console.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "esp_ota_ops.h"
//#include "esp_system.h"
//#include "nvs_flash.h"

#include "sdkconfig.h"

#include "zp_nvs.h"
#include "zp_app.h"
static const char *TAG = "zp_app" ;

#if CONFIG_STORE_HISTORY
	#define HISTORY_PATH CONFIG_HISTORY_MOUNT_PATH CONFIG_HISTORY_FILENAME
#endif

#define PROMPT_STR "zpPulser"

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_HISTORY_PERSISTENCE

#define CONFIG_HISTORY_PATH CONFIG_HISTORY_MOUNT_PATH CONFIG_HISTORY_FILENAME

static int partition_validated = false ;

static int validate_current_partition(void)
{
	esp_err_t esp_err = ESP_OK ;
	const esp_partition_t *current_partition ;
	esp_ota_img_states_t ota_state ;

	// Log function
	ESP_LOGI(TAG, "validate_current_partition()") ;

	// Get current Partition
	current_partition = esp_ota_get_running_partition() ;

	// Get current partition state from partition pointer
	ota_state = esp_ota_get_state_partition(current_partition, &ota_state);
	// Log current OTA state
	char *state_string = "undefined" ;
	switch(ota_state)
	{
		case ESP_OTA_IMG_NEW:				state_string = "ESP_OTA_IMG_NEW" ; break ;
		case ESP_OTA_IMG_PENDING_VERIFY:	state_string = "ESP_OTA_IMG_PENDING_VERIFY" ; break ;
		case ESP_OTA_IMG_VALID:				state_string = "ESP_OTA_IMG_VALID" ; break ;
		case ESP_OTA_IMG_INVALID:			state_string = "ESP_OTA_IMG_INVALID" ; break ;
		case ESP_OTA_IMG_ABORTED:			state_string = "ESP_OTA_IMG_ABORTED" ; break ;
		case ESP_OTA_IMG_UNDEFINED:			state_string = "ESP_OTA_IMG_UNDEFINED" ; break ;
	}
	ESP_LOGI(TAG, "OTA State is:%s", state_string) ;

	// Check partition state
	// If not yet validated do so
	if(ota_state != ESP_OTA_IMG_VALID)
	{
		// Validate partition
		esp_err = esp_ota_mark_app_valid_cancel_rollback() ;
		if (esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "Partition set to ESP_OTA_IMG_VALID") ;
	}

	Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
		return false ;
	}
//Exit:
	return true ;
}

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount(CONFIG_HISTORY_MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#endif // CONFIG_HISTORY_PERSISTENCE

static esp_err_t initialize_console(void)
{
    /* Drain stdout before reconfiguring it */
    fflush(stdout);
    fsync(fileno(stdout));

    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
        .source_clk = UART_SCLK_REF_TICK,
#else
        .source_clk = UART_SCLK_XTAL,
#endif
    };
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
            256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
            .max_cmdline_args = 12,
            .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
            .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);

    /* Don't return empty lines */
    linenoiseAllowEmpty(false);

#if CONFIG_HISTORY_PERSISTENCE
		// Check nvs config option if this feature is enabled
		if (option_get_u8("STORE_HISTORY")){
			/* Load command history from filesystem */
			linenoiseHistoryLoad(CONFIG_HISTORY_PATH);
		}
#endif

	return ESP_OK ;
}

esp_err_t app_set_history_factory_defaults(int force_flag)
{
    esp_err_t esp_err = ESP_OK ;
    nvs_handle_t nvs ;
    uint8_t out_value = 0 ;

    // Open nvs for read/write operations
    esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READWRITE, &nvs);
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

    // Check if option defaults have already been set to nvs
    esp_err = nvs_get_u8(nvs, "APP_HIST_INIT", &out_value) ;
    if (esp_err == ESP_OK && out_value == 1 && !force_flag) {
		ESP_LOGI(TAG, "PWM timer factory defaults are already set at nvs namespace '%s'", CONFIG_NVS_OPTIONS_NAMESPACE);
		goto Exit ;
    }
	ESP_LOGI(TAG, "Set PWM timer factory defaults at nvs namespace '%s'", CONFIG_NVS_OPTIONS_NAMESPACE);

	// Set history configuration options
	#ifdef CONFIG_HISTORY_PERSISTENCE
		#ifndef CONFIG_STORE_HISTORY
			#define CONFIG_STORE_HISTORY 0
		#endif

		// Set console history mode default
    	esp_err = nvs_set_u8(nvs, "STORE_HISTORY", CONFIG_STORE_HISTORY);
    	// Check for and handle error
    	if (esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "STORE_HISTORY default set to '%d'",CONFIG_STORE_HISTORY);
	#endif

	// Set Default Initialization Flag
	esp_err = nvs_set_u8(nvs, "APP_HIST_INIT", 1 ) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
Exit:
	return esp_err ;
}

esp_err_t initialize_app(int force_flag)
{
    esp_err_t esp_err = ESP_OK ;

    // Initialize command history defaults
    esp_err = app_set_history_factory_defaults(force_flag) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Initialize file system
	#if CONFIG_HISTORY_PERSISTENCE
		initialize_filesystem();
		// Check for and handle error
		if (esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "Command history enabled");
	#else
		ESP_LOGI(TAG, "Command history disabled");
	#endif

	// Initialize console
	if(!esp_err) esp_err = initialize_console();
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

#if CONFIG_HISTORY_PERSISTENCE
	static int app_enable_history(int argc, char **argv)
	{
		esp_err_t esp_err = ESP_OK ;
		nvs_handle_t nvs ;

		// Open nvs for write operations
		esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READWRITE, &nvs);
		if (esp_err != ESP_OK) {
			ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
			return esp_err;
		}

		// Set console history option to on
		esp_err = nvs_set_u8(nvs, "STORE_HISTORY", 1);
		if (esp_err != ESP_OK) {
			ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
			return esp_err;
		}
		ESP_LOGI(TAG, "STORE_HISTORY option set to 1 (enabled)");

		return esp_err ;
	}

	static int app_disable_history(int argc, char **argv)
	{
		esp_err_t esp_err = ESP_OK ;
		nvs_handle_t nvs ;

		// Open nvs for write operations
		esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READWRITE, &nvs);
		if (esp_err != ESP_OK) {
			ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
			return esp_err;
		}

		// Set console history option to on
		esp_err = nvs_set_u8(nvs, "STORE_HISTORY", 0);
		if (esp_err != ESP_OK) {
			ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
			return esp_err;
		}
		ESP_LOGI(TAG, "STORE_HISTORY option set to 0 (disabled)");

		return esp_err ;
	}
#endif

static int app_reset_factory_defaults(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;

	// Overwrite / Create all configuration option values with factory defaults
	if(!esp_err) esp_err = pwm_set_gpio_factory_defaults(true) ;
	if(!esp_err) esp_err = pwm_set_timer_factory_defaults(true) ;
	if(!esp_err) esp_err = app_set_history_factory_defaults(true) ;

#if CONFIG_STORE_HISTORY
	// Remove history file from filesystem
	remove(HISTORY_PATH) ;
	ESP_LOGI(TAG, "Command history deleted");
#endif

	// Check for and handle error
//Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

void register_app(){

    esp_console_register_help_command();


    const esp_console_cmd_t reset_defaults_cmd = {
        .command = "app_reset_factory_defaults",
        .help = "Reset all flash configuration options to factory defaults.\n",
        .hint = NULL,
        .func = &app_reset_factory_defaults
    };

#if CONFIG_HISTORY_PERSISTENCE
	const esp_console_cmd_t enable_history_cmd = {
		.command = "app_enable_history",
		.help = "Enable command history persistence\n",
		.hint = NULL,
		.func = &app_enable_history
	};

	const esp_console_cmd_t disable_history_cmd = {
		.command = "app_disable_history",
		.help = "Disable command history persistence\n",
		.hint = NULL,
		.func = &app_disable_history
	};

	ESP_ERROR_CHECK(esp_console_cmd_register(&enable_history_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&disable_history_cmd));
#endif

	ESP_ERROR_CHECK(esp_console_cmd_register(&reset_defaults_cmd));
}

void start_app()
{
	esp_err_t esp_err ;

	/* Prompt to be printed before each line.
	 * This can be customized, made dynamic, etc.
	 */
	const char* prompt = LOG_COLOR_I PROMPT_STR "> " LOG_RESET_COLOR ;

	printf("\n================================================================\n");
	printf("|                                                              |\n");
	printf("| Welcome to the ZeroPoint Pulser console!                     |\n");
	printf("|                                                              |\n");
	printf("| PWM Related Commands:                                        |\n");
	printf("| pwm_list      - Show all actual parameters of the PWM timner |\n");
	printf("| pwm_set       - Set PWM parameter of PWM timer               |\n");
	printf("| pwm_start     - Start a PWM timer                            |\n");
	printf("| pwm_stop      - Stop a PWM timer                             |\n");
	printf("| pwm_start_all - Start all PWM timer (0,1,2)                  |\n");
	printf("| pwm_stop_all  - Stop all PWM timer (0,1,2)                   |\n");
	printf("| pwm_reset     - Reset all PWM timer to default values        |\n");
	printf("|                                                              |\n");
	printf("| Other Commands:                                              |\n");
	printf("| help          - List all supported commands                  |\n");
	printf("| restart       - Reboot Controller                            |\n");
	printf("| ota_update    - Download and install new firmware            |\n");
	printf("|                                                              |\n");

	#if CONFIG_HISTORY_PERSISTENCE
	printf("| Command History Persistence                                  |\n");
	printf("| nvs_enable_history  - Enable history persistance             |\n");
	printf("| nvs_disable_history - Disable history persistance            |\n");
	printf("|                                                              |\n");
	#endif

	printf("| Hints:                                                       |\n");
	printf("| Use UP/DOWN arrows to navigate through command history.      |\n");
	printf("| Press TAB when typing command name to auto-complete.         |\n");
	printf("| Press Enter or Ctrl+C to terminate the console environment.  |\n");
	printf("|                                                              |\n");

	/* Figure out if the terminal supports escape sequences */
	int probe_status = linenoiseProbe();
	if (probe_status)
	{ /* zero indicates success */
		printf("| Your terminal application does not support escape sequences. |\n");
		printf("| Line editing and history features are disabled.              |\n");
		printf("| On Windows, try using Putty instead.                         |\n");
		printf("|                                                              |\n");

		linenoiseSetDumbMode(1);

		#if CONFIG_LOG_COLORS
			/* Since the terminal doesn't support escape sequences,
			 * don't use color codes in the prompt.
			 */
			prompt = PROMPT_STR "> ";
		#endif //CONFIG_LOG_COLORS
	}

	printf("| Version: %-50s  |\n", CONFIG_APP_PROJECT_VER " [Build: " __DATE__ "/" __TIME__ "]" );
	printf("| (c) ZeroPoint Workshop, all rigths reserved                  |\n");
	printf("================================================================\n\n");

	/* Main loop */
	while(true) {
		/* Get a line using linenoise.
		 * The line is returned when ENTER is pressed.
		 */
		char* line = linenoise(prompt);

		// Check if line was entered
		// loop on EOF or error
		if(line != NULL)
		{
			/* Add the command to the history if not empty*/
			if (strlen(line) > 0) {
				linenoiseHistoryAdd(line);

				#if CONFIG_HISTORY_PERSISTENCE
					// Check nvs config option if this feature is enabled
					if (option_get_u8("STORE_HISTORY")){
						// If so save command history to filesystem
						linenoiseHistorySave(CONFIG_HISTORY_PATH);
						ESP_LOGD(TAG, "History file saved");
					}
				#endif
			}

			/* Try to run the command */
			int ret;

			esp_err = esp_console_run(line, &ret);
			if (esp_err == ESP_ERR_NOT_FOUND) {
				printf("Unrecognized command\n");
			} else if (esp_err == ESP_ERR_INVALID_ARG) {
				// command was empty
			} else if (esp_err == ESP_OK && ret != ESP_OK) {
				printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
			} else if (esp_err != ESP_OK) {
				printf("Internal error: %s\n", esp_err_to_name(esp_err));
			}

			// After first successful command mark image as valid to prevent rollback at next boot
			if(!partition_validated) partition_validated = validate_current_partition() ;
		}
		/* linenoise allocates line buffer on the heap, so need to free it */
		linenoiseFree(line);
	}

	ESP_LOGE(TAG, "Error or end-of-input, terminating console");
	esp_console_deinit();
}
