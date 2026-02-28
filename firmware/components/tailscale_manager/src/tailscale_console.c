#include "tailscale_console.h"
#include "tailscale_manager.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char *TAG = "ts_console";

// ts_auth command
static struct {
    struct arg_str *key;
    struct arg_end *end;
} ts_auth_args;

static int cmd_ts_auth(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ts_auth_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ts_auth_args.end, argv[0]);
        return 1;
    }

    const char *key = ts_auth_args.key->sval[0];
    if (key == NULL || strlen(key) == 0) {
        printf("Usage: ts_auth <tskey-auth-xxxxx>\n");
        return 1;
    }

    esp_err_t ret = ts_mgr_set_auth_key(key);
    if (ret == ESP_OK) {
        printf("Auth key stored. Connecting to Tailscale...\n");
    } else if (ret == ESP_ERR_INVALID_ARG) {
        printf("Error: Invalid key format. Must start with 'tskey-auth-'\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// ts_clear command
static int cmd_ts_clear(int argc, char **argv)
{
    esp_err_t ret = ts_mgr_clear_auth_key();
    if (ret == ESP_OK) {
        printf("Auth key cleared. Tailscale disabled.\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// ts_status command
static int cmd_ts_status(int argc, char **argv)
{
    printf("Tailscale Status:\n");
    printf("  State: %s\n", ts_mgr_get_state_name());
    printf("  Configured: %s\n", ts_mgr_has_auth_key() ? "yes" : "no");

    if (ts_mgr_is_connected()) {
        char ip[16];
        if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
            printf("  Tailscale IP: %s\n", ip);
        }
    }

    return 0;
}

esp_err_t ts_console_register(void)
{
    // ts_auth command
    ts_auth_args.key = arg_str1(NULL, NULL, "<key>", "Tailscale auth key");
    ts_auth_args.end = arg_end(1);

    const esp_console_cmd_t ts_auth_cmd = {
        .command = "ts_auth",
        .help = "Set Tailscale auth key (stored in NVS)",
        .hint = NULL,
        .func = &cmd_ts_auth,
        .argtable = &ts_auth_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ts_auth_cmd));

    // ts_clear command
    const esp_console_cmd_t ts_clear_cmd = {
        .command = "ts_clear",
        .help = "Clear Tailscale auth key",
        .hint = NULL,
        .func = &cmd_ts_clear,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ts_clear_cmd));

    // ts_status command
    const esp_console_cmd_t ts_status_cmd = {
        .command = "ts_status",
        .help = "Show Tailscale connection status",
        .hint = NULL,
        .func = &cmd_ts_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ts_status_cmd));

    ESP_LOGI(TAG, "Registered commands: ts_auth, ts_clear, ts_status");
    return ESP_OK;
}
