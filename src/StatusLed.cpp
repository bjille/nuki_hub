#include "StatusLed.h"
#include "PreferencesKeys.h"
#include "EspMillis.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include "driver/gpio.h"

#define STATUS_LED_BLINK_INTERVAL_MS 500
#define STATUS_LED_BLE_ERROR_SHOW_MS 10000
#define STATUS_LED_TASK_INTERVAL_MS 50

StatusLed* StatusLed::_inst = nullptr;

void StatusLed::initialize(Preferences* preferences)
{
    if(_inst != nullptr)
    {
        return;
    }

    int pin = preferences->getInt(preference_status_led_pin, -1);
    if(pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(pin))
    {
        if(pin >= 0)
        {
            Log->print("Status LED: GPIO ");
            Log->print(pin);
            Log->println(" can't be used as output, status LED disabled.");
        }
        return;
    }

    _inst = new StatusLed(preferences);
    xTaskCreatePinnedToCore(taskFunc, "statusled", 3072, _inst, 1, nullptr, 0);
}

StatusLed* StatusLed::instance()
{
    return _inst;
}

bool StatusLed::enabled()
{
    return _inst != nullptr;
}

StatusLed::StatusLed(Preferences* preferences)
{
    _pin = preferences->getInt(preference_status_led_pin, -1);
    _type = (StatusLedType)preferences->getInt(preference_status_led_type, (int)StatusLedType::RgbWs2812);
    _flags = (uint8_t)preferences->getInt(preference_status_led_flags, STATUS_LED_DEFAULT_FLAGS);
    _brightness = (uint8_t)constrain(preferences->getInt(preference_status_led_brightness, 40), 1, 255);

    if(!isRgb())
    {
        pinMode(_pin, OUTPUT);
    }

    Log->print("Status LED on GPIO ");
    Log->print(_pin);
    Log->println(isRgb() ? " (WS2812 RGB)" : " (plain LED)");
}

bool StatusLed::isRgb() const
{
    return _type == StatusLedType::RgbWs2812 || _type == StatusLedType::RgbWs2812RgbOrder;
}

void StatusLed::setNetworkState(bool networkConnected, bool mqttConnected, bool apOpen)
{
    if(_inst == nullptr)
    {
        return;
    }
    _inst->_networkConnected = networkConnected;
    _inst->_mqttConnected = mqttConnected;
    _inst->_apOpen = apOpen;
}

void StatusLed::setBleActive(bool active)
{
    if(_inst == nullptr)
    {
        return;
    }
    _inst->_bleActive = active;
}

void StatusLed::notifyBleError()
{
    if(_inst == nullptr)
    {
        return;
    }
    _inst->_bleErrorTs = espMillis();
}

void StatusLed::taskFunc(void* param)
{
    StatusLed* led = (StatusLed*)param;
    while(true)
    {
        led->loop();
        vTaskDelay(STATUS_LED_TASK_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

void StatusLed::currentStatusColor(uint8_t& r, uint8_t& g, uint8_t& b, bool& blink)
{
    int64_t now = espMillis();

    if(_bleErrorTs != 0 && now - _bleErrorTs < STATUS_LED_BLE_ERROR_SHOW_MS)
    {
        r = 255; g = 0; b = 255;
        blink = _flags & STATUS_LED_BLINK_BLE_ERROR;
    }
    else if(_bleActive)
    {
        r = 0; g = 0; b = 255;
        blink = _flags & STATUS_LED_BLINK_BLE;
    }
    else if(_apOpen)
    {
        r = 0; g = 255; b = 255;
        blink = true;
    }
    else if(!_networkConnected)
    {
        r = 255; g = 0; b = 0;
        blink = _flags & STATUS_LED_BLINK_NO_NETWORK;
    }
    else if(_flags & STATUS_LED_OFF_WHEN_OK)
    {
        r = 0; g = 0; b = 0;
        blink = false;
    }
    else
    {
        r = 0; g = 255; b = 0;
        blink = _flags & STATUS_LED_BLINK_OK;
    }
}

void StatusLed::loop()
{
    uint8_t r = 0, g = 0, b = 0;
    bool blink = false;

    switch(_mode)
    {
    case Mode::Off:
        break;
    case Mode::Solid:
        r = _r; g = _g; b = _b;
        break;
    case Mode::Blink:
        r = _r; g = _g; b = _b;
        blink = true;
        break;
    case Mode::Auto:
    default:
        currentStatusColor(r, g, b, blink);
        break;
    }

    if(blink && ((espMillis() / STATUS_LED_BLINK_INTERVAL_MS) % 2) == 1)
    {
        r = 0; g = 0; b = 0;
    }

    write(r, g, b);
}

void StatusLed::write(uint8_t r, uint8_t g, uint8_t b)
{
    if(isRgb())
    {
        uint8_t br = _brightness;
        r = (uint16_t)r * br / 255;
        g = (uint16_t)g * br / 255;
        b = (uint16_t)b * br / 255;
    }

    if(r == _lastR && g == _lastG && b == _lastB)
    {
        return;
    }
    _lastR = r; _lastG = g; _lastB = b;

    switch(_type)
    {
    case StatusLedType::RgbWs2812:
        rgbLedWrite(_pin, r, g, b);
        break;
    case StatusLedType::RgbWs2812RgbOrder:
        // rgbLedWrite sends GRB, swap red and green for LEDs that expect RGB
        rgbLedWrite(_pin, g, r, b);
        break;
    case StatusLedType::PlainInverted:
        digitalWrite(_pin, (r | g | b) ? LOW : HIGH);
        break;
    case StatusLedType::Plain:
    default:
        digitalWrite(_pin, (r | g | b) ? HIGH : LOW);
        break;
    }
}

bool StatusLed::applyCommand(const char* payload, size_t length)
{
    String cmd = String(payload).substring(0, length);
    cmd.trim();
    String lower = cmd;
    lower.toLowerCase();

    if(lower == "auto")
    {
        _mode = Mode::Auto;
    }
    else if(lower == "off" || lower == "0")
    {
        _mode = Mode::Off;
    }
    else if(lower == "on" || lower == "1" || lower == "solid")
    {
        _mode = Mode::Solid;
    }
    else if(lower == "blink")
    {
        _mode = Mode::Blink;
    }
    else
    {
        JsonDocument doc;
        if(deserializeJson(doc, cmd.c_str()) != DeserializationError::Ok || !doc.is<JsonObject>())
        {
            Log->println("Status LED: invalid command");
            return false;
        }

        bool colorGiven = false;
        if(doc["color"].is<JsonObject>())
        {
            _r = (uint8_t)(doc["color"]["r"] | (int)_r);
            _g = (uint8_t)(doc["color"]["g"] | (int)_g);
            _b = (uint8_t)(doc["color"]["b"] | (int)_b);
            colorGiven = true;
        }
        if(doc["brightness"].is<int>())
        {
            _brightness = (uint8_t)constrain(doc["brightness"].as<int>(), 1, 255);
        }

        const char* state = doc["state"] | "";
        const char* effect = doc["effect"] | "";

        if(strcasecmp(state, "OFF") == 0)
        {
            _mode = Mode::Off;
        }
        else if(strcasecmp(effect, "auto") == 0)
        {
            _mode = Mode::Auto;
        }
        else if(strcasecmp(effect, "blink") == 0)
        {
            _mode = Mode::Blink;
        }
        else if(strcasecmp(effect, "solid") == 0 || colorGiven)
        {
            _mode = Mode::Solid;
        }
        else if(strcasecmp(state, "ON") == 0 && _mode == Mode::Off)
        {
            _mode = _lastOnMode;
        }
    }

    if(_mode != Mode::Off)
    {
        _lastOnMode = _mode;
    }

    _stateChanged = true;
    return true;
}

void StatusLed::stateJson(char* buffer, size_t bufferSize)
{
    JsonDocument doc;
    doc["state"] = _mode == Mode::Off ? "OFF" : "ON";

    switch(_mode)
    {
    case Mode::Blink:
        doc["effect"] = "blink";
        break;
    case Mode::Solid:
        doc["effect"] = "solid";
        break;
    case Mode::Auto:
    case Mode::Off:
    default:
        doc["effect"] = (_mode == Mode::Off && _lastOnMode != Mode::Auto) ? (_lastOnMode == Mode::Blink ? "blink" : "solid") : "auto";
        break;
    }

    if(isRgb())
    {
        doc["color_mode"] = "rgb";
        doc["brightness"] = (int)_brightness;
        JsonObject color = doc["color"].to<JsonObject>();
        color["r"] = (int)_r;
        color["g"] = (int)_g;
        color["b"] = (int)_b;
    }
    else
    {
        doc["color_mode"] = "onoff";
    }

    serializeJson(doc, buffer, bufferSize);
}

bool StatusLed::consumeStateChanged()
{
    bool changed = _stateChanged;
    _stateChanged = false;
    return changed;
}
