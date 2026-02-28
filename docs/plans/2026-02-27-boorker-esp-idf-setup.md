# Boorker ESP-IDF Setup & MicroLink Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Set up ESP-IDF development environment and get Tailscale (MicroLink) working on ESP32-S3-DEVKITC-1

**Architecture:** ESP-IDF v5.5.3 project with MicroLink component for Tailscale VPN, web server for remote access, mDNS for local discovery

**Tech Stack:** ESP-IDF v5.5.3, MicroLink, esp_http_server, mDNS, LittleFS

**Hardware:** ESP32-S3-DEVKITC-1-N32R8V (32MB Flash, 8MB PSRAM)

**Development Environment:** WSL2 Ubuntu 24.04 on Windows 11

**Documentation References:**
- ESP-IDF v5.5.3: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/
- MicroLink: https://github.com/CamM2325/microlink
- Tailscale Auth Keys: https://login.tailscale.com/admin/settings/keys

---

## Prerequisites

### Environment Setup

Use the `esp32-wsl2-dev` plugin to validate and manage the development environment:

```
/esp:check    # Validate WSL2, usbipd, ESP-IDF, tools
/esp:attach   # Attach USB device to WSL2
/esp:flash    # Flash firmware
/esp:monitor  # Monitor serial output
```

**Required before proceeding:**
- WSL2 Ubuntu with usbipd-win installed
- ESP-IDF v5.5.3 installed at `~/esp/esp-idf`
- User in `dialout` group
- ESP32-S3-DevKitC-1 connected via USB

### Hardware Notes

The ESP32-S3-DevKitC-1 has **two USB ports**:

| Port | Chip | Linux Device | Use Case |
|------|------|--------------|----------|
| **UART** | CP2102N | `/dev/ttyUSB0` | Serial console (recommended for WSL2) |
| **USB** | Native | `/dev/ttyACM0` | Flashing, JTAG debugging |

**Recommendation:** Use UART port (`/dev/ttyUSB0`) for serial monitoring in WSL2 - it survives device resets without USB re-enumeration.

---

## Phase 1: Project Structure (Complete)

The ESP-IDF project skeleton exists at `firmware/`:

```
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── components/
│   └── .gitkeep
└── main/
    ├── CMakeLists.txt
    └── main.c
```

**Verify build:**
```bash
source ~/esp/esp-idf/export.sh
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

---

## Phase 2: WiFi Connection

> **Design Document:** See [WiFi Manager Design](./2026-02-28-wifi-manager-design.md) for full architecture details.

### Overview

WiFi is implemented as a reusable ESP-IDF component (`wifi_manager`) with:

| Feature | Description |
|---------|-------------|
| **BLE Provisioning** | First-time setup via ESP BLE Prov app |
| **NVS Storage** | Encrypted credential storage |
| **Auto-reconnect** | Exponential backoff (1s → 5min), never gives up |
| **mDNS** | Device discoverable as `boorker.local` |
| **Power Management** | Modem sleep for battery efficiency |
| **Security** | WPA3 preferred, Proof-of-Possession for BLE |

### Task 5: Implement wifi_manager Component

**Files to create:**
```
firmware/components/wifi_manager/
├── CMakeLists.txt
├── Kconfig
├── include/wifi_manager.h
└── src/
    ├── wifi_manager.c
    ├── wifi_provisioning.c
    ├── wifi_power.c
    └── wifi_mdns.c
```

**Step 1: Build and configure**

```bash
cd ~/claude/boorker-watchdog/firmware
idf.py set-target esp32s3
idf.py menuconfig
# Navigate to: Component config → WiFi Manager
# Set device name and PoP PIN
```

**Step 2: Update main.c**

```c
#include "wifi_manager.h"

static void wifi_callback(wifi_mgr_event_t event, void *ctx) {
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            break;
        case WIFI_MGR_EVENT_PROVISIONING:
            ESP_LOGI(TAG, "Open ESP BLE Prov app to configure WiFi");
            break;
        default:
            break;
    }
}

