/**
 * @file sensor_manager.c
 * @brief Sensor manager implementation - see sensor_manager.h for API details
 */

#include "sensor_manager.h"
#include "dht22_driver.h"
#include "sw420_driver.h"
#include "button_driver.h"
#include "event_bus.h"
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

    // Handle-based drivers
    void *driver_handle;

    // Transition tracking (for digital sensors)
    bool last_bool_value;
    uint32_t last_transition_ms;
    uint32_t transition_count;
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

static void sensor_task(void *arg);
static esp_err_t load_config_from_nvs(void);
static esp_err_t init_default_sensor(void);
static uint32_t calculate_backoff(uint8_t fail_count, uint32_t base_interval);

static void button_event_handler(int button_id, button_press_t type, void *ctx)
{
    (void)ctx;
    uint8_t press;
    switch (type) {
    case BUTTON_PRESS_SHORT:     press = EVENT_PRESS_SHORT;     break;
    case BUTTON_PRESS_LONG:      press = EVENT_PRESS_LONG;      break;
    case BUTTON_PRESS_VERY_LONG: press = EVENT_PRESS_VERY_LONG; break;
    default: return;
    }
    event_notify_t event = {
        .type = EVENT_NOTIFY_BUTTON,
        .button = { .button_id = (uint8_t)button_id, .press = press },
    };
    event_bus_notify(&event);
}

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
        } else if (strcmp(sensor->driver, "sw420") == 0) {
            sw420_handle_t handle;
            err = sw420_driver_create(CONFIG_SW420_DEFAULT_GPIO, &handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "SW420 init failed for '%s': %s",
                         sensor->id, esp_err_to_name(err));
                if (err == ESP_ERR_INVALID_ARG) {
                    ESP_LOGE(TAG, "Check CONFIG_SW420_DEFAULT_GPIO value (%d)",
                             CONFIG_SW420_DEFAULT_GPIO);
                }
                sensor->status = SENSOR_STATUS_ERROR;
            } else {
                sensor->driver_handle = handle;
                sensor->status = SENSOR_STATUS_OFFLINE;
                sensor->last_bool_value = false;
                sensor->last_transition_ms = 0;
                sensor->transition_count = 0;
                ESP_LOGI(TAG, "SW420 driver initialized for '%s'", sensor->id);
            }
        } else {
            ESP_LOGW(TAG, "Unknown driver '%s' for sensor '%s'",
                     sensor->driver, sensor->id);
            sensor->status = SENSOR_STATUS_ERROR;
        }
    }

    // Register buttons
    button_driver_init();

    button_config_t prg_cfg = {
        .gpio = CONFIG_BUTTON_DRIVER_PRG_GPIO,
#ifdef CONFIG_BUTTON_DRIVER_PRG_ACTIVE_HIGH
        .active_high = true,
#endif
        .mode = BUTTON_MODE_MOMENTARY,
        .debounce_ms = CONFIG_BUTTON_DRIVER_DEBOUNCE_MS,
        .long_press_ms = CONFIG_BUTTON_DRIVER_LONG_PRESS_MS,
        .very_long_press_ms = CONFIG_BUTTON_DRIVER_VERY_LONG_PRESS_MS,
        .callback = button_event_handler,
        .ctx = NULL,
    };
    int prg_id = button_driver_register(&prg_cfg);
    if (prg_id < 0) {
        ESP_LOGW(TAG, "Failed to register PRG button");
    } else {
        ESP_LOGI(TAG, "PRG button registered (id=%d, GPIO%d)",
                 prg_id, CONFIG_BUTTON_DRIVER_PRG_GPIO);
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Sensor manager initialized with %zu sensor(s)", s_ctx.sensor_count);
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
        if (strcmp(sensor->driver, "sw420") == 0 && sensor->driver_handle != NULL) {
            esp_err_t err = sw420_driver_destroy(sensor->driver_handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SW420 deinit failed: %s", esp_err_to_name(err));
            }
            sensor->driver_handle = NULL;
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

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for get_status");
        return SENSOR_STATUS_ERROR;
    }

    sensor_status_t result = SENSOR_STATUS_ERROR;
    for (size_t i = 0; i < s_ctx.sensor_count; i++) {
        if (strcmp(s_ctx.sensors[i].id, sensor_id) == 0) {
            result = s_ctx.sensors[i].status;
            break;
        }
    }

    xSemaphoreGive(s_ctx.mutex);
    return result;
}

size_t sensor_manager_get_sensor_count(void)
{
    return s_ctx.sensor_count;
}

