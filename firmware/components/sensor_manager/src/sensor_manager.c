/**
 * @file sensor_manager.c
 * @brief Sensor manager implementation - see sensor_manager.h for API details
 */

#include "sensor_manager.h"
#include "dht22_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "sensor_manager";

/** Maximum backoff delay in milliseconds */
#define MAX_BACKOFF_MS  30000

/**
 * @brief Internal sensor instance
 */
typedef struct {
    char id[32];                   /**< Sensor ID */
    char driver[16];               /**< Driver name ("dht22") */
    uint32_t poll_interval_ms;     /**< Poll interval */
    bool enabled;                  /**< User enabled flag */
    sensor_status_t status;        /**< Runtime status */
    sensor_reading_t last_reading; /**< Cached reading */
    uint8_t fail_count;            /**< Consecutive failures */
    uint32_t next_retry_ms;        /**< Next retry time (uptime) */
} sensor_instance_t;

/**
 * @brief Manager context
 */
static struct {
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
    volatile bool stop_requested;
    bool initialized;

    sensor_instance_t sensors[CONFIG_SENSOR_MAX_COUNT];
    size_t sensor_count;

    sensor_callback_t callback;
    void *callback_user_data;
} s_ctx = {0};

// Forward declarations
static void sensor_task(void *arg);
static esp_err_t load_config_from_nvs(void);
static esp_err_t init_default_sensor(void);
static uint32_t calculate_backoff(uint8_t fail_count, uint32_t base_interval);

esp_err_t sensor_manager_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGE(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing sensor manager");

    // Create mutex
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Load config from NVS or use defaults
    esp_err_t err = load_config_from_nvs();
    if (err == ESP_ERR_NVS_NOT_FOUND || err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS config, using defaults");
        err = init_default_sensor();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config load failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return err;
    }

    // Initialize drivers for configured sensors
    for (size_t i = 0; i < s_ctx.sensor_count; i++) {
        sensor_instance_t *sensor = &s_ctx.sensors[i];
        if (!sensor->enabled) {
            sensor->status = SENSOR_STATUS_DISABLED;
            continue;
        }

        if (strcmp(sensor->driver, "dht22") == 0) {
            err = dht22_driver_init();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "DHT22 init failed for '%s': %s",
                         sensor->id, esp_err_to_name(err));
                sensor->status = SENSOR_STATUS_ERROR;
            } else {
                sensor->status = SENSOR_STATUS_OFFLINE; // Until first read
                ESP_LOGI(TAG, "DHT22 driver initialized for '%s'", sensor->id);
            }
        } else {
            ESP_LOGW(TAG, "Unknown driver '%s' for sensor '%s'",
                     sensor->driver, sensor->id);
            sensor->status = SENSOR_STATUS_ERROR;
        }
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Sensor manager initialized with %d sensor(s)", s_ctx.sensor_count);
    return ESP_OK;
}

esp_err_t sensor_manager_deinit(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing sensor manager");

    // Stop task if running
    if (s_ctx.task != NULL) {
        sensor_manager_stop();
    }

    // Deinit drivers
    for (size_t i = 0; i < s_ctx.sensor_count; i++) {
        sensor_instance_t *sensor = &s_ctx.sensors[i];
        if (strcmp(sensor->driver, "dht22") == 0 && dht22_driver_is_initialized()) {
            esp_err_t err = dht22_driver_deinit();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "DHT22 deinit failed: %s", esp_err_to_name(err));
            }
        }
    }

    // Cleanup
    if (s_ctx.mutex != NULL) {
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "Sensor manager deinitialized");
    return ESP_OK;
}