void app_main(void) {
    nvs_flash_init();

    wifi_mgr_config_t config = {
        .device_name = "boorker-dev",
        .callback = wifi_callback,
    };
    wifi_mgr_init(&config);

    // Wait for connection
    EventGroupHandle_t events = wifi_mgr_get_event_group();
    xEventGroupWaitBits(events, WIFI_MGR_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi ready, starting services...");
}
```

**Step 3: First-time provisioning**

1. Flash firmware: `idf.py -p /dev/ttyUSB0 flash`
2. Install "ESP BLE Prov" app (iOS/Android)
3. Open app → Scan for "boorker-dev"
4. Enter Proof-of-Possession PIN (default: `boorker123`)
5. Select WiFi network and enter password
6. Device stores credentials and connects

**Step 4: Verify**

```bash
/esp:monitor
```

Expected:
```
I (xxx) wifi_manager: Checking for stored credentials...
I (xxx) wifi_manager: Connecting to saved network...
I (xxx) wifi_manager: Connected! IP: 192.168.1.xxx
I (xxx) wifi_manager: mDNS registered: boorker-dev.local
```

**Step 5: Commit**

```bash
git add firmware/components/wifi_manager/
git commit -m "feat: add wifi_manager component

- BLE provisioning with Proof-of-Possession
- NVS encrypted credential storage
- Exponential backoff reconnection
- mDNS discovery (device.local)
- Power management (modem sleep)
- Callbacks + event groups for state notification"
```

---

## Phase 3: MicroLink Tailscale Integration

### Task 6: Add MicroLink Component

**Files:**
- Create: `firmware/components/microlink/` (git submodule)
- Modify: `firmware/sdkconfig.defaults`

**Step 1: Add MicroLink as submodule**

```bash
cd ~/claude/boorker-watchdog/firmware/components
git submodule add https://github.com/CamM2325/microlink.git
```

**Step 2: Update sdkconfig.defaults for MicroLink**

Append to `firmware/sdkconfig.defaults`:
```
# MicroLink/Tailscale Requirements

# TLS Configuration
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y

# Network stack
CONFIG_LWIP_IP4_FRAG=y
CONFIG_LWIP_IP4_REASSEMBLY=y
CONFIG_LWIP_IP6_FRAG=y
CONFIG_LWIP_IP6_REASSEMBLY=y

# HTTP client (for Tailscale coordination)
CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y
```

**Step 3: Clean and reconfigure**

```bash
cd ~/claude/boorker-watchdog/firmware
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py menuconfig
```

Navigate to: `Component config → MicroLink` and verify defaults.
Press `Q` then `Y` to save and exit.

**Step 4: Build to verify**

```bash
idf.py build
```

**Step 5: Commit**

```bash
cd ~/claude/boorker-watchdog
git add .gitmodules firmware/components/microlink firmware/sdkconfig.defaults
git commit -m "feat: add MicroLink component for Tailscale VPN

- Add microlink as git submodule
- Update sdkconfig for TLS, network stack requirements"
```

---

### Task 7: Generate Tailscale Auth Key

**Step 1:** Open https://login.tailscale.com/admin/settings/keys

**Step 2:** Generate auth key:
- Reusable: Yes (for development)
- Ephemeral: No (device persists)
- Tags: Optional, e.g., `tag:iot`

**Step 3:** Create `firmware/main/secrets.h` (NOT committed):
```c
#ifndef SECRETS_H
#define SECRETS_H

// WiFi credentials handled by wifi_manager (BLE provisioning + NVS)
// No need to define WIFI_SSID/WIFI_PASSWORD here

#define TAILSCALE_AUTH_KEY "tskey-auth-xxxxxxxxxxxxx"

#endif // SECRETS_H
```

**Step 4:** Add to .gitignore:
```bash
echo "firmware/main/secrets.h" >> .gitignore
git add .gitignore
git commit -m "chore: ignore secrets.h"
```

---

### Task 8: Integrate MicroLink into Main App

**Files:**
- Modify: `firmware/main/main.c`
- Create: `firmware/main/tailscale.h`
- Create: `firmware/main/tailscale.c`
- Modify: `firmware/main/CMakeLists.txt`

**Step 1: Update CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c" "tailscale.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager microlink
)
```

**Step 2: Create tailscale.h**

```c
#ifndef TAILSCALE_H
#define TAILSCALE_H

#include "esp_err.h"

esp_err_t tailscale_init(const char *auth_key, const char *device_name);
void tailscale_update(void);
bool tailscale_is_connected(void);
esp_err_t tailscale_get_ip(char *buf, size_t buf_len);

#endif // TAILSCALE_H
```

**Step 3: Create tailscale.c**

```c
#include "tailscale.h"
#include "microlink.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "tailscale";
static microlink_t *s_ml = NULL;

esp_err_t tailscale_init(const char *auth_key, const char *device_name)
{
    ESP_LOGI(TAG, "Initializing MicroLink...");

    microlink_config_t config;
    microlink_get_default_config(&config);
    config.auth_key = auth_key;
    config.device_name = device_name;

    s_ml = microlink_init(&config);
    if (s_ml == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MicroLink");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to Tailscale as '%s'...", device_name);
    microlink_connect(s_ml);

    return ESP_OK;
}

void tailscale_update(void)
{
    if (s_ml != NULL) {
        microlink_update(s_ml);
    }
}

bool tailscale_is_connected(void)
{
    if (s_ml == NULL) return false;
    return microlink_is_connected(s_ml);
}

esp_err_t tailscale_get_ip(char *buf, size_t buf_len)
{
    if (s_ml == NULL || !tailscale_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    microlink_get_vpn_ip(s_ml, buf, buf_len);
    return ESP_OK;
}
```

**Step 4: Update main.c**

```c
#include "wifi_manager.h"
#include "tailscale.h"
#include "secrets.h"

static void on_wifi_connected(wifi_mgr_event_t event, void *ctx) {
    if (event == WIFI_MGR_EVENT_CONNECTED) {
        // Start Tailscale once WiFi is up
        tailscale_init(TAILSCALE_AUTH_KEY, "boorker-dev");
    }
}

void app_main(void) {
    nvs_flash_init();

    // Initialize WiFi (blocks until connected or provisioning)
    wifi_mgr_config_t wifi_cfg = {
        .device_name = "boorker-dev",
        .callback = on_wifi_connected,
    };
    wifi_mgr_init(&wifi_cfg);

    // Wait for WiFi connection
    xEventGroupWaitBits(wifi_mgr_get_event_group(),
                        WIFI_MGR_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    // Main loop
    char ts_ip[16] = {0};
    while (1) {
        tailscale_update();

        if (tailscale_is_connected()) {
            tailscale_get_ip(ts_ip, sizeof(ts_ip));
            ESP_LOGI(TAG, "Tailscale: %s, heap: %lu", ts_ip, esp_get_free_heap_size());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**Step 5: Build, flash, verify**

```bash
idf.py build && idf.py -p /dev/ttyUSB0 flash
/esp:monitor
```

From another Tailscale device:
```bash
tailscale ping boorker-dev
```

---

## Phase 4: Web Server

### Task 9: Add Basic HTTP Server

**Files:**
- Create: `firmware/main/webserver.h`
- Create: `firmware/main/webserver.c`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/main.c`

**Step 1: Update CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c" "tailscale.c" "webserver.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager microlink esp_http_server json
)
```

**Step 2: Create webserver.h**

```c
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"

