/**
 * @file dht22_driver.c
 * @brief Implementation of DHT22 driver - see dht22_driver.h for API details
 */

#include "dht22_driver.h"
#include "am2302_rmt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "dht22_driver";

/** DHT22 valid temperature range in Celsius */
#define DHT22_TEMP_MIN_C  (-40.0f)
#define DHT22_TEMP_MAX_C  (80.0f)

/** DHT22 valid humidity range in percent */
#define DHT22_HUMIDITY_MIN_PCT  (0.0f)
#define DHT22_HUMIDITY_MAX_PCT  (100.0f)

/**
 * @brief Internal driver context
 */
typedef struct {
    SemaphoreHandle_t mutex;  /**< Thread safety mutex */
    am2302_handle_t handle;   /**< am2302_rmt sensor handle */
    bool initialized;         /**< Initialization state */
} dht22_driver_ctx_t;

static dht22_driver_ctx_t s_ctx = {0};

/**
 * @brief Convert Celsius to Fahrenheit
 */
static inline float celsius_to_fahrenheit(float temp_c)
{
    return (temp_c * 9.0f / 5.0f) + 32.0f;
}

/**
 * @brief Validate sensor reading is within DHT22 spec
 */
static bool is_reading_valid(float temp_c, float humidity)
{
    return (temp_c >= DHT22_TEMP_MIN_C && temp_c <= DHT22_TEMP_MAX_C &&
            humidity >= DHT22_HUMIDITY_MIN_PCT && humidity <= DHT22_HUMIDITY_MAX_PCT);
}

esp_err_t dht22_driver_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGE(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing DHT22 driver on GPIO %d", CONFIG_DHT22_DEFAULT_GPIO);

    // Create mutex first
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    am2302_config_t am2302_config = {
        .gpio_num = CONFIG_DHT22_DEFAULT_GPIO,
    };

    am2302_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
    };

    esp_err_t err = am2302_new_sensor_rmt(&am2302_config, &rmt_config, &s_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sensor: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return err;  // Propagate original error
    }

    if (s_ctx.handle == NULL) {
        ESP_LOGE(TAG, "Sensor handle is NULL after creation");
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ESP_FAIL;
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "DHT22 driver initialized");
    return ESP_OK;
}

esp_err_t dht22_driver_deinit(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing DHT22 driver");

    // Take mutex before cleanup
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for deinit");
        return ESP_ERR_TIMEOUT;
    }

    // Clear initialized flag FIRST to prevent concurrent access
    s_ctx.initialized = false;

    esp_err_t err = am2302_del_sensor(s_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete sensor - resources may be leaked: %s",
                 esp_err_to_name(err));
        s_ctx.handle = NULL;
        xSemaphoreGive(s_ctx.mutex);
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return err;  // Report actual failure
    }

    s_ctx.handle = NULL;
    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "DHT22 driver deinitialized");
    return ESP_OK;
}

esp_err_t dht22_driver_read(float *temperature_f, float *humidity_pct)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (temperature_f == NULL && humidity_pct == NULL) {
        ESP_LOGE(TAG, "Both output pointers are NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Acquire mutex for thread-safe read
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for read");
        return ESP_ERR_TIMEOUT;
    }

    // Double-check initialized state after acquiring mutex
    if (!s_ctx.initialized) {
        xSemaphoreGive(s_ctx.mutex);
        ESP_LOGE(TAG, "Driver deinitialized while waiting for mutex");
        return ESP_ERR_INVALID_STATE;
    }

    float temp_c = 0.0f;
    float humidity = 0.0f;

    esp_err_t err = am2302_read_temp_humi(s_ctx.handle, &temp_c, &humidity);
    if (err != ESP_OK) {
        xSemaphoreGive(s_ctx.mutex);
        ESP_LOGE(TAG, "Failed to read sensor: %s", esp_err_to_name(err));
        return err;
    }

    // Validate reading is within DHT22 spec
    if (!is_reading_valid(temp_c, humidity)) {
        xSemaphoreGive(s_ctx.mutex);
        ESP_LOGE(TAG, "Sensor returned out-of-range values: temp=%.1f C, humidity=%.1f%%",
                 temp_c, humidity);
        return ESP_ERR_INVALID_RESPONSE;
    }

    xSemaphoreGive(s_ctx.mutex);

    if (temperature_f != NULL) {
        *temperature_f = celsius_to_fahrenheit(temp_c);
    }

    if (humidity_pct != NULL) {
        *humidity_pct = humidity;
    }

    ESP_LOGD(TAG, "Read: %.1f F, %.1f%% RH",
             temperature_f ? *temperature_f : celsius_to_fahrenheit(temp_c),
             humidity_pct ? *humidity_pct : humidity);

    return ESP_OK;
}

bool dht22_driver_is_initialized(void)
{
    return s_ctx.initialized;
}
