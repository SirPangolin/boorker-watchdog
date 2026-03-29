/**
 * @file sx1262_driver.c
 * @brief SX1262 LoRa radio HAL driver
 *
 * Handle-based SPI driver for the Semtech SX1262. Provides raw packet
 * TX/RX with DIO1 interrupt handling via a FreeRTOS task. No region
 * enforcement or mesh logic — see lora_manager for that.
 *
 * Reference: Semtech DS.SX1261-2.W.APP (Rev 2.1)
 * PA table: RadioLib (see ATTRIBUTIONS.md)
 */

#include "sx1262_driver.h"
#include "sx1262_regs.h"
#include "sx1262_pa_table.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "sx1262";

// ---------------------------------------------------------------------------
// Instance struct
// ---------------------------------------------------------------------------

struct sx1262_inst {
    spi_device_handle_t spi;
    spi_host_device_t spi_host;
    gpio_num_t pin_nss;
    gpio_num_t pin_rst;
    gpio_num_t pin_busy;
    gpio_num_t pin_dio1;
    TaskHandle_t irq_task;
    SemaphoreHandle_t tx_done;
    sx1262_rx_cb_t rx_cb;
    void *rx_ctx;
    bool initialized;
    bool receiving;
    uint32_t frequency_hz;
    int8_t tx_power_dbm;
    sx1262_pkt_params_t pkt_params;
};

// ---------------------------------------------------------------------------
// SPI helpers
// ---------------------------------------------------------------------------

