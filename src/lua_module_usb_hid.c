/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * lua_module_usb_hid.c
 *
 * Lua bindings for USB Host HID support on ESP32-P4.
 *
 * Exposed Lua API  (require "usb_hid"):
 *
 *   usb_hid.init()                        -- install USB host + HID driver
 *   usb_hid.deinit()                      -- uninstall, release all resources
 *   usb_hid.on_event(fn)                  -- register event callback
 *   usb_hid.process_events([timeout_ms])  -- drain queue, call on_event for each
 *   usb_hid.get_devices()                 -- returns array of connected device info
 *   usb_hid.USAGE.*                       -- HID usage page constants
 *
 * Event table passed to the callback:
 *   { type="connected",    dev=0, usage_page=6 }
 *   { type="disconnected", dev=0 }
 *   { type="report",       dev=0, usage_page=6, id=0, data=<string> }
 */
#include "lua_module_usb_hid.h"
#include "lua_module_usb_hid_priv.h"

#include "cap_lua.h"
#include "esp_check.h"
#include "esp_log.h"

/* ── Singleton runtime ───────────────────────────────────────────────────── */

usb_hid_runtime_t s_usb_hid_rt = {
    .initialized   = false,
    .usb_task      = NULL,
    .event_task    = NULL,
    .lock          = NULL,
    .event_queue   = NULL,
    .on_event_ref  = LUA_NOREF,
    .L             = NULL,
};

const char *TAG_USB_HID = "lua_usb_hid";

/* ── Forward declarations for host start/stop ───────────────────────────── */
esp_err_t usb_hid_host_start(void);
esp_err_t usb_hid_host_stop(void);

/* ── Lock helpers ────────────────────────────────────────────────────────── */

esp_err_t usb_hid_runtime_lock(void)
{
    if (!s_usb_hid_rt.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_usb_hid_rt.lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void usb_hid_runtime_unlock(void)
{
    if (s_usb_hid_rt.lock) {
        xSemaphoreGive(s_usb_hid_rt.lock);
    }
}

/* ── Lua: usb_hid.init() ─────────────────────────────────────────────────── */

static int lua_usb_hid_init(lua_State *L)
{
    if (s_usb_hid_rt.initialized) {
        lua_pushboolean(L, 1);
        return 1;
    }

    s_usb_hid_rt.lock = xSemaphoreCreateMutex();
    if (!s_usb_hid_rt.lock) {
        return luaL_error(L, "usb_hid: failed to create mutex");
    }

    s_usb_hid_rt.event_queue = xQueueCreate(LUA_USB_HID_REPORT_QUEUE_SIZE,
                                             sizeof(usb_hid_lua_event_t));
    if (!s_usb_hid_rt.event_queue) {
        vSemaphoreDelete(s_usb_hid_rt.lock);
        s_usb_hid_rt.lock = NULL;
        return luaL_error(L, "usb_hid: failed to create event queue");
    }

    s_usb_hid_rt.L = L;

    esp_err_t err = usb_hid_host_start();
    if (err != ESP_OK) {
        vQueueDelete(s_usb_hid_rt.event_queue);
        vSemaphoreDelete(s_usb_hid_rt.lock);
        s_usb_hid_rt.event_queue = NULL;
        s_usb_hid_rt.lock = NULL;
        return luaL_error(L, "usb_hid: host start failed (%s)", esp_err_to_name(err));
    }

    s_usb_hid_rt.initialized = true;
    ESP_LOGI(TAG_USB_HID, "USB HID host initialized");
    lua_pushboolean(L, 1);
    return 1;
}

/* ── Lua: usb_hid.deinit() ──────────────────────────────────────────────── */

static int lua_usb_hid_deinit(lua_State *L)
{
    if (!s_usb_hid_rt.initialized) {
        return 0;
    }

    usb_hid_host_stop();

    if (s_usb_hid_rt.on_event_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s_usb_hid_rt.on_event_ref);
        s_usb_hid_rt.on_event_ref = LUA_NOREF;
    }

    if (s_usb_hid_rt.event_queue) {
        vQueueDelete(s_usb_hid_rt.event_queue);
        s_usb_hid_rt.event_queue = NULL;
    }

    if (s_usb_hid_rt.lock) {
        vSemaphoreDelete(s_usb_hid_rt.lock);
        s_usb_hid_rt.lock = NULL;
    }

    memset(s_usb_hid_rt.devices, 0, sizeof(s_usb_hid_rt.devices));
    s_usb_hid_rt.initialized = false;
    s_usb_hid_rt.L = NULL;

    ESP_LOGI(TAG_USB_HID, "USB HID host deinitialized");
    return 0;
}

/* ── Lua: usb_hid.on_event(fn) ──────────────────────────────────────────── */

static int lua_usb_hid_on_event(lua_State *L)
{
    if (lua_isnil(L, 1)) {
        if (s_usb_hid_rt.on_event_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, s_usb_hid_rt.on_event_ref);
            s_usb_hid_rt.on_event_ref = LUA_NOREF;
        }
        return 0;
    }

    luaL_checktype(L, 1, LUA_TFUNCTION);

    if (s_usb_hid_rt.on_event_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s_usb_hid_rt.on_event_ref);
    }

    lua_pushvalue(L, 1);
    s_usb_hid_rt.on_event_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

/* ── Internal: push one event table onto the Lua stack ──────────────────── */

