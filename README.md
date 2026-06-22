# Spark V1

Spark V1 is a smart companion robot firmware running on the ESP32-S3 with LVGL 8 for expressive eye/mouth animations and emotion rendering.

## Architecture
The firmware is built on the **SparkCore** framework, composed of:
- **State Manager:** Centralized Finite State Machine tracking Boot, Idle, Listening, and Sleeping modes.
- **Hardware Manager:** Abstracts battery monitoring, tap detection, accelerometer tilt, and backlight.
- **Intent Manager:** Routes audio context and matches specific user requests.
- **Emotion Manager:** Evaluates intent tags and selects contextual facial responses.
- **Face Manager:** Maintains static coordinate/configuration data for all visual states (Normal, Happy, Angry, Crying, etc.).
- **Animation Manager:** Executes dynamic rendering (Blinking, Shaking, Bouncing) cleanly through isolated callbacks.

Please see `ARCHITECTURE.md` and `docs/` for in-depth technical documentation.

## Hardware Support
- **MCU:** ESP32-S3
- **Display:** LVGL 8.x compatible SPI/I8080 display
- **Sensors:** QMI8658 (Accelerometer/Gyro)
- **Audio:** Custom I2S MIC and Codec setup

## Build Instructions
1. Install ESP-IDF v5.3.2
2. Navigate to `firmware/`
3. Run `idf.py build`
4. Run `idf.py flash`
