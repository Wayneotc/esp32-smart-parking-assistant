#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD: connected to the board's dedicated I2C connector
LiquidCrystal_I2C lcd(0x27, 16, 2);

// HC-SR04 ultrasonic sensor
const int TRIG_PIN = 25;
const int ECHO_PIN = 33;

// LEDs
const int GREEN_LED = 26;
const int RED_LED = 27;

// Active buzzer
const int BUZZER_PIN = 13;
const bool BUZZER_ENABLED = true;

// Read distance in centimetres
float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    return -1.0;
  }

  return duration / 58.0;
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialise I2C using the board's default SDA/SCL pins
  Wire.begin();

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Parking Assist");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1500);
  lcd.clear();
}

void loop() {
  float distance = readDistanceCM();

  bool green = false;
  bool red = false;
  const char* status = "";

  // Determine parking status
  if (distance < 0) {
    status = "No echo";
  }
  else if (distance > 100) {
    green = true;
    status = "SAFE";
  }
  else if (distance > 50) {
    green = true;
    status = "CAUTION";
  }
  else if (distance > 20) {
    red = true;
    status = "WARNING";
  }
  else {
    red = true;
    status = "STOP!";
  }

  // Control LEDs
  digitalWrite(GREEN_LED, green ? HIGH : LOW);
  digitalWrite(RED_LED, red ? HIGH : LOW);

  // Buzzer beeps ONLY when distance is 20 cm or less
  static unsigned long lastBeepChange = 0;
  static bool beepState = false;
  unsigned long now = millis();

  if (BUZZER_ENABLED && distance >= 0 && distance <= 20) {
    if (now - lastBeepChange >= 100) {
      lastBeepChange = now;
      beepState = !beepState;
    }

    digitalWrite(BUZZER_PIN, beepState ? HIGH : LOW);
  }
  else {
    beepState = false;
    lastBeepChange = now;
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Display distance on LCD
  lcd.setCursor(0, 0);

  if (distance < 0) {
    lcd.print("Distance: ---   ");
  }
  else {
    lcd.print("Distance:");
    lcd.print(distance, 1);
    lcd.print(" cm   ");
  }

  // Display status
  lcd.setCursor(0, 1);
  lcd.print("Status: ");
  lcd.print(status);
  lcd.print("        ");

  // Print readings to Serial Monitor
  Serial.print("Distance: ");

  if (distance < 0) {
    Serial.println("No echo");
  }
  else {
    Serial.print(distance, 1);
    Serial.println(" cm");
  }

  delay(100);
}