esp_err_t webserver_start(void);
void webserver_stop(void);

#endif // WEBSERVER_H
```

**Step 3: Create webserver.c**

```c
#include "webserver.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "tailscale.h"
#include "wifi_manager.h"

static const char *TAG = "webserver";
static httpd_handle_t s_server = NULL;

static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html><head><title>Boorker</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:system-ui;max-width:600px;margin:40px auto;padding:0 20px;}"
        "h1{color:#333;}pre{background:#f4f4f4;padding:15px;border-radius:5px;}"
        "</style></head>"
        "<body><h1>Boorker</h1>"
        "<p>ESP32-S3 IoT Platform</p>"
        "<p><a href='/api/status'>API Status</a></p>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "device", "boorker-dev");
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_mgr_is_connected());
    cJSON_AddBoolToObject(root, "tailscale_connected", tailscale_is_connected());

    char ts_ip[16] = "N/A";
    if (tailscale_is_connected()) {
        tailscale_get_ip(ts_ip, sizeof(ts_ip));
    }
    cJSON_AddStringToObject(root, "tailscale_ip", ts_ip);

    char *json = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return ESP_FAIL;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &status);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
```

**Step 4: Update main.c**

```c
#include "webserver.h"

// After tailscale_init():
webserver_start();
```

**Step 5: Test**

```bash
idf.py build && idf.py -p /dev/ttyUSB0 flash
curl http://100.x.y.z/api/status  # Via Tailscale
```

---

## Summary

| Phase | Description | Status |
|-------|-------------|--------|
| Prerequisites | Environment setup via `esp32-wsl2-dev` plugin | ✓ |
| Phase 1 | Project structure | ✓ |
| Phase 2 | WiFi connection | Pending |
| Phase 3 | MicroLink/Tailscale | Pending |
| Phase 4 | Web server | Pending |

**Next phases (after Heltec V3 arrives):**
- LoRa mesh communication
- OLED display driver
- Sensor plugin system
- Rules engine
- Full web interface
