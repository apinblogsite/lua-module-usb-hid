/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * lua_module_usb_hid_host.c
 *
 * USB Host library task and HID device open/close logic.
 * Runs two FreeRTOS tasks:
 *   - s_usb_hid_rt.usb_task  : drives usb_host_lib client handler (must be on CPU1 if PSRAM)
 *   - s_usb_hid_rt.event_task: drives hid_host driver event loop
 */
#include "lua_module_usb_hid_priv.h"

#include "esp_check.h"
#include "esp_log.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"

/* ── Forward declarations ────────────────────────────────────────────────── */

static void usb_lib_task(void *arg);
static void hid_host_task(void *arg);
static void usb_hid_open_interface(hid_host_device_handle_t dev_hdl);

/* ── USB host library task ───────────────────────────────────────────────── */

static void usb_lib_task(void *arg)
{
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG_USB_HID, "USB: no clients, freeing devices");
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG_USB_HID, "USB: all devices freed");
        }
    }
}

/* ── HID host driver event task ─────────────────────────────────────────── */

static void hid_host_task(void *arg)
{
    hid_host_event_queue_t evt;

    while (1) {
        if (hid_host_device_event(&evt, portMAX_DELAY) == ESP_OK) {
            usb_hid_on_device_event(evt.hid_device_handle,
                                    evt.event,
                                    evt.arg);
        }
    }
}

/* ── HID interface callback (reports arrive here) ───────────────────────── */

static void hid_interface_callback(const uint8_t *const data,
                                   const int length,
                                   void *arg)
{
    int dev_index = (int)(intptr_t)arg;
    if (dev_index < 0 || dev_index >= LUA_USB_HID_MAX_DEVICES || length <= 0) {
        return;
    }

    usb_hid_lua_event_t evt = {
        .type       = USB_HID_EVT_REPORT,
        .dev_index  = (uint8_t)dev_index,
        .report_len = (length > LUA_USB_HID_MAX_REPORT_LEN) ? LUA_USB_HID_MAX_REPORT_LEN : (size_t)length,
        .report_id  = 0,
    };

    if (usb_hid_runtime_lock() == ESP_OK) {
        evt.usage_page = s_usb_hid_rt.devices[dev_index].usage_page;
        usb_hid_runtime_unlock();
    }

    memcpy(evt.report, data, evt.report_len);
    usb_hid_push_event(&evt);
}

/* ── Open an interface on a newly connected device ───────────────────────── */

static void usb_hid_open_interface(hid_host_device_handle_t dev_hdl)
{
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(dev_hdl, &params) != ESP_OK) {
        ESP_LOGW(TAG_USB_HID, "Could not get device params");
        return;
    }

    /* Find a free slot */
    int slot = -1;
    if (usb_hid_runtime_lock() != ESP_OK) {
        return;
    }
    for (int i = 0; i < LUA_USB_HID_MAX_DEVICES; i++) {
        if (!s_usb_hid_rt.devices[i].opened) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        usb_hid_runtime_unlock();
        ESP_LOGW(TAG_USB_HID, "No free HID device slots (max=%d)", LUA_USB_HID_MAX_DEVICES);
        return;
    }

    /* Open interface */
    const hid_host_interface_config_t iface_cfg = {
        .proto      = HID_PROTOCOL_NONE,  /* accept both boot and report protocol */
        .callback   = hid_interface_callback,
        .callback_arg = (void *)(intptr_t)slot,
    };

    hid_host_interface_handle_t iface_hdl;
    esp_err_t err = hid_host_interface_open(dev_hdl, 0, &iface_cfg, &iface_hdl);
    if (err != ESP_OK) {
        usb_hid_runtime_unlock();
        ESP_LOGE(TAG_USB_HID, "Interface open failed: %s", esp_err_to_name(err));
        return;
    }

    s_usb_hid_rt.devices[slot].dev_handle  = dev_hdl;
    s_usb_hid_rt.devices[slot].iface_handle = iface_hdl;
    s_usb_hid_rt.devices[slot].usage_page  = params.usage_page;
    s_usb_hid_rt.devices[slot].opened      = true;
    usb_hid_runtime_unlock();

    ESP_LOGI(TAG_USB_HID, "HID device opened: slot=%d usage_page=0x%02x", slot, params.usage_page);

    hid_host_interface_start(iface_hdl);

    /* Notify Lua */
    usb_hid_lua_event_t evt = {
        .type      = USB_HID_EVT_CONNECTED,
        .dev_index = (uint8_t)slot,
        .usage_page = params.usage_page,
    };
    usb_hid_push_event(&evt);
}

