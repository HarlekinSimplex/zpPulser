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
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "driver/mcpwm.h"
#include "nvs.h"

#include "sdkconfig.h"

#include "zp_nvs.h"
#include "zp_pwm.h"
static const char *TAG = "zp_pwm" ;

typedef struct{
	uint32_t frq ;
    float duty_a ;
    float duty_b ;
    uint32_t resolution ;
    mcpwm_sync_signal_t sync_sig ;
    mcpwm_timer_sync_trigger_t sync_output ;
	float timer_val ;
    mcpwm_timer_direction_t count_direction ;
    int is_stopped ;
    int is_initialized ;
} pwm_timer_register_t ;

typedef struct{
	uint32_t group_resolution ;
	pwm_timer_register_t timer [MCPWM_TIMER_MAX] ;
} pwm_timer_group_t ;

static pwm_timer_group_t pwm_timer_groups[MCPWM_UNIT_MAX] ;

static esp_err_t pwm_initial_startup(void) ;
static esp_err_t pwm_start_timer(int pwm_timer, int force_flag) ;

esp_err_t pwm_set_resolution(int pwm_timer)
{
	esp_err_t esp_err ;

	// Log Function
	ESP_LOGI(TAG,"pwm_set_resolution()") ;

	// Configure MCPWM_UNIT_0 group resolution
	esp_err = mcpwm_group_set_resolution(MCPWM_UNIT_0, pwm_timer_groups[0].group_resolution) ;
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG,"MCPWM_UNIT_0 Group resolution set to %u Hz", pwm_timer_groups[0].group_resolution) ;

	// Configure MCPWM_TIMER_0 group resolution
	esp_err = mcpwm_timer_set_resolution(MCPWM_UNIT_0, pwm_timer, pwm_timer_groups[0].timer[pwm_timer].resolution) ;
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG,"MCPWM0 Timer resolution set to %u Hz", pwm_timer_groups[0].timer[pwm_timer].resolution) ;

	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

esp_err_t pwm_set_gpio_factory_defaults(int force_flag)
{
    esp_err_t esp_err = ESP_OK ;
    nvs_handle_t nvs ;
    uint8_t out_value = 0 ;

	// Log Function
	ESP_LOGI(TAG,"pwm_set_gpio_factory_defaults()") ;

    // Open nvs for read/write operations
    esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READWRITE, &nvs);
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

    // Check if option defaults have already been set to nvs
    esp_err = nvs_get_u8(nvs, "PWM_GPIO_INIT", &out_value) ;
    if (esp_err == ESP_OK && out_value == 1 && !force_flag) {
		ESP_LOGI(TAG, "PWM GPIO factory defaults are already stored to flash namespace '%s'", CONFIG_NVS_OPTIONS_NAMESPACE);
		goto Exit ;
    }
	ESP_LOGI(TAG, "Store PWM GPIO factory defaults to flash namespace '%s'", CONFIG_NVS_OPTIONS_NAMESPACE);

	// Set GPIO Default options
	esp_err = nvs_set_u8(nvs, "MCPWM0A_GPIO", CONFIG_MCPWM0A_GPIO ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs, "MCPWM0B_GPIO", CONFIG_MCPWM0B_GPIO ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs, "MCPWM1A_GPIO", CONFIG_MCPWM1A_GPIO ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs, "MCPWM1B_GPIO", CONFIG_MCPWM1B_GPIO ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs, "MCPWM2A_GPIO", CONFIG_MCPWM2A_GPIO ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs, "MCPWM2B_GPIO", CONFIG_MCPWM2B_GPIO ) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM GPIO factory defaults set");

	// Set Default Initialization Flag
	esp_err = nvs_set_u8(nvs, "PWM_GPIO_INIT", 1 ) ;
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

esp_err_t pwm_set_timer_factory_defaults(int force_flag)
{
    esp_err_t esp_err = ESP_OK ;
    nvs_handle_t nvs ;
    uint8_t out_value = 0 ;

	// Log Function
	ESP_LOGI(TAG,"set_pwm_timer_factory_defaults()") ;

    // Open nvs for read/write operations
    esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READWRITE, &nvs);
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

    // Check if option defaults have already been set to nvs
    esp_err = nvs_get_u8(nvs, "PWM_TIMER_INIT", &out_value) ;
    if (esp_err == ESP_OK && out_value == 1 && !force_flag) {
		ESP_LOGI(TAG, "PWM timer factory defaults are already stored to flash at namespace '%s'", CONFIG_NVS_OPTIONS_NAMESPACE);
		goto Exit ;
    }
	ESP_LOGI(TAG, "Store PWM timer factory defaults to flash namespace '%s'", CONFIG_NVS_OPTIONS_NAMESPACE);

	// Set MCPWM_UNIT_0 group resolution options
	esp_err = nvs_set_u32(nvs, "MCPWM_U0_RES", CONFIG_MCPWM_UNIT_0_RESOLUTION ) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM Resolution factory defaults stored to flash");

	// Set MCPWM0 timer configuration options
	#ifdef CONFIG_MCPWM0_AUTOSTART
		esp_err = nvs_set_u8(nvs, "MCPWM0_AUTO", CONFIG_MCPWM0_AUTOSTART ) ;
	#else
		esp_err = nvs_set_u8(nvs, "MCPWM0_AUTO", 0 ) ;
	#endif
	if (!esp_err) esp_err = nvs_set_u32(nvs, "MCPWM0_FRQ",      CONFIG_MCPWM0_FRQ ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM0_DUTY_A",   CONFIG_MCPWM0_DUTY_A ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM0_DUTY_B",   CONFIG_MCPWM0_DUTY_B ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM0_SYNC_VAL", CONFIG_MCPWM0_SYNC_VAL ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM0_SYNC_SIG", CONFIG_MCPWM0_SYNC_SIG ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM0_SYNC_OUT", CONFIG_MCPWM0_SYNC_OUT ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM0_DIR",      CONFIG_MCPWM0_DIR ) ;
	if (!esp_err) esp_err = nvs_set_u32(nvs, "MCPWM0_RES",      CONFIG_MCPWM0_RESOLUTION ) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM0 factory defaults stored to flash");

	// Set MCPWM1 timer configuration options
	#ifdef CONFIG_MCPWM1_AUTOSTART
		esp_err = nvs_set_u8(nvs, "MCPWM1_AUTO", CONFIG_MCPWM1_AUTOSTART ) ;
	#else
		esp_err = nvs_set_u8(nvs, "MCPWM1_AUTO", 0 ) ;
	#endif
	if (!esp_err) esp_err = nvs_set_u32(nvs, "MCPWM1_FRQ",      CONFIG_MCPWM1_FRQ ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM1_DUTY_A",   CONFIG_MCPWM1_DUTY_A ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM1_DUTY_B",   CONFIG_MCPWM1_DUTY_B ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM1_SYNC_VAL", CONFIG_MCPWM1_SYNC_VAL ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM1_SYNC_SIG", CONFIG_MCPWM1_SYNC_SIG ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM1_SYNC_OUT", CONFIG_MCPWM1_SYNC_OUT ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM1_DIR",      CONFIG_MCPWM1_DIR ) ;
	if (!esp_err) esp_err = nvs_set_u32(nvs, "MCPWM1_RES",      CONFIG_MCPWM1_RESOLUTION ) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM1 factory defaults stored to flash");

	// Set MCPWM2 timer configuration options
	#ifdef CONFIG_MCPWM2_AUTOSTART
		esp_err = nvs_set_u8(nvs, "MCPWM2_AUTO", CONFIG_MCPWM2_AUTOSTART ) ;
	#else
		esp_err = nvs_set_u8(nvs, "MCPWM2_AUTO", 0 ) ;
	#endif
	if (!esp_err) esp_err = nvs_set_u32(nvs, "MCPWM2_FRQ",      CONFIG_MCPWM2_FRQ ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM2_DUTY_A",   CONFIG_MCPWM2_DUTY_A ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM2_DUTY_B",   CONFIG_MCPWM2_DUTY_B ) ;
	if (!esp_err) esp_err = nvs_set_str(nvs, "MCPWM2_SYNC_VAL", CONFIG_MCPWM2_SYNC_VAL ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM2_SYNC_SIG", CONFIG_MCPWM2_SYNC_SIG ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM2_SYNC_OUT", CONFIG_MCPWM2_SYNC_OUT ) ;
	if (!esp_err) esp_err = nvs_set_u8(nvs,  "MCPWM2_DIR",      CONFIG_MCPWM2_DIR ) ;
	if (!esp_err) esp_err = nvs_set_u32(nvs, "MCPWM2_RES",      CONFIG_MCPWM2_RESOLUTION ) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM2 factory defaults stored to flash");


	// Set Default Initialization Flag
	esp_err = nvs_set_u8(nvs, "PWM_TIMER_INIT", 1 ) ;
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