esp_err_t sensor_manager_start(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctx.task != NULL) {
        ESP_LOGE(TAG, "Already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.stop_requested = false;

    BaseType_t ret = xTaskCreate(
        sensor_task,
        "sensor_mgr",
        CONFIG_SENSOR_TASK_STACK_SIZE,
        NULL,
        CONFIG_SENSOR_TASK_PRIORITY,
        &s_ctx.task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sensor polling started (interval: %d ms)", CONFIG_SENSOR_DEFAULT_POLL_MS);
    return ESP_OK;
}

esp_err_t sensor_manager_stop(void)
{
    if (s_ctx.task == NULL) {
        ESP_LOGE(TAG, "Not running");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping sensor polling");
    s_ctx.stop_requested = true;

    // Wait for task to exit (5x normal timeout)
    for (int i = 0; i < 50 && s_ctx.task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (s_ctx.task != NULL) {
        ESP_LOGW(TAG, "Task did not exit cleanly, deleting");
        vTaskDelete(s_ctx.task);
        s_ctx.task = NULL;
    }

    ESP_LOGI(TAG, "Sensor polling stopped");
    return ESP_OK;
}

esp_err_t sensor_manager_register_callback(sensor_callback_t callback, void *user_data)
{
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.callback = callback;
    s_ctx.callback_user_data = user_data;

    xSemaphoreGive(s_ctx.mutex);
    ESP_LOGI(TAG, "Callback %s", callback ? "registered" : "unregistered");
    return ESP_OK;
}

esp_err_t sensor_manager_get_reading(const char *sensor_id, sensor_reading_t *out)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sensor_id == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < s_ctx.sensor_count; i++) {
        if (strcmp(s_ctx.sensors[i].id, sensor_id) == 0) {
            *out = s_ctx.sensors[i].last_reading;
            out->sensor_id = s_ctx.sensors[i].id; // Point to our copy
            xSemaphoreGive(s_ctx.mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

sensor_status_t sensor_manager_get_status(const char *sensor_id)
{
    if (!s_ctx.initialized || sensor_id == NULL) {
        return SENSOR_STATUS_ERROR;
    }

    for (size_t i = 0; i < s_ctx.sensor_count; i++) {
        if (strcmp(s_ctx.sensors[i].id, sensor_id) == 0) {
            return s_ctx.sensors[i].status;
        }
    }

    return SENSOR_STATUS_ERROR;
}

size_t sensor_manager_get_sensor_count(void)
{
    return s_ctx.sensor_count;
}

// --- Internal functions ---

static esp_err_t load_config_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sensors", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t count = 0;
    err = nvs_get_u8(handle, "sensor_count", &count);
    if (err != ESP_OK || count == 0) {
        nvs_close(handle);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    // TODO: Load each sensor config from NVS
    // For now, force defaults for initial implementation
    nvs_close(handle);
    return ESP_ERR_NVS_NOT_FOUND;
}

static esp_err_t init_default_sensor(void)
{
    // Add default DHT22 sensor using Kconfig values
    sensor_instance_t *sensor = &s_ctx.sensors[0];
    strncpy(sensor->id, "temp_humidity", sizeof(sensor->id) - 1);
    strncpy(sensor->driver, "dht22", sizeof(sensor->driver) - 1);
    sensor->poll_interval_ms = CONFIG_SENSOR_DEFAULT_POLL_MS;
    sensor->enabled = true;
    sensor->status = SENSOR_STATUS_OFFLINE;
    sensor->last_reading.value = NAN;
    sensor->last_reading.value2 = NAN;
    sensor->fail_count = 0;
    sensor->next_retry_ms = 0;

    s_ctx.sensor_count = 1;
    return ESP_OK;
}

/**
 * @brief Calculate backoff delay based on failure count
 *
 * Backoff schedule:
 * - 1-2 failures: normal poll interval
 * - 3-5 failures: 2x poll interval
 * - 6-10 failures: 5x poll interval
 * - 11+ failures: 30 seconds max
 */
static uint32_t calculate_backoff(uint8_t fail_count, uint32_t base_interval)
{
    if (fail_count <= 2) {
        return base_interval;
    } else if (fail_count <= 5) {
        return base_interval * 2;
    } else if (fail_count <= 10) {
        return base_interval * 5;
    } else {
        return MAX_BACKOFF_MS;
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Sensor task started");

    while (!s_ctx.stop_requested) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        for (size_t i = 0; i < s_ctx.sensor_count; i++) {
            sensor_instance_t *sensor = &s_ctx.sensors[i];

            // Skip disabled or error sensors
            if (sensor->status == SENSOR_STATUS_DISABLED ||
                sensor->status == SENSOR_STATUS_ERROR) {
                continue;
            }

            // Skip if not time to retry yet
            if (sensor->status == SENSOR_STATUS_OFFLINE &&
                now_ms < sensor->next_retry_ms) {
                continue;
            }

            // Attempt read
            float temp = 0, humidity = 0;
            esp_err_t err = ESP_FAIL;

            if (strcmp(sensor->driver, "dht22") == 0) {
                err = dht22_driver_read(&temp, &humidity);
            }

            if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (err == ESP_OK) {
                    // Success
                    if (sensor->status == SENSOR_STATUS_OFFLINE) {
                        ESP_LOGI(TAG, "Sensor '%s' came online", sensor->id);
                    }
                    sensor->status = SENSOR_STATUS_ONLINE;
                    sensor->fail_count = 0;
                    sensor->last_reading.sensor_id = sensor->id;
                    sensor->last_reading.timestamp_ms = now_ms;
                    sensor->last_reading.value = temp;
                    sensor->last_reading.value2 = humidity;
                    sensor->last_reading.status = SENSOR_STATUS_ONLINE;

                    // Invoke callback (still holding mutex for consistent state)
                    if (s_ctx.callback != NULL) {
                        s_ctx.callback(&sensor->last_reading, s_ctx.callback_user_data);
                    }
                } else {
                    // Failure
                    if (sensor->status == SENSOR_STATUS_ONLINE) {
                        ESP_LOGW(TAG, "Sensor '%s' went offline: %s",
                                 sensor->id, esp_err_to_name(err));
                    }
                    sensor->fail_count++;
                    sensor->status = SENSOR_STATUS_OFFLINE;
                    sensor->next_retry_ms = now_ms +
                        calculate_backoff(sensor->fail_count, sensor->poll_interval_ms);

                    ESP_LOGD(TAG, "Sensor '%s' fail_count=%d, next_retry in %lu ms",
                             sensor->id, sensor->fail_count,
                             (unsigned long)(sensor->next_retry_ms - now_ms));
                }
                xSemaphoreGive(s_ctx.mutex);
            }
        }

        // Wait for next poll cycle
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SENSOR_DEFAULT_POLL_MS));
    }

    ESP_LOGI(TAG, "Sensor task exiting");
    s_ctx.task = NULL;
    vTaskDelete(NULL);
}
