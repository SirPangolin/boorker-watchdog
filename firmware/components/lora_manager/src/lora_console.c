/**
 * @file lora_console.c
 * @brief Console commands for LoRa manager (send, listen, stop, status, config)
 */

#include "lora_manager.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "lora_console";

// ---------------------------------------------------------------------------
// lora send <message>
// ---------------------------------------------------------------------------

static struct {
    struct arg_str *message;
    struct arg_end *end;
} s_send_args;

static int cmd_lora_send(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_send_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_send_args.end, argv[0]);
        return 1;
    }

    const char *msg = s_send_args.message->sval[0];
    if (msg == NULL || strlen(msg) == 0) {
        printf("Usage: lora send <message>\n");
        return 1;
    }

    esp_err_t err = lora_manager_send((const uint8_t *)msg, strlen(msg));
    if (err == ESP_OK) {
        printf("TX_DONE\n");
    } else {
        printf("TX failed: %s\n", esp_err_to_name(err));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// lora listen
// ---------------------------------------------------------------------------

static int cmd_lora_listen(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t err = lora_manager_start_listen();
    if (err != ESP_OK) {
        printf("Listen failed: %s\n", esp_err_to_name(err));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// lora stop
// ---------------------------------------------------------------------------

static int cmd_lora_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t err = lora_manager_stop_listen();
    if (err != ESP_OK) {
        printf("Stop failed: %s\n", esp_err_to_name(err));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// lora status
// ---------------------------------------------------------------------------

static int cmd_lora_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    lora_status_t status;
    esp_err_t err = lora_manager_get_status(&status);
    if (err != ESP_OK) {
        printf("LoRa not initialized\n");
        return 0;
    }

    printf("LoRa Status:\n");
    printf("  Region:    %s\n", status.region_name);
    printf("  Frequency: %.3f MHz\n", status.frequency_hz / 1000000.0f);
    printf("  TX Power:  %+d dBm", status.tx_power_dbm);
    if (status.tx_power_dbm > status.region_max_power_dbm) {
        printf(" (exceeds %s default of %+d dBm)", status.region_name, status.region_max_power_dbm);
    }
    printf("\n");
    printf("  Radio:     %s\n", status.receiving ? "LISTENING" : "STANDBY");
    printf("  Antenna:   %s\n", status.antenna_verified
        ? "OK (verified via RSSI)"
        : "Not verified - attach before transmitting");
    printf("  Airtime:   %lu / %lu ms (%.1f%%)\n",
           (unsigned long)status.airtime_used_ms,
           (unsigned long)status.airtime_budget_ms,
           status.airtime_budget_ms > 0
               ? (100.0f * status.airtime_used_ms / status.airtime_budget_ms)
               : 0.0f);
    if (status.duty_cycle_pct < 100) {
        printf("  Duty Cycle: %d%% (regional limit)\n", status.duty_cycle_pct);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// lora config
// ---------------------------------------------------------------------------

static int cmd_lora_config(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("LoRa Modulation Config:\n");
    printf("  SF:       %d\n", CONFIG_LORA_MANAGER_SPREADING_FACTOR);
    printf("  BW:       %s kHz\n",
           CONFIG_LORA_MANAGER_BANDWIDTH == 0 ? "125" :
           CONFIG_LORA_MANAGER_BANDWIDTH == 1 ? "250" : "500");
    printf("  CR:       4/%d\n", CONFIG_LORA_MANAGER_CODING_RATE);
    printf("  Preamble: %d symbols\n", CONFIG_LORA_MANAGER_PREAMBLE_LENGTH);
    return 0;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

esp_err_t lora_manager_register_console(void)
{
    // lora send <message>
    s_send_args.message = arg_str1(NULL, NULL, "<message>", "message to transmit");
    s_send_args.end = arg_end(2);

    // Register subcommands with "lora_" prefix
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "lora_send",
        .help = "Transmit a LoRa message",
        .hint = "<message>",
        .func = cmd_lora_send,
        .argtable = &s_send_args,
    });

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "lora_listen",
        .help = "Start LoRa continuous receive",
        .hint = NULL,
        .func = cmd_lora_listen,
    });

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "lora_stop",
        .help = "Stop LoRa receive",
        .hint = NULL,
        .func = cmd_lora_stop,
    });

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "lora_status",
        .help = "Show LoRa radio status",
        .hint = NULL,
        .func = cmd_lora_status,
    });

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "lora_config",
        .help = "Show LoRa modulation config",
        .hint = NULL,
        .func = cmd_lora_config,
    });

    ESP_LOGI(TAG, "Registered commands: lora_send, lora_listen, lora_stop, lora_status, lora_config");
    return ESP_OK;
}