static esp_err_t pwm_set_default_timer_configuration(){
    esp_err_t esp_err = ESP_OK ;
    nvs_handle_t nvs ;
    char key[NVS_KEY_NAME_MAX_SIZE] ;

	// Log Function
	ESP_LOGI(TAG,"pwm_set_default_timer_configuration()") ;

	// Open nvs for read only operations
    esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READONLY, &nvs);
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Read PWM UNIT_0 group configuration from flash
	pwm_timer_groups[0].group_resolution = option_get_u32("MCPWM_U0_RES") ;
	ESP_LOGI(TAG, "MCPWM_UNIT_0 Resolution=%d", pwm_timer_groups[0].group_resolution);

	// Read PWM UNIT_0 timer configuration
	for(uint8_t pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
	{
		// Read defaults from flash and store them to timer register
		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_FRQ", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].frq = option_get_u32(key) ;
		ESP_LOGI(TAG, "MCPWM%d Frequency=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].frq);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_DUTY_A", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].duty_a = option_get_float(key) ;
		ESP_LOGI(TAG, "MCPWM%d CMP_A=%.1f", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].duty_a);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_DUTY_B", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].duty_b = option_get_float(key) ;
		ESP_LOGI(TAG, "MCPWM%d CMP_B=%.1f", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].duty_b);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_SYNC_VAL", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].timer_val = option_get_float(key) ;
		ESP_LOGI(TAG, "MCPWM%d Timer Val=%.1f", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].timer_val);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_SYNC_SIG", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].sync_sig = option_get_u8(key) ;
		ESP_LOGI(TAG, "MCPWM%d Sync Input=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].sync_sig);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_SYNC_OUT", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].sync_output = option_get_u8(key) ;
		ESP_LOGI(TAG, "MCPWM%d Sync Output=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].sync_output);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_DIR", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].count_direction = option_get_u8(key) ;
		ESP_LOGI(TAG, "MCPWM%d Count Direction=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].count_direction);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_RES", pwm_timer) ;
		pwm_timer_groups[0].timer[pwm_timer].resolution = option_get_u32(key) ;
		ESP_LOGI(TAG, "MCPWM%d Resolution=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].resolution);

		pwm_timer_groups[0].timer[pwm_timer].is_initialized = false ;
		ESP_LOGI(TAG, "MCPWM%d is_initialized=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].is_initialized);

		// Check if timer auto start flag is set
		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_AUTO", pwm_timer) ;
		if(option_get_u8(key)){
			// Enable start during initial startup
			pwm_timer_groups[0].timer[pwm_timer].is_stopped = false ;
			ESP_LOGI(TAG, "MCPWM%d is_stopped=%d (auto start option)", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].is_stopped);
		}else
		{
			// Disable start during initial startup
			pwm_timer_groups[0].timer[pwm_timer].is_stopped = true ;
			ESP_LOGI(TAG, "MCPWM%d is_stopped=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].is_stopped);
		}

		ESP_LOGI(TAG, "MCPWM%d Timer configuration set from flash defaults", pwm_timer);
	}
	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}


/******************************************************************************************
 * pwm_list
 * -------------------------------------
 * Log the actual PWM parameter for all three timer to the console
 *
*/

