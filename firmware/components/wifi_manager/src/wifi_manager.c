#include "wifi_manager.h"
#include "wifi_prov_internal.h"
#include "wifi_mdns_internal.h"
#include "wifi_power_internal.h"
#include "system_state.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "wifi_manager";

// Internal state machine
typedef enum {
    WIFI_MGR_STATE_IDLE,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_PROVISIONING,
    WIFI_MGR_STATE_RECONNECTING,
} wifi_mgr_state_t;

// Max device name length (mDNS hostname limit)
#define WIFI_MGR_MAX_DEVICE_NAME_LEN 32

// Internal state
static struct {
    wifi_mgr_state_t state;
    SemaphoreHandle_t state_mutex;
    EventGroupHandle_t event_group;
    wifi_mgr_callback_t callback;
    void *callback_ctx;
    char device_name[WIFI_MGR_MAX_DEVICE_NAME_LEN]; // Owned copy, avoids lifetime hazard
    esp_netif_t *sta_netif;
    esp_timer_handle_t reconnect_timer;
    uint32_t reconnect_delay_ms;
    uint8_t reconnect_attempts;
    bool wifi_started;
    esp_event_handler_instance_t wifi_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    esp_event_handler_instance_t prov_event_instance;
} s_wifi_mgr = {
    .state = WIFI_MGR_STATE_IDLE,
    .reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_INITIAL_DELAY_MS,
    .reconnect_attempts = 0,
};

// Forward declarations
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
static void reconnect_timer_callback(void *arg);
static void set_state(wifi_mgr_state_t new_state);
static void notify_callback(wifi_mgr_event_t event);
static bool has_stored_credentials(void);

// State name lookup
static const char *state_names[] = {
    [WIFI_MGR_STATE_IDLE] = "IDLE",
    [WIFI_MGR_STATE_CONNECTING] = "CONNECTING",
    [WIFI_MGR_STATE_CONNECTED] = "CONNECTED",
    [WIFI_MGR_STATE_PROVISIONING] = "PROVISIONING",
    [WIFI_MGR_STATE_RECONNECTING] = "RECONNECTING",
};

