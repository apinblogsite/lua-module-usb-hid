-- start_usb_hid_monitor.lua
-- Mulai USB HID host monitoring. Cetak semua event ke konsol.
-- Jalankan dari agent atau CLI.

local hid    = require("usb_hid")
local delay  = require("delay")
local thread = require("thread")

print("[usb_hid] Initializing USB HID host...")
hid.init()
print("[usb_hid] Ready. Connect a USB keyboard, mouse, or gamepad to the USB-A port.")

hid.on_event(function(evt)
    if evt.type == "connected" then
        local name = "Unknown"
        if evt.usage_page == hid.USAGE.KEYBOARD then
            name = "Keyboard"
        elseif evt.usage_page == hid.USAGE.MOUSE then
            name = "Mouse"
        elseif evt.usage_page == hid.USAGE.GAMEPAD then
            name = "Gamepad"
        elseif evt.usage_page == hid.USAGE.CONSUMER then
            name = "Consumer Control"
        end
        print(string.format("[usb_hid] %s connected (slot %d, usage=0x%02X)",
                            name, evt.dev, evt.usage_page))

    elseif evt.type == "disconnected" then
        print(string.format("[usb_hid] Device disconnected (slot %d)", evt.dev))

    elseif evt.type == "report" then
        local hex = {}
        for i = 1, math.min(#evt.data, 8) do
            hex[i] = string.format("%02X", string.byte(evt.data, i))
        end
        print(string.format("[usb_hid] slot=%d  %s", evt.dev, table.concat(hex, " ")))
    end
end)

-- Background polling thread
thread.create(function()
    while true do
        hid.process_events(50)
        delay.ms(5)
    end
end)
