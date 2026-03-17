/**
 * @file display_driver.c
 * @brief SSD1306 OLED display HAL via ESP-IDF esp_lcd framework
 *
 * Uses the new I2C master API (i2c_new_master_bus) and esp_lcd_panel_ssd1306
 * for hardware abstraction. Manages a page-mode framebuffer (1KB for 128x64).
 */

#include "display_driver.h"
#include "sdkconfig.h"

#if CONFIG_DISPLAY_DRIVER_ENABLED

#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "display_driver";

/**
 * @brief Internal driver context
 */
typedef struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel;
    uint8_t framebuffer[DISPLAY_BUF_SIZE];
} display_driver_ctx_t;

static display_driver_ctx_t s_ctx = {0};

/**
 * @brief Hardware reset the display via RST GPIO
 */
static esp_err_t reset_display(void)
{
#if CONFIG_DISPLAY_DRIVER_RST_GPIO >= 0
    gpio_num_t rst = (gpio_num_t)CONFIG_DISPLAY_DRIVER_RST_GPIO;

    esp_err_t ret = gpio_set_direction(rst, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RST GPIO %d direction failed: %s", rst, esp_err_to_name(ret));
        return ret;
    }

    // Reset sequence: HIGH → LOW (10ms) → HIGH (10ms wait)
    gpio_set_level(rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGD(TAG, "Display reset via GPIO %d", rst);
#endif
    return ESP_OK;
}

esp_err_t display_driver_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing display driver (SSD1306 %dx%d, I2C port %d, SDA=%d, SCL=%d)",
             DISPLAY_WIDTH, DISPLAY_HEIGHT,
             CONFIG_DISPLAY_DRIVER_I2C_PORT,
             CONFIG_DISPLAY_DRIVER_I2C_SDA,
             CONFIG_DISPLAY_DRIVER_I2C_SCL);

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Hardware reset before I2C init
    esp_err_t ret = reset_display();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display reset failed (continuing): %s", esp_err_to_name(ret));
    }

    // Initialize I2C master bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = CONFIG_DISPLAY_DRIVER_I2C_PORT,
        .sda_io_num = CONFIG_DISPLAY_DRIVER_I2C_SDA,
        .scl_io_num = CONFIG_DISPLAY_DRIVER_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&bus_config, &s_ctx.i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus init failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    // Create I2C panel IO for SSD1306
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = CONFIG_DISPLAY_DRIVER_I2C_ADDR,
        .scl_speed_hz = CONFIG_DISPLAY_DRIVER_I2C_FREQ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_i2c(s_ctx.i2c_bus, &io_config, &s_ctx.io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel IO init failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_ctx.i2c_bus);
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    // Create SSD1306 panel
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = DISPLAY_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,  // We handle reset manually above
        .vendor_config = &ssd1306_config,
    };
    ret = esp_lcd_new_panel_ssd1306(s_ctx.io_handle, &panel_config, &s_ctx.panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 panel init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(s_ctx.io_handle);
        i2c_del_master_bus(s_ctx.i2c_bus);
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    // Initialize panel
    ret = esp_lcd_panel_reset(s_ctx.panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
    }

    ret = esp_lcd_panel_init(s_ctx.panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(s_ctx.panel);
        esp_lcd_panel_io_del(s_ctx.io_handle);
        i2c_del_master_bus(s_ctx.i2c_bus);
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    // SSD1306 convention: invert color so 1 = pixel on (white/blue)
    esp_lcd_panel_invert_color(s_ctx.panel, true);

    // Turn on display
    esp_lcd_panel_disp_on_off(s_ctx.panel, true);

    // Clear framebuffer
    memset(s_ctx.framebuffer, 0, sizeof(s_ctx.framebuffer));

    // Push blank frame to clear any OLED garbage
    esp_lcd_panel_draw_bitmap(s_ctx.panel, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, s_ctx.framebuffer);

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Display driver initialized (%dx%d, addr 0x%02X)",
             DISPLAY_WIDTH, DISPLAY_HEIGHT, CONFIG_DISPLAY_DRIVER_I2C_ADDR);

    return ESP_OK;
}

esp_err_t display_driver_deinit(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing display driver");

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // Turn off display
    esp_lcd_panel_disp_on_off(s_ctx.panel, false);

    // Clean up in reverse order
    esp_lcd_panel_del(s_ctx.panel);
    s_ctx.panel = NULL;

    esp_lcd_panel_io_del(s_ctx.io_handle);
    s_ctx.io_handle = NULL;

    i2c_del_master_bus(s_ctx.i2c_bus);
    s_ctx.i2c_bus = NULL;

    s_ctx.initialized = false;

    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "Display driver deinitialized");
    return ESP_OK;
}

uint8_t *display_driver_get_framebuffer(void)
{
    if (!s_ctx.initialized) {
        return NULL;
    }
    return s_ctx.framebuffer;
}

esp_err_t display_driver_clear(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    memset(s_ctx.framebuffer, 0, sizeof(s_ctx.framebuffer));
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t display_driver_flush(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_ctx.panel,
        0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, s_ctx.framebuffer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Flush failed: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t display_driver_on(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    esp_err_t ret = esp_lcd_panel_disp_on_off(s_ctx.panel, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display on failed: %s", esp_err_to_name(ret));
    }
    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t display_driver_off(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    esp_err_t ret = esp_lcd_panel_disp_on_off(s_ctx.panel, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display off failed: %s", esp_err_to_name(ret));
    }
    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t display_driver_set_contrast(uint8_t contrast)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // SSD1306 contrast command: 0x81 followed by contrast value
    uint8_t param = contrast;
    esp_err_t ret = esp_lcd_panel_io_tx_param(s_ctx.io_handle, 0x81, &param, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set contrast failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Contrast set to %d", contrast);
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

#endif /* CONFIG_DISPLAY_DRIVER_ENABLED */