/* ── Device event handler (called from hid_host_task) ───────────────────── */

void usb_hid_on_device_event(hid_host_device_handle_t dev_hdl,
                              const hid_host_driver_event_t event,
                              void *arg)
{
    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        ESP_LOGI(TAG_USB_HID, "HID device connected");
        usb_hid_open_interface(dev_hdl);
        break;

    case HID_HOST_DRIVER_EVENT_DISCONNECTED: {
        /* Find which slot this device was in */
        int slot = -1;
        if (usb_hid_runtime_lock() == ESP_OK) {
            for (int i = 0; i < LUA_USB_HID_MAX_DEVICES; i++) {
                if (s_usb_hid_rt.devices[i].opened &&
                        s_usb_hid_rt.devices[i].dev_handle == dev_hdl) {
                    slot = i;
                    break;
                }
            }
            if (slot >= 0) {
                hid_host_interface_close(s_usb_hid_rt.devices[slot].iface_handle);
                s_usb_hid_rt.devices[slot].opened = false;
                s_usb_hid_rt.devices[slot].dev_handle  = NULL;
                s_usb_hid_rt.devices[slot].iface_handle = NULL;
            }
            usb_hid_runtime_unlock();
        }

        ESP_LOGI(TAG_USB_HID, "HID device disconnected (slot=%d)", slot);

        if (slot >= 0) {
            usb_hid_lua_event_t evt = {
                .type      = USB_HID_EVT_DISCONNECTED,
                .dev_index = (uint8_t)slot,
            };
            usb_hid_push_event(&evt);
        }
        break;
    }

    default:
        break;
    }
}

/* ── Start/stop the USB host stack ──────────────────────────────────────── */

esp_err_t usb_hid_host_start(void)
{
    /* Install USB host library */
    const usb_host_config_t host_cfg = {
        .skip_phy_setup     = false,
        .intr_flags         = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_RETURN_ON_ERROR(usb_host_install(&host_cfg), TAG_USB_HID,
                        "usb_host_install failed");

    /* Install HID host driver */
    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = false,  /* we drive it ourselves */
        .task_priority          = LUA_USB_HID_TASK_PRIORITY,
        .stack_size             = LUA_USB_HID_STACK_SIZE,
        .core_id                = tskNO_AFFINITY,
        .callback               = usb_hid_on_device_event,
        .callback_arg           = NULL,
    };
    ESP_RETURN_ON_ERROR(hid_host_install(&hid_cfg), TAG_USB_HID,
                        "hid_host_install failed");

    /* USB host event task – handles USB_HOST_LIB events */
    BaseType_t ret = xTaskCreatePinnedToCore(
                         usb_lib_task, "usb_lib", LUA_USB_HID_STACK_SIZE,
                         NULL, LUA_USB_HID_TASK_PRIORITY,
                         &s_usb_hid_rt.usb_task, 0);
    if (ret != pdPASS) {
        hid_host_uninstall();
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    /* HID host event task – dequeues HID device events */
    ret = xTaskCreate(hid_host_task, "hid_host", LUA_USB_HID_STACK_SIZE,
                      NULL, LUA_USB_HID_TASK_PRIORITY,
                      &s_usb_hid_rt.event_task);
    if (ret != pdPASS) {
        vTaskDelete(s_usb_hid_rt.usb_task);
        s_usb_hid_rt.usb_task = NULL;
        hid_host_uninstall();
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t usb_hid_host_stop(void)
{
    if (s_usb_hid_rt.event_task) {
        vTaskDelete(s_usb_hid_rt.event_task);
        s_usb_hid_rt.event_task = NULL;
    }
    if (s_usb_hid_rt.usb_task) {
        vTaskDelete(s_usb_hid_rt.usb_task);
        s_usb_hid_rt.usb_task = NULL;
    }
    hid_host_uninstall();
    usb_host_uninstall();
    return ESP_OK;
}
