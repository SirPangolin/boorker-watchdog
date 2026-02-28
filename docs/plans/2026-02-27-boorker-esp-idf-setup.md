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
- usbipd-win: https://github.com/dorssel/usbipd-win

---

## Phase 0: WSL2 USB Passthrough Setup

WSL2 doesn't have native USB access. We use **usbipd-win** to forward USB devices from Windows to WSL2.

### Task 0: Install usbipd-win for USB Passthrough

**Step 1: Install usbipd on Windows**

Open **PowerShell as Administrator** and run:
```powershell
winget install usbipd
```

Expected: usbipd installs successfully

**Step 2: Install USB/IP tools in WSL2**

In WSL2 Ubuntu terminal:
```bash
sudo apt update
sudo apt install linux-tools-generic hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip /usr/lib/linux-tools/*-generic/usbip 20
```

**Step 3: Verify usbipd installation**

In PowerShell (Admin):
```powershell
usbipd --version
```

Expected: Version number (e.g., `4.x.x`)

**Step 4: Test USB device listing**

Plug in the ESP32-S3-DEVKITC-1 via USB, then in PowerShell (Admin):
```powershell
usbipd list
```

Expected output (example):
```
Connected:
BUSID  VID:PID    DEVICE                           STATE
1-3    303a:1001  USB Serial Device (COM3)         Not shared
```

Note your BUSID (e.g., `1-3`) — you'll need this for flashing.

**Step 5: Create helper scripts (optional but recommended)**

Create `~/esp-attach.ps1` on Windows (save to your user folder):
```powershell
# Run as: powershell -ExecutionPolicy Bypass -File ~/esp-attach.ps1
$busid = (usbipd list | Select-String "303a:1001" | ForEach-Object { ($_ -split '\s+')[0] })
if ($busid) {
    usbipd bind --busid $busid 2>$null
    usbipd attach --wsl --busid $busid
    Write-Host "ESP32 attached to WSL on busid $busid"
} else {
    Write-Host "ESP32 not found. Is it plugged in?"
}
```

**Step 6: Test attaching to WSL2**

In PowerShell (Admin):
```powershell
usbipd bind --busid 1-3
usbipd attach --wsl --busid 1-3
```

Then in WSL2:
```bash
ls /dev/ttyACM*
```

Expected: `/dev/ttyACM0` appears

**Step 7: Add user to dialout group (WSL2)**

```bash
sudo usermod -a -G dialout $USER
```

Note: Log out and back into WSL2 for this to take effect.

---

### USB Workflow Reference

Each time you want to flash:

1. **Plug in ESP32** to Windows USB
2. **PowerShell (Admin):**
   ```powershell
   usbipd list                           # Find BUSID
   usbipd bind --busid <BUSID>           # First time only
   usbipd attach --wsl --busid <BUSID>   # Attach to WSL
   ```
3. **WSL2:** `idf.py -p /dev/ttyACM0 flash monitor`
4. **When done:** Unplug, or `usbipd detach --busid <BUSID>`

---

## Phase 1: Development Environment Setup

### Task 1: Install ESP-IDF Prerequisites

**Step 1: Install system dependencies**

Run (Ubuntu/Debian):
```bash
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

**Step 2: Verify installations**

Run:
```bash
git --version && cmake --version && ninja --version && python3 --version
```

Expected: Version numbers for each tool (cmake >= 3.16)

**Step 3: Commit** - N/A (system setup)

---

### Task 2: Clone and Install ESP-IDF v5.5.3

**Step 1: Create esp directory and clone**

Run:
```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git
```

Expected: Repository cloned with all submodules (~2-5 minutes)

**Step 2: Install ESP-IDF tools for ESP32-S3**

Run:
```bash
cd ~/esp/esp-idf
./install.sh esp32s3
```

Expected: Tools downloaded and installed (~5-10 minutes)

**Step 3: Set up environment**

Run:
```bash
. $HOME/esp/esp-idf/export.sh
```

Expected: Environment variables set, `idf.py` available

**Step 4: Add alias to shell profile**

Run:
```bash
echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.bashrc
```

**Step 5: Verify installation**

Run:
```bash
idf.py --version
```

Expected: `ESP-IDF v5.5.3` or similar

---

### Task 3: Create Boorker Project Structure

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/main.c`
- Create: `firmware/sdkconfig.defaults`

