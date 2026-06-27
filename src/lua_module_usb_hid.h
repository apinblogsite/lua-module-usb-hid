/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"
#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the usb_hid Lua module and push it onto the Lua stack.
 *
 * Called automatically by the cap_lua registration mechanism; not
 * typically called directly by application code.
 */
int luaopen_usb_hid(lua_State *L);

/**
 * @brief Register the usb_hid Lua module with cap_lua.
 *
 * Called from app_lua_modules.c during firmware boot.
 */
esp_err_t lua_module_usb_hid_register(void);

#ifdef __cplusplus
}
#endif
