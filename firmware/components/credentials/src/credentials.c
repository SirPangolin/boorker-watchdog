#include "credentials.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/ecp.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "cred";

#define NVS_NAMESPACE "cred"
#define NVS_KEY_WEB_PASS "web_pass"
#define NVS_KEY_AP_PASS "ap_pass"
#define NVS_KEY_BLE_POP "ble_pop"
#define NVS_KEY_FIRST_BOOT "first_boot"
#define NVS_KEY_TLS_CERT   "tls_cert"
#define NVS_KEY_TLS_KEY    "tls_key"

static credentials_t s_cred;
static bool s_initialized = false;
static bool s_first_boot = false;

// ECDSA P-256 cert/key PEM buffers (~800 bytes cert, ~230 bytes key)
static char s_tls_cert[1024];
static char s_tls_key[512];

// Characters for password generation (alphanumeric + some symbols)
static const char PASS_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789#$@!";
static const uint32_t PASS_CHARS_LEN = sizeof(PASS_CHARS) - 1;

static void generate_random_string(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        // Use multiply-and-shift to avoid modulo bias
        // index = (rand * PASS_CHARS_LEN) >> 32, uniformly distributed in [0, PASS_CHARS_LEN)
        uint32_t index = (uint32_t)(((uint64_t)esp_random() * PASS_CHARS_LEN) >> 32);
        buf[i] = PASS_CHARS[index];
    }
    buf[len] = '\0';
}

static void generate_random_digits(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        // Use multiply-and-shift for uniform distribution in [0, 10)
        uint32_t digit = (uint32_t)(((uint64_t)esp_random() * 10) >> 32);
        buf[i] = '0' + digit;
    }
    buf[len] = '\0';
}

static esp_err_t derive_node_name(void)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    // Node suffix from last 2 bytes of MAC
    snprintf(s_cred.node_suffix, sizeof(s_cred.node_suffix),
             "%02X%02X", mac[4], mac[5]);

    // Full node name
    snprintf(s_cred.node_name, sizeof(s_cred.node_name),
             "%s-%s", CONFIG_CRED_NAME_PREFIX, s_cred.node_suffix);

    return ESP_OK;
}

static esp_err_t generate_tls_cert(const char *cn)
{
    // Clear buffers first — prevents stale partial data if generation fails midway
    s_tls_cert[0] = '\0';
    s_tls_key[0] = '\0';

    int ret;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    unsigned char serial[8];
    char subject[64];

    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) goto cleanup;

    // Generate ECDSA P-256 key pair (<500ms on ESP32-S3 with HW acceleration)
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0) goto cleanup;
    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                               mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) goto cleanup;

    // Build self-signed X.509 certificate
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);

    snprintf(subject, sizeof(subject), "CN=%s", cn);
    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject);
    if (ret != 0) goto cleanup;
    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject);
    if (ret != 0) goto cleanup;

    // Random serial number
    ret = mbedtls_ctr_drbg_random(&ctr_drbg, serial, sizeof(serial));
    if (ret != 0) goto cleanup;
    ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
    if (ret != 0) goto cleanup;

    // No expiry (no NTP on device)
    ret = mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "99991231235959");
    if (ret != 0) goto cleanup;

    // Write cert PEM
    ret = mbedtls_x509write_crt_pem(&crt, (unsigned char *)s_tls_cert, sizeof(s_tls_cert),
                                     mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) goto cleanup;

    // Write key PEM
    ret = mbedtls_pk_write_key_pem(&key, (unsigned char *)s_tls_key, sizeof(s_tls_key));
    if (ret != 0) goto cleanup;

    ESP_LOGI(TAG, "TLS certificate generated (ECDSA P-256, CN=%s)", cn);

