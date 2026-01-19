#include <math.h>
#include <Bluepad32.h>
#include <SPI.h>
#include <Preferences.h>

#define JOYSTICK_STEPS     1024
#define JOYSTICK_RADIUS    512
#define SQUARENESS         0.75  // see squareTheCircle for explanation
#define WIPER_SCALE_FACTOR 0.75
#define WIPER_MIN_SAFE     75    // minimum wiper value to prevent any overflow (64 would be ideal)
#define WIPER_STEPS        257   // 0 to 256, 257 steps

#define PIN_U1_CS    4  // x-axis, upper pot
#define PIN_U2_CS    5  // x-axis, lower pot
#define PIN_U3_CS    25 // y-axis, upper pot
#define PIN_U4_CS    26 // y-axis, lower pot
#define PIN_SPI_SCK  18
#define PIN_SPI_MOSI 23
#define PIN_G_BTN0   33
#define PIN_G_BTN1   32
#define PIN_PAIR     27 // Active Low

#define WRITE_WIPER_CMD 0b00000000

bool          g_is_pairing   = false;
bool          g_data_changed = false;
int32_t       g_offsetx      = 0;
int32_t       g_offsety      = 0;
uint16_t      g_wiperx       = 0.0f;
uint16_t      g_wipery       = 0.0f;
bool          g_btn0         = false;
bool          g_btn1         = true;

// 1 MHz
SPISettings digipotSPI(1000000, MSBFIRST, SPI_MODE0);
ControllerPtr controller = nullptr;
Preferences preferences;

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
    // and convert back to original range
    squaredx = constrain(cx * final_scale, -1.0f, 1.0f) * JOYSTICK_RADIUS;
    squaredy = constrain(cy * final_scale, -1.0f, 1.0f) * JOYSTICK_RADIUS;

    // Serial.printf("rawx: %+d, rawy: %+d, cx: %+.3f, cy: %+.3f, squaredx: %+.3f, squaredy: %+.3f\n", rawx, rawy, cx, cy, squaredx, squaredy);
}

void process_stick() {
    int32_t rawx = controller->axisX() - g_offsetx;
    int32_t rawy = controller->axisY() + g_offsety;

    // Apply a small center deadzone.
    if (abs(rawx) < 45) rawx = 0;
    if (abs(rawy) < 45) rawy = 0;

    float squaredx, squaredy;
    squareTheCircle(rawx, rawy, squaredx, squaredy);

    // Percentage is 0.0 at left/top, 1.0 at right/bottom
    float percentx = constrain((squaredx + 512.0f) / JOYSTICK_STEPS, 0.0f, 1.0f);
    float percenty = constrain((squaredy + 512.0f) / JOYSTICK_STEPS, 0.0f, 1.0f);

    // We want zero ohms (value 256) to 75k ohms (value 64) per pot.
    // Range is 256 to 64
    uint16_t wiperx = (WIPER_STEPS-1) - (uint16_t)(percentx * WIPER_SCALE_FACTOR * (WIPER_STEPS-1));
    uint16_t wipery = (WIPER_STEPS-1) - (uint16_t)(percenty * WIPER_SCALE_FACTOR * (WIPER_STEPS-1));

    wiperx = max((uint16_t)WIPER_MIN_SAFE, wiperx);
    wipery = max((uint16_t)WIPER_MIN_SAFE, wipery);

    if (wiperx != g_wiperx) {
        g_data_changed = true;
        g_wiperx = wiperx;
        write_digipot(PIN_U1_CS, g_wiperx);
        write_digipot(PIN_U2_CS, g_wiperx);
    }

    if (wipery != g_wipery) {
        g_data_changed = true;
        g_wipery = wipery;
        write_digipot(PIN_U3_CS, g_wipery);
        write_digipot(PIN_U4_CS, g_wipery);
    }


    // Serial.printf("rawx: %+d, squaredx: %+.3f, g_wiperx: %d, rawy: %+d, squaredy: %+.3f, g_wipery: %d\n", rawx, squaredx, g_wiperx, rawy, squaredy, g_wipery);
}