static void prov_event_handler_internal(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Credentials received successfully");
            xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_PROVISIONED_BIT);
            notify_callback(WIFI_MGR_EVENT_PROVISIONED);
            break;

        case WIFI_PROV_END:
            // Provisioning ended, WiFi should auto-connect
            if (s_wifi_mgr.state == WIFI_MGR_STATE_PROVISIONING) {
                set_state(WIFI_MGR_STATE_CONNECTING);
            }
            break;

        default:
            break;
    }
}

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config)
{
    ESP_LOGI(TAG, "Initializing WiFi manager...");

    // Create state mutex for thread safety
    s_wifi_mgr.state_mutex = xSemaphoreCreateMutex();
    if (s_wifi_mgr.state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create event group
    s_wifi_mgr.event_group = xEventGroupCreate();
    if (s_wifi_mgr.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(s_wifi_mgr.state_mutex);
        s_wifi_mgr.state_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Store config - copy device_name to avoid lifetime hazard with caller's string
    if (config) {
        s_wifi_mgr.callback = config->callback;
        s_wifi_mgr.callback_ctx = config->callback_ctx;
        const char *name = config->device_name ? config->device_name
                                               : CONFIG_WIFI_MGR_DEVICE_NAME;
        strncpy(s_wifi_mgr.device_name, name, sizeof(s_wifi_mgr.device_name) - 1);
        s_wifi_mgr.device_name[sizeof(s_wifi_mgr.device_name) - 1] = '\0';
    } else {
        strncpy(s_wifi_mgr.device_name, CONFIG_WIFI_MGR_DEVICE_NAME,
                sizeof(s_wifi_mgr.device_name) - 1);
        s_wifi_mgr.device_name[sizeof(s_wifi_mgr.device_name) - 1] = '\0';
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi station
    s_wifi_mgr.sta_netif = esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &s_wifi_mgr.wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL,
        &s_wifi_mgr.ip_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler_internal, NULL,
        &s_wifi_mgr.prov_event_instance));

    // Set WiFi mode and start
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_mgr.wifi_started = true;

    // Create reconnect timer
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_wifi_mgr.reconnect_timer));

    // Check for stored credentials
    if (config && config->start_provisioning) {
        ESP_LOGI(TAG, "Force provisioning requested");
        return wifi_mgr_start_provisioning();
    }

    if (has_stored_credentials()) {
        ESP_LOGI(TAG, "Found stored credentials, connecting...");
        set_state(WIFI_MGR_STATE_CONNECTING);
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "No stored credentials, starting provisioning...");
        return wifi_mgr_start_provisioning();
    }

    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Disconnected from AP (reason: %d)", event->reason);

            // Clear WiFi state
            system_wifi_t wifi_clear = { .connected = false };
            system_state_set_wifi(&wifi_clear);

            // Stop mDNS since we're no longer connected
            wifi_mdns_stop();

            xEventGroupClearBits(s_wifi_mgr.event_group, WIFI_MGR_CONNECTED_BIT);
            xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_DISCONNECTED_BIT);

            if (s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTED ||
                s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTING) {
                set_state(WIFI_MGR_STATE_RECONNECTING);
                notify_callback(WIFI_MGR_EVENT_DISCONNECTED);

                // Start exponential backoff reconnection
                ESP_LOGI(TAG, "Reconnecting in %lu ms...", s_wifi_mgr.reconnect_delay_ms);
                esp_timer_stop(s_wifi_mgr.reconnect_timer);
                esp_timer_start_once(s_wifi_mgr.reconnect_timer,
                                     s_wifi_mgr.reconnect_delay_ms * 1000);
            }
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP, waiting for IP...");
            break;

        default:
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Reset reconnect state on successful connection
        s_wifi_mgr.reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_INITIAL_DELAY_MS;
        s_wifi_mgr.reconnect_attempts = 0;

        set_state(WIFI_MGR_STATE_CONNECTED);

        xEventGroupClearBits(s_wifi_mgr.event_group, WIFI_MGR_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_CONNECTED_BIT);

        // Start mDNS
        wifi_mdns_start(s_wifi_mgr.device_name);

        // Enable power save
        esp_err_t ps_ret = wifi_power_enable();
        if (ps_ret != ESP_OK) {
            ESP_LOGW(TAG, "Power save not enabled: %s", esp_err_to_name(ps_ret));
        }

        // Publish WiFi state
        wifi_ap_record_t ap;
        uint8_t mac[6];
        system_wifi_t wifi_state = { .connected = true };
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            strncpy(wifi_state.ssid, (const char *)ap.ssid, sizeof(wifi_state.ssid) - 1);
            wifi_state.rssi = ap.rssi;
        }
        snprintf(wifi_state.ip, sizeof(wifi_state.ip), IPSTR, IP2STR(&event->ip_info.ip));
        if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
            memcpy(wifi_state.mac, mac, 6);
        }
        system_state_set_wifi(&wifi_state);

        notify_callback(WIFI_MGR_EVENT_CONNECTED);
    }
}

static void reconnect_timer_callback(void *arg)
{
    s_wifi_mgr.reconnect_attempts++;

#if CONFIG_WIFI_MGR_RECONNECT_MAX_ATTEMPTS > 0
    if (s_wifi_mgr.reconnect_attempts >= CONFIG_WIFI_MGR_RECONNECT_MAX_ATTEMPTS) {
        ESP_LOGW(TAG, "Max reconnection attempts (%d) reached",
                 CONFIG_WIFI_MGR_RECONNECT_MAX_ATTEMPTS);
        notify_callback(WIFI_MGR_EVENT_RECONNECT_EXHAUSTED);
        // Stay in RECONNECTING state but stop retrying
        // Application can call wifi_mgr_start_provisioning() if desired
        return;
    }
#endif

    ESP_LOGI(TAG, "Attempting reconnection (attempt %d)...", s_wifi_mgr.reconnect_attempts);
    set_state(WIFI_MGR_STATE_CONNECTING);
    esp_wifi_connect();

    // Increase delay for next attempt (exponential backoff)
    s_wifi_mgr.reconnect_delay_ms *= 2;
    if (s_wifi_mgr.reconnect_delay_ms > CONFIG_WIFI_MGR_RECONNECT_MAX_DELAY_MS) {
        s_wifi_mgr.reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_MAX_DELAY_MS;
    }
}

