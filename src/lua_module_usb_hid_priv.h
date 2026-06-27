/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_hid_common.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"

#include "lua.h"
#include "lauxlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────────────── */

#define LUA_USB_HID_MODULE_NAME        "usb_hid"
#define LUA_USB_HID_MAX_DEVICES        4
#define LUA_USB_HID_REPORT_QUEUE_SIZE  16
#define LUA_USB_HID_STACK_SIZE         4096
#define LUA_USB_HID_TASK_PRIORITY      5
#define LUA_USB_HID_MAX_REPORT_LEN     64

/* HID usage page constants exposed to Lua */
#define LUA_USB_HID_USAGE_KEYBOARD     0x06
#define LUA_USB_HID_USAGE_MOUSE        0x02
#define LUA_USB_HID_USAGE_GAMEPAD      0x05
#define LUA_USB_HID_USAGE_CONSUMER     0x0C
#define LUA_USB_HID_USAGE_GENERIC      0x01

/* ── Event types sent to the Lua event queue ────────────────────────────── */

typedef enum {
    USB_HID_EVT_CONNECTED,
    USB_HID_EVT_DISCONNECTED,
    USB_HID_EVT_REPORT,
} usb_hid_event_type_t;

typedef struct {
    usb_hid_event_type_t type;
    uint8_t              dev_index;   /* slot 0..LUA_USB_HID_MAX_DEVICES-1 */

    /* Only valid when type == USB_HID_EVT_REPORT */
    uint8_t  report[LUA_USB_HID_MAX_REPORT_LEN];
    size_t   report_len;
    uint8_t  report_id;
    uint8_t  usage_page;  /* coarse classification */
} usb_hid_lua_event_t;

/* ── Per-device slot ─────────────────────────────────────────────────────── */

typedef struct {
    hid_host_device_handle_t dev_handle;
    hid_host_interface_handle_t iface_handle;
    uint8_t  usage_page;
    bool     opened;
} usb_hid_device_slot_t;

/* ── Module runtime (singleton) ─────────────────────────────────────────── */

typedef struct {
    bool                  initialized;
    TaskHandle_t          usb_task;
    TaskHandle_t          event_task;

    usb_host_library_handle_t usb_lib;

    SemaphoreHandle_t     lock;
    QueueHandle_t         event_queue;

    usb_hid_device_slot_t devices[LUA_USB_HID_MAX_DEVICES];

    /* Lua callback reference (LUA_NOREF when not set) */
    int  on_event_ref;
    lua_State *L;
} usb_hid_runtime_t;

extern usb_hid_runtime_t s_usb_hid_rt;
extern const char *TAG_USB_HID;

/* ── Internal helpers ────────────────────────────────────────────────────── */

esp_err_t usb_hid_runtime_lock(void);
void      usb_hid_runtime_unlock(void);

/* called from lua_module_usb_hid_host.c */
void usb_hid_on_device_event(hid_host_device_handle_t dev_hdl,
                              const hid_host_driver_event_t event,
                              void *arg);

/* called from lua_module_usb_hid_events.c */
esp_err_t usb_hid_push_event(const usb_hid_lua_event_t *evt);

#ifdef __cplusplus
}
#endif
