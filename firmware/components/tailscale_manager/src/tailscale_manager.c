#include "tailscale_manager.h"
#include "tailscale_nvs.h"
#include "sdkconfig.h"

#include "microlink.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "tailscale_mgr";

// Auth key buffer size (Tailscale keys are ~60 chars)
#define AUTH_KEY_MAX_LEN 128

// Internal state machine
typedef enum {
    TS_MGR_STATE_IDLE,
    TS_MGR_STATE_UNCONFIGURED,
    TS_MGR_STATE_CONNECTING,
    TS_MGR_STATE_CONNECTED,
    TS_MGR_STATE_RECONNECTING,
} ts_mgr_state_t;

// Internal state
static struct {
    ts_mgr_state_t state;
    SemaphoreHandle_t state_mutex;
    microlink_t *ml_handle;
    TaskHandle_t update_task;
    ts_mgr_callback_t callback;
    void *callback_ctx;
    const char *device_name;
    uint8_t reconnect_attempts;
    uint32_t reconnect_delay_ms;
    char auth_key[AUTH_KEY_MAX_LEN];
    bool stop_requested;
} s_ts_mgr = {
    .state = TS_MGR_STATE_IDLE,
    .reconnect_delay_ms = CONFIG_TS_MGR_RECONNECT_INITIAL_MS,
};

// State name lookup
static const char *state_names[] = {
    [TS_MGR_STATE_IDLE] = "IDLE",
    [TS_MGR_STATE_UNCONFIGURED] = "UNCONFIGURED",
    [TS_MGR_STATE_CONNECTING] = "CONNECTING",
    [TS_MGR_STATE_CONNECTED] = "CONNECTED",
    [TS_MGR_STATE_RECONNECTING] = "RECONNECTING",
};

// Forward declarations
static void update_task(void *arg);
static void set_state(ts_mgr_state_t new_state);
static void notify_callback(ts_mgr_event_t event);
static esp_err_t start_microlink(void);
static void handle_reconnect(void);

