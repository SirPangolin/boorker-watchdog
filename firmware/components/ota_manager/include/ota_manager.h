/**
 * @file ota_manager.h
 * @brief OTA firmware update management via GitHub Releases and local upload
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Information about an available firmware update.
 */
typedef struct {
    char version[32];           /**< Semantic version string, e.g. "1.3.0" */
    char tag_name[48];          /**< Git tag name, e.g. "v1.3.0" */
    char download_url[256];     /**< Asset download URL */
    char sha256[65];            /**< Expected SHA-256 hash (hex string, 64 chars + NUL) */
    bool has_sha256;            /**< True if sha256 field contains a valid hash */
    char release_notes[512];    /**< Truncated release body / notes */
    bool is_prerelease;         /**< True if this is a pre-release */
    uint32_t size_bytes;        /**< Firmware binary size in bytes */
} ota_update_info_t;

/**
 * @brief Progress callback invoked during OTA download or upload.
 *
 * @param bytes_written Number of bytes written so far.
 * @param total_bytes   Total expected bytes (0 if unknown).
 * @param ctx           User-provided context pointer.
 */
typedef void (*ota_progress_cb_t)(uint32_t bytes_written, uint32_t total_bytes, void *ctx);

/**
 * @brief Initialize the OTA manager and start the background check task.
 *
 * Sets up internal state, reads NVS configuration (release channel, boot
 * counter), and spawns the periodic background task that checks GitHub
 * for new releases. Must be called from app_main() after NVS and
 * networking are available; not safe to call from multiple tasks.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_STATE if already initialized
 *  - ESP_ERR_NO_MEM if task creation failed
 */
esp_err_t ota_manager_init(void);

/**
 * @brief Trigger an immediate check for firmware updates.
 *
 * Queries the GitHub Releases API for the configured repository. If a
 * newer release is found, stores the update info internally and posts
 * an MOTD event via the event bus.
 *
 * @return
 *  - ESP_OK on success (regardless of whether an update was found)
 *  - ESP_ERR_INVALID_STATE if not initialized
 *  - ESP_FAIL on network or parsing error
 */
esp_err_t ota_manager_check_now(void);

/**
 * @brief Copy available update info into caller-provided buffer.
 *
 * Thread-safe: copies update data under lock so the caller owns the result.
 *
 * @param out  Destination buffer (must not be NULL).
 * @return
 *  - ESP_OK if an update was copied into @p out
 *  - ESP_ERR_NOT_FOUND if no update is available
 *  - ESP_ERR_INVALID_ARG if out is NULL
 *  - ESP_ERR_TIMEOUT if mutex could not be acquired
 */
esp_err_t ota_manager_get_available_update(ota_update_info_t *out);

/**
 * @brief Start downloading and flashing a firmware update from GitHub.
 *
 * Downloads the firmware binary from the URL in the most recent update
 * info, verifies the SHA-256 hash, and writes it to the next OTA
 * partition. On success, sets the new partition as the boot target.
 *
 * This is a blocking operation that runs on the calling task.
 *
 * @param cb  Optional progress callback (may be NULL).
 * @param ctx User context passed to the progress callback.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_STATE if no update available or already in progress
 *  - ESP_ERR_TIMEOUT if mutex could not be acquired
 *  - ESP_ERR_OTA_VALIDATE_FAILED if hash verification failed
 *  - ESP_FAIL on download or flash error
 */
esp_err_t ota_manager_start_update(ota_progress_cb_t cb, void *ctx);

/**
 * @brief Start a local firmware upload session (e.g., from the web UI).
 *
 * Prepares the next OTA partition for writing. Callers must then feed
 * data via ota_manager_write_upload_chunk() and finalize with
 * ota_manager_finish_upload().
 *
 * @param size            Expected total firmware size in bytes.
 * @param expected_sha256 Expected SHA-256 hex string (may be NULL to skip verification).
 * @param cb              Optional progress callback (may be NULL).
 * @param ctx             User context passed to the progress callback.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_STATE if an update is already in progress
 *  - ESP_ERR_INVALID_SIZE if firmware exceeds OTA partition capacity
 *  - ESP_ERR_NOT_FOUND if no OTA partition exists
 */
esp_err_t ota_manager_start_upload(uint32_t size, const char *expected_sha256,
                                   ota_progress_cb_t cb, void *ctx);

/**
 * @brief Write a chunk of firmware data during a local upload session.
 *
 * Must be called between ota_manager_start_upload() and
 * ota_manager_finish_upload().
 *
 * @param data Pointer to the data buffer.
 * @param len  Number of bytes in the buffer.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_STATE if no upload session is active
 *  - ESP_FAIL on flash write error
 */
esp_err_t ota_manager_write_upload_chunk(const void *data, size_t len);

/**
 * @brief Finalize a local upload session.
 *
 * Verifies the SHA-256 hash (if one was provided at session start),
 * validates the firmware image, and sets the new partition as the boot
 * target. A reboot is required to run the new firmware.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_STATE if no upload session is active
 *  - ESP_ERR_OTA_VALIDATE_FAILED if hash or image verification failed
 */
esp_err_t ota_manager_finish_upload(void);

/**
 * @brief Abort an in-progress OTA update or upload session.
 *
 * Cleans up internal state and releases the OTA partition handle.
 * Safe to call even if no operation is in progress.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_FAIL on cleanup error
 */
esp_err_t ota_manager_abort(void);

/**
 * @brief Mark the currently running firmware as valid.
 *
 * Should be called early during boot (after basic self-tests pass) to
 * confirm that this firmware is working. Calls
 * esp_ota_mark_app_valid_cancel_rollback() which prevents the bootloader
 * from rolling back on subsequent reboots.
 *
 * Safe to call even on factory-only (non-OTA) boots — returns ESP_OK
 * if no rollback state exists.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_FAIL on OTA API error
 */
esp_err_t ota_manager_mark_valid(void);

#ifdef __cplusplus
}
#endif

