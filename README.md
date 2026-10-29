# ESP32 Smart Parking Assistant

An IoT and embedded-systems project that helps a driver estimate the distance between a vehicle and an obstacle while parking. The system uses an ESP32 microcontroller and an HC-SR04 ultrasonic sensor to measure distance, an I2C LCD to display the readings, and LEDs to indicate the parking status.

## Project Overview

The ESP32 Smart Parking Assistant continuously measures the distance to an object and classifies the reading into four parking states:

- **SAFE:** The obstacle is more than 100 cm away.
- **CAUTION:** The obstacle is between 51 cm and 100 cm away.
- **WARNING:** The obstacle is between 21 cm and 50 cm away.
- **STOP:** The obstacle is 20 cm or less away.

The green LED indicates a safe or caution state, while the red LED indicates warning or stop. An active buzzer is intended to beep only when the measured distance reaches the stopping threshold of 20 cm or less.

## Features

- Real-time distance measurement using an HC-SR04 ultrasonic sensor.
- Distance display in centimetres on a 16×2 I2C LCD.
- Visual parking guidance using green and red LEDs.
- Buzzer alert at the stopping distance.
- Serial Monitor output for debugging and testing.
- Timeout handling for ultrasonic readings that return no echo.

## Hardware Requirements

- ESP32 development board
- HC-SR04 ultrasonic distance sensor
- 16×2 LCD with I2C interface
- One green LED
- One red LED
- Two 220 Ω LED current-limiting resistors
- Active buzzer
- Breadboard and jumper wires
- USB cable and computer
- Suitable resistors or a logic-level shifter for voltage protection
- Suitable buzzer driver circuit, if required by the buzzer's current rating

## Wiring Connections

The following pin assignments match the current Arduino sketch. Confirm the labels and connections on your actual ESP32 board before wiring.

| Component | ESP32 connection |
|---|---|
| HC-SR04 TRIG | GPIO 25 |
| HC-SR04 ECHO | GPIO 33 through a voltage divider |
| Green LED anode | GPIO 26 through a 220 Ω resistor |
| Red LED anode | GPIO 27 through a 220 Ω resistor |
| LED cathodes | GND |
| I2C LCD SDA | Board's dedicated SDA connector |
| I2C LCD SCL | Board's dedicated SCL connector |
| I2C LCD VCC | Supply appropriate for the module |
| I2C LCD GND | GND |
| Buzzer control | GPIO 13 through a suitable driver, if required |
| Common ground | Shared ground between connected components |

### Voltage and Electrical Safety

- The ESP32 uses 3.3 V GPIO logic. The HC-SR04 ECHO output can be 5 V, so use a voltage divider or suitable level shifter before connecting ECHO to GPIO 33.
- A typical divider uses a 1 kΩ resistor between ECHO and the GPIO junction and a 2 kΩ resistor between that junction and GND.
- Check the LCD module's supply requirements and I2C pull-up voltage. Use a suitable level shifter if the SDA/SCL lines are pulled up to 5 V.
- Do not drive a buzzer directly from an ESP32 GPIO unless its electrical requirements explicitly permit it. Use a suitable driver circuit when needed.
- Ensure all connected components share a common ground.

## Software Requirements

- Arduino IDE 2
- ESP32 board support package by Espressif Systems
- `Wire.h` library, included with the Arduino environment
- A compatible `LiquidCrystal_I2C` library

## Getting Started

### 1. Install the Arduino IDE

Download Arduino IDE from the official website:

https://www.arduino.cc/en/software

### 2. Install ESP32 Board Support

In Arduino IDE, open **Boards Manager**, search for `esp32`, and install the package published by **Espressif Systems**.

If the package index is not already configured, add the following URL under Additional Boards Manager URLs in Preferences:

https://espressif.github.io/arduino-esp32/package_esp32_index.json

### 3. Install the LCD Library

Open **Library Manager**, search for a compatible `LiquidCrystal_I2C` library, and install one that supports the `lcd.init()` and `lcd.backlight()` methods used in the sketch.

### 4. Open the Project

Open `src/smart_parking_assistant.ino` in Arduino IDE, or open the `.ino` file from the downloaded repository.

### 5. Select the Board and Port

- Select the ESP32 board that matches your development board.
- Select the correct serial port.
- Verify that the pin assignments match your wiring.

### 6. Upload and Test

Upload the sketch, open Serial Monitor at **115200 baud**, and place an object at different distances from the ultrasonic sensor.

If the LCD lights up but displays no text, check the I2C address, wiring, library compatibility, and module contrast. The sketch currently uses LCD address `0x27`; some modules use `0x3F`.

## Parking Status Logic

| Measured distance | LCD status | LED indication | Buzzer |
|---|---|---|---|
| More than 100 cm | SAFE | Green | Off |
| 51–100 cm | CAUTION | Green | Off |
| 21–50 cm | WARNING | Red | Off |
| 20 cm or less | STOP! | Red | Beeps |
| No echo received | No echo | Both LEDs off | Off |

The distance thresholds are configurable in the Arduino sketch.

## Repository Structure

```text
esp32-smart-parking-assistant/
├── src/
│   └── smart_parking_assistant.ino
├── docs/
│   └── wiring-diagram.png
├── .gitignore
├── LICENSE
└── README.md
```

The wiring diagram and license are optional files. Add the diagram only after creating or obtaining one that accurately represents your hardware.

## Future Improvements

- Add an OLED display or graphical parking-distance indicator.
- Introduce progressively faster alerts as an obstacle approaches.
- Calibrate the distance thresholds for the intended parking environment.
- Add wireless monitoring through a web dashboard or mobile application.
- Record distance readings for testing and analysis.
- Improve the enclosure and mounting for practical use.

## Learning Outcomes

This project demonstrates practical skills in:

- ESP32 programming with C++ and the Arduino framework.
- Ultrasonic distance measurement.
- Digital input and output control.
- I2C communication with LCD modules.
- Basic embedded-system debugging.
- Hardware interfacing and electrical safety.

## Disclaimer

This is an educational prototype, not a certified vehicle safety system. Sensor readings can be affected by object shape, angle, surface, environmental conditions, and wiring. Do not rely on this project as the sole means of avoiding collisions.

## License

This project is intended for educational and demonstration purposes. If you want others to reuse and modify the code, add an appropriate open-source license, such as the MIT License, after reviewing its terms.

## Author

Created as a hands-on ESP32, electronics, and IoT learning project.