static void set_state(wifi_mgr_state_t new_state)
{
    xSemaphoreTake(s_wifi_mgr.state_mutex, portMAX_DELAY);
    if (s_wifi_mgr.state != new_state) {
        ESP_LOGI(TAG, "State: %s -> %s",
                 state_names[s_wifi_mgr.state],
                 state_names[new_state]);
        s_wifi_mgr.state = new_state;
    }
    xSemaphoreGive(s_wifi_mgr.state_mutex);
}

static void notify_callback(wifi_mgr_event_t event)
{
    if (s_wifi_mgr.callback) {
        s_wifi_mgr.callback(event, s_wifi_mgr.callback_ctx);
    }
}

static bool has_stored_credentials(void)
{
    wifi_config_t wifi_config;
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        return false;
    }
    return wifi_config.sta.ssid[0] != '\0';
}

bool wifi_mgr_is_connected(void)
{
    return s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTED;
}

esp_err_t wifi_mgr_get_ip(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!wifi_mgr_is_connected() || !s_wifi_mgr.sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_wifi_mgr.sta_netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

const char* wifi_mgr_get_state_name(void)
{
    return state_names[s_wifi_mgr.state];
}

EventGroupHandle_t wifi_mgr_get_event_group(void)
{
    return s_wifi_mgr.event_group;
}

esp_err_t wifi_mgr_start_provisioning(void)
{
    // Stop any reconnection attempts
    if (s_wifi_mgr.reconnect_timer) {
        esp_timer_stop(s_wifi_mgr.reconnect_timer);
    }

    // Disconnect if connected
    if (s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTED) {
        esp_wifi_disconnect();
    }

    set_state(WIFI_MGR_STATE_PROVISIONING);
    notify_callback(WIFI_MGR_EVENT_PROVISIONING);

    return wifi_prov_start(s_wifi_mgr.device_name);
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    wifi_config_t wifi_config = {0};
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

esp_err_t wifi_mgr_stop(void)
{
    // Stop mDNS
    wifi_mdns_stop();

    // Stop provisioning if active
    wifi_prov_stop();

    if (s_wifi_mgr.reconnect_timer) {
        esp_timer_stop(s_wifi_mgr.reconnect_timer);
        esp_timer_delete(s_wifi_mgr.reconnect_timer);
        s_wifi_mgr.reconnect_timer = NULL;
    }

    // Unregister event handlers
    if (s_wifi_mgr.wifi_event_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
            s_wifi_mgr.wifi_event_instance);
        s_wifi_mgr.wifi_event_instance = NULL;
    }
    if (s_wifi_mgr.ip_event_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
            s_wifi_mgr.ip_event_instance);
        s_wifi_mgr.ip_event_instance = NULL;
    }
    if (s_wifi_mgr.prov_event_instance) {
        esp_event_handler_instance_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
            s_wifi_mgr.prov_event_instance);
        s_wifi_mgr.prov_event_instance = NULL;
    }

    // Stop and deinitialize WiFi
    if (s_wifi_mgr.wifi_started) {
        esp_wifi_stop();
        s_wifi_mgr.wifi_started = false;
    }
    esp_wifi_deinit();

    // Destroy network interface
    if (s_wifi_mgr.sta_netif) {
        esp_netif_destroy_default_wifi(s_wifi_mgr.sta_netif);
        s_wifi_mgr.sta_netif = NULL;
    }

    // Clean up synchronization primitives
    if (s_wifi_mgr.event_group) {
        vEventGroupDelete(s_wifi_mgr.event_group);
        s_wifi_mgr.event_group = NULL;
    }
    if (s_wifi_mgr.state_mutex) {
        vSemaphoreDelete(s_wifi_mgr.state_mutex);
        s_wifi_mgr.state_mutex = NULL;
    }

    s_wifi_mgr.state = WIFI_MGR_STATE_IDLE;
    return ESP_OK;
}
