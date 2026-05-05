# FujitsuAC for HomeKit #

This is a fork of https://github.com/Benas09/FujitsuAC which uses HomeSpan to add native HomeKit support.

See the original repository for details on building ESP32 hardware to work with this software, or to order a pre-made unit from Benas. Because of the increased memory needs of HomeSpan, you'll need to allocate at least 2mb of flash to the software. I recommend using the "Minimal SPIFFS" partition layout which allows enough space for the application with OTA.

## Installation ##
To use with the Arduino IDE, use the "manage library" feature to install the HomeSpan library. After installing, you should be able to upload the compiled sketch to your ESP32.

## Setup ##

If the unit does not have a wifi configuration, or can't find the target wifi network, it will automatically fall back into access point setup mode. You'll be able to join a network called `HomeSpan-Setup`. The default password if prompted is `homespan`. 

You should get a captive wifi portal allowing you to configure your device. After configuring the wifi, the device should reboot and you'll be able to configure it via HomeKit.

Use the "+" button in the HomeKit application and select "add accessory". Tap the "more options" button to find other devices and select the AC. The default pairing code is `466-37-726` (you can change the pairing code over serial).

The accessory will present two "Fans" - the first one controls the actual fan speed of the unit, and Fan 2 controls the vertical slats (vanes). You can relabel them when adding the device, or via the settings. (pretty sure this is a HomeKit limitation - the code sets a `ConfiguredName`). 

"Off" for Fan 1 maps to "auto" mode on the Fujitsu.

"Off" for Fan 2 maps to "swing" for vanes. This is done because the "oscillate" control isn't accessible with grouped accessories in HomeKit. 

## Updating ##

The default OTA password is `homespan-ota`. This can be changed via serial mode. 