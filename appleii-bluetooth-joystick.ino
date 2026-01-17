#include <math.h>
#include <Bluepad32.h>

#define JOYSTICK_STEPS     1024
#define JOYSTICK_RADIUS    512
#define SQUARENESS         0.75  // see squareTheCircle for explanation
#define WIPER_SCALE_FACTOR 0.75
#define JOYSTICK_STEPS     1024  // 0 to 1023
#define WIPER_STEPS        257   // 0 to 256, 257 steps

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
    bool foundEmptySlot = false;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
            // Additionally, you can get certain gamepad properties like:
            // Model, VID, PID, BTAddr, flags, etc.
            ControllerProperties properties = ctl->getProperties();
            Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName().c_str(), properties.vendor_id,
                           properties.product_id);
            myControllers[i] = ctl;
            foundEmptySlot = true;
            break;
        }
    }
    if (!foundEmptySlot) {
        Serial.println("CALLBACK: Controller connected, but could not found empty slot");
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    bool foundController = false;

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
            myControllers[i] = nullptr;
            foundController = true;
            break;
        }
    }

    if (!foundController) {
        Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
    }
}

// Apple II joysticks (at the least the one I have) allows input
// along the x- and y-axis to be closer to a square, or more like
// a rounded square.
//
// Modern analog sticks (at least the one I have) seem to be more
// constrained to a physical circle.
//
// So this function will project our inputs out to fill a square.
//
// You can control how much of a square we make using SQUARENESS.

void squareTheCircle(int32_t &rawx, int32_t &rawy, float &squaredx, float &squaredy) {
    // normalize the inputs to a unit circle
    float cx = (float)rawx / JOYSTICK_RADIUS;
    float cy = (float)rawy / JOYSTICK_RADIUS;

    float radius = cx * cx + cy * cy;

    if (radius < 0.0001) {
        squaredx = 0.0f;
        squaredy = 0.0f;
        return;
    }

    // angle/direction of the input
    float theta = atan2(cy, cx);
    float cos_theta = cos(theta);
    float sin_theta = sin(theta);

    // now project our vector outwards until it would hit the
    // edge of a square. scale will always be >= 1.0;
    float scale = 1.0f;
    if (abs(cos_theta) > abs(sin_theta)) {
        // will hit left or right edge first, so scale such that
        // x would reach +/- 1.
        scale = 1.0f / abs(cos_theta);
    }
    else {
        // will hit top or bottom edge first, so scale such that
        // y would reach +/- 1.
        scale = 1.0f / abs(sin_theta);
    }

    // Interpolate between original circle and square,
    // allowing for a squareness factor.
    float final_scale = 1.0f + (scale - 1.0f) * SQUARENESS;

    // make sure to clamp so we never output greater than 1.0.
    squaredx = constrain(cx * final_scale, -1.0, 1.0);
    squaredy = constrain(cy * final_scale, -1.0, 1.0);

    // now convert back to original range
    squaredx = squaredx * JOYSTICK_RADIUS;
    squaredy = squaredy * JOYSTICK_RADIUS;

    Serial.printf("rawx: %+.3f, rawy: %+.3f, cx: %+.3f, cy: %+.3f, squaredx: %+.3f, squaredy: %+.3f\n", (float)rawx, (float)rawy, cx, cy, squaredx, squaredy);
}

void dumpGamepad(ControllerPtr ctl) {
    int32_t rawx = ctl->axisX();
    int32_t rawy = ctl->axisY();

    float squaredx, squaredy;
    squareTheCircle(rawx, rawy, squaredx, squaredy);

    float percentx = (squaredx + 512) / JOYSTICK_STEPS;
    int32_t wiperx = WIPER_STEPS * percentx * WIPER_SCALE_FACTOR;

    float percenty = (squaredy + 512) / JOYSTICK_STEPS;
    int32_t wipery = WIPER_STEPS * percenty * WIPER_SCALE_FACTOR;

    Serial.printf("rawx: %+.3f, squaredx: %+.3f, wiperx: %d, rawy: %+.3f, squaredy: %+.3f, wiperY: %d\n", rawx, squaredx, wiperx, rawy, squaredy, wipery);

}

void processGamepad(ControllerPtr ctl) {
    // There are different ways to query whether a button is pressed.
    // By query each button individually:
    //  a(), b(), x(), y(), l1(), etc...
    if (ctl->a()) {
        static int colorIdx = 0;
        // Some gamepads like DS4 and DualSense support changing the color LED.
        // It is possible to change it by calling:
        switch (colorIdx % 3) {
            case 0:
                // Red
                ctl->setColorLED(255, 0, 0);
                break;
            case 1:
                // Green
                ctl->setColorLED(0, 255, 0);
                break;
            case 2:
                // Blue
                ctl->setColorLED(0, 0, 255);
                break;
        }
        colorIdx++;
    }

    if (ctl->b()) {
        // Turn on the 4 LED. Each bit represents one LED.
        static int led = 0;
        led++;
        // Some gamepads like the DS3, DualSense, Nintendo Wii, Nintendo Switch
        // support changing the "Player LEDs": those 4 LEDs that usually indicate
        // the "gamepad seat".
        // It is possible to change them by calling:
        ctl->setPlayerLEDs(led & 0x0f);
    }

    if (ctl->x()) {
        // Some gamepads like DS3, DS4, DualSense, Switch, Xbox One S, Stadia support rumble.
        // It is possible to set it by calling:
        // Some controllers have two motors: "strong motor", "weak motor".
        // It is possible to control them independently.
        ctl->playDualRumble(0 /* delayedStartMs */, 250 /* durationMs */, 0x80 /* weakMagnitude */,
                            0x40 /* strongMagnitude */);
    }

    // Another way to query controller data is by getting the buttons() function.
    // See how the different "dump*" functions dump the Controller info.
    dumpGamepad(ctl);
}


void processControllers() {
    for (auto myController : myControllers) {
        if (myController && myController->isConnected() && myController->hasData()) {
            if (myController->isGamepad()) {
                processGamepad(myController);
            } else {
                Serial.println("Unsupported controller");
            }
        }
    }
}

// Arduino setup function. Runs in CPU 1
void setup() {
    // let things settle before starting. was getting garbe in serial monitor until I did this.
    delay(1000);
    Serial.flush();
    Serial.begin(115200);
    Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Setup the Bluepad32 callbacks
    BP32.setup(&onConnectedController, &onDisconnectedController);

    // "forgetBluetoothKeys()" should be called when the user performs
    // a "device factory reset", or similar.
    // Calling "forgetBluetoothKeys" in setup() just as an example.
    // Forgetting Bluetooth keys prevents "paired" gamepads to reconnect.
    // But it might also fix some connection / re-connection issues.
    BP32.forgetBluetoothKeys();

    // Enables mouse / touchpad support for gamepads that support them.
    // When enabled, controllers like DualSense and DualShock4 generate two connected devices:
    // - First one: the gamepad
    // - Second one, which is a "virtual device", is a mouse.
    // By default, it is disabled.
    BP32.enableVirtualDevice(false);
}

// Arduino loop function. Runs in CPU 1.
void loop() {
    // This call fetches all the controllers' data.
    // Call this function in your main loop.
    bool dataUpdated = BP32.update();
    if (dataUpdated)
        processControllers();

    // The main loop must have some kind of "yield to lower priority task" event.
    // Otherwise, the watchdog will get triggered.
    // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
    // Detailed info here:
    // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

    vTaskDelay(1);
    delay(150);
}