esp_err_t sensor_manager_get_reading_by_index(size_t index, sensor_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index >= s_ctx.sensor_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = s_ctx.sensors[index].last_reading;

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

const char *sensor_manager_get_sensor_id(size_t index)
{
    // No mutex: sensor IDs are immutable after init. If hot-add/remove
    // is added in the future, this must take s_ctx.mutex.
    if (!s_ctx.initialized || index >= s_ctx.sensor_count) {
        return NULL;
    }
    return s_ctx.sensors[index].id;
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
    size_t count = 0;

    // Add default DHT22 sensor using Kconfig values
    sensor_instance_t *sensor = &s_ctx.sensors[count];
    strncpy(sensor->id, "temp_humidity", sizeof(sensor->id) - 1);
    sensor->id[sizeof(sensor->id) - 1] = '\0';
    strncpy(sensor->driver, "dht22", sizeof(sensor->driver) - 1);
    sensor->driver[sizeof(sensor->driver) - 1] = '\0';
    sensor->poll_interval_ms = CONFIG_SENSOR_DEFAULT_POLL_MS;
    sensor->enabled = true;
    sensor->status = SENSOR_STATUS_OFFLINE;
    sensor->last_reading.value = NAN;
    sensor->last_reading.value2 = NAN;
    sensor->fail_count = 0;
    sensor->next_retry_ms = 0;
    count++;

#ifdef CONFIG_SW420_DRIVER_ENABLED
    // Add SW420 vibration sensor
    sensor = &s_ctx.sensors[count];
    strncpy(sensor->id, "vibration", sizeof(sensor->id) - 1);
    sensor->id[sizeof(sensor->id) - 1] = '\0';
    strncpy(sensor->driver, "sw420", sizeof(sensor->driver) - 1);
    sensor->driver[sizeof(sensor->driver) - 1] = '\0';
    sensor->poll_interval_ms = CONFIG_SENSOR_DEFAULT_POLL_MS;
    sensor->enabled = true;
    sensor->status = SENSOR_STATUS_OFFLINE;
    sensor->last_reading.value = NAN;
    sensor->last_reading.value2 = NAN;
    sensor->fail_count = 0;
    sensor->next_retry_ms = 0;
    sensor->driver_handle = NULL;
    sensor->last_bool_value = false;
    sensor->last_transition_ms = 0;
    sensor->transition_count = 0;
    count++;
#endif

    s_ctx.sensor_count = count;
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
            sensor_reading_t reading_copy = {0};
            sensor_callback_t cb = NULL;
            void *cb_user_data = NULL;

            if (strcmp(sensor->driver, "dht22") == 0) {
                err = dht22_driver_read(&temp, &humidity);

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

                        // Copy reading and callback info for invocation outside mutex
                        reading_copy = sensor->last_reading;
                        cb = s_ctx.callback;
                        cb_user_data = s_ctx.callback_user_data;
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

                    // Invoke callback outside mutex to prevent deadlock
                    if (cb != NULL) {
                        cb(&reading_copy, cb_user_data);
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to acquire mutex after reading sensor '%s' "
                             "(result: %s)", sensor->id, esp_err_to_name(err));
                }
            } else if (strcmp(sensor->driver, "sw420") == 0) {
                bool vibrating;
                err = sw420_driver_read(sensor->driver_handle, &vibrating);

                if (err == ESP_OK) {
                    float current_value = vibrating ? 1.0f : 0.0f;

                    // Detect transition
                    if ((current_value > 0.5f) != sensor->last_bool_value) {
                        sensor->last_transition_ms = now_ms;
                        sensor->transition_count++;
                        sensor->last_bool_value = (current_value > 0.5f);
                        ESP_LOGD(TAG, "Sensor '%s' transition #%lu: %s",
                                 sensor->id,
                                 (unsigned long)sensor->transition_count,
                                 vibrating ? "VIBRATING" : "IDLE");
                    }

                    // Calculate duration (guard against underflow if last_transition_ms not yet set)
                    uint32_t duration_ms = 0;
                    if (sensor->last_transition_ms > 0 && now_ms >= sensor->last_transition_ms) {
                        duration_ms = now_ms - sensor->last_transition_ms;
                    }

                    // Update with mutex
                    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                        sensor->status = SENSOR_STATUS_ONLINE;
                        sensor->fail_count = 0;
                        sensor->last_reading.sensor_id = sensor->id;
                        sensor->last_reading.timestamp_ms = now_ms;
                        sensor->last_reading.value = current_value;
                        sensor->last_reading.value2 = (float)duration_ms;
                        sensor->last_reading.status = SENSOR_STATUS_ONLINE;

                        reading_copy = sensor->last_reading;
                        cb = s_ctx.callback;
                        cb_user_data = s_ctx.callback_user_data;
                        xSemaphoreGive(s_ctx.mutex);

                        if (cb != NULL) {
                            cb(&reading_copy, cb_user_data);
                        }
                    } else {
                        ESP_LOGW(TAG, "Mutex timeout updating '%s' after successful read",
                                 sensor->id);
                    }
                } else {
                    // Handle read error - must use mutex like dht22 path
                    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                        sensor->fail_count++;
                        if (sensor->status == SENSOR_STATUS_ONLINE) {
                            ESP_LOGW(TAG, "Sensor '%s' went offline: %s",
                                     sensor->id, esp_err_to_name(err));
                        }
                        sensor->status = SENSOR_STATUS_OFFLINE;

                        // Calculate backoff for retry
                        sensor->next_retry_ms = now_ms +
                            calculate_backoff(sensor->fail_count, sensor->poll_interval_ms);

                        ESP_LOGD(TAG, "Sensor '%s' fail_count=%d, next_retry in %lu ms",
                                 sensor->id, sensor->fail_count,
                                 (unsigned long)(sensor->next_retry_ms - now_ms));

                        xSemaphoreGive(s_ctx.mutex);
                    } else {
                        ESP_LOGW(TAG, "Mutex timeout updating '%s' status", sensor->id);
                    }
                }
            }
        }

        // Wait for next poll cycle
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SENSOR_DEFAULT_POLL_MS));
    }

    ESP_LOGI(TAG, "Sensor task exiting");
    s_ctx.task = NULL;
    vTaskDelete(NULL);
}