static esp_err_t wait_busy(sx1262_handle_t handle, uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (gpio_get_level(handle->pin_busy)) {
        if ((xTaskGetTickCount() - start) > timeout_ticks) {
            ESP_LOGE(TAG, "BUSY timeout after %lu ms", (unsigned long)timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

static esp_err_t write_command(sx1262_handle_t handle, uint8_t opcode,
                                const uint8_t *params, size_t param_len)
{
    esp_err_t err = wait_busy(handle, 1000);
    if (err != ESP_OK) return err;

    size_t total = 1 + param_len;
    uint8_t tx_buf[total];
    tx_buf[0] = opcode;
    if (param_len > 0 && params != NULL) {
        memcpy(&tx_buf[1], params, param_len);
    }

    spi_transaction_t t = {
        .length = total * 8,
        .tx_buffer = tx_buf,
    };
    return spi_device_transmit(handle->spi, &t);
}

static esp_err_t read_command(sx1262_handle_t handle, uint8_t opcode,
                               uint8_t *result, size_t result_len)
{
    esp_err_t err = wait_busy(handle, 1000);
    if (err != ESP_OK) return err;

    size_t total = 1 + 1 + result_len;
    uint8_t tx_buf[total];
    uint8_t rx_buf[total];
    memset(tx_buf, SX1262_CMD_NOP, total);
    tx_buf[0] = opcode;

    spi_transaction_t t = {
        .length = total * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    err = spi_device_transmit(handle->spi, &t);
    if (err == ESP_OK && result != NULL) {
        memcpy(result, &rx_buf[2], result_len);
    }
    return err;
}

static esp_err_t write_register(sx1262_handle_t handle, uint16_t addr,
                                 const uint8_t *data, size_t len)
{
    esp_err_t err = wait_busy(handle, 1000);
    if (err != ESP_OK) return err;

    size_t total = 3 + len;
    uint8_t tx_buf[total];
    tx_buf[0] = SX1262_CMD_WRITE_REGISTER;
    tx_buf[1] = (addr >> 8) & 0xFF;
    tx_buf[2] = addr & 0xFF;
    memcpy(&tx_buf[3], data, len);

    spi_transaction_t t = {
        .length = total * 8,
        .tx_buffer = tx_buf,
    };
    return spi_device_transmit(handle->spi, &t);
}

static esp_err_t read_register(sx1262_handle_t handle, uint16_t addr,
                                uint8_t *data, size_t len)
{
    esp_err_t err = wait_busy(handle, 1000);
    if (err != ESP_OK) return err;

    size_t total = 4 + len;
    uint8_t tx_buf[total];
    uint8_t rx_buf[total];
    memset(tx_buf, SX1262_CMD_NOP, total);
    tx_buf[0] = SX1262_CMD_READ_REGISTER;
    tx_buf[1] = (addr >> 8) & 0xFF;
    tx_buf[2] = addr & 0xFF;

    spi_transaction_t t = {
        .length = total * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    err = spi_device_transmit(handle->spi, &t);
    if (err == ESP_OK) {
        memcpy(data, &rx_buf[4], len);
    }
    return err;
}

static esp_err_t write_buffer(sx1262_handle_t handle, uint8_t offset,
                               const uint8_t *data, size_t len)
{
    esp_err_t err = wait_busy(handle, 1000);
    if (err != ESP_OK) return err;

    size_t total = 2 + len;
    uint8_t tx_buf[total];
    tx_buf[0] = SX1262_CMD_WRITE_BUFFER;
    tx_buf[1] = offset;
    memcpy(&tx_buf[2], data, len);

    spi_transaction_t t = {
        .length = total * 8,
        .tx_buffer = tx_buf,
    };
    return spi_device_transmit(handle->spi, &t);
}

static esp_err_t read_buffer(sx1262_handle_t handle, uint8_t offset,
                              uint8_t *data, size_t len)
{
    esp_err_t err = wait_busy(handle, 1000);
    if (err != ESP_OK) return err;

    size_t total = 3 + len;
    uint8_t tx_buf[total];
    uint8_t rx_buf[total];
    memset(tx_buf, SX1262_CMD_NOP, total);
    tx_buf[0] = SX1262_CMD_READ_BUFFER;
    tx_buf[1] = offset;

    spi_transaction_t t = {
        .length = total * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    err = spi_device_transmit(handle->spi, &t);
    if (err == ESP_OK) {
        memcpy(data, &rx_buf[3], len);
    }
    return err;
}

// ---------------------------------------------------------------------------
// Errata workarounds
// ---------------------------------------------------------------------------

static esp_err_t fix_pa_clamping(sx1262_handle_t handle)
{
    uint8_t clamp_config = 0;
    esp_err_t err = read_register(handle, SX1262_REG_TX_CLAMP_CONFIG, &clamp_config, 1);
    if (err != ESP_OK) return err;
    clamp_config |= 0x1E;
    return write_register(handle, SX1262_REG_TX_CLAMP_CONFIG, &clamp_config, 1);
}

// ---------------------------------------------------------------------------
// DIO1 interrupt handler
// ---------------------------------------------------------------------------

static void IRAM_ATTR dio1_isr_handler(void *arg)
{
    sx1262_handle_t handle = (sx1262_handle_t)arg;
    BaseType_t higher_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(handle->irq_task, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

static void irq_handler_task(void *arg)
{
    sx1262_handle_t handle = (sx1262_handle_t)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t irq_buf[2];
        if (read_command(handle, SX1262_CMD_GET_IRQ_STATUS, irq_buf, 2) != ESP_OK) {
            continue;
        }
        uint16_t irq = ((uint16_t)irq_buf[0] << 8) | irq_buf[1];

        uint8_t clear_buf[2] = { (uint8_t)(irq >> 8), (uint8_t)(irq & 0xFF) };
        write_command(handle, SX1262_CMD_CLEAR_IRQ_STATUS, clear_buf, 2);

        if (irq & SX1262_IRQ_TX_DONE) {
            xSemaphoreGive(handle->tx_done);
        }

        if (irq & SX1262_IRQ_RX_DONE) {
            uint8_t rx_status[2];
            if (read_command(handle, SX1262_CMD_GET_RX_BUFFER_STATUS, rx_status, 2) == ESP_OK) {
                uint8_t pkt_len = rx_status[0];
                uint8_t offset = rx_status[1];

                if (pkt_len > 0) {
                    uint8_t pkt_data[SX1262_MAX_PACKET_LENGTH];
                    if (read_buffer(handle, offset, pkt_data, pkt_len) == ESP_OK) {
                        uint8_t pkt_status[3];
                        read_command(handle, SX1262_CMD_GET_PACKET_STATUS, pkt_status, 3);
                        int16_t rssi = -(int16_t)pkt_status[0] / 2;
                        int8_t snr = (int8_t)pkt_status[1] / 4;

                        if (handle->rx_cb) {
                            sx1262_rx_packet_t packet = {
                                .data = pkt_data,
                                .length = pkt_len,
                                .rssi = rssi,
                                .snr = snr,
                            };
                            handle->rx_cb(&packet, handle->rx_ctx);
                        }
                    }
                }
            }

            if (handle->receiving) {
                uint8_t rx_params[3] = { 0xFF, 0xFF, 0xFF };
                write_command(handle, SX1262_CMD_SET_RX, rx_params, 3);
            }
        }

        if (irq & SX1262_IRQ_TIMEOUT) {
            ESP_LOGW(TAG, "Radio timeout IRQ");
        }

        if (irq & SX1262_IRQ_CRC_ERR) {
            ESP_LOGW(TAG, "CRC error on received packet");
            if (handle->receiving) {
                uint8_t rx_params[3] = { 0xFF, 0xFF, 0xFF };
                write_command(handle, SX1262_CMD_SET_RX, rx_params, 3);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

esp_err_t sx1262_create(const sx1262_config_t *config, sx1262_handle_t *out)
{
    if (config == NULL || out == NULL) return ESP_ERR_INVALID_ARG;

    ESP_LOGW(TAG, "Initializing SX1262. Ensure LoRa antenna is connected before transmitting.");

    struct sx1262_inst *inst = calloc(1, sizeof(struct sx1262_inst));
    if (inst == NULL) return ESP_ERR_NO_MEM;

    inst->pin_nss = config->pin_nss;
    inst->pin_rst = config->pin_rst;
    inst->pin_busy = config->pin_busy;
    inst->pin_dio1 = config->pin_dio1;
    inst->spi_host = config->spi_host;

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = config->pin_miso,
        .sclk_io_num = config->pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256 + 8,
    };
    esp_err_t err = spi_bus_initialize(config->spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        free(inst);
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = config->spi_clock_hz,
        .mode = 0,
        .spics_io_num = config->pin_nss,
        .queue_size = 1,
    };
    err = spi_bus_add_device(config->spi_host, &dev_cfg, &inst->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        spi_bus_free(config->spi_host);
        free(inst);
        return err;
    }

    // GPIO: RST output, BUSY + DIO1 input
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << config->pin_busy);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << config->pin_dio1);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // TX done semaphore
    inst->tx_done = xSemaphoreCreateBinary();

    // IRQ handler task
    xTaskCreate(irq_handler_task, "sx1262_irq",
                CONFIG_SX1262_TASK_STACK_SIZE, inst,
                CONFIG_SX1262_TASK_PRIORITY, &inst->irq_task);

    // DIO1 GPIO ISR
    gpio_install_isr_service(0);
    gpio_set_intr_type(config->pin_dio1, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(config->pin_dio1, dio1_isr_handler, inst);

    // Hardware reset
    gpio_set_level(config->pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(config->pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    err = wait_busy(inst, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SX1262 not responding after reset");
        goto fail;
    }

    // --- Chip init sequence ---

    uint8_t standby_mode = SX1262_STANDBY_RC;
    err = write_command(inst, SX1262_CMD_SET_STANDBY, &standby_mode, 1);
    if (err != ESP_OK) goto fail;

    uint8_t reg_mode = SX1262_REGULATOR_DCDC;
    err = write_command(inst, SX1262_CMD_SET_REGULATOR_MODE, &reg_mode, 1);
    if (err != ESP_OK) goto fail;

    uint8_t dio2_rf = 0x01;
    err = write_command(inst, SX1262_CMD_SET_DIO2_AS_RF_SWITCH, &dio2_rf, 1);
    if (err != ESP_OK) goto fail;

    // DIO3 as TCXO: 1.7V, 5ms wakeup (320 * 15.625us)
    uint8_t tcxo_params[4] = { 0x02, 0x00, 0x01, 0x40 };
    err = write_command(inst, SX1262_CMD_SET_DIO3_AS_TCXO_CTRL, tcxo_params, 4);
    if (err != ESP_OK) goto fail;

    uint8_t cal_mask = SX1262_CALIBRATE_ALL;
    err = write_command(inst, SX1262_CMD_CALIBRATE, &cal_mask, 1);
    if (err != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t pkt_type = SX1262_PACKET_TYPE_LORA;
    err = write_command(inst, SX1262_CMD_SET_PACKET_TYPE, &pkt_type, 1);
    if (err != ESP_OK) goto fail;

    uint8_t buf_addrs[2] = { 0x00, 0x00 };
    err = write_command(inst, SX1262_CMD_SET_BUFFER_BASE_ADDRESS, buf_addrs, 2);
    if (err != ESP_OK) goto fail;

    err = fix_pa_clamping(inst);
    if (err != ESP_OK) goto fail;

    // DIO1 IRQ mask: TX_DONE, RX_DONE, TIMEOUT, CRC_ERR
    uint16_t irq_mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE |
                         SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR;
    uint8_t irq_params[8] = {
        (uint8_t)(irq_mask >> 8), (uint8_t)(irq_mask & 0xFF),
        (uint8_t)(irq_mask >> 8), (uint8_t)(irq_mask & 0xFF),
        0x00, 0x00,
        0x00, 0x00,
    };
    err = write_command(inst, SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_params, 8);
    if (err != ESP_OK) goto fail;

    inst->initialized = true;
    *out = inst;
    ESP_LOGI(TAG, "SX1262 initialized");
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "SX1262 init failed: %s", esp_err_to_name(err));
    gpio_isr_handler_remove(config->pin_dio1);
    if (inst->irq_task) vTaskDelete(inst->irq_task);
    if (inst->tx_done) vSemaphoreDelete(inst->tx_done);
    spi_bus_remove_device(inst->spi);
    spi_bus_free(config->spi_host);
    free(inst);
    return err;
}

esp_err_t sx1262_destroy(sx1262_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    handle->initialized = false;

    gpio_isr_handler_remove(handle->pin_dio1);
    if (handle->irq_task) vTaskDelete(handle->irq_task);
    if (handle->tx_done) vSemaphoreDelete(handle->tx_done);

    uint8_t sleep_cfg = SX1262_SLEEP_WARM_START;
    write_command(handle, SX1262_CMD_SET_SLEEP, &sleep_cfg, 1);

    spi_bus_remove_device(handle->spi);
    spi_bus_free(handle->spi_host);
    free(handle);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static const uint8_t bw_lookup[] = {
    SX1262_LORA_BW_125,
    SX1262_LORA_BW_250,
    SX1262_LORA_BW_500,
};

esp_err_t sx1262_set_frequency(sx1262_handle_t handle, uint32_t freq_hz)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    uint32_t old_freq = handle->frequency_hz;
    if (old_freq == 0 || (freq_hz > old_freq + 4000000) || (old_freq > freq_hz + 4000000)) {
        uint8_t cal_freq[2];
        if (freq_hz >= 902000000) {
            cal_freq[0] = SX1262_CAL_IMG_902_MHZ_1;
            cal_freq[1] = SX1262_CAL_IMG_902_MHZ_2;
        } else if (freq_hz >= 863000000) {
            cal_freq[0] = SX1262_CAL_IMG_863_MHZ_1;
            cal_freq[1] = SX1262_CAL_IMG_863_MHZ_2;
        } else if (freq_hz >= 470000000) {
            cal_freq[0] = SX1262_CAL_IMG_470_MHZ_1;
            cal_freq[1] = SX1262_CAL_IMG_470_MHZ_2;
        } else {
            cal_freq[0] = SX1262_CAL_IMG_430_MHZ_1;
            cal_freq[1] = SX1262_CAL_IMG_430_MHZ_2;
        }
        esp_err_t err = write_command(handle, SX1262_CMD_CALIBRATE_IMAGE, cal_freq, 2);
        if (err != ESP_OK) return err;
    }

    uint32_t frf = (uint32_t)((float)freq_hz / SX1262_FREQ_STEP);
    uint8_t buf[4] = {
        (uint8_t)((frf >> 24) & 0xFF),
        (uint8_t)((frf >> 16) & 0xFF),
        (uint8_t)((frf >> 8) & 0xFF),
        (uint8_t)(frf & 0xFF),
    };
    esp_err_t err = write_command(handle, SX1262_CMD_SET_RF_FREQUENCY, buf, 4);
    if (err == ESP_OK) {
        handle->frequency_hz = freq_hz;
        ESP_LOGI(TAG, "Frequency: %lu Hz", (unsigned long)freq_hz);
    }
    return err;
}

esp_err_t sx1262_set_tx_power(sx1262_handle_t handle, int8_t power_dbm)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    if (power_dbm < SX1262_PA_TABLE_MIN_DBM) power_dbm = SX1262_PA_TABLE_MIN_DBM;
    if (power_dbm > SX1262_PA_TABLE_MAX_DBM) power_dbm = SX1262_PA_TABLE_MAX_DBM;

    int idx = power_dbm - SX1262_PA_TABLE_MIN_DBM;
    const sx1262_pa_entry_t *pa = &sx1262_pa_table[idx];

    uint8_t pa_config[4] = { pa->pa_duty_cycle, pa->hp_max, SX1262_PA_CONFIG_SX1262, 0x01 };
    esp_err_t err = write_command(handle, SX1262_CMD_SET_PA_CONFIG, pa_config, 4);
    if (err != ESP_OK) return err;

    uint8_t tx_params[2] = { (uint8_t)pa->pa_val, SX1262_TX_RAMP_200US };
    err = write_command(handle, SX1262_CMD_SET_TX_PARAMS, tx_params, 2);
    if (err == ESP_OK) {
        handle->tx_power_dbm = power_dbm;
        ESP_LOGI(TAG, "TX power: %d dBm", power_dbm);
    }
    return err;
}

esp_err_t sx1262_set_modulation(sx1262_handle_t handle, const sx1262_mod_params_t *params)
{
    if (handle == NULL || params == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    uint8_t bw = (params->bandwidth < 3) ? bw_lookup[params->bandwidth] : SX1262_LORA_BW_125;
    uint8_t buf[4] = {
        params->spreading_factor,
        bw,
        params->coding_rate - 4,
        params->low_data_rate_optimize ? 0x01 : 0x00,
    };
    return write_command(handle, SX1262_CMD_SET_MODULATION_PARAMS, buf, 4);
}

esp_err_t sx1262_set_packet_params(sx1262_handle_t handle, const sx1262_pkt_params_t *params)
{
    if (handle == NULL || params == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    handle->pkt_params = *params;

    uint8_t buf[6] = {
        (uint8_t)((params->preamble_length >> 8) & 0xFF),
        (uint8_t)(params->preamble_length & 0xFF),
        params->implicit_header ? SX1262_LORA_HEADER_IMPLICIT : SX1262_LORA_HEADER_EXPLICIT,
        params->payload_length,
        params->crc_on ? SX1262_LORA_CRC_ON : SX1262_LORA_CRC_OFF,
        params->invert_iq ? SX1262_LORA_IQ_INVERTED : SX1262_LORA_IQ_STANDARD,
    };
    return write_command(handle, SX1262_CMD_SET_PACKET_PARAMS, buf, 6);
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

esp_err_t sx1262_transmit(sx1262_handle_t handle, const uint8_t *data,
                           size_t len, uint32_t timeout_ms)
{
    if (handle == NULL || data == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;
    if (len == 0 || len > SX1262_MAX_PACKET_LENGTH) return ESP_ERR_INVALID_SIZE;

    uint8_t standby = SX1262_STANDBY_RC;
    esp_err_t err = write_command(handle, SX1262_CMD_SET_STANDBY, &standby, 1);
    if (err != ESP_OK) return err;

    err = write_buffer(handle, 0x00, data, len);
    if (err != ESP_OK) return err;

    // Update packet params with actual payload length
    sx1262_pkt_params_t tx_pkt = handle->pkt_params;
    tx_pkt.payload_length = (uint8_t)len;
    uint8_t pkt_buf[6] = {
        (uint8_t)((tx_pkt.preamble_length >> 8) & 0xFF),
        (uint8_t)(tx_pkt.preamble_length & 0xFF),
        tx_pkt.implicit_header ? SX1262_LORA_HEADER_IMPLICIT : SX1262_LORA_HEADER_EXPLICIT,
        tx_pkt.payload_length,
        tx_pkt.crc_on ? SX1262_LORA_CRC_ON : SX1262_LORA_CRC_OFF,
        tx_pkt.invert_iq ? SX1262_LORA_IQ_INVERTED : SX1262_LORA_IQ_STANDARD,
    };
    err = write_command(handle, SX1262_CMD_SET_PACKET_PARAMS, pkt_buf, 6);
    if (err != ESP_OK) return err;

    xSemaphoreTake(handle->tx_done, 0);

    uint8_t tx_params[3] = { 0x00, 0x00, 0x00 };
    err = write_command(handle, SX1262_CMD_SET_TX, tx_params, 3);
    if (err != ESP_OK) return err;

    if (xSemaphoreTake(handle->tx_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "TX timeout after %lu ms", (unsigned long)timeout_ms);
        write_command(handle, SX1262_CMD_SET_STANDBY, &standby, 1);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t sx1262_start_receive(sx1262_handle_t handle, sx1262_rx_cb_t callback, void *ctx)
{
    if (handle == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    handle->rx_cb = callback;
    handle->rx_ctx = ctx;
    handle->receiving = true;

    uint8_t rx_params[3] = { 0xFF, 0xFF, 0xFF };
    return write_command(handle, SX1262_CMD_SET_RX, rx_params, 3);
}

esp_err_t sx1262_stop_receive(sx1262_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    handle->receiving = false;
    handle->rx_cb = NULL;
    handle->rx_ctx = NULL;

    uint8_t standby = SX1262_STANDBY_RC;
    return write_command(handle, SX1262_CMD_SET_STANDBY, &standby, 1);
}

esp_err_t sx1262_sleep(sx1262_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    handle->receiving = false;
    uint8_t sleep_cfg = SX1262_SLEEP_WARM_START;
    return write_command(handle, SX1262_CMD_SET_SLEEP, &sleep_cfg, 1);
}

esp_err_t sx1262_standby(sx1262_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    handle->receiving = false;
    uint8_t standby = SX1262_STANDBY_RC;
    return write_command(handle, SX1262_CMD_SET_STANDBY, &standby, 1);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

esp_err_t sx1262_get_packet_status(sx1262_handle_t handle, int16_t *rssi, int8_t *snr)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    if (!handle->initialized) return ESP_ERR_INVALID_STATE;

    uint8_t buf[3];
    esp_err_t err = read_command(handle, SX1262_CMD_GET_PACKET_STATUS, buf, 3);
    if (err == ESP_OK) {
        if (rssi) *rssi = -(int16_t)buf[0] / 2;
        if (snr) *snr = (int8_t)buf[1] / 4;
    }
    return err;
}

esp_err_t sx1262_get_status(sx1262_handle_t handle, sx1262_status_t *out)
{
    if (handle == NULL || out == NULL) return ESP_ERR_INVALID_ARG;

    out->initialized = handle->initialized;
    out->receiving = handle->receiving;
    out->frequency_hz = handle->frequency_hz;
    out->tx_power_dbm = handle->tx_power_dbm;
    return ESP_OK;
}
