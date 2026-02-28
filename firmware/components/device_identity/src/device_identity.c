#include "device_identity.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "device_id";

#define NVS_NAMESPACE "device_id"
#define NVS_KEY_WEB_PASS "web_pass"
#define NVS_KEY_AP_PASS "ap_pass"
#define NVS_KEY_BLE_POP "ble_pop"
#define NVS_KEY_FIRST_BOOT "first_boot"

static device_identity_t s_identity;
static bool s_initialized = false;
static bool s_first_boot = false;

// Characters for password generation (alphanumeric + some symbols)
static const char PASS_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789#$@!";
static const int PASS_CHARS_LEN = sizeof(PASS_CHARS) - 1;

static void generate_random_string(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t rand = esp_random();
        buf[i] = PASS_CHARS[rand % PASS_CHARS_LEN];
    }
    buf[len] = '\0';
}

static void generate_random_digits(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t rand = esp_random();
        buf[i] = '0' + (rand % 10);
    }
    buf[len] = '\0';
}

static void derive_node_name(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Node suffix from last 2 bytes of MAC
    snprintf(s_identity.node_suffix, sizeof(s_identity.node_suffix),
             "%02X%02X", mac[4], mac[5]);

    // Full node name
    snprintf(s_identity.node_name, sizeof(s_identity.node_name),
             "%s-%s", CONFIG_DEVICE_ID_NAME_PREFIX, s_identity.node_suffix);
}

static esp_err_t load_or_generate_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check if credentials exist
    size_t len = sizeof(s_identity.web_password);
    ret = nvs_get_str(handle, NVS_KEY_WEB_PASS, s_identity.web_password, &len);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // First boot - generate new credentials
        ESP_LOGI(TAG, "First boot - generating credentials with hardware RNG");
        s_first_boot = true;

        generate_random_string(s_identity.web_password, CONFIG_DEVICE_ID_WEB_PASS_LEN);
        generate_random_string(s_identity.ap_password, CONFIG_DEVICE_ID_AP_PASS_LEN);
        generate_random_digits(s_identity.ble_pop, 6);

        // Store in NVS
        nvs_set_str(handle, NVS_KEY_WEB_PASS, s_identity.web_password);
        nvs_set_str(handle, NVS_KEY_AP_PASS, s_identity.ap_password);
        nvs_set_str(handle, NVS_KEY_BLE_POP, s_identity.ble_pop);
        nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, 1);

        ret = nvs_commit(handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        }

        ESP_LOGI(TAG, "Credentials generated for %s", s_identity.node_name);
    } else if (ret == ESP_OK) {
        // Load existing credentials
        len = sizeof(s_identity.ap_password);
        nvs_get_str(handle, NVS_KEY_AP_PASS, s_identity.ap_password, &len);

        len = sizeof(s_identity.ble_pop);
        nvs_get_str(handle, NVS_KEY_BLE_POP, s_identity.ble_pop, &len);

        // Check if first boot was acknowledged
        uint8_t fb = 0;
        if (nvs_get_u8(handle, NVS_KEY_FIRST_BOOT, &fb) == ESP_OK && fb == 1) {
            s_first_boot = true;
        }

        ESP_LOGI(TAG, "Credentials loaded for %s", s_identity.node_name);
    } else {
        ESP_LOGE(TAG, "Failed to read NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

esp_err_t device_identity_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Derive node name from MAC (always deterministic)
    derive_node_name();

    // Load or generate credentials
    esp_err_t ret = load_or_generate_credentials();
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = true;

#if CONFIG_DEVICE_ID_LOG_CREDENTIALS
    // Display credentials on every boot until acknowledged
    if (s_first_boot) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "CREDENTIALS (save these!):");
        ESP_LOGI(TAG, "  Node: %s", s_identity.node_name);
        ESP_LOGI(TAG, "  Web Password: %s", s_identity.web_password);
        ESP_LOGI(TAG, "  AP Password: %s", s_identity.ap_password);
        ESP_LOGI(TAG, "  BLE PoP: %s", s_identity.ble_pop);
        ESP_LOGI(TAG, "========================================");
    }
#endif

    return ESP_OK;
}

const device_identity_t* device_identity_get(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return NULL;
    }
    return &s_identity;
}

bool device_identity_is_first_boot(void)
{
    return s_first_boot;
}

esp_err_t device_identity_ack_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, 0);
    ret = nvs_commit(handle);
    nvs_close(handle);

    s_first_boot = false;
    ESP_LOGI(TAG, "First boot acknowledged");
    return ret;
}

esp_err_t device_identity_regenerate(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // Erase all credentials
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    // Re-initialize (will generate new credentials)
    s_initialized = false;
    return device_identity_init();
}

esp_err_t device_identity_get_qr_json(char *buf, size_t buf_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = snprintf(buf, buf_len,
        "{"
        "\"name\":\"%s\","
        "\"web_pass\":\"%s\","
        "\"ap_pass\":\"%s\","
        "\"ble_pop\":\"%s\","
        "\"ble_name\":\"PROV_%s\","
        "\"setup_url\":\"http://192.168.4.1\""
        "}",
        s_identity.node_name,
        s_identity.web_password,
        s_identity.ap_password,
        s_identity.ble_pop,
        s_identity.node_suffix
    );

    if (written < 0 || (size_t)written >= buf_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
