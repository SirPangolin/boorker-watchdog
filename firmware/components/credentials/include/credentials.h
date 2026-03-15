#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Field size constants for credentials_t
#define CRED_NODE_NAME_LEN     32  // boorker-XXXX (MAC-derived)
#define CRED_WEB_PASSWORD_LEN  33  // Hardware RNG generated + null
#define CRED_AP_PASSWORD_LEN   33  // Hardware RNG generated + null
#define CRED_BLE_POP_LEN       7   // 6-digit PIN + null
#define CRED_NODE_SUFFIX_LEN   5   // Last 2 bytes of MAC as hex + null

/**
 * Device credentials - unique secrets generated at first boot
 *
 * @note Thread Safety: Functions must be called from single task or
 *       externally synchronized. Typically called during initialization.
 */
typedef struct {
    char node_name[CRED_NODE_NAME_LEN];
    char web_password[CRED_WEB_PASSWORD_LEN];
    char ap_password[CRED_AP_PASSWORD_LEN];
    char ble_pop[CRED_BLE_POP_LEN];
    char node_suffix[CRED_NODE_SUFFIX_LEN];
} credentials_t;

/**
 * Initialize credentials
 * - Loads from NVS if exists
 * - Generates new credentials using hardware RNG if first boot
 *
 * @return ESP_OK on success
 */
esp_err_t credentials_init(void);

/**
 * Get credentials (read-only)
 * Must call credentials_init() first
 *
 * @return Pointer to credentials struct (valid until deinit)
 */
const credentials_t* credentials_get(void);

/**
 * Check if this is first boot (credentials just generated)
 * Used to trigger QR code display on OLED
 *
 * @return true if credentials were just generated
 */
bool credentials_is_first_boot(void);

/**
 * Mark first boot as acknowledged (user saw credentials)
 * Clears the first_boot flag in NVS
 *
 * @return ESP_OK on success
 */
esp_err_t credentials_ack_first_boot(void);

/**
 * Regenerate all credentials (factory reset)
 *
 * @return ESP_OK on success
 */
esp_err_t credentials_regenerate(void);

/**
 * Generate QR code data as JSON string
 *
 * @param buf Output buffer
 * @param buf_len Buffer size
 * @return ESP_OK on success
 */
esp_err_t credentials_get_qr_json(char *buf, size_t buf_len);

/**
 * Get TLS certificate PEM string (ECDSA P-256, self-signed).
 * Generated at first boot alongside other credentials.
 *
 * @return PEM string or NULL if not generated yet.
 */
const char *credentials_get_tls_cert(void);

/**
 * Get TLS private key PEM string (ECDSA P-256).
 *
 * @return PEM string or NULL if not generated yet.
 */
const char *credentials_get_tls_key(void);

#ifdef __cplusplus
}
#endif
