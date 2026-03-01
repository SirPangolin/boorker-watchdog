#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Field size constants for device_identity_t
#define DEVICE_ID_NODE_NAME_LEN     32  // boorker-XXXX (MAC-derived)
#define DEVICE_ID_WEB_PASSWORD_LEN  33  // Hardware RNG generated + null
#define DEVICE_ID_AP_PASSWORD_LEN   33  // Hardware RNG generated + null
#define DEVICE_ID_BLE_POP_LEN       7   // 6-digit PIN + null
#define DEVICE_ID_NODE_SUFFIX_LEN   5   // Last 2 bytes of MAC as hex + null

/**
 * Device identity - unique credentials generated at first boot
 *
 * @note Thread Safety: Functions must be called from single task or
 *       externally synchronized. Typically called during initialization.
 */
typedef struct {
    char node_name[DEVICE_ID_NODE_NAME_LEN];
    char web_password[DEVICE_ID_WEB_PASSWORD_LEN];
    char ap_password[DEVICE_ID_AP_PASSWORD_LEN];
    char ble_pop[DEVICE_ID_BLE_POP_LEN];
    char node_suffix[DEVICE_ID_NODE_SUFFIX_LEN];
} device_identity_t;

/**
 * Initialize device identity
 * - Loads from NVS if exists
 * - Generates new credentials using hardware RNG if first boot
 *
 * @return ESP_OK on success
 */
esp_err_t device_identity_init(void);

/**
 * Get device identity (read-only)
 * Must call device_identity_init() first
 *
 * @return Pointer to identity struct (valid until deinit)
 */
const device_identity_t* device_identity_get(void);

/**
 * Check if this is first boot (credentials just generated)
 * Used to trigger QR code display on OLED
 *
 * @return true if credentials were just generated
 */
bool device_identity_is_first_boot(void);

/**
 * Mark first boot as acknowledged (user saw credentials)
 * Clears the first_boot flag in NVS
 *
 * @return ESP_OK on success
 */
esp_err_t device_identity_ack_first_boot(void);

/**
 * Regenerate all credentials (factory reset)
 *
 * @return ESP_OK on success
 */
esp_err_t device_identity_regenerate(void);

/**
 * Generate QR code data as JSON string
 *
 * @param buf Output buffer
 * @param buf_len Buffer size
 * @return ESP_OK on success
 */
esp_err_t device_identity_get_qr_json(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