static void usb_hid_push_lua_event(lua_State *L, const usb_hid_lua_event_t *evt)
{
    lua_newtable(L);

    switch (evt->type) {
    case USB_HID_EVT_CONNECTED:
        lua_pushstring(L, "connected");
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, evt->dev_index);
        lua_setfield(L, -2, "dev");
        lua_pushinteger(L, evt->usage_page);
        lua_setfield(L, -2, "usage_page");
        break;

    case USB_HID_EVT_DISCONNECTED:
        lua_pushstring(L, "disconnected");
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, evt->dev_index);
        lua_setfield(L, -2, "dev");
        break;

    case USB_HID_EVT_REPORT:
        lua_pushstring(L, "report");
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, evt->dev_index);
        lua_setfield(L, -2, "dev");
        lua_pushinteger(L, evt->usage_page);
        lua_setfield(L, -2, "usage_page");
        lua_pushinteger(L, evt->report_id);
        lua_setfield(L, -2, "id");
        lua_pushlstring(L, (const char *)evt->report, evt->report_len);
        lua_setfield(L, -2, "data");
        break;
    }
}

/* ── Lua: usb_hid.process_events([timeout_ms]) ──────────────────────────── */

static int lua_usb_hid_process_events(lua_State *L)
{
    if (!s_usb_hid_rt.initialized) {
        return luaL_error(L, "usb_hid: not initialized");
    }

    TickType_t timeout_ticks = 0;
    if (lua_isnumber(L, 1)) {
        int ms = (int)lua_tointeger(L, 1);
        timeout_ticks = (ms > 0) ? pdMS_TO_TICKS(ms) : 0;
    }

    if (s_usb_hid_rt.on_event_ref == LUA_NOREF) {
        /* No callback registered — just drain and discard */
        usb_hid_lua_event_t evt;
        while (xQueueReceive(s_usb_hid_rt.event_queue, &evt, timeout_ticks) == pdTRUE) {
            timeout_ticks = 0; /* only block on first receive */
        }
        return 0;
    }

    usb_hid_lua_event_t evt;
    int count = 0;
    while (xQueueReceive(s_usb_hid_rt.event_queue, &evt, timeout_ticks) == pdTRUE) {
        timeout_ticks = 0;
        lua_rawgeti(L, LUA_REGISTRYINDEX, s_usb_hid_rt.on_event_ref);
        usb_hid_push_lua_event(L, &evt);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            ESP_LOGW(TAG_USB_HID, "on_event callback error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        count++;
    }

    lua_pushinteger(L, count);
    return 1;
}

/* ── Lua: usb_hid.get_devices() ─────────────────────────────────────────── */

static int lua_usb_hid_get_devices(lua_State *L)
{
    if (!s_usb_hid_rt.initialized) {
        return luaL_error(L, "usb_hid: not initialized");
    }

    if (usb_hid_runtime_lock() != ESP_OK) {
        return luaL_error(L, "usb_hid: lock timeout");
    }

    lua_newtable(L);
    int idx = 1;
    for (int i = 0; i < LUA_USB_HID_MAX_DEVICES; i++) {
        if (s_usb_hid_rt.devices[i].opened) {
            lua_newtable(L);
            lua_pushinteger(L, i);
            lua_setfield(L, -2, "dev");
            lua_pushinteger(L, s_usb_hid_rt.devices[i].usage_page);
            lua_setfield(L, -2, "usage_page");
            lua_rawseti(L, -2, idx++);
        }
    }

    usb_hid_runtime_unlock();
    return 1;
}

/* ── luaopen_usb_hid ─────────────────────────────────────────────────────── */

int luaopen_usb_hid(lua_State *L)
{
    lua_newtable(L);

    lua_pushcfunction(L, lua_usb_hid_init);
    lua_setfield(L, -2, "init");

    lua_pushcfunction(L, lua_usb_hid_deinit);
    lua_setfield(L, -2, "deinit");

    lua_pushcfunction(L, lua_usb_hid_on_event);
    lua_setfield(L, -2, "on_event");

    lua_pushcfunction(L, lua_usb_hid_process_events);
    lua_setfield(L, -2, "process_events");

    lua_pushcfunction(L, lua_usb_hid_get_devices);
    lua_setfield(L, -2, "get_devices");

    /* USAGE constants sub-table */
    lua_newtable(L);
    lua_pushinteger(L, LUA_USB_HID_USAGE_KEYBOARD); lua_setfield(L, -2, "KEYBOARD");
    lua_pushinteger(L, LUA_USB_HID_USAGE_MOUSE);    lua_setfield(L, -2, "MOUSE");
    lua_pushinteger(L, LUA_USB_HID_USAGE_GAMEPAD);  lua_setfield(L, -2, "GAMEPAD");
    lua_pushinteger(L, LUA_USB_HID_USAGE_CONSUMER); lua_setfield(L, -2, "CONSUMER");
    lua_pushinteger(L, LUA_USB_HID_USAGE_GENERIC);  lua_setfield(L, -2, "GENERIC");
    lua_setfield(L, -2, "USAGE");

    return 1;
}

/* ── Registration entry point ───────────────────────────────────────────── */

esp_err_t lua_module_usb_hid_register(void)
{
    return cap_lua_register_module(LUA_USB_HID_MODULE_NAME, luaopen_usb_hid);
}
