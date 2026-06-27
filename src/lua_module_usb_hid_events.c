/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * lua_module_usb_hid_events.c
 *
 * Queue helper: ISR/task → Lua event queue.
 * Lua-side event dispatch is handled by usb_hid.process_events() in
 * lua_module_usb_hid.c.
 */
#include "lua_module_usb_hid_priv.h"

#include "esp_log.h"

esp_err_t usb_hid_push_event(const usb_hid_lua_event_t *evt)
{
    if (!s_usb_hid_rt.event_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(s_usb_hid_rt.event_queue, evt, 0) != pdTRUE) {
        ESP_LOGW(TAG_USB_HID, "USB HID event queue full, event dropped (type=%d)", evt->type);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