esp_err_t ts_mgr_init(const ts_mgr_config_t *config)
{
    ESP_LOGI(TAG, "Initializing Tailscale manager...");

    // Create state mutex
    s_ts_mgr.state_mutex = xSemaphoreCreateMutex();
    if (s_ts_mgr.state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Store config
    if (config) {
        s_ts_mgr.callback = config->callback;
        s_ts_mgr.callback_ctx = config->callback_ctx;
        s_ts_mgr.device_name = config->device_name ? config->device_name
                                                   : CONFIG_TS_MGR_DEVICE_NAME;
    } else {
        s_ts_mgr.device_name = CONFIG_TS_MGR_DEVICE_NAME;
    }

    s_ts_mgr.stop_requested = false;

    // Check for stored auth key
    esp_err_t ret = ts_nvs_load_key(s_ts_mgr.auth_key, sizeof(s_ts_mgr.auth_key));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Found stored auth key, connecting...");
        ret = start_microlink();
        if (ret != ESP_OK) {
            vSemaphoreDelete(s_ts_mgr.state_mutex);
            s_ts_mgr.state_mutex = NULL;
            return ret;
        }
    } else {
        ESP_LOGI(TAG, "No stored auth key, waiting for configuration...");
        set_state(TS_MGR_STATE_UNCONFIGURED);
        notify_callback(TS_MGR_EVENT_UNCONFIGURED);
    }

    // Create update task
    BaseType_t task_ret = xTaskCreate(
        update_task,
        "ts_update",
        CONFIG_TS_MGR_TASK_STACK_SIZE,
        NULL,
        CONFIG_TS_MGR_TASK_PRIORITY,
        &s_ts_mgr.update_task
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create update task");
        if (s_ts_mgr.ml_handle) {
            microlink_deinit(s_ts_mgr.ml_handle);
            s_ts_mgr.ml_handle = NULL;
        }
        vSemaphoreDelete(s_ts_mgr.state_mutex);
        s_ts_mgr.state_mutex = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t start_microlink(void)
{
    microlink_config_t ml_config;
    microlink_get_default_config(&ml_config);
    ml_config.auth_key = s_ts_mgr.auth_key;
    ml_config.device_name = s_ts_mgr.device_name;

    s_ts_mgr.ml_handle = microlink_init(&ml_config);
    if (s_ts_mgr.ml_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MicroLink");
        return ESP_FAIL;
    }

    set_state(TS_MGR_STATE_CONNECTING);
    ESP_LOGI(TAG, "Connecting to Tailscale as '%s'...", s_ts_mgr.device_name);
    microlink_connect(s_ts_mgr.ml_handle);

    return ESP_OK;
}

static void update_task(void *arg)
{
    bool was_connected = false;

    while (!s_ts_mgr.stop_requested) {
        if (s_ts_mgr.ml_handle != NULL &&
            s_ts_mgr.state != TS_MGR_STATE_UNCONFIGURED) {

            microlink_update(s_ts_mgr.ml_handle);

            bool is_connected = microlink_is_connected(s_ts_mgr.ml_handle);

            xSemaphoreTake(s_ts_mgr.state_mutex, portMAX_DELAY);

            if (is_connected && !was_connected) {
                // Just connected
                s_ts_mgr.state = TS_MGR_STATE_CONNECTED;
                s_ts_mgr.reconnect_attempts = 0;
                s_ts_mgr.reconnect_delay_ms = CONFIG_TS_MGR_RECONNECT_INITIAL_MS;
                xSemaphoreGive(s_ts_mgr.state_mutex);

                ESP_LOGI(TAG, "State: %s -> CONNECTED", state_names[s_ts_mgr.state]);
                notify_callback(TS_MGR_EVENT_CONNECTED);
            } else if (!is_connected && was_connected) {
                // Just disconnected
                s_ts_mgr.state = TS_MGR_STATE_RECONNECTING;
                xSemaphoreGive(s_ts_mgr.state_mutex);

                ESP_LOGW(TAG, "State: CONNECTED -> RECONNECTING");
                notify_callback(TS_MGR_EVENT_DISCONNECTED);
                handle_reconnect();
            } else {
                xSemaphoreGive(s_ts_mgr.state_mutex);
            }

            was_connected = is_connected;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_TS_MGR_UPDATE_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

static void handle_reconnect(void)
{
    s_ts_mgr.reconnect_attempts++;

#if CONFIG_TS_MGR_RECONNECT_MAX_ATTEMPTS > 0
    if (s_ts_mgr.reconnect_attempts >= CONFIG_TS_MGR_RECONNECT_MAX_ATTEMPTS) {
        ESP_LOGW(TAG, "Max reconnection attempts (%d) reached",
                 CONFIG_TS_MGR_RECONNECT_MAX_ATTEMPTS);
        notify_callback(TS_MGR_EVENT_RECONNECT_EXHAUSTED);
        return;
    }
#endif

    ESP_LOGI(TAG, "Reconnecting in %lu ms (attempt %d)...",
             s_ts_mgr.reconnect_delay_ms, s_ts_mgr.reconnect_attempts);

    vTaskDelay(pdMS_TO_TICKS(s_ts_mgr.reconnect_delay_ms));

    // Exponential backoff
    s_ts_mgr.reconnect_delay_ms *= 2;
    if (s_ts_mgr.reconnect_delay_ms > CONFIG_TS_MGR_RECONNECT_MAX_MS) {
        s_ts_mgr.reconnect_delay_ms = CONFIG_TS_MGR_RECONNECT_MAX_MS;
    }

    if (s_ts_mgr.ml_handle) {
        set_state(TS_MGR_STATE_CONNECTING);
        microlink_connect(s_ts_mgr.ml_handle);
    }
}

static void set_state(ts_mgr_state_t new_state)
{
    xSemaphoreTake(s_ts_mgr.state_mutex, portMAX_DELAY);
    if (s_ts_mgr.state != new_state) {
        ESP_LOGI(TAG, "State: %s -> %s",
                 state_names[s_ts_mgr.state],
                 state_names[new_state]);
        s_ts_mgr.state = new_state;
    }
    xSemaphoreGive(s_ts_mgr.state_mutex);
}

static void notify_callback(ts_mgr_event_t event)
{
    if (s_ts_mgr.callback) {
        s_ts_mgr.callback(event, s_ts_mgr.callback_ctx);
    }
}

esp_err_t ts_mgr_stop(void)
{
    ESP_LOGI(TAG, "Stopping Tailscale manager...");

    s_ts_mgr.stop_requested = true;

    // Wait for task to stop
    if (s_ts_mgr.update_task) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_TS_MGR_UPDATE_INTERVAL_MS * 2));
        s_ts_mgr.update_task = NULL;
    }

    // Disconnect and deinit MicroLink
    if (s_ts_mgr.ml_handle) {
        microlink_disconnect(s_ts_mgr.ml_handle);
        microlink_deinit(s_ts_mgr.ml_handle);
        s_ts_mgr.ml_handle = NULL;
    }

    // Clean up mutex
    if (s_ts_mgr.state_mutex) {
        vSemaphoreDelete(s_ts_mgr.state_mutex);
        s_ts_mgr.state_mutex = NULL;
    }

    s_ts_mgr.state = TS_MGR_STATE_IDLE;
    memset(s_ts_mgr.auth_key, 0, sizeof(s_ts_mgr.auth_key));

    return ESP_OK;
}

bool ts_mgr_is_connected(void)
{
    return s_ts_mgr.state == TS_MGR_STATE_CONNECTED;
}

bool ts_mgr_is_configured(void)
{
    return ts_nvs_has_key();
}

const char* ts_mgr_get_state_name(void)
{
    return state_names[s_ts_mgr.state];
}

esp_err_t ts_mgr_get_ip(char *buf, size_t len)
{
    if (buf == NULL || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ts_mgr_is_connected() || s_ts_mgr.ml_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t vpn_ip = microlink_get_vpn_ip(s_ts_mgr.ml_handle);
    if (vpn_ip == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    microlink_vpn_ip_to_str(vpn_ip, buf);
    return ESP_OK;
}

esp_err_t ts_mgr_set_auth_key(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate key format
    if (strncmp(key, "tskey-auth-", 11) != 0) {
        ESP_LOGE(TAG, "Invalid auth key format (must start with 'tskey-auth-')");
        return ESP_ERR_INVALID_ARG;
    }

    // Store in NVS
    esp_err_t ret = ts_nvs_store_key(key);
    if (ret != ESP_OK) {
        return ret;
    }

    // Copy to internal buffer
    strncpy(s_ts_mgr.auth_key, key, sizeof(s_ts_mgr.auth_key) - 1);
    s_ts_mgr.auth_key[sizeof(s_ts_mgr.auth_key) - 1] = '\0';

    notify_callback(TS_MGR_EVENT_KEY_UPDATED);

    // If we were unconfigured, start connecting
    if (s_ts_mgr.state == TS_MGR_STATE_UNCONFIGURED) {
        ret = start_microlink();
    }

    return ret;
}

esp_err_t ts_mgr_clear_auth_key(void)
{
    esp_err_t ret = ts_nvs_clear_key();
    memset(s_ts_mgr.auth_key, 0, sizeof(s_ts_mgr.auth_key));

    if (s_ts_mgr.ml_handle) {
        microlink_disconnect(s_ts_mgr.ml_handle);
        microlink_deinit(s_ts_mgr.ml_handle);
        s_ts_mgr.ml_handle = NULL;
    }

    set_state(TS_MGR_STATE_UNCONFIGURED);
    notify_callback(TS_MGR_EVENT_UNCONFIGURED);

    return ret;
}

bool ts_mgr_has_auth_key(void)
{
    return ts_nvs_has_key();
}
