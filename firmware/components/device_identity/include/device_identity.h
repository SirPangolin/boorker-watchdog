#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device identity - unique credentials generated at first boot
 */
typedef struct {
    char node_name[32];       // boorker-XXXX (MAC-derived)
    char web_password[33];    // Hardware RNG generated
    char ap_password[33];     // Hardware RNG generated
    char ble_pop[7];          // 6-digit PIN
    char node_suffix[5];      // Last 2 bytes of MAC as hex
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