static esp_err_t do_pwm_list(int argc, char **argv)
{
	// List PWM parameters of group 0
	printf("MCPWM_UNIT_0 Resolution=%d\n", pwm_timer_groups[0].group_resolution) ;

	// List PWM parameters of timers
	for(int pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
	{
		// Log parameters set
		printf("MCPWM%d Frq=%d\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].frq);
		printf("MCPWM%d CMP_A=%.1f\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].duty_a);
		printf("MCPWM%d CMP_B=%.1f\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].duty_b);
		printf("MCPWM%d Count Direction=%d\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].count_direction);
		printf("MCPWM%d Sync Signal=%d\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].sync_sig);
		printf("MCPWM%d Sync Output=%d\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].sync_output);
		printf("MCPWM%d Sync Timer Value=%.1f\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].timer_val);
		printf("MCPWM%d Resolution=%d\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].resolution);
		printf("MCPWM%d %s\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].is_initialized ? "Initialized" : "Not Initialized");
		printf("MCPWM%d %s\n", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].is_stopped ? "Stopped" : "Running");
	};

	return ESP_OK ;
}

static void register_pwm_list(void)
{
	const esp_console_cmd_t cmd = {
        .command = "pwm_list",
        .help = "List actual PWM timer parameters",
        .hint = NULL,
        .func = &do_pwm_list
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_set  [-t <0..2>] [-f <Hz>] [-a <Percent>] [-b <Percent>] [-d <0..4>] [-s <0..6>] [-o <0..3>] [-v <Percent>]
 * -------------------------------------
 * Set the PWM parameter for a PWM timer
 *
*/

static struct {
    struct arg_int *timer;
    struct arg_dbl *duty_a;
    struct arg_dbl *duty_b;
    struct arg_dbl *timer_val;
    struct arg_int *count_direction;
    struct arg_int *sync_sig;
    struct arg_int *sync_output;
    struct arg_dbl *frq;
    struct arg_end *end;
} pwm_set_args;

static esp_err_t pwm_set_timer_synchronization(int pwm_timer)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"pwm_set_timer_synchronization()") ;

	// Set synchronization pattern
    // Configure sync source to t0 with delay as specified counting up
    mcpwm_sync_config_t sync_conf = {
   		.count_direction = pwm_timer_groups[0].timer[pwm_timer].count_direction,
    	.sync_sig = pwm_timer_groups[0].timer[pwm_timer].sync_sig,
		.timer_val = (abs(1000 - (uint32_t)(pwm_timer_groups[0].timer[pwm_timer].timer_val) * 10) % 1000),
    };
    // Set timer timer_val INPUT configuration
	esp_err = mcpwm_sync_configure(MCPWM_UNIT_0, pwm_timer, &sync_conf);
	if(esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM%d Count Direction is %d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].count_direction);
	ESP_LOGI(TAG, "MCPWM%d Sync Signal is %d",  pwm_timer, sync_conf.sync_sig);
	ESP_LOGI(TAG, "MCPWM%d Sync Timer Value=%d", pwm_timer, sync_conf.timer_val);

    // Set timer timer_val OUTPUT configuration to generate timer_val signal when counting to zero
	esp_err = mcpwm_set_timer_sync_output(MCPWM_UNIT_0, pwm_timer, pwm_timer_groups[0].timer[pwm_timer].sync_output) ;

	if(esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM%d Sync Output is %d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].sync_output);


	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t pwm_set_timer_frequency(int pwm_timer)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"pwm_set_timer_frequency()") ;

	// Set timer waveform as specified in timer register
	esp_err = mcpwm_set_frequency(MCPWM_UNIT_0, pwm_timer, (uint32_t)pwm_timer_groups[0].timer[pwm_timer].frq) ;
	if(esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM%d Frequency=%d", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].frq);

	esp_err = mcpwm_set_duty(MCPWM_UNIT_0, pwm_timer, MCPWM0A, pwm_timer_groups[0].timer[pwm_timer].duty_a) ;
	if(esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM%d CMP_A=%.1f", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].duty_a);

	esp_err = mcpwm_set_duty(MCPWM_UNIT_0, pwm_timer, MCPWM0B, pwm_timer_groups[0].timer[pwm_timer].duty_b) ;
	if(esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM%d CMP_B=%.1f", pwm_timer, pwm_timer_groups[0].timer[pwm_timer].duty_a);

	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t do_pwm_set(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;

	int pwm_timer           = 0 ; // Default timer to use is 0 (optional argument bt required to proceed)
    float pwm_frq           = 0 ;
    float pwm_duty_a        = 0 ;
    float pwm_duty_b        = 0 ;
    int pwm_count_direction = 0 ;
    int pwm_sync_sig        = 0 ; // Default MCPWM_SELECT_NO_INPUT
    int pwm_sync_output     = 3 ; // Default MCPWM_SWSYNC_SOURCE_DISABLED
    float pwm_timer_val     = 0 ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_set_args.end, argv[0]);
    	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
    }

    // Log number of arguments parsed
    ESP_LOGD(TAG,"PWM_SET Argument counts argc:%d -t:%d -f:%d -a:%d -b:%d -d:%d -s:%d -o:%d -v:%d", \
    		argc,                       \
			pwm_set_args.timer->count,  \
			pwm_set_args.frq->count,    \
			pwm_set_args.duty_a->count, \
			pwm_set_args.duty_b->count, \
			pwm_set_args.count_direction->count, \
			pwm_set_args.sync_sig->count, \
			pwm_set_args.sync_output->count, \
			pwm_set_args.timer_val->count );

    /* Check "-t --timer" option */
    if (pwm_set_args.timer->count) {
    	pwm_timer = pwm_set_args.timer->ival[0];
        if (pwm_timer < 0 || pwm_timer > 2)
       	{
            ESP_LOGE(TAG,"Invalid PWM timer (0..2)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }

    // Get actual PWM parameters for timer from PWM register
    pwm_frq             = pwm_timer_groups[0].timer[pwm_timer].frq ;
    pwm_duty_a          = pwm_timer_groups[0].timer[pwm_timer].duty_a ;
    pwm_duty_b          = pwm_timer_groups[0].timer[pwm_timer].duty_b ;
    pwm_count_direction = pwm_timer_groups[0].timer[pwm_timer].count_direction ;
    pwm_sync_sig        = pwm_timer_groups[0].timer[pwm_timer].sync_sig ;
    pwm_sync_output     = pwm_timer_groups[0].timer[pwm_timer].sync_output ;
    pwm_timer_val       = pwm_timer_groups[0].timer[pwm_timer].timer_val ;

    /* Check "-f --frq" option */
    if (pwm_set_args.frq->count) {
    	pwm_frq = (float)pwm_set_args.frq->dval[0];
    	if (pwm_frq<0 || pwm_frq>60000000)
       	{
            ESP_LOGE(TAG,"Invalid Frequency (0..20MHz)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }
    /* Check "-a" option */
    if (pwm_set_args.duty_a->count) {
    	pwm_duty_a = (float)pwm_set_args.duty_a->dval[0];
        if (pwm_duty_a<0 || pwm_duty_a>100)
       	{
            ESP_LOGE(TAG,"Invalid Duty Cycle (0..100)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }
    /* Check "-b" option */
    if (pwm_set_args.duty_b->count) {
    	pwm_duty_b = (float)pwm_set_args.duty_b->dval[0];
        if (pwm_duty_b<0 || pwm_duty_b>100)
       	{
            ESP_LOGE(TAG,"Invalid Duty Cycle (0..100)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }
    /* Check "-d" option */
    if (pwm_set_args.count_direction->count) {
    	pwm_count_direction = pwm_set_args.count_direction->ival[0];
    	if (pwm_count_direction<0 || pwm_count_direction>1)
       	{
            ESP_LOGE(TAG,"Invalid Count Direction (0..4)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }
    /* Check "-s" option */
    if (pwm_set_args.sync_sig->count) {
    	pwm_sync_sig = pwm_set_args.sync_sig->ival[0];
    	if (pwm_sync_sig<0 || pwm_sync_sig>6)
       	{
            ESP_LOGE(TAG,"Invalid Sync Signal (0..6)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }
    /* Check "-o" option */
    if (pwm_set_args.sync_output->count) {
    	pwm_sync_output = pwm_set_args.sync_output->ival[0];
    	if (pwm_sync_output<0 || pwm_sync_output>3)
       	{
            ESP_LOGE(TAG,"Invalid Sync Output (0..3)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }
    /* Check "-v" option */
    if (pwm_set_args.timer_val->count) {
    	pwm_timer_val = (float)pwm_set_args.timer_val->dval[0];
    	if (pwm_timer_val<0 || pwm_timer_val>100)
       	{
            ESP_LOGE(TAG,"Invalid Sync Duty (0..100)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }


    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_SET Timer:%d", pwm_timer);
    ESP_LOGI(TAG,"PWM_SET Frequency:%.0f", pwm_frq);
    ESP_LOGI(TAG,"PWM_SET CMP_A:%.1f", pwm_duty_a);
    ESP_LOGI(TAG,"PWM_SET CMP_B:%.1f", pwm_duty_b);
    ESP_LOGI(TAG,"PWM_SET Count Direction:%d", pwm_count_direction);
    ESP_LOGI(TAG,"PWM_SET Sync Signal:%d", pwm_sync_sig);
    ESP_LOGI(TAG,"PWM_SET Sync Output:%d", pwm_sync_output);
    ESP_LOGI(TAG,"PWM_SET Timer Value:%.1f", pwm_timer_val);

	// Save given parameters into PWM registers
	pwm_timer_groups[0].timer[pwm_timer].frq = pwm_frq ;
	pwm_timer_groups[0].timer[pwm_timer].duty_a = pwm_duty_a ;
	pwm_timer_groups[0].timer[pwm_timer].duty_b = pwm_duty_b ;
	pwm_timer_groups[0].timer[pwm_timer].count_direction = pwm_count_direction ;
	pwm_timer_groups[0].timer[pwm_timer].sync_sig = pwm_sync_sig ;
	pwm_timer_groups[0].timer[pwm_timer].sync_output = pwm_sync_output ;
	pwm_timer_groups[0].timer[pwm_timer].timer_val = pwm_timer_val ;

	// Propagate signal parameters to PWM timer (does not start yet)
	esp_err = pwm_set_timer_frequency(pwm_timer) ;
	if (esp_err != ESP_OK) goto Error ;

	// Propagate sync parameters to PWM timer (does not start yet)
	esp_err = pwm_set_timer_synchronization(pwm_timer) ;
	if (esp_err != ESP_OK) goto Error ;

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_set(void)
{
	pwm_set_args.timer = arg_int0("t", "timer", "<0..2>", "PWM timer") ;
	pwm_set_args.frq = arg_dbl0("f", "freq", "<Hz>", "Frequency") ;
	pwm_set_args.duty_a = arg_dbl0("a", "duty-a", "<Percent>", "Duty cycle of comperator A") ;
	pwm_set_args.duty_b = arg_dbl0("b", "duty-b", "<Percent>", "Duty cycle of comperator B") ;
	pwm_set_args.count_direction = arg_int0("d", "count-direction", "<0..4>", "Count Direction") ;
	pwm_set_args.sync_sig = arg_int0("s", "sync-signal", "<0..6>", "Sync Signal") ;
	pwm_set_args.sync_output = arg_int0("o", "sync-output", "<0..3>", "Sync Output") ;
	pwm_set_args.timer_val = arg_dbl0("v", "timer-value", "<Percent>", "Timer start duty after sync signal") ;
	pwm_set_args.end = arg_end(2);

	const esp_console_cmd_t cmd = {
        .command = "pwm_set",
        .help = "Set PWM timer parameters",
        .hint = NULL,
        .func = &do_pwm_set,
        .argtable = &pwm_set_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_start
 * -------------------------------------
 * Start a PWM timer (0..2)
 *
*/

static struct {
    struct arg_int *timer;
    struct arg_end *end;
} pwm_start_args;

static esp_err_t pwm_start_timer(int pwm_timer, int force_flag)
{
	esp_err_t esp_err = ESP_OK ;
    mcpwm_config_t pwm_config;

	// Log Function
	ESP_LOGI(TAG,"pwm_start_timer()") ;

    // Initialize timer if not already initialized
    if (!pwm_timer_groups[0].timer[pwm_timer].is_initialized || force_flag)
    {
		// Force pwm gen pin to low and stop timer
		// Set pwm pins A to low
		esp_err = mcpwm_set_signal_low(MCPWM_UNIT_0, pwm_timer, 0 ) ;
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Generator pin A forced to low", pwm_timer);
		// Set pwm pins B to low
		esp_err = mcpwm_set_signal_low(MCPWM_UNIT_0, pwm_timer, 1 ) ;
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Generator pin B forced to low", pwm_timer);

			// Configure MCPWM_UNIT_0 group and timer resolution
			esp_err = pwm_set_resolution(pwm_timer) ;
			if(esp_err != ESP_OK) goto Error ;

			// Dummy initialization parameters
			// Will be overwritten by pwm_set_resolution
			//		pwm_config.frequency = pwm_timer_group_register[0].timer[pwm_timer].frq ;
			pwm_config.frequency = 1 ;
			pwm_config.duty_mode = MCPWM_DUTY_MODE_0 ;
			pwm_config.counter_mode = MCPWM_UP_COUNTER ;

			// Initialize and start timer
			esp_err = mcpwm_init(MCPWM_UNIT_0, pwm_timer, &pwm_config); // mcpwm_init() sets prescaler to default !
			if(esp_err != ESP_OK) goto Error ;

			// Set frequency and duties (required because of resolution reconfiguration)
			esp_err = pwm_set_timer_frequency(pwm_timer) ;
			if(esp_err != ESP_OK) goto Error ;

		// Initialize sync input and output
		esp_err = pwm_set_timer_synchronization(pwm_timer) ;
		if(esp_err != ESP_OK) goto Error ;

    	// Resume duty mode after gen output forced to low
    	// Resume gen A
		esp_err = mcpwm_set_duty_type(MCPWM_UNIT_0, pwm_timer, 0 , MCPWM_DUTY_MODE_0) ;
		if(esp_err != ESP_OK) goto Error ;
    	// Resume gen B
		esp_err = mcpwm_set_duty_type(MCPWM_UNIT_0, pwm_timer, 0 , MCPWM_DUTY_MODE_0) ;
		if(esp_err != ESP_OK) goto Error ;


		// Set initialization flag
		pwm_timer_groups[0].timer[pwm_timer].is_initialized = true ;
		ESP_LOGI(TAG, "MCPWM%d Initialized and started", pwm_timer);
    }
    else
    {
    	// Resume duty mode after gen output forced to low
    	// Resume gen A
		esp_err = mcpwm_set_duty_type(MCPWM_UNIT_0, pwm_timer, 0 , MCPWM_DUTY_MODE_0) ;
		if(esp_err != ESP_OK) goto Error ;
    	// Resume gen B
		esp_err = mcpwm_set_duty_type(MCPWM_UNIT_0, pwm_timer, 0 , MCPWM_DUTY_MODE_0) ;
		if(esp_err != ESP_OK) goto Error ;

    	// Timer was initialized and stopped and just needs to be started again
		esp_err = mcpwm_start(MCPWM_UNIT_0, pwm_timer) ;
		if(esp_err != ESP_OK) goto Error ;

		// Log result
		ESP_LOGI(TAG, "MCPWM%d started", pwm_timer);
    }

	// Clear 'is_stopped flag' of timer
    pwm_timer_groups[0].timer[pwm_timer].is_stopped = false ;

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t do_pwm_start(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;
	int pwm_timer = 0 ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_start_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_start_args.end, argv[0]);
		esp_err = ESP_ERR_INVALID_ARG ; goto Error ;
    }

    // Check if timer ID was specified otherwise use default 0
    if(pwm_start_args.timer->count > 0 ){
    	// Timer ID specified
       	pwm_timer = pwm_start_args.timer->ival[0];
    	// Validate timer ID
        if (pwm_timer < 0 || pwm_timer > 2)	{
    		ESP_LOGE(TAG,"Inavlid PWM timer (0..2)");
    		esp_err = ESP_ERR_INVALID_ARG ; goto Error ;
    	} ;
    }

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_START Timer:%d", pwm_timer);

    // Initialize (if not yet done) and start timer
	esp_err = pwm_start_timer(pwm_timer, false) ;
	if (esp_err != ESP_OK) goto Error ;

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_start(void)
{
	pwm_start_args.timer = arg_int0(NULL, NULL, "<0..2>", "PWM timer");
	pwm_start_args.end = arg_end(5);

	const esp_console_cmd_t cmd = {
        .command = "pwm_start",
        .help = "Start a PWM timer (0..2)",
        .hint = NULL,
        .func = &do_pwm_start,
        .argtable = &pwm_start_args
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_stop
 * -------------------------------------
 * Stop a PWM timer (0..2)
 *
*/

static struct {
    struct arg_int *timer;
    struct arg_end *end;
} pwm_stop_args;

static esp_err_t pwm_stop_timer(int pwm_timer)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"pwm_stop_timer()") ;

	// Set is_stoped flag
	pwm_timer_groups[0].timer[pwm_timer].is_stopped = true ;

	// Stop pwm timer if initialized
	if(pwm_timer_groups[0].timer[pwm_timer].is_initialized){
		// Stop timer

		// Force pwm gen pin to low and stop timer
		// Set pwm pins A to low
		esp_err = mcpwm_set_signal_low(MCPWM_UNIT_0, pwm_timer, 0 ) ;
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Generator pin A forced to low", pwm_timer);
		// Set pwm pins B to low
		esp_err = mcpwm_set_signal_low(MCPWM_UNIT_0, pwm_timer, 1 ) ;
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Generator pin B forced to low", pwm_timer);
		// Stop timer
		esp_err = mcpwm_stop(MCPWM_UNIT_0, pwm_timer);
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Timer stopped", pwm_timer);
	}
	else ESP_LOGI(TAG, "MCPWM%d Timer stopped (was not initialized yet)", pwm_timer);

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t do_pwm_stop(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;
	int pwm_timer = 0 ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_stop_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_stop_args.end, argv[0]);
		esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
    }

    // Check if timer ID was specified otherwise use default 0
    if(pwm_stop_args.timer->count > 0 ){
    	// Timer ID specified
       	pwm_timer = pwm_stop_args.timer->ival[0];
    	// Validate timer ID
        if (pwm_timer < 0 || pwm_timer > 2)	{
    		ESP_LOGE(TAG,"Inavlid PWM timer (0..2)");
    		esp_err = ESP_ERR_INVALID_ARG ; goto Error ;
    	} ;
    }

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_STOP Timer:%d", pwm_timer);

	// Stop pwm timer and check for errors
	esp_err = pwm_stop_timer(pwm_timer) ;
	// Check for errors
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_stop(void)
{
	pwm_stop_args.timer = arg_int0(NULL, NULL, "<0..2>", "PWM timer");
	pwm_stop_args.end = arg_end(5);

	const esp_console_cmd_t cmd = {
        .command = "pwm_stop",
        .help = "Stop a PWM timer (0..2)",
        .hint = NULL,
        .func = &do_pwm_stop,
        .argtable = &pwm_stop_args
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_start_all
 * -------------------------------------
 * Start all PWM timer (0,1,2)
 *
*/

static esp_err_t pwm_start_all(void)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"pwm_start_all()") ;

	// Start all timer
	for(int pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
	{
		// Start pwm timer and check for errors
		esp_err = pwm_start_timer(pwm_timer, false) ;
		// Check for errors
		if (esp_err != ESP_OK) {
			break ;
		}
	}

//Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t do_pwm_start_all(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;

	// Start all timer
	esp_err = pwm_start_all() ;

//Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}


static void register_pwm_start_all(void)
{
	const esp_console_cmd_t cmd = {
        .command = "pwm_start_all",
        .help = "Start all PWM timer",
        .hint = NULL,
        .func = &do_pwm_start_all
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_stop_all
 * -------------------------------------
 * Stop all PWM timer (0..2)
 *
*/

static esp_err_t pwm_stop_all(void)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"pwm_stop_all()") ;

	// Stop all timer
	for(int pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
	{
		// Stop pwm timer and check for errors
		esp_err = pwm_stop_timer(pwm_timer) ;
		// Check for errors
		if (esp_err != ESP_OK) {
			break ;
		}
	}

//Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t do_pwm_stop_all(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;

	// Stop all timer
	esp_err = pwm_stop_all() ;
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
	return esp_err ;
}

static void register_pwm_stop_all(void)
{
	const esp_console_cmd_t cmd = {
        .command = "pwm_stop_all",
        .help = "Stop all PWM timer",
        .hint = NULL,
        .func = &do_pwm_stop_all
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_set_group_resolution
 * -------------------------------------
 * Set group resolution
 *
*/
static struct {
    struct arg_int *group ;
    struct arg_dbl *resolution ;
    struct arg_end *end ;
} pwm_set_group_resolution_args ;

static esp_err_t do_pwm_set_group_resolution(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;
	int pwm_group = 0 ;
	float pwm_resolution = 100000000 ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_set_group_resolution_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_set_group_resolution_args.end, argv[0]);
        esp_err = ESP_ERR_INVALID_ARG ; goto Error ;
    }

    // Log parsed command
    ESP_LOGI(TAG,"PWM_GROUP_RESOLUTION command parsed");

    /* Check "-g --group" option */
    if (pwm_set_group_resolution_args.group->count) {
    	pwm_group = pwm_set_group_resolution_args.group->ival[0];
        if (pwm_group < 0 || pwm_group > 1)
       	{
            ESP_LOGE(TAG,"Invalid pwm group (0..2)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }

    /* Check resolution */
    if (pwm_set_group_resolution_args.resolution->count) {
    	pwm_resolution  = (float)pwm_set_group_resolution_args.resolution->dval[0];
    	if (pwm_resolution<0 || pwm_resolution>120000000)
       	{
            ESP_LOGE(TAG,"Invalid pwm group resolution (1..120MHz)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }

    // Set group resolution
	pwm_timer_groups[0].group_resolution = pwm_resolution ;

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM UNIT_0 RESOLUTION set to %.0f", pwm_resolution);

    // Bounce all timer of group UNIT_0 if one of its timers actually runs
    for( unsigned short pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
    {
        ESP_LOGI(TAG,"Bounce Timer %d", pwm_timer);
		if(!pwm_timer_groups[0].timer[pwm_timer].is_stopped)
		{
			// Stop timer
			esp_err = pwm_stop_timer(pwm_timer) ;
			if(esp_err != ESP_OK) goto Error ;

			// Restart timer with new resolution (force reinitialization)
			esp_err = pwm_start_timer(pwm_timer, true) ;
			if(esp_err != ESP_OK) goto Error ;
		}
    }

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_GROUP_RESOLUTION Timer=%d", pwm_group);
    ESP_LOGI(TAG,"PWM_GROUP_RESOLUTION Resolution=%.0f", pwm_resolution);

    // Configure group resolution
    esp_err = mcpwm_group_set_resolution(pwm_group, (unsigned long int)pwm_resolution) ;

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_set_group_resolution(void)
{
	pwm_set_group_resolution_args.group = arg_int0("g", NULL, "<0|1>", "Timer Group (default 0)");
	pwm_set_group_resolution_args.resolution = arg_dbl0(NULL, NULL, "<Hz>", "Resolution to use (Default 100Mhz)");
	pwm_set_group_resolution_args.end = arg_end(5);

	const esp_console_cmd_t cmd = {
        .command = "pwm_set_group_resolution",
        .help = "Set timer group resolution from 0Mhz..120Mhz.",
        .hint = NULL,
        .func = &do_pwm_set_group_resolution,
        .argtable = &pwm_set_group_resolution_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


/******************************************************************************************
 * pwm_set_timer resolution
 * -------------------------------------
 * Set timer resolution
 *
*/

static struct {
    struct arg_int *timer ;
    struct arg_dbl *resolution ;
    struct arg_end *end ;
} pwm_set_timer_resolution_args ;

static esp_err_t do_pwm_set_timer_resolution(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;
	int pwm_timer = 0 ;
	float pwm_resolution = 10000000 ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_set_timer_resolution_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_set_timer_resolution_args.end, argv[0]);
        esp_err = ESP_ERR_INVALID_ARG ; goto Error ;
    }

    // Log parsed command
    ESP_LOGI(TAG,"PWM_TIMER_RESOLUTION command parsed");

    /* Check "-t --timer" option */
    if (pwm_set_timer_resolution_args.timer->count) {
    	pwm_timer = pwm_set_timer_resolution_args.timer->ival[0];
        if (pwm_timer < 0 || pwm_timer > 2)
       	{
            ESP_LOGE(TAG,"Invalid PWM timer (0..2)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }

    /* Check resolution */
    if (pwm_set_timer_resolution_args.resolution->count) {
    	pwm_resolution  = (float)pwm_set_timer_resolution_args.resolution->dval[0];
    	if (pwm_resolution<0 || pwm_resolution>120000000)
       	{
            ESP_LOGE(TAG,"Invalid timer resolution (1..120MHz)");
        	esp_err = ESP_ERR_INVALID_ARG ;	goto Error ;
       	} ;
    }

    // Set timer resolution
	pwm_timer_groups[0].timer[pwm_timer].resolution = pwm_resolution ;

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_TIMER_RESOLUTION Timer=%d", pwm_timer);
    ESP_LOGI(TAG,"PWM_TIMER_RESOLUTION Resolution=%.0f", pwm_resolution);

    // Bounce timer if it actually runs
    if(!pwm_timer_groups[0].timer[pwm_timer].is_stopped)
    {
        ESP_LOGI(TAG,"Bounce Timer %d", pwm_timer);

        // Stop timer
        esp_err = pwm_stop_timer(pwm_timer) ;
        if(esp_err != ESP_OK) goto Error ;

        // Restart timer with new resolution (force reinitialization)
        esp_err = pwm_start_timer(pwm_timer, true) ;
        if(esp_err != ESP_OK) goto Error ;
    }

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_set_timer_resolution(void)
{
	pwm_set_timer_resolution_args.timer = arg_int0("t", NULL, "<0|1>", "Timer (default 0)");
	pwm_set_timer_resolution_args.resolution = arg_dbl0(NULL , NULL, "<Hz>", "Resolution to use (Default 1Mz)");
	pwm_set_timer_resolution_args.end = arg_end(5);

	const esp_console_cmd_t cmd = {
        .command = "pwm_set_timer_resolution",
        .help = "Set timer resolution from 0Mhz..120Mhz.",
        .hint = NULL,
        .func = &do_pwm_set_timer_resolution,
        .argtable = &pwm_set_timer_resolution_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/******************************************************************************************
 * pwm_reset
 * -------------------------------------
 * Reset all PWM timer to default values
 *
*/

static struct {
    struct arg_lit *factory;
    struct arg_end *end;
} pwm_reset_args;

static esp_err_t do_pwm_reset(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_reset_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_reset_args.end, argv[0]);
		return ESP_ERR_INVALID_ARG ;
    }

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_RESET command parsed");

	// Stop all pwm timer
	esp_err = pwm_stop_all() ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
    ESP_LOGI(TAG,"All timer stopped");

	// Check if flash or firmware (reseting flash also) defaults are to be used
    if(pwm_reset_args.factory->count){

    	// Set PWM timer factory defaults to flash
    	esp_err =  pwm_set_timer_factory_defaults( true ) ;
    	// Check for and handle error
    	if (esp_err != ESP_OK) goto Error ;
        ESP_LOGI(TAG,"Timer boot (flash) defaults are reset to factory (firmware) defaults");
    }

	// Configure timer register with flash values
	for(int pwm_timer=0 ; pwm_timer <3 ; pwm_timer++){
		// Configure timer with flash values
		esp_err = pwm_set_default_timer_configuration() ;
		if (esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Timer defaults configured", pwm_timer);
	}
    ESP_LOGI(TAG,"Timer configuration reset to current boot (flash) defaults");

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_reset(void)
{
	pwm_reset_args.factory = arg_lit0(NULL, "factory", "Reset to factory defaults");
	pwm_reset_args.end = arg_end(5);

	const esp_console_cmd_t cmd = {
        .command = "pwm_reset",
        .help = "Reset all PWM timer configuration values to default values",
        .hint = NULL,
        .func = &do_pwm_reset,
        .argtable = &pwm_reset_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/******************************************************************************************
 * pwm_save
 * -------------------------------------
 * Save actual PWM timer configuration as defaults to flash
 *
*/

static struct {
    struct arg_lit *autostart;
    struct arg_end *end;
} pwm_save_args;


static esp_err_t pwm_save_timer_configuration(int force_flag)
{
    esp_err_t esp_err = ESP_OK ;
    nvs_handle_t nvs ;
    char float_string_buffer[10] ; // 0.0 .. 99.9
    char key[NVS_KEY_NAME_MAX_SIZE] ;

	// Log Function
	ESP_LOGI(TAG,"pwm_save_timer_configuration()") ;

   // Open nvs for read/write operations
    esp_err = nvs_open(CONFIG_NVS_OPTIONS_NAMESPACE, NVS_READWRITE, &nvs);
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Set MCPWM UNIT_0 resolution options
	esp_err = nvs_set_u32(nvs, "MCPWM_U0_RES", pwm_timer_groups[0].group_resolution ) ;
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM UNIT_0 resolution factory defaults stored to flash");

	// Save MCPWMx timer configuration to flash default values
	// Loop through all three timer
	for(unsigned short pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
	{
		// Set timer to auto start when force flag is set
		if(force_flag){
			// Set actual timer run state as auto start when force flag is set
			snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_AUTO", pwm_timer) ;
			esp_err = nvs_set_u8(nvs, key, !pwm_timer_groups[0].timer[pwm_timer].is_stopped ) ;
			if(esp_err) break  ;
			ESP_LOGI(TAG, "Key %s set to %d", key, !pwm_timer_groups[0].timer[pwm_timer].is_stopped);
		}
		else{
			// Clear auto start flag for this timer
			snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_AUTO", pwm_timer) ;
			esp_err = nvs_set_u8(nvs, key, false ) ;
			if(esp_err) break  ;
			ESP_LOGI(TAG, "Key %s set to %d", key, false);
		}

		// Save signal configuration to flash
		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_FRQ", pwm_timer) ;
		esp_err = nvs_set_u32(nvs, key, pwm_timer_groups[0].timer[pwm_timer].frq ) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %d", key, pwm_timer_groups[0].timer[pwm_timer].frq);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_DUTY_A", pwm_timer) ;
		snprintf(float_string_buffer, 10, "%2.1f", pwm_timer_groups[0].timer[pwm_timer].duty_a);
		esp_err = nvs_set_str(nvs, key, float_string_buffer ) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %s", key, float_string_buffer);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_DUTY_B", pwm_timer) ;
		snprintf(float_string_buffer, 10, "%2.1f", pwm_timer_groups[0].timer[pwm_timer].duty_b);
		esp_err = nvs_set_str(nvs, key, float_string_buffer ) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %s", key, float_string_buffer);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_SYNC_VAL", pwm_timer) ;
		snprintf(float_string_buffer, 10, "%2.1f", pwm_timer_groups[0].timer[pwm_timer].timer_val);
		esp_err = nvs_set_str(nvs, key, float_string_buffer ) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %s", key, float_string_buffer);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_SYNC_SIG", pwm_timer) ;
		esp_err = nvs_set_u8(nvs, key, pwm_timer_groups[0].timer[pwm_timer].sync_sig) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %d", key, pwm_timer_groups[0].timer[pwm_timer].sync_sig);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_SYNC_OUT", pwm_timer) ;
		esp_err = nvs_set_u8(nvs, key, pwm_timer_groups[0].timer[pwm_timer].sync_output) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %d", key, pwm_timer_groups[0].timer[pwm_timer].sync_output);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_DIR", pwm_timer) ;
		esp_err = nvs_set_u8(nvs, key, pwm_timer_groups[0].timer[pwm_timer].count_direction) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %d", key, pwm_timer_groups[0].timer[pwm_timer].count_direction);

		snprintf(key, NVS_KEY_NAME_MAX_SIZE, "MCPWM%u_RES", pwm_timer) ;
		esp_err = nvs_set_u32(nvs, key, pwm_timer_groups[0].timer[pwm_timer].resolution ) ;
		if(esp_err) break  ;
		ESP_LOGI(TAG, "Key %s set to %d", key, pwm_timer_groups[0].timer[pwm_timer].resolution);
	}

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t do_pwm_save(int argc, char **argv)
{
	esp_err_t esp_err = ESP_OK ;

    int nerrors = arg_parse(argc, argv, (void **)&pwm_save_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pwm_save_args.end, argv[0]);
		return ESP_ERR_INVALID_ARG ;
    }

    // Log parsed parameters
    ESP_LOGI(TAG,"PWM_SAVE command parsed");

	// Check if actual running timers should set to auto start
    if(pwm_save_args.autostart->count){

    	// Save pwm timer configuration /w auto start to flash
    	esp_err =  pwm_save_timer_configuration( true ) ;
    	// Check for and handle error
    	if (esp_err != ESP_OK) goto Error ;
        ESP_LOGI(TAG,"Actual Timer configuration stored as defaults (with autostart option)");
    }
    else{
    	// Save pwm timer configuration /wo autostart to flash
    	esp_err =  pwm_save_timer_configuration( false ) ;
    	// Check for and handle error
    	if (esp_err != ESP_OK) goto Error ;
        ESP_LOGI(TAG,"Actual Timer configuration stored as defaults");
    }

Error:
	if (esp_err != ESP_OK) {
		// Log error message
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static void register_pwm_save(void)
{
	pwm_save_args.autostart = arg_lit0(NULL, "autostart", "Save running timers with autostart option");
	pwm_save_args.end = arg_end(5);

	const esp_console_cmd_t cmd = {
        .command = "pwm_save",
        .help = "Save actual timer configuration as default values",
        .hint = NULL,
        .func = &do_pwm_save,
        .argtable = &pwm_save_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}


////////////////////////////////////////////////


esp_err_t register_pwm(void)
{
    register_pwm_list();
    register_pwm_set();
    register_pwm_start();
    register_pwm_stop();
    register_pwm_start_all();
    register_pwm_stop_all();
    register_pwm_reset();
    register_pwm_save() ;
    register_pwm_set_group_resolution() ;
    register_pwm_set_timer_resolution() ;

    return ESP_OK ;
}

esp_err_t initialize_pwm(int force_flag)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"initialize_pwm()") ;

	// Set PWM GPIO factory defaults to flash
	esp_err =  pwm_set_gpio_factory_defaults(force_flag) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Set PWM timer factory defaults to flash
	esp_err =  pwm_set_timer_factory_defaults(force_flag) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;

	// Configure timer with flash values
	esp_err = pwm_set_default_timer_configuration(0) ;
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "Timer default configuration (read from flash) completed");

	// Force timer pins down to low
	for(int pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++){

		// Force Output Pins to low
		// Set pwm pins A to low
		esp_err = mcpwm_set_signal_low(MCPWM_UNIT_0, pwm_timer, 0 ) ;
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Generator pin A forced to low", pwm_timer);
		// Set pwm pins B to low
		esp_err = mcpwm_set_signal_low(MCPWM_UNIT_0, pwm_timer, 1 ) ;
		if(esp_err != ESP_OK) goto Error ;
		ESP_LOGI(TAG, "MCPWM%d Generator pin B forced to low", pwm_timer);
	}

	// Bring up pwm timer as configured
	esp_err = pwm_initial_startup() ;
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG, "MCPWM initialization and startup completed");

	// Initialize GPIO Pins
	// Read and log pin configuration from flash
	ESP_LOGD(TAG, "Pins [%d][%d][%d][%d][%d][%d]", \
		  option_get_u8("MCPWM0A_GPIO"), \
		  option_get_u8("MCPWM0B_GPIO"), \
		  option_get_u8("MCPWM1A_GPIO"), \
		  option_get_u8("MCPWM1B_GPIO"), \
		  option_get_u8("MCPWM2A_GPIO"), \
		  option_get_u8("MCPWM2B_GPIO") \
		  );
	// Initialize GPIO pin with assignments from flash
	esp_err =  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, option_get_u8("MCPWM0A_GPIO")) ;
	if(!esp_err) esp_err =  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, option_get_u8("MCPWM0B_GPIO")) ;
	if(!esp_err) esp_err =  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1A, option_get_u8("MCPWM1A_GPIO")) ;
	if(!esp_err) esp_err =  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1B, option_get_u8("MCPWM1B_GPIO")) ;
	if(!esp_err) esp_err =  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2A, option_get_u8("MCPWM2A_GPIO")) ;
	if(!esp_err) esp_err =  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2B, option_get_u8("MCPWM2B_GPIO")) ;
	// Check for and handle error
	if (esp_err != ESP_OK) goto Error ;
	ESP_LOGI(TAG,"GPIO initialization completed");


	// Check for and handle error
Error:
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG, "%s", esp_err_to_name(esp_err));
	}
//Exit:
	return esp_err ;
}

static esp_err_t pwm_initial_startup(void)
{
	esp_err_t esp_err = ESP_OK ;

	// Log Function
	ESP_LOGI(TAG,"pwm_initial_startup()") ;

	// Start or stop pwm timer as indicates by is_stoped preset parameter
	for(int pwm_timer=0 ; pwm_timer<MCPWM_TIMER_MAX ; pwm_timer++)
	{
		// Start / Stop Timer according its_stopped flag
		if(pwm_timer_groups[0].timer[pwm_timer].is_stopped){
			// Stop timer
			esp_err = pwm_stop_timer(pwm_timer) ;
			if (esp_err != ESP_OK) {
				// Log error message
				ESP_LOGE(TAG, "Unable to stop timer '%d'", pwm_timer);
			}
		}
		else{
		    // Start timer
			esp_err = pwm_start_timer(pwm_timer, false) ;
			if (esp_err != ESP_OK) {
				// Log error message
				ESP_LOGE(TAG, "Unable to start timer '%d'", pwm_timer);
			}
		}
	}

	return esp_err ;
}