cleanup:
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);

    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "TLS cert generation failed: %s (-0x%04X)", err_buf, -ret);
        return ESP_FAIL;
    }
    return ESP_OK;
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
    size_t len = sizeof(s_cred.web_password);
    ret = nvs_get_str(handle, NVS_KEY_WEB_PASS, s_cred.web_password, &len);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // First boot - generate new credentials
        ESP_LOGI(TAG, "First boot - generating credentials with hardware RNG");
        s_first_boot = true;

        generate_random_string(s_cred.web_password, CONFIG_CRED_WEB_PASS_LEN);
        generate_random_string(s_cred.ap_password, CONFIG_CRED_AP_PASS_LEN);
        generate_random_digits(s_cred.ble_pop, 6);

        // Store in NVS
        ret = nvs_set_str(handle, NVS_KEY_WEB_PASS, s_cred.web_password);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set web_pass: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_set_str(handle, NVS_KEY_AP_PASS, s_cred.ap_password);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set ap_pass: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_set_str(handle, NVS_KEY_BLE_POP, s_cred.ble_pop);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set ble_pop: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set first_boot: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_commit(handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        // Generate TLS certificate (ECDSA P-256)
        esp_err_t tls_ret = generate_tls_cert(s_cred.node_name);
        if (tls_ret == ESP_OK) {
            esp_err_t w1 = nvs_set_str(handle, NVS_KEY_TLS_CERT, s_tls_cert);
            esp_err_t w2 = nvs_set_str(handle, NVS_KEY_TLS_KEY, s_tls_key);
            esp_err_t w3 = (w1 == ESP_OK && w2 == ESP_OK) ? nvs_commit(handle) : ESP_FAIL;
            if (w1 != ESP_OK || w2 != ESP_OK || w3 != ESP_OK) {
                ESP_LOGE(TAG, "Failed to persist TLS cert/key (cert=%s, key=%s, commit=%s)",
                         esp_err_to_name(w1), esp_err_to_name(w2), esp_err_to_name(w3));
                ESP_LOGE(TAG, "HTTPS will not survive reboot — check NVS partition size");
                s_tls_cert[0] = '\0';
                s_tls_key[0] = '\0';
            }
        } else {
            ESP_LOGE(TAG, "TLS cert generation failed — HTTPS will not be available");
        }

        ESP_LOGI(TAG, "Credentials generated for %s", s_cred.node_name);
    } else if (ret == ESP_OK) {
        // Load existing credentials
        len = sizeof(s_cred.ap_password);
        ret = nvs_get_str(handle, NVS_KEY_AP_PASS, s_cred.ap_password, &len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ap_pass: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        len = sizeof(s_cred.ble_pop);
        ret = nvs_get_str(handle, NVS_KEY_BLE_POP, s_cred.ble_pop, &len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ble_pop: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        // Validate loaded credentials - check for reasonable values
        bool valid = true;
        if (strlen(s_cred.web_password) < 8) {
            ESP_LOGW(TAG, "Loaded web_password too short (%d chars)", (int)strlen(s_cred.web_password));
            valid = false;
        }
        if (strlen(s_cred.ap_password) < 8) {
            ESP_LOGW(TAG, "Loaded ap_password too short (%d chars)", (int)strlen(s_cred.ap_password));
            valid = false;
        }
        if (strlen(s_cred.ble_pop) != 6) {
            ESP_LOGW(TAG, "Loaded ble_pop invalid length (%d chars, expected 6)",
                     (int)strlen(s_cred.ble_pop));
            valid = false;
        }
        // Check ble_pop contains only digits
        for (size_t i = 0; i < strlen(s_cred.ble_pop) && valid; i++) {
            if (s_cred.ble_pop[i] < '0' || s_cred.ble_pop[i] > '9') {
                ESP_LOGW(TAG, "Loaded ble_pop contains non-digit character");
                valid = false;
            }
        }

        if (!valid) {
            ESP_LOGE(TAG, "Credential validation failed - regenerating");
            nvs_close(handle);
            // Clear invalid data and regenerate
            s_initialized = false;
            return credentials_regenerate();
        }

        // Check if first boot was acknowledged
        uint8_t fb = 0;
        if (nvs_get_u8(handle, NVS_KEY_FIRST_BOOT, &fb) == ESP_OK && fb == 1) {
            s_first_boot = true;
        }

        // Load TLS cert/key (non-fatal if missing — HTTPS just won't start)
        size_t cert_len = sizeof(s_tls_cert);
        if (nvs_get_str(handle, NVS_KEY_TLS_CERT, s_tls_cert, &cert_len) != ESP_OK) {
            ESP_LOGW(TAG, "TLS cert not found in NVS partition");
            s_tls_cert[0] = '\0';
        }
        size_t key_len = sizeof(s_tls_key);
        if (nvs_get_str(handle, NVS_KEY_TLS_KEY, s_tls_key, &key_len) != ESP_OK) {
            ESP_LOGW(TAG, "TLS key not found in NVS partition");
            s_tls_key[0] = '\0';
        }

        ESP_LOGI(TAG, "Credentials loaded and validated for %s", s_cred.node_name);
        ret = ESP_OK;  // Ensure success return
    } else {
        ESP_LOGE(TAG, "Failed to read NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

esp_err_t credentials_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Derive node name from MAC (always deterministic)
    esp_err_t ret = derive_node_name();
    if (ret != ESP_OK) {
        return ret;
    }

    // Load or generate credentials
    ret = load_or_generate_credentials();
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = true;

#if CONFIG_CRED_LOG_CREDENTIALS
    // Display credentials on every boot until acknowledged
    if (s_first_boot) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "CREDENTIALS (save these!):");
        ESP_LOGI(TAG, "  Node: %s", s_cred.node_name);
        ESP_LOGI(TAG, "  Web Password: %s", s_cred.web_password);
        ESP_LOGI(TAG, "  AP Password: %s", s_cred.ap_password);
        ESP_LOGI(TAG, "  BLE PoP: %s", s_cred.ble_pop);
        ESP_LOGI(TAG, "========================================");
    }
#endif

    return ESP_OK;
}

const credentials_t* credentials_get(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return NULL;
    }
    return &s_cred;
}

bool credentials_is_first_boot(void)
{
    return s_first_boot;
}

esp_err_t credentials_ack_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for ack: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set first_boot=0: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit first_boot ack: %s", esp_err_to_name(ret));
    }
    nvs_close(handle);

    s_first_boot = false;
    ESP_LOGI(TAG, "First boot acknowledged");
    return ret;
}

esp_err_t credentials_regenerate(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for regenerate: %s", esp_err_to_name(ret));
        return ret;
    }

    // Erase all credentials
    ret = nvs_erase_all(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    nvs_close(handle);

    // Scrub sensitive material before regeneration
    memset(s_tls_key, 0, sizeof(s_tls_key));
    memset(s_tls_cert, 0, sizeof(s_tls_cert));

    // Re-initialize (will generate new credentials)
    s_initialized = false;
    return credentials_init();
}

const char *credentials_get_tls_cert(void)
{
    return s_tls_cert[0] != '\0' ? s_tls_cert : NULL;
}

const char *credentials_get_tls_key(void)
{
    return s_tls_key[0] != '\0' ? s_tls_key : NULL;
}

esp_err_t credentials_get_qr_json(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

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
        s_cred.node_name,
        s_cred.web_password,
        s_cred.ap_password,
        s_cred.ble_pop,
        s_cred.node_suffix
    );

    if (written < 0 || (size_t)written >= buf_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
