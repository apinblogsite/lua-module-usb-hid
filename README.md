# lua_module_usb_hid

USB Host HID module for ESP-Claw.  
Allows the ESP32-P4 to act as a **USB HID host**, accepting keyboards, mice, gamepads, and other HID devices connected to the board's USB-A port.

> Supported hardware: ESP32-P4 (USB DWC OTG-HS controller). On the Waveshare ESP32-P4-NANO this maps to the USB-A connector.

---

## Lua API

```lua
local hid = require("usb_hid")
```

### `hid.init()`

Install the USB host library and HID host driver.  
Must be called once before any other function.  
Returns `true` on success, raises an error on failure.

### `hid.deinit()`

Uninstall the HID host driver and USB host library. Releases all tasks, queues, and device handles. Safe to call even if `init()` was not called.

### `hid.on_event(fn)`

Register a callback function `fn(event)` that is called by `process_events()` for each queued event.

The `event` table has the following fields depending on type:

| `event.type`     | Additional fields                                      |
|------------------|--------------------------------------------------------|
| `"connected"`    | `dev` (int), `usage_page` (int)                        |
| `"disconnected"` | `dev` (int)                                            |
| `"report"`       | `dev` (int), `usage_page` (int), `id` (int), `data` (string) |

`dev` is the device slot index (0–3).  
Pass `nil` to unregister the callback.

### `hid.process_events([timeout_ms])`

Drain the internal event queue and dispatch each event to the registered callback.  
`timeout_ms` (optional, default 0): milliseconds to wait for the first event.  
Returns the number of events dispatched.

Call this in your Lua polling loop or a dedicated thread.

### `hid.get_devices()`

Returns an array of connected device info tables:

```lua
{ { dev=0, usage_page=6 }, ... }
```

### `hid.USAGE.*`

Predefined HID usage page constants:

| Constant              | Value  | Device type        |
|-----------------------|--------|--------------------|
| `hid.USAGE.KEYBOARD`  | `0x06` | Keyboard           |
| `hid.USAGE.MOUSE`     | `0x02` | Mouse              |
| `hid.USAGE.GAMEPAD`   | `0x05` | Gamepad/Joystick   |
| `hid.USAGE.CONSUMER`  | `0x0C` | Consumer control   |
| `hid.USAGE.GENERIC`   | `0x01` | Generic desktop    |

---

## Usage example

```lua
local hid = require("usb_hid")
local delay = require("delay")

hid.init()

hid.on_event(function(evt)
    if evt.type == "connected" then
        print("Device connected: slot=" .. evt.dev ..
              " usage_page=" .. evt.usage_page)
    elseif evt.type == "disconnected" then
        print("Device disconnected: slot=" .. evt.dev)
    elseif evt.type == "report" then
        -- evt.data is a raw byte string; parse per HID report descriptor
        local bytes = { string.byte(evt.data, 1, #evt.data) }
        print("Report from slot=" .. evt.dev ..
              " len=" .. #evt.data ..
              " b1=" .. (bytes[1] or 0))
    end
end)

-- Poll loop: call from a thread or main loop
while true do
    hid.process_events(50)   -- wait up to 50 ms for first event
    delay.ms(10)
end

-- When done:
-- hid.deinit()
```

---

## Hardware notes

- **ESP32-P4-NANO**: Connect a USB HID device to the USB-A port. No additional wiring required.
- The USB Host mode and USB Device mode (USB-JTAG console) cannot run simultaneously on the same port. `sdkconfig.defaults.board` for `waveshare_esp32_p4_nano` already disables the USB-JTAG console (`CONFIG_ESP_CONSOLE_UART=y`), so the port is free for host use.
- USB 2.0 Full Speed (12 Mbit/s) and High Speed (480 Mbit/s) are supported.

## Cleanup

Call `hid.deinit()` to release all resources. Failing to call `deinit()` before rebooting is harmless but will leave USB host driver tasks running.

## Concurrency

Reports arrive from a background FreeRTOS task via a queue.  
The callback is only invoked from the Lua task context inside `process_events()`.  
No Lua API is thread-safe to call from C ISR context.
