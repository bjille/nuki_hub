#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Optional status LED (plain LED or WS2812 RGB LED, e.g. GPIO 35 on the M5Stack AtomS3 Lite).
//
// AUTO mode shows the Nuki Hub status (highest priority first):
//   BLE command failed (10 s)  -> magenta
//   BLE communication active   -> blue
//   Wi-Fi access point open    -> cyan (always blinking)
//   No network                 -> red
//   Everything OK              -> green (or off, configurable)
// For every state you can choose blinking or solid in the web interface.
//
// The LED can be overridden via MQTT (<path>/maintenance/statusLed/action), compatible with
// a Home Assistant JSON light: {"state":"ON","color":{"r":255,"g":0,"b":0},"brightness":80,"effect":"blink"}
// Effects: "auto" (show status), "solid", "blink". Plain payloads "auto", "on", "off", "blink" also work.

enum class StatusLedType
{
    Plain = 0,
    PlainInverted = 1,
    RgbWs2812 = 2,
    RgbWs2812RgbOrder = 3   // WS2812-type LED with red and green swapped (RGB instead of GRB order)
};

// Bits for preference_status_led_flags (0x02 was "blink when MQTT disconnected", no longer used)
#define STATUS_LED_BLINK_NO_NETWORK 0x01
#define STATUS_LED_BLINK_OK         0x04
#define STATUS_LED_BLINK_BLE        0x08
#define STATUS_LED_BLINK_BLE_ERROR  0x10
#define STATUS_LED_OFF_WHEN_OK      0x20
#define STATUS_LED_DEFAULT_FLAGS    (STATUS_LED_BLINK_NO_NETWORK | STATUS_LED_BLINK_BLE_ERROR)

class StatusLed
{
public:
    // Creates the singleton and starts the LED task when a pin is configured.
    static void initialize(Preferences* preferences);
    static StatusLed* instance();
    static bool enabled();

    // Status input (safe to call from any task, also when the LED is disabled)
    static void setNetworkState(bool networkConnected, bool mqttConnected, bool apOpen);
    static void setBleActive(bool active);
    static void notifyBleError();

    // MQTT / Home Assistant override. Returns true if the payload was understood.
    bool applyCommand(const char* payload, size_t length);
    // Fills buffer with the Home Assistant JSON light state.
    void stateJson(char* buffer, size_t bufferSize);
    bool consumeStateChanged();

    bool isRgb() const;

private:
    enum class Mode
    {
        Auto,
        Off,
        Solid,
        Blink
    };

    explicit StatusLed(Preferences* preferences);
    static void taskFunc(void* param);
    void loop();
    void currentStatusColor(uint8_t& r, uint8_t& g, uint8_t& b, bool& blink);
    void write(uint8_t r, uint8_t g, uint8_t b);

    static StatusLed* _inst;

    int _pin = -1;
    StatusLedType _type = StatusLedType::Plain;
    uint8_t _flags = STATUS_LED_DEFAULT_FLAGS;
    volatile uint8_t _brightness = 40;

    volatile bool _networkConnected = false;
    volatile bool _mqttConnected = false;
    volatile bool _apOpen = false;
    volatile bool _bleActive = false;
    volatile int64_t _bleErrorTs = 0;

    volatile Mode _mode = Mode::Auto;
    Mode _lastOnMode = Mode::Auto;
    volatile uint8_t _r = 255;
    volatile uint8_t _g = 255;
    volatile uint8_t _b = 255;
    volatile bool _stateChanged = true;

    uint8_t _lastR = 1, _lastG = 1, _lastB = 1;
};
