if(CONFIG_APP_CLAW_LUA_MODULE_USB_HID)
    if(NOT CONFIG_USB_HOST_ENABLED)
        message(FATAL_ERROR
            "APP_CLAW_LUA_MODULE_USB_HID requires USB Host support. "
            "Enable CONFIG_USB_HOST_ENABLED in sdkconfig."
        )
    endif()
endif()

idf_component_register(
    SRCS
        "src/lua_module_usb_hid.c"
        "src/lua_module_usb_hid_host.c"
        "src/lua_module_usb_hid_events.c"
    INCLUDE_DIRS
        "src"
    REQUIRES
        cap_lua
        usb
        esp_hid
        freertos
        esp_event
        esp_timer
)