**Step 1: Create project directories**

Run:
```bash
cd ~/claude/basement-watchdog
mkdir -p firmware/main firmware/components
```

**Step 2: Create root CMakeLists.txt**

Create `firmware/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(boorker)
```

**Step 3: Create main component CMakeLists.txt**

Create `firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)
```

**Step 4: Create minimal main.c**

Create `firmware/main/main.c`:
```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "boorker";

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS (required for WiFi and storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", esp_get_free_internal_heap_size());

    while (1) {
        ESP_LOGI(TAG, "Heartbeat - heap: %lu", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

**Step 5: Create sdkconfig.defaults**

Create `firmware/sdkconfig.defaults`:
```
# Target
CONFIG_IDF_TARGET="esp32s3"

# PSRAM Configuration (critical for MicroLink)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768

# Partition Table (large app for MicroLink)
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y

# Stack size
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

# Log level
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

**Step 6: Build and verify**

Run:
```bash
cd ~/claude/basement-watchdog/firmware
get_idf  # or . $HOME/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

Expected: Build succeeds with "Project build complete" message

**Step 7: Commit**

```bash
cd ~/claude/basement-watchdog
git add firmware/
git commit -m "feat: add ESP-IDF project skeleton for Boorker

- CMakeLists.txt for ESP-IDF build system
- Minimal main.c with NVS init and heap monitoring
- sdkconfig.defaults with PSRAM and ESP32-S3 settings"
```

---

### Task 4: Flash and Test on Hardware

**Step 1: Connect ESP32-S3-DEVKITC-1 via USB**

Note: The DevKitC-1 has two USB ports. Use the **USB** port (not UART) for native USB.

**Step 2: Find the serial port**

Run:
```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Expected: `/dev/ttyACM0` or `/dev/ttyUSB0`

**Step 3: Flash the firmware**

Run:
```bash
cd ~/claude/basement-watchdog/firmware
idf.py -p /dev/ttyACM0 flash monitor
```

Expected output:
```
I (xxx) boorker: Boorker starting...
I (xxx) boorker: NVS initialized
I (xxx) boorker: Free heap: ~370000 bytes
I (xxx) boorker: Free PSRAM: ~8000000 bytes
I (xxx) boorker: Heartbeat - heap: ~370000
```

**Step 4: Exit monitor**

Press `Ctrl+]` to exit

---

## Phase 2: WiFi Connection

### Task 5: Add WiFi Station Mode

**Files:**
- Modify: `firmware/main/main.c`
- Create: `firmware/main/wifi.h`
- Create: `firmware/main/wifi.c`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/sdkconfig.defaults`

**Step 1: Update CMakeLists.txt**

Edit `firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.c" "wifi.c"
    INCLUDE_DIRS "."
)
```

**Step 2: Create wifi.h**

Create `firmware/main/wifi.h`:
```c
#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/**
 * Initialize WiFi in station mode and connect to AP
 * Blocks until connected or max retries exceeded
 *
 * @param ssid WiFi network name
 * @param password WiFi password
 * @return ESP_OK on success, ESP_FAIL on connection failure
 */
esp_err_t wifi_init_sta(const char *ssid, const char *password);

/**
 * Check if WiFi is currently connected
 * @return true if connected
 */
bool wifi_is_connected(void);

#endif // WIFI_H
```

**Step 3: Create wifi.c**

Create `firmware/main/wifi.c`:
```c
#include "wifi.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_connected = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to %s...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to connect to %s", ssid);
        return ESP_FAIL;
    }
}

bool wifi_is_connected(void)
{
    return s_is_connected;
}
```

**Step 4: Update main.c to use WiFi**

Edit `firmware/main/main.c`:
```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "wifi.h"

static const char *TAG = "boorker";

// TODO: Move to NVS or Kconfig
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Connect to WiFi
    ret = wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return;
    }

    ESP_LOGI(TAG, "WiFi connected, free heap: %lu", esp_get_free_heap_size());

    while (1) {
        ESP_LOGI(TAG, "Heartbeat - connected: %s, heap: %lu",
                 wifi_is_connected() ? "yes" : "no",
                 esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

**Step 5: Build and flash**

Run:
```bash
cd ~/claude/basement-watchdog/firmware
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Expected: WiFi connects and shows IP address

**Step 6: Commit**

```bash
git add firmware/main/
git commit -m "feat: add WiFi station mode connection

- wifi.c/h with event-driven connection handling
- Auto-retry on disconnect (max 5 attempts)
- Integrated into main.c startup"
```

---

## Phase 3: MicroLink Tailscale Integration

### Task 6: Add MicroLink Component

**Files:**
- Create: `firmware/components/microlink/` (git submodule)
- Modify: `firmware/sdkconfig.defaults`

**Step 1: Add MicroLink as submodule**

Run:
```bash
cd ~/claude/basement-watchdog/firmware/components
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

Run:
```bash
cd ~/claude/basement-watchdog/firmware
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py menuconfig
```

Navigate to: `Component config → MicroLink` and verify defaults are sensible.
Press `Q` then `Y` to save and exit.

**Step 4: Build to verify component integration**

Run:
```bash
idf.py build
```

Expected: Build succeeds with MicroLink compiled

**Step 5: Commit**

```bash
cd ~/claude/basement-watchdog
git add .gitmodules firmware/components/microlink firmware/sdkconfig.defaults
git commit -m "feat: add MicroLink component for Tailscale VPN

- Add microlink as git submodule
- Update sdkconfig for TLS, network stack requirements"
```

---

### Task 7: Generate Tailscale Auth Key

**Step 1: Log into Tailscale admin console**

Open: https://login.tailscale.com/admin/settings/keys

**Step 2: Generate auth key**

- Click "Generate auth key"
- Options:
  - Reusable: Yes (for development)
  - Ephemeral: No (we want the device to persist)
  - Tags: Optional, e.g., `tag:iot`
- Copy the key (starts with `tskey-auth-`)

**Step 3: Store key securely**

For development, create a local config file (NOT committed):

Create `firmware/main/secrets.h`:
```c
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
#define TAILSCALE_AUTH_KEY "tskey-auth-xxxxxxxxxxxxx"

#endif // SECRETS_H
```

**Step 4: Add to .gitignore**

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

Edit `firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.c" "wifi.c" "tailscale.c"
    INCLUDE_DIRS "."
    REQUIRES microlink
)
```

**Step 2: Create tailscale.h**

Create `firmware/main/tailscale.h`:
```c
#ifndef TAILSCALE_H
#define TAILSCALE_H

#include "esp_err.h"

/**
 * Initialize and connect to Tailscale network
 * Requires WiFi to be connected first
 *
 * @param auth_key Tailscale auth key (tskey-auth-xxx)
 * @param device_name Name shown in Tailscale admin
 * @return ESP_OK on success
 */
esp_err_t tailscale_init(const char *auth_key, const char *device_name);

/**
 * Must be called regularly from main loop
 */
void tailscale_update(void);

/**
 * Check if connected to Tailscale network
 */
bool tailscale_is_connected(void);

/**
 * Get the Tailscale IP address (100.x.y.z)
 * @param buf Buffer to store IP string
 * @param buf_len Buffer length
 * @return ESP_OK if connected and IP available
 */
esp_err_t tailscale_get_ip(char *buf, size_t buf_len);

#endif // TAILSCALE_H
```

**Step 3: Create tailscale.c**

Create `firmware/main/tailscale.c`:
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

Edit `firmware/main/main.c`:
```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "tailscale.h"
#include "secrets.h"

static const char *TAG = "boorker";

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");
    ESP_LOGI(TAG, "Free heap: %lu, PSRAM: %lu",
             esp_get_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Connect to WiFi
    ret = wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return;
    }

    ESP_LOGI(TAG, "WiFi connected, heap: %lu", esp_get_free_heap_size());

    // Connect to Tailscale
    ret = tailscale_init(TAILSCALE_AUTH_KEY, "boorker-dev");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tailscale init failed!");
        return;
    }

    // Main loop
    char ts_ip[16] = {0};
    while (1) {
        tailscale_update();

        if (tailscale_is_connected()) {
            tailscale_get_ip(ts_ip, sizeof(ts_ip));
            ESP_LOGI(TAG, "Tailscale connected: %s, heap: %lu",
                     ts_ip, esp_get_free_heap_size());
        } else {
            ESP_LOGI(TAG, "Tailscale connecting..., heap: %lu",
                     esp_get_free_heap_size());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**Step 5: Build and flash**

Run:
```bash
cd ~/claude/basement-watchdog/firmware
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Expected:
```
I (xxx) boorker: Boorker starting...
I (xxx) boorker: WiFi connected
I (xxx) tailscale: Connecting to Tailscale as 'boorker-dev'...
I (xxx) boorker: Tailscale connected: 100.x.y.z
```

**Step 6: Verify from another device**

From any device on your Tailscale network:
```bash
tailscale ping boorker-dev
```

Expected: Successful ping response

**Step 7: Commit**

```bash
git add firmware/main/
git commit -m "feat: integrate MicroLink Tailscale VPN

- tailscale.c/h wrapper for MicroLink API
- Connect to Tailscale after WiFi
- Display Tailscale IP in logs"
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

Edit `firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.c" "wifi.c" "tailscale.c" "webserver.c"
    INCLUDE_DIRS "."
    REQUIRES microlink esp_http_server json
)
```

**Step 2: Create webserver.h**

Create `firmware/main/webserver.h`:
```c
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"

/**
 * Start the HTTP server on port 80
 */
esp_err_t webserver_start(void);

/**
 * Stop the HTTP server
 */
void webserver_stop(void);

#endif // WEBSERVER_H
```

**Step 3: Create webserver.c**

Create `firmware/main/webserver.c`:
```c
#include "webserver.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "tailscale.h"
#include "wifi.h"

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
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_is_connected());
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

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler
    };
    httpd_register_uri_handler(s_server, &root);

    httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler
    };
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

**Step 4: Update main.c to start web server**

Add after Tailscale init in `firmware/main/main.c`:
```c
#include "webserver.h"

// ... in app_main(), after tailscale_init():

    // Start web server
    ret = webserver_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web server failed to start");
    }
```

**Step 5: Build, flash, test**

Run:
```bash
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Test locally (from same network):
```bash
curl http://<local-ip>/api/status
```

Test via Tailscale (from any tailnet device):
```bash
curl http://100.x.y.z/api/status
```

Expected JSON response with device status.

**Step 6: Commit**

```bash
git add firmware/main/
git commit -m "feat: add HTTP web server with status API

- Root page with device info
- /api/status JSON endpoint with heap, WiFi, Tailscale status
- Accessible via local IP and Tailscale IP"
```

---

## Summary

After completing all tasks, you will have:

1. ESP-IDF v5.5.3 development environment
2. Boorker firmware project with PSRAM support
3. WiFi connection with auto-reconnect
4. Tailscale VPN via MicroLink (accessible from anywhere)
5. Basic web server with status API

**Next phases (after Heltec V3 arrives):**
- LoRa mesh communication
- OLED display driver
- Sensor plugin system
- Rules engine
- Full web interface
