# USB HID Host Skill

This skill enables the agent to receive input from USB HID devices (keyboards, mice, gamepads) connected to the ESP32-P4-NANO USB-A port.

## Usage

```lua
local hid = require("usb_hid")
```

## Quick start

```lua
-- {CUR_SKILL_DIR}/scripts/start_usb_hid_monitor.lua
local hid   = require("usb_hid")
local delay = require("delay")
local thread = require("thread")

hid.init()

hid.on_event(function(evt)
    if evt.type == "connected" then
        print("USB HID device connected: dev=" .. evt.dev ..
              " usage_page=" .. evt.usage_page)
    elseif evt.type == "disconnected" then
        print("USB HID device disconnected: dev=" .. evt.dev)
    elseif evt.type == "report" then
        local bytes = { string.byte(evt.data, 1, #evt.data) }
        -- For a standard USB keyboard boot report:
        -- byte 1 = modifier keys (Ctrl/Alt/Shift etc.)
        -- byte 3..8 = keycodes (0 = no key)
        print(string.format("HID report dev=%d  mod=%02X keys=%02X %02X %02X",
            evt.dev,
            bytes[1] or 0,
            bytes[3] or 0, bytes[4] or 0, bytes[5] or 0))
    end
end)

-- Run event loop in a background thread
thread.create(function()
    while true do
        hid.process_events(100)
        delay.ms(10)
    end
end)
```

## Supported device types

| `usage_page`           | Value | Device            |
|------------------------|-------|-------------------|
| `hid.USAGE.KEYBOARD`   | 6     | USB Keyboard      |
| `hid.USAGE.MOUSE`      | 2     | USB Mouse         |
| `hid.USAGE.GAMEPAD`    | 5     | Gamepad/Joystick  |
| `hid.USAGE.CONSUMER`   | 12    | Media/Volume keys |
| `hid.USAGE.GENERIC`    | 1     | Generic HID       |

## Hardware requirement

- Board: **Waveshare ESP32-P4-NANO** (or any ESP32-P4 board with USB-A host port)
- No BLE required; this is USB-only

## Notes

- Call `hid.deinit()` to release the USB host driver when done
- Maximum 4 simultaneous HID devices
- Raw HID report data (`evt.data`) must be parsed per device's HID report descriptor
- The USB-A port and USB-JTAG console cannot be active simultaneously; the board `sdkconfig` already routes console to UART0
