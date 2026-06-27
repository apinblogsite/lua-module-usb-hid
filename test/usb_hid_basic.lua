-- test/usb_hid_basic.lua
-- Basic USB HID host test: init, wait for a device, print reports, then deinit.
-- Connect a USB keyboard or mouse to the USB-A port of the Waveshare ESP32-P4-NANO.

local hid   = require("usb_hid")
local delay = require("delay")

print("[usb_hid_test] Starting USB HID host...")
hid.init()

local report_count = 0
local MAX_REPORTS   = 20

hid.on_event(function(evt)
    if evt.type == "connected" then
        print(string.format("[usb_hid_test] CONNECTED  dev=%d usage_page=0x%02x", evt.dev, evt.usage_page))
        if evt.usage_page == hid.USAGE.KEYBOARD then
            print("[usb_hid_test] => Keyboard detected")
        elseif evt.usage_page == hid.USAGE.MOUSE then
            print("[usb_hid_test] => Mouse detected")
        elseif evt.usage_page == hid.USAGE.GAMEPAD then
            print("[usb_hid_test] => Gamepad detected")
        end

    elseif evt.type == "disconnected" then
        print(string.format("[usb_hid_test] DISCONNECTED dev=%d", evt.dev))

    elseif evt.type == "report" then
        report_count = report_count + 1
        -- Print first 8 bytes in hex
        local hex = {}
        for i = 1, math.min(#evt.data, 8) do
            hex[i] = string.format("%02X", string.byte(evt.data, i))
        end
        print(string.format("[usb_hid_test] REPORT #%d dev=%d len=%d  %s",
                            report_count, evt.dev, #evt.data, table.concat(hex, " ")))
    end
end)

print("[usb_hid_test] Waiting for HID device (plug in USB keyboard/mouse)...")
print(string.format("[usb_hid_test] Will capture up to %d reports then exit.", MAX_REPORTS))

while report_count < MAX_REPORTS do
    hid.process_events(100)   -- block up to 100 ms
    delay.ms(5)
end

print("[usb_hid_test] Done. Devices still connected:")
local devs = hid.get_devices()
for _, d in ipairs(devs) do
    print(string.format("  slot=%d usage_page=0x%02x", d.dev, d.usage_page))
end

hid.deinit()
print("[usb_hid_test] Deinitialized. Test complete.")
