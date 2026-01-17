> [!WARNING]
> Note: this isn't yet working. Just a repo where I'm playing around! So don't use as-is!
> Also, I'm not really a hardware guy, so there might be stupid stuff here.

# appleii-bluetooth-joystick

Project to allow you to use bluetooth joysticks on an Apple II.

# Pairing controllers

Pairing is automatic, but resets when you lose power. Could probably implement something better, but this isn't a production kinda situation, you know? Probably worth adding a switch or something to put the esp32 in pairing mode.

## Bluepad32

This project uses Bluepad32. Here are some links I found helpful.
* [Some Bluepad32 docs](https://gitlab.com/ricardoquesada/bluepad32/-/blob/main/docs/plat_arduino.md#1-add-esp32-and-bluepad32-board-packages-to-board-manager)
* [ESP-IDF + Arduino + Bluepad32 template app](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template)


# Analog sticks using digital potentiometers (used as rheostats)

Apple II joysticks use resistance to determine how far along the x and y axis the stick is. For this project, I used two 100 kohm digital potentiometers/rheostats per axis. The Apple II expects between 0 and 150kohms resistance. 150kohm digital pots seem nearly impossible to find. At least I couldn't find any.

If you want to make it cheaper, just use one 100kohm digipot. You'll just lose some range at the upper end. I haven't tried this, but it seems like that's what others have done when they've built physical joysticks...

But for mine, I use two [MCP4161-104E/P digipots](https://www.digikey.com/en/products/detail/microchip-technology/MCP4161-104E-P/1874169). These have 256 steps and when used in serial we can provide the expected range for the Apple II. These were only $1.38 when I bought them, so I bought a handful of them. Shipping is extra.

So how to use 2 per axis? The Bluepad32 library gives readings between -512 and 511 for analog axes. At least on my controller. At -512, we set the digipots to 0kohms. At 512, we set them to 150kohms total for the series.

To keep everything simpler, we just set equal resistance on each digipot. So if we want to go max resistance, we set both digipots to 75kohms.

# Buttons

The Apple II buttons are active low, so we write zero when pressed, high when not pressed.
TODO: add some notes about transistors, resistors, all that jazz.
