> [!WARNING]
> I'm not really a hardware guy, so there might be stupid stuff here. I assume no liability for you using this with your Apple II. Damage could occur both to it and your ESP32 if I got anything wrong or if you wire it wrong. Use at your own risk.
> Also, this was built for my Apple IIGS and Laser 128.  They use 9 pin DE9 connectors. You'll have to figure out how to do something different if you have the 15 pin connector.

[Breadboard version](images/appleii-bluetooth-joystick.jpeg)

# Purpose

My old school analog joystick isn't great so I wanted to buy a "new" one. But I discovered that even crappy period correct ones are like $50 on ebay. I'm cheap and I also have a bunch of modern gamepads around. So I thought, why not build something to allow those modern gamepads to work with my Apple IIGS and my Laser 128?

So this let's you use a modern bluetooth gamepad on an Apple II. I'll probably still buy a joystick, but this was a fun project anyway.

If you do anything with electronics -- and let's be honest, you shouldn't build one of these if you don't -- then you probably have a lot of parts on hand. The whole thing can be built for pretty cheap if you already have common parts like breadboards, resistors, diodes, etc. The actual parts are cheap, but you end up paying shipping costs and have to buy in bulk when you only need a couple of things. See the parts list at the bottom.

Note: I also saw you can buy an A2io. It probably works better, but it was $55 and it seemed like it required using a mobile device. I didn't want to have to use a mobile app.

# Credits

