/**
 * @file display_hal.c
 * @brief u8g2 HAL callbacks for ESP-IDF I2C communication
 *
 * Provides the byte-level I2C callback and GPIO/delay callback that
 * u8g2 requires to communicate with the SSD1306 over I2C.
 *
 * Uses the legacy I2C driver (i2c_cmd_link) which u8g2 HAL ports
 * traditionally use. The new I2C master API isn't well-suited to
 * u8g2's byte-at-a-time callback model.
 */

#include "sdkconfig.h"

#if CONFIG_STATUS_DISPLAY_ENABLED

#include "display_hal.h"
#include "u8g2.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_hal";

// I2C command handle for current transaction
static i2c_cmd_handle_t s_i2c_handle = NULL;

/**
 * @brief u8g2 I2C byte callback for ESP-IDF
 */
uint8_t display_hal_i2c_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)arg_ptr;

    switch (msg) {
    case U8X8_MSG_BYTE_INIT:
        // I2C already initialized in display_hal_init()
        break;

    case U8X8_MSG_BYTE_SET_DC:
        // SSD1306 I2C doesn't use D/C pin — handled by control byte
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        s_i2c_handle = i2c_cmd_link_create();
        if (s_i2c_handle == NULL) {
            ESP_LOGE(TAG, "i2c_cmd_link_create failed (OOM)");
            return 0;
        }
        esp_err_t start_ret = i2c_master_start(s_i2c_handle);
        if (start_ret != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_start failed: %s", esp_err_to_name(start_ret));
        }
        start_ret = i2c_master_write_byte(s_i2c_handle, u8x8_GetI2CAddress(u8x8), true);
        if (start_ret != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_write_byte (addr) failed: %s", esp_err_to_name(start_ret));
        }
        break;

    case U8X8_MSG_BYTE_SEND: {
        if (s_i2c_handle == NULL) {
            return 0;  // OOM during START_TRANSFER — u8g2 still calls SEND
        }
        esp_err_t send_ret = i2c_master_write(s_i2c_handle, (uint8_t *)arg_ptr, arg_int, true);
        if (send_ret != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_write failed: %s", esp_err_to_name(send_ret));
        }
        break;
    }

    case U8X8_MSG_BYTE_END_TRANSFER: {
        if (s_i2c_handle == NULL) {
            return 0;  // OOM during START_TRANSFER — nothing to end
        }
        i2c_master_stop(s_i2c_handle);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, s_i2c_handle, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(s_i2c_handle);
        s_i2c_handle = NULL;
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2C transfer failed: %s", esp_err_to_name(ret));
            return 0;
        }
        break;
    }

    default:
        return 1;  // Return 1 = "handled" for unrecognized I2C messages
    }

    return 1;
}

/**
 * @brief u8g2 GPIO and delay callback for ESP-IDF
 */
uint8_t display_hal_gpio_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        // GPIO already initialized in display_hal_init()
        break;

    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(arg_int));
        break;

    case U8X8_MSG_DELAY_10MICRO:
        esp_rom_delay_us(arg_int * 10);
        break;

    case U8X8_MSG_DELAY_100NANO:
        esp_rom_delay_us(1);
        break;

    case U8X8_MSG_GPIO_RESET:
#if CONFIG_STATUS_DISPLAY_RST_GPIO >= 0
    {
        esp_err_t rst_ret = gpio_set_level((gpio_num_t)CONFIG_STATUS_DISPLAY_RST_GPIO, arg_int);
        if (rst_ret != ESP_OK) {
            ESP_LOGW(TAG, "RST gpio_set_level(%d) failed: %s", arg_int, esp_err_to_name(rst_ret));
        }
    }
#endif
        break;

    default:
        return 1;  // Return 1 for unhandled GPIO/delay messages (software I2C not used)
    }

    return 1;
}

/**
 * @brief Initialize I2C bus and reset GPIO for the display
 */
esp_err_t display_hal_init(void)
{
    ESP_LOGI(TAG, "Initializing display HAL (SDA=%d, SCL=%d, RST=%d)",
             CONFIG_STATUS_DISPLAY_I2C_SDA,
             CONFIG_STATUS_DISPLAY_I2C_SCL,
             CONFIG_STATUS_DISPLAY_RST_GPIO);

    // Configure I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_STATUS_DISPLAY_I2C_SDA,
        .scl_io_num = CONFIG_STATUS_DISPLAY_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure reset GPIO
#if CONFIG_STATUS_DISPLAY_RST_GPIO >= 0
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_STATUS_DISPLAY_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RST GPIO %d config failed: %s — display may not init correctly",
                 CONFIG_STATUS_DISPLAY_RST_GPIO, esp_err_to_name(ret));
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }
#endif

    ESP_LOGI(TAG, "Display HAL initialized");
    return ESP_OK;
}

/**
 * @brief Deinitialize the I2C bus
 */
esp_err_t display_hal_deinit(void)
{
    esp_err_t ret = i2c_driver_delete(I2C_NUM_0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C driver delete failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */
