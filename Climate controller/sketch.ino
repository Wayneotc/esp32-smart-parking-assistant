/*
  Board : ESP32 DevKit
  Parts : DHT22, 16x2 I2C LCD (0x27), relay + DC fan, buzzer, alarm LED,
          2 buttons, Telegram alerts
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Preferences.h>

// ======================= USER SETTINGS =======================
// Use "Velxio-GUEST" for Wi-Fi in the simulator, with an empty password.
const char* WIFI_SSID = "Velxio-GUEST";
const char* WIFI_PASS = "";
// Token format: number, colon, long string (e.g. 1234567890:AAH...)
const char* BOT_TOKEN = "";
const char* CHAT_ID   = "";

#define RELAY_ACTIVE_LOW  false
// Set to true if the buzzer sounds when the pin is LOW, false if it sounds when HIGH
#define BUZZER_ACTIVE_LOW false

const float DEFAULT_SETPOINT = 30.0;   // fan ON at/above this
const float HYSTERESIS       = 2.0;    // fan OFF at setpoint - this
const float ALARM_MARGIN     = 5.0;    // alarm ON at setpoint + this
const float ALARM_CLEAR_BAND = 1.0;    // alarm OFF at setpoint + margin - this
const float SETPOINT_MIN     = 20.0;
const float SETPOINT_MAX     = 40.0;
const float SETPOINT_STEP    = 0.5;

// If the sensor fails, run the fan (safer for live birds / plants).
const bool FAN_ON_SENSOR_FAIL = true;
// =============================================================

// Pins
#define DHT_PIN     4
#define RELAY_PIN   26
#define BUZZER_PIN  25
#define LED_PIN     33
#define BTN_UP      18
#define BTN_DOWN    19
#define DHT_TYPE    DHT22

// Timing (ms)
const unsigned long READ_INTERVAL    = 2000;
const unsigned long DISPLAY_INTERVAL = 500;
const unsigned long BOT_INTERVAL     = 3000;
const unsigned long WIFI_RETRY       = 15000;
const unsigned long ALERT_REPEAT_MS  = 300000;  // repeat alert every 5 min
const unsigned long BTN_REPEAT       = 250;
const unsigned long SAVE_DELAY       = 2000;

// Telegram tasks
const unsigned long SEND_RETRY_MS = 2000;
const int SEND_MAX_TRIES = 3;

// Objects
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);
Preferences prefs;

// State
enum AlarmReason { ALARM_NONE, ALARM_HOT, ALARM_SENSOR };
AlarmReason alarmReason = ALARM_NONE;

float temperature = NAN;
float humidity    = NAN;
float setpoint    = DEFAULT_SETPOINT;
bool  fanOn       = false;
bool  sensorFail  = false;
int   failCount   = 0;
bool  muted       = false;
bool  buzzerOn    = false; // Keep track of buzzer state

unsigned long lastRead     = 0;
unsigned long lastDisplay  = 0;
unsigned long lastBot      = 0;
unsigned long lastWifi     = 0;
unsigned long lastAlert    = 0;
unsigned long lastBlink    = 0;
unsigned long lastBtn      = 0;
unsigned long lastSetpointChange = 0;
unsigned long nextTry      = 0;
unsigned long lastPoll     = 0;


bool blinkState    = false;
bool setpointDirty = false;
bool lastUpState   = HIGH;
bool lastDownState = HIGH;
bool bothHeld      = false;
bool announced     = false;

// ---------------------------------------------------------------
void setRelay(bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

void setBuzzer(bool on) {
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? !on : on);
  buzzerOn = on;
}

bool telegramConfigured() {
  // Token format is 123456789:AAH...
  return strlen(BOT_TOKEN) > 20 && strchr(BOT_TOKEN, ':') != nullptr;
}

bool telegramReady() {
  return WiFi.status() == WL_CONNECTED && telegramConfigured();
}

struct Msg {
  String text;
  int tries;
};
QueueHandle_t msgQueue;
Msg pending = {"", 0};
bool havePending = false;


void sendTelegram(const String& msg) {
  Msg m = {msg, 0};
  xQueueSend(msgQueue, &m, portMAX_DELAY);
}

String statusText() {
  String s = "Climate status\n";
  s += "Temperature: " + (isnan(temperature) ? String("--") : String(temperature, 1)) + " C\n";
  s += "Humidity: " + (isnan(humidity) ? String("--") : String(humidity, 0)) + " %\n";
  s += "Setpoint: " + String(setpoint, 1) + " C\n";
  s += "Fan: " + String(fanOn ? "ON" : "OFF") + "\n";
  if (alarmReason == ALARM_HOT)         s += "ALARM: too hot";
  else if (alarmReason == ALARM_SENSOR) s += "ALARM: sensor not responding";
  else                                  s += "Alarm: none";
  return s;
}

void readSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    failCount++;
    if (failCount >= 3) sensorFail = true;
  } else {
    failCount = 0;
    sensorFail = false;
    temperature = t;
    humidity = h;
  }
}

void onAlarmChange() {
  lastAlert = millis();
  if (alarmReason == ALARM_NONE) {
    muted = false;
    sendTelegram("Back to normal.\n" + statusText());
    Serial.println("Alarm state changed: ALARM_NONE");
  } else if (alarmReason == ALARM_HOT) {
    sendTelegram("WARNING: too hot!\n" + statusText());
    Serial.println("Alarm state changed: ALARM_HOT");
  } else { // ALARM_SENSOR
    sendTelegram("WARNING: DHT22 sensor not responding. Fan forced ON.\n" + statusText());
    Serial.println("Alarm state changed: ALARM_SENSOR");
  }
}

void updateControl() {
  // Fan with hysteresis
  if (sensorFail) {
    fanOn = FAN_ON_SENSOR_FAIL;
  } else if (!isnan(temperature)) {
    if (temperature >= setpoint) fanOn = true;
    else if (temperature <= setpoint - HYSTERESIS) fanOn = false;
  }
  setRelay(fanOn);

  // Alarm state
  AlarmReason r = alarmReason;
  if (sensorFail) {
    r = ALARM_SENSOR;
  } else if (!isnan(temperature)) {
    if (temperature >= setpoint + ALARM_MARGIN) r = ALARM_HOT;
    else if (r == ALARM_SENSOR && temperature <= setpoint) r = ALARM_NONE; // Clear sensor alarm if temp is normal
    else if (r == ALARM_HOT && temperature <= setpoint + ALARM_MARGIN - ALARM_CLEAR_BAND) r = ALARM_NONE;
  }
  if (r != alarmReason) {
    alarmReason = r;
    onAlarmChange();
  }
}

void repeatAlert(unsigned long now) {
  if (alarmReason != ALARM_NONE && now - lastAlert >= ALERT_REPEAT_MS) {
    lastAlert = now;
    sendTelegram("STILL ALARMING:\n" + statusText());
  }
}

void updateAlarmOutputs(unsigned long now) {
  if (alarmReason == ALARM_NONE) {
    digitalWrite(LED_PIN, LOW);
    setBuzzer(false); // Ensure buzzer is off
    return;
  }

  // Blink the LED and Buzzer for alerts
  unsigned long period = (alarmReason == ALARM_HOT) ? 300 : 800;
  if (now - lastBlink >= period) {
    lastBlink = now;
    blinkState = !blinkState;
  }

  digitalWrite(LED_PIN, blinkState ? HIGH : LOW);

  // Only sound buzzer if not muted
  if (!muted) {
    setBuzzer(blinkState);
  } else {
    setBuzzer(false); // If muted, keep buzzer off
  }
}

void handleButtons(unsigned long now) {
  bool up = (digitalRead(BTN_UP) == LOW);
  bool down = (digitalRead(BTN_DOWN) == LOW);

  if (up && down) {
    if (!bothHeld && alarmReason != ALARM_NONE) {
      muted = !muted;
      setBuzzer(false); // Mute immediately
    }
    bothHeld = true;
    return;
  }
  if (bothHeld) {
    if (!up && !down) bothHeld = false;
    return;
  }
  if ((up || down) && now - lastBtn >= BTN_REPEAT) {
    lastBtn = now;
    setpoint += up ? SETPOINT_STEP : -SETPOINT_STEP;
    setpoint = constrain(setpoint, SETPOINT_MIN, SETPOINT_MAX);
    setpointDirty = true;
    lastSetpointChange = now;
    updateControl();
  }
}

void saveSetpointIfNeeded(unsigned long now) {
  if (setpointDirty && now - lastSetpointChange >= SAVE_DELAY) {
    prefs.putFloat("setpoint", setpoint);
    setpointDirty = false;
  }
}

void maintainWifi(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED && now - lastWifi >= WIFI_RETRY) {
    lastWifi = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

float requestedSetpoint = DEFAULT_SETPOINT;
bool setpointRequest = false;
void applySetpointRequest() {
  if (setpointRequest) {
    setpoint = constrain(requestedSetpoint, SETPOINT_MIN, SETPOINT_MAX);
    prefs.putFloat("setpoint", setpoint);
    setpointDirty = false;
    updateControl();
    setpointRequest = false;
  }
}

void telegramTask(void* pvParameters) {
  (void) pvParameters;

  while (true) {
    if (telegramReady()) {
      if (!announced) {
        Serial.printf("Telegram: connected to %s. Bot token: %s, Chat ID: %s.\n",
                      WiFi.SSID().c_str(), BOT_TOKEN, CHAT_ID);
        sendTelegram("Controller started. Current status:\n" + statusText());
        announced = true;
      }

      // Send any pending message
      if (!havePending) {
        if (xQueueReceive(msgQueue, &pending, 0) == pdTRUE) {
          havePending = true;
          pending.tries = 0;
          nextTry = 0;
        }
      }
      if (havePending && millis() >= nextTry) {
        Serial.printf("Telegram: sending \"%s\" (try %d)\n", pending.text.c_str(), pending.tries + 1);
        bool ok = bot.sendMessage(CHAT_ID, String(pending.text), "");
        if (ok) {
          Serial.println("Telegram: message sent");
          havePending = false;
        } else {
          pending.tries++;
          Serial.printf("Telegram: send failed (try %d/%d). Check token, chat id, and press Start in the bot.\n",
                        pending.tries, SEND_MAX_TRIES);
          if (pending.tries >= SEND_MAX_TRIES) havePending = false;
          else nextTry = millis() + SEND_RETRY_MS;
        }
      }

      // Check for commands
      if (millis() - lastPoll >= BOT_INTERVAL) {
        lastPoll = millis();
        int n = bot.getUpdates(bot.last_message_received + 1);
        while (n) {
          for (int i = 0; i < n; i++) {
            if (String(bot.messages[i].chat_id) != String(CHAT_ID)) continue;
            String text = bot.messages[i].text;
            text.trim();

            if (text == "/status") {
              sendTelegram(statusText());
            } else if (text == "/test") {
              sendTelegram("TEST ALERT: Telegram notifications are working.");
            } else if (text.startsWith("/set")) {
              float v = text.substring(4).toFloat();
              if (v >= SETPOINT_MIN && v <= SETPOINT_MAX) {
                requestedSetpoint = v;
                setpointRequest = true;
                sendTelegram("Setpoint changed to " + String(v, 1) + " C");
              } else {
                sendTelegram("Use /set followed by a value from " +
                             String(SETPOINT_MIN, 0) + " to " + String(SETPOINT_MAX, 0));
              }
            } else {
              sendTelegram("Commands:\n/status - current readings\n/test - test alert\n/set 30 - change setpoint");
            }
          }
          n = bot.getUpdates(bot.last_message_received + 1);
        }
      }
    } else {
      announced = false;   // announce again after Wi-Fi comes back
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ---------------------------------------------------------------
void updateDisplay() {
  char left[17];
  char line[17];

  // Row 0: temperature, humidity, Wi-Fi marker (W = connected, - = offline)
  if (isnan(temperature)) snprintf(left, sizeof(left), "T:--.-C H:--%%");
  else snprintf(left, sizeof(left), "T:%.1fC H:%.0f%%", temperature, humidity);
  snprintf(line, sizeof(line), "%-15s%c", left, WiFi.status() == WL_CONNECTED ? 'W' : '-');
  lcd.setCursor(0, 0);
  lcd.print(line);

  // Row 1: alarm or setpoint/fan
  lcd.setCursor(0, 1);
  if (alarmReason == ALARM_HOT) {
    lcd.print("ALARM: TOO HOT! ");
  } else if (alarmReason == ALARM_SENSOR) {
    lcd.print("ALARM: SENSOR   ");
  } else {
    snprintf(left, sizeof(left), "Set:%.1f Fan:%s", setpoint, fanOn ? "ON" : "OFF");
    snprintf(line, sizeof(line), "%-16s", left);
    lcd.print(line);
  }
}

// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Initialize Relay pin mode first, then set initial state
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  setRelay(false);

  // Initialize Buzzer pin mode first, then set initial state and do short beep
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? HIGH : LOW); // Ensure initial state is OFF
  setBuzzer(true); // Short beep on startup
  delay(150);
  setBuzzer(false); // Then silent until a real alarm

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  prefs.begin("climate", false);
  setpoint = constrain(prefs.getFloat("setpoint", DEFAULT_SETPOINT), SETPOINT_MIN, SETPOINT_MAX);

  dht.begin();

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Climate Control");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  secureClient.setInsecure();

  if (!telegramConfigured()) {
    Serial.println("WARNING: Telegram not configured. Token must look like 123456789:AAH...");
  }

  msgQueue = xQueueCreate(8, sizeof(Msg));
  xTaskCreatePinnedToCore(telegramTask, "telegram", 10240, NULL, 1, NULL, 0);

  delay(2000); // Let the DHT22 settle
  readSensor();
  updateControl();
  lastRead = millis();
}

void loop() {
  unsigned long now = millis();

  handleButtons(now);
  applySetpointRequest();

  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;
    readSensor();
    updateControl();
    Serial.printf("T=%.1fC H=%.0f%% Set=%.1f Fan=%s Alarm=%d Buzzer=%s WiFi=%s\n",
                  temperature, humidity, setpoint, fanOn ? "ON" : "OFF",
                  (int)alarmReason, buzzerOn ? "ON" : "off",
                  WiFi.status() == WL_CONNECTED ? "yes" : "no");
  }

  updateAlarmOutputs(now);

  if (now - lastDisplay >= DISPLAY_INTERVAL) {
    lastDisplay = now;
    updateDisplay();
  }

  maintainWifi(now);
  repeatAlert(now);
  saveSetpointIfNeeded(now);
}