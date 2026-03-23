/* Mock esp_netif.h — stubs for desktop emulator */
#pragma once
#include "mock_esp.h"

typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef void* esp_netif_t;

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ip) 192, 168, 68, 54

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);

static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
    (void)netif; (void)info;
    return ESP_FAIL;  // Will use the IP2STR fallback
}