void check_for_calibration() {
    if (controller->l1() && controller->r1()) {
        g_data_changed = true;
        g_offsetx = controller->axisX();
        g_offsety = controller->axisY();

        // save to flash memory
        preferences.begin("joycal", false);
        preferences.putInt("ox", g_offsetx);
        preferences.putInt("oy", g_offsety);
        preferences.end();

        Serial.printf("Calibrated, new offsets x: %d, y: %d\n", g_offsetx, g_offsety);

        delay(1000); // mild debounce
    }
}

void process_buttons() {
    bool btn0 = controller->a();
    bool btn1 = controller->b();

    if (btn0 != g_btn0) {
        g_data_changed = true;
        g_btn0 = btn0;
        digitalWrite(PIN_G_BTN0, g_btn0);
    }

    if (btn1 != g_btn1) {
        g_data_changed = true;
        g_btn1 = btn1;
        digitalWrite(PIN_G_BTN1, g_btn1);
    }
}

void process_controller() {
    if (!controller->isGamepad()) {
        Serial.println(F("Ignoring controller input, not a gamepad"));
        return;
    }

    g_data_changed = false;

    check_for_calibration();
    process_stick();
    process_buttons();

    if (g_data_changed) {
        String g_btn0_str = g_btn0 ? "pressed" : "not pressed";
        String g_btn1_str = g_btn1 ? "pressed" : "not pressed";
        Serial.printf("offset: (%d, %d), wipers: (%.3d,%.3d), g_btn0: %s, g_btn1: %s\n",
            g_offsetx, g_offsety, g_wiperx, g_wipery, g_btn0_str.c_str(), g_btn1_str.c_str());
    }
}

void write_spi(uint8_t chip_select_pin, uint8_t command_byte, uint8_t data_byte) {
    SPI.beginTransaction(digipotSPI);
    digitalWrite(chip_select_pin, LOW);

    SPI.transfer(command_byte);
    SPI.transfer(data_byte);

    digitalWrite(chip_select_pin, HIGH);
    SPI.endTransaction();
}

// Commands to the digipot are 16 bits:
//   AAAACCDD DDDDDDDD
//   A = address
//   C = command
//   D = data (D0-D8, D9 is unused)

void write_digipot(uint8_t chip_select_pin, uint16_t data) {
    // Wiper data is 9 bits. The LSB on the command byte is the data
    uint8_t command_byte = WRITE_WIPER_CMD | (uint8_t)((data >> 8) & 0x01);
    uint8_t data_byte = (uint8_t)(data & 0xFF);
    write_spi(chip_select_pin, command_byte, data_byte);
}

void connect_terminals(uint8_t chip_select_pin) {
    uint8_t command_byte = 0x40; // TCON Register address.
    uint8_t data_byte = 0xFF;    // connect A,B,wiper
    write_spi(chip_select_pin, command_byte, data_byte);
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
    pinMode(PIN_G_BTN0, OUTPUT);
    pinMode(PIN_G_BTN1, OUTPUT);

    digitalWrite(PIN_U1_CS, HIGH);
    digitalWrite(PIN_U2_CS, HIGH);
    digitalWrite(PIN_U3_CS, HIGH);
    digitalWrite(PIN_U4_CS, HIGH);
    digitalWrite(PIN_G_BTN0, LOW);
    digitalWrite(PIN_G_BTN1, LOW);

    pinMode(PIN_PAIR, INPUT_PULLUP);

    preferences.begin("joycal", true);
    g_offsetx = preferences.getInt("ox", 0);
    g_offsety = preferences.getInt("oy", 0);
    preferences.end();
    Serial.printf("Loaded Calibration: g_offsetx: %d, g_offsety: %d\n", g_offsetx, g_offsety);

    SPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);

    connect_terminals(PIN_U1_CS);
    connect_terminals(PIN_U2_CS);
    connect_terminals(PIN_U3_CS);
    connect_terminals(PIN_U4_CS);

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

    if (pairing == g_is_pairing) return;

    g_is_pairing = pairing;
    Serial.printf("Pairing enabled: %s\n", g_is_pairing ? "true" : "false");
    BP32.enableNewBluetoothConnections(g_is_pairing);
}

void loop() {
    check_pairing();

    if (!BP32.update()) return;

    if(controller && controller->isConnected() && controller->hasData()) {
        process_controller();
    }

    vTaskDelay(1);
    //delay(150);
}
