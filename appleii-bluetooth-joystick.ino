#include <math.h>
#include <Bluepad32.h>
#include <SPI.h>

#define JOYSTICK_STEPS     1024
#define JOYSTICK_RADIUS    512
#define SQUARENESS         0.75  // see squareTheCircle for explanation
#define WIPER_SCALE_FACTOR 0.75
#define JOYSTICK_STEPS     1024  // 0 to 1023
#define WIPER_STEPS        257   // 0 to 256, 257 steps

#define PIN_U1_CS    4  // x-axis, upper pot
#define PIN_U2_CS    5  // x-axis, lower pot
#define PIN_U3_CS    16 // y-axis, upper pot
#define PIN_U4_CS    19 // y-axis, lower pot
#define PIN_SPI_SCK  18
#define PIN_SPI_MOSI 23
#define PIN_BTN0     32
#define PIN_BTN1     33
#define PIN_PAIR     26 // Active Low

#define WRITE_WIPER_CMD 0b00000000

volatile bool is_pairing = false;
uint8_t       wiperx     = 0.0f;
uint8_t       wipery     = 0.0f;
bool          btn0       = false;
bool          btn1       = true;

// 1 MHz
SPISettings digipotSPI(1000000, MSBFIRST, SPI_MODE0);
ControllerPtr controller = nullptr;

void onConnectedController(ControllerPtr ctl) {
    ControllerProperties properties = ctl->getProperties();

    Serial.printf("Controller connected: %s, VID=0x%04x, PID=0x%04x\n",
        ctl->getModelName().c_str(), properties.vendor_id, properties.product_id);

    controller = ctl;
}

void onDisconnectedController(ControllerPtr ctl) {
    ControllerProperties properties = ctl->getProperties();

    Serial.printf("Controller disconnected: %s, VID=0x%04x, PID=0x%04x\n",
        ctl->getModelName().c_str(), properties.vendor_id, properties.product_id);
    
    if (controller == ctl) {
        // Note: don't know how bluepad32 handles pointers. There could be a memory leak.
        // Just setting our ptr to null
        controller = nullptr;
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
        scale = 1.0f / max(abs(cos_theta), 0.0001f);
    }
    else {
        // will hit top or bottom edge first, so scale such that
        // y would reach +/- 1.
        scale = 1.0f / max(abs(sin_theta), 0.0001f);
    }

    // Interpolate between original circle and square,
    // allowing for a squareness factor.
    float final_scale = 1.0f + (scale - 1.0f) * SQUARENESS;

    // make sure to clamp so we never output greater than 1.0.
    squaredx = constrain(cx * final_scale, -1.0f, 1.0f);
    squaredy = constrain(cy * final_scale, -1.0f, 1.0f);

    // now convert back to original range
    squaredx = squaredx * JOYSTICK_RADIUS;
    squaredy = squaredy * JOYSTICK_RADIUS;

    // Serial.printf("rawx: %+d, rawy: %+d, cx: %+.3f, cy: %+.3f, squaredx: %+.3f, squaredy: %+.3f\n", rawx, rawy, cx, cy, squaredx, squaredy);
}

void process_stick() {
    int32_t rawx = controller->axisX();
    int32_t rawy = controller->axisY();

    float squaredx, squaredy;
    squareTheCircle(rawx, rawy, squaredx, squaredy);

    float percentx = (squaredx + 512.0f) / JOYSTICK_STEPS;
    wiperx = WIPER_STEPS * percentx * WIPER_SCALE_FACTOR;

    float percenty = (squaredy + 512.0f) / JOYSTICK_STEPS;
    wipery = WIPER_STEPS * percenty * WIPER_SCALE_FACTOR;

    write_digipot(PIN_U1_CS, wiperx);
    write_digipot(PIN_U2_CS, wiperx);

    write_digipot(PIN_U3_CS, wipery);
    write_digipot(PIN_U4_CS, wipery);

    // Serial.printf("rawx: %+d, squaredx: %+.3f, wiperx: %d, rawy: %+d, squaredy: %+.3f, wipery: %d\n", rawx, squaredx, wiperx, rawy, squaredy, wipery);
}

void process_buttons() {
    btn0 = controller->a();
    btn1 = controller->b();

    if (btn0)
        digitalWrite(PIN_BTN0, HIGH);
    else
        digitalWrite(PIN_BTN0, LOW);
    
    if (btn1)
        digitalWrite(PIN_BTN1, HIGH);
    else
        digitalWrite(PIN_BTN1, LOW);
}

void process_controller() {
    if (!controller->isGamepad()) {
        Serial.println(F("Ignoring controller input, not a gamepad"));
        return;
    }

    process_stick();
    process_buttons();

    String btn0_str = btn0 ? "pressed" : "not pressed";
    String btn1_str = btn1 ? "pressed" : "not pressed";
    Serial.printf("wipers: (%.3d,%.3d), btn0: %s, btn1: %s\n",
        wiperx, wipery, btn0_str.c_str(), btn1_str.c_str());
}

void write_digipot(uint8_t chip_select_pin, uint16_t data) {
    SPI.beginTransaction(digipotSPI);
    digitalWrite(chip_select_pin, LOW);

    // Commands to the digipot are 16 bits:
    //   AAAACCDD DDDDDDDD
    //   A = address
    //   C = command
    //   D = data (D0-D8, D9 is unused)

    // Because we never go above 192 for the wiper for each
    // digipot, we don't need the two MSBs. 
    byte command_byte = WRITE_WIPER_CMD;

    SPI.transfer(WRITE_WIPER_CMD);
    SPI.transfer(data);

    digitalWrite(chip_select_pin, HIGH);
    SPI.endTransaction();
}

void setup() {
    // let things settle before starting. was getting garbage in serial monitor until I did this.
    delay(1000);
    Serial.flush();
    Serial.begin(115200);

    pinMode(PIN_U1_CS, OUTPUT);
    pinMode(PIN_U2_CS, OUTPUT);
    pinMode(PIN_U3_CS, OUTPUT);
    pinMode(PIN_U4_CS, OUTPUT);
    pinMode(PIN_BTN0, OUTPUT);
    pinMode(PIN_BTN1, OUTPUT);

    digitalWrite(PIN_U1_CS, HIGH);
    digitalWrite(PIN_U2_CS, HIGH);
    digitalWrite(PIN_U3_CS, HIGH);
    digitalWrite(PIN_U4_CS, HIGH);
    digitalWrite(PIN_BTN0, LOW);
    digitalWrite(PIN_BTN1, LOW);

    pinMode(PIN_PAIR, INPUT_PULLUP);

    SPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);

    Serial.printf("Bluepad32 Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Setup the Bluepad32 callbacks
    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys();

    Serial.println(F("Pairing enabled: false"));
    BP32.enableNewBluetoothConnections(false);
}

void check_pairing() {
    bool pairing = !digitalRead(PIN_PAIR);

    if (pairing == is_pairing) return;

    is_pairing = pairing;
    Serial.printf("Pairing enabled: %s\n", is_pairing ? "true" : "false");
    BP32.enableNewBluetoothConnections(is_pairing);
}

void loop() {
    check_pairing();

    if (!BP32.update()) return;

    if(controller && controller->isConnected() && controller->hasData()) {
        process_controller();
    }

    vTaskDelay(1);
    delay(150);
}
