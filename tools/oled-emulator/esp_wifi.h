/* Mock esp_wifi.h — redirects to mock_esp.h */
#pragma once
#include "mock_esp.h"
#include "esp_netif.h"
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);
esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);
