#ifndef ESP_NOW_COMPAT_H
#define ESP_NOW_COMPAT_H

#include <esp_now.h>

// Compatibility layer for newer ESP-NOW API
// Provides esp_now_recv_info struct for older frameworks

#ifndef ESP_NOW_RECV_INFO_DEFINED
#if !defined(esp_now_recv_info)

typedef struct {
    uint8_t src_addr[ESP_NOW_ETH_ALEN];
    int rssi;
    uint8_t rx_ctrl;
} esp_now_recv_info;

// Wrapper function to adapt old callback signature to new one
static esp_now_recv_info* g_recv_info = NULL;
static const uint8_t* g_mac_addr = NULL;

typedef void (*esp_now_recv_cb_t)(const uint8_t *mac_addr, const uint8_t *data, int len);

void *_esp_now_recv_cb_wrapper;

void esp_now_recv_cb_wrapper_static(const uint8_t *mac_addr, const uint8_t *data, int len);

#endif
#endif

#endif // ESP_NOW_COMPAT_H