I learned about how the Apple II joysticks work primarily by reading my Laser 128 technical reference manual and [this article](https://blondihacks.com/apple-ii-gamepad-prototype/) by Quinn Dunki.

For the ESP32/bluetooth side, this project uses Bluepad32. Here are some links I found helpful.
* [Some Bluepad32 docs](https://gitlab.com/ricardoquesada/bluepad32/-/blob/main/docs/plat_arduino.md#1-add-esp32-and-bluepad32-board-packages-to-board-manager)
* [ESP-IDF + Arduino + Bluepad32 template app](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template)


# Usage

## Pairing controllers

By default, bluepad32 automatically pairs with any devices trying to pair. Instead, I implemented a pairing mode so that you can ignore new controllers if you're trying to pair them with other devices in the area. To pair a new controller, you need to bring Pin 13 to low. You can do this with a physical switch or if you're using a breadboard just connect Pin 13 to ground.

If you have one controller connected, then connect another one, the second one will be the one used.

## Calibrating your controller

Pressing L1 and R1 simultaneously on your controller will capture an offset value for x and y and apply this to your joystick readings. This allows you to center the controller. If you have the TotalReplay image, there's a joystick program you can use. You can move the stick until it's in the center and press L1 and R2.

The calibration is saved in flash memory so it should hold through power loss.

# How this stuff works if you want to build one

As mentioned in the warning at the top of this readme, I know just enough hardware stuff to get by. And not enough to get by in some cases. So if you're reading this and you see I'm wrong about something or did something dangerous, let me know! Especially if there's risk of damaging an Apple II! Better yet, contribute some code or modify the design if you have a better way.

## Analog sticks using digital potentiometers (used as rheostats)

Traditional Apple II analog joysticks were pretty simple. Inside are two 150k ohm potentiometers, one for each axis. As you move the stick, it adjusts the potentiometers (pots) and changes the resistance in a circuit on the Apple II. The computer measures how long it takes to charge an internal capacitor through that resistance. Because the charging time varies, it can detect fine movements and exact angles. This is pretty cool compared to digital joysticks like the C64 or Ataris used, which were simple on-off switches for four directions.

This project works by using digital potentiometers (digipots). It converts the position of the stick on your modern gamepad to resistance values for the digipots. The Apple II expects between 0 and 150kohms resistance but 150kohm resistors are hard to find. So this project uses two 100kohm digipots in series per axis.

If you want to make it cheaper, just use one 100kohm digipot. You may lose some range at the upper end. I haven't tried this, but it seems like that's what others have done when they've built physical joysticks, and the common complaint is that it may not work great for things like flight simulators.

The code basically sets the two MCP4161 digipots per axis to the same wiper values and wires them in series. So to get 150kohms of resistance, both digipots are set to 75kohm.

Also, my oldschool joystick allowed close to a square movement pattern. My modern gamepads are pretty constrained to a circle. So the code "squares the circle" a bit to provide a more authentic response. You can control how much by tweaking the code.

Finally, there are some variables you can set to control how low the wipers can go. Play with those if you want.

## Buttons

Apple II buttons are quite simple. We get the button state from bluepad32. When a button is not being pressed, we want the Apple II switch input to float. When it's being pressed, we bring it to 5V through a 470 ohm resistor. We make use of a diode for this.

## Power rails

This project interfaces an ESP32 (3.3V logic) with an Apple II (5V logic). Because these devices operate at different voltages, some care was taken to ensure signal integrity and hardware safety. Again, I'm not an expert so please point out any flaws in the design.

## Level shifting, isolation, and fail-safe

All 3.3V SPI and control signals from the ESP32 are level-shifted through a 74HCT245 bus transceiver.
* The 74HCT245 is powered by the 5V rail from the Apple II Game I/O port.
* Note: The "T" variant (HCT) is used because its input threshold is compatible with 3.3V logic but it can output 5V for the Apple II and digipots.

To prevent backfeeding power or sending spurious SPI commands to the digipots when one system is off, the 74HCT245's OE pin is managed by a hardware failsafe.
* A 10K pullup resistor connects the OE pin to the Apple II 5V rail. So if the ESP32 is not connected or is powered down, OE is HIGH and the buffer is in a high impedance state.
* A 2N7000 MOSFET acts as an inverter. When the ESP32 GPIO pins initialize and drive the MOSFET Gate HIGH, the MOSFET pulls the OE pin to ground, enabling the outputs.

Note: The 2N7000 has a gate threshold that can range up to 3.0V. Since the ESP32 outputs 3.3V, there isn't much overhead for the transistor to turn fully on. This should still be sufficient for switching the OE pin, but your mileage may vary. To test, check the voltage at the OE pin. It should drop reasonably close to 0V when the ESP32 is active. A logic-level MOSFET with a lower gate threshold is probably better, but I had a ton of these on hand and it's working for me.

## Parts list

Here's what I used in building this device. It would probably be cheaper to make a PCB since a lot of the expense is breadboards/breakout boards, etc. Most of what I bought came in packs of a bunch, so it could be more expensive if you don't already have stuff on hand like resistors, diodes, etc.
* 1 - [ESP32](https://www.amazon.com/dp/B0D8T53CQ5?ref=ppx_yo2ov_dt_b_fed_asin_title) - $6.67 each. Make sure you get one that supports classic bluetooth mode. Look at bluepad32's docs above to make sure.
* 1 - [ESP32 breakout board](https://www.amazon.com/dp/B0BNQ85GF3?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) - $4.33 each. Not strictly necessary but made it a lot easier to mess around.
* 4 - [MCP4161-104E/P digipots](https://www.digikey.com/en/products/detail/microchip-technology/MCP4161-104E-P/1874169) - $1.38 each
* 1- [DB9 breakout board] (https://www.amazon.com/dp/B09L7JWNDQ?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) - $7.96. You can do this with a simple male connector if you want to save money. I just found it a lot easier to wire up this way.
* About 10 [104 capacitors] - about $0.70 total.
* 1- [74HCT245N](https://www.digikey.com/en/products/detail/texas-instruments/SN74HCT245N/277258) - $0.92 each.
* 2 - 1N5817 diodes
* 1 - [2N7000 MOSFET](https://www.amazon.com/dp/B0CBKHJQZF?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) - $0.08 each. 
* Resistors. Just get a kit if you don't already have any. You'll need 6x1kohm resistors, a couple of 10K resistors, a couple of 470ohm resistors.
* Wire and terminals depending on how you want to build it.
* Breadboard or a solderable board of some kind.. Get whatever you want. A good one's about $8.

## Layout

Check out the [schematic](./schematic/schematic.kicad_sch) built using [KiCad](https://www.kicad.org/). 

Notes:
* My ESP32 had different pin numbers than the drawing shows, so just make sure the right GPIO/IO lines are used rather than paying attention to the pin numbers listed.
* I also sprinkled some 0.1 µF ceramic capacitors (104s) along the power rails (VCC to GND).
* The Apple II calls for 470ohm resistors on the buttons. On my Laser 128, that kept me borderline on button presses. I DON'T RECOMMEND IT BECAUSE THE SPEC CALLS FOR 470 ohms and I'm paranoid. But if the buttons aren't triggering you could try a lower value resistor. Do this at your own risk.

# Next steps / Possible problems

It might be good to add some physical pots for the stick calibration. Some adjustments for min/max resistance and for centering. We could get much more precise calibrations that way and you wouldn't need to use a calibration program. You could also do it mid-game.

I noticed that when you have the stick all the way down and to the left, sometimes there's an overflow and the stick will jump to the top in the joystick calibration program. This also happens with my old school physical joystick so maybe it's just a practical hardware limitation.

I've never designed a PCB before, but I think this would be pretty cheap if someone wanted to give it a go. I'd go in on it with you if you want to build a bunch. I used through hole components since I suck at soldering and am scared to try doing SMDs.
