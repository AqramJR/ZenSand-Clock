# ⏳ ZenSand Clock: The Smart Kinetic Sand Art

A premium, 3D-printed Smart Kinetic Sand Art Clock powered by ESP32. This project combines the mesmerizing, continuous motion of a kinetic sand wiper with a feature-rich, 100% non-blocking software architecture. 

It serves not only as an aesthetically pleasing desk clock but also as a fully functional Smart Home IoT device featuring a local Web Dashboard, live weather synchronization, Pomodoro focus mode, and smooth embedded animations.

## ✨ Key Features

* **100% Non-Blocking Architecture:** Built entirely on `millis()` state machines. The 28BYJ-48 stepper motor runs flawlessly using `AccelStepper::runSpeed()` without a single `delay()`, ensuring the sand wiper never stutters, even during Wi-Fi connection or HTTP parsing.
* **Local Web Dashboard:** A built-in, mobile-friendly HTML/JS web server. Adjust motor RPM, toggle direction, change clock faces, tweak OLED brightness, or send scrolling messages to the screen instantly from your phone.
* **Live Weather & NTP Sync:** Connects to Wi-Fi on-demand (or keeps it alive) to sync exact time via NTP and fetches live weather data (temperature & conditions) from the Open-Meteo API (No API key required!).
* **Pomodoro Zen Mode:** A dedicated focus timer (25m work / 5m break). The sand moves agonizingly slow while you focus, then speeds up to "wipe the slate clean" during your break.
* **Smart Auto-Wipe:** Configurable daily schedules (e.g., 03:00 and 15:00) where the motor ramps up to maximum RPM for a deep sand-cleaning cycle.
* **Easter Egg Mini-Game:** Tap the capacitive sensor exactly 5 times rapidly to launch a hidden "Dino Jump" side-scroller game on the OLED!
* **Advanced U8g2 Animations:** Features multiple parallax and mathematically driven clock faces (e.g., Cyberpunk Neon Skyline, Star Voyage) running purely on calculated geometry.

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32 (NodeMCU-32S / WROOM-32)
* **Display:** 1.3" OLED SH1106 (I2C)
* **Touch Sensor:** TTP223 Capacitive Touch Module
* **Motor & Driver:** 28BYJ-48 Stepper Motor + ULN2003 Driver Board
* **RTC Module:** DS1307 (I2C) + CR2032 Coin Cell Battery
* **Sand Medium:** Kinetic sand (or ultra-fine baking soda)
* **Chassis:** 3D Printed housing (Sand tray and motor mount).

## 💻 Software Dependencies

Ensure you have the following libraries installed in your Arduino IDE:
* `U8g2` by olikraus
* `AccelStepper` by Mike McCauley
* `RTClib` by Adafruit
* `ArduinoJson` by Benoit Blanchon (v6.x)
* Built-in ESP32 Libraries: `WiFi.h`, `Preferences.h`, `WebServer.h`, `HTTPClient.h`

## 🔌 Wiring Guide

| Component | Pin | ESP32 Pin |
| :--- | :--- | :--- |
| **OLED & RTC** | SDA | `GPIO 21` |
| **OLED & RTC** | SCL | `GPIO 22` |
| **TTP223 Touch** | SIG | `GPIO 4` |
| **ULN2003 IN1** | IN1 | `GPIO 13` |
| **ULN2003 IN2** | IN2 | `GPIO 14` |
| **ULN2003 IN3** | IN3 | `GPIO 12` |
| **ULN2003 IN4** | IN4 | `GPIO 27` |

*(Note: The `AccelStepper` constructor in the code correctly handles the IN1-IN3-IN2-IN4 half-step sequence required for the 28BYJ-48 motor).*

## ⚙️ Software Setup

1. Install the latest **Arduino IDE**.
2. Add the ESP32 board package to your Boards Manager.
3. Install the following libraries via the Library Manager:
   * `U8g2` by olikraus
   * `AccelStepper` by Mike McCauley
   * `RTClib` by Adafruit
   * `ArduinoJson` by Benoit Blanchon
4. Open the `.ino` file and update your Wi-Fi credentials:
   ```cpp
   const char* WIFI_SSID     = "YOUR_SSID";
   const char* WIFI_PASSWORD = "YOUR_PASSWORD";
   ```
5. Adjust your Timezone offsets if necessary:
   ```cpp
   const long  GMT_OFFSET_SEC   = 7200;   // Example: UTC+2 (Egypt)
   const int   DST_OFFSET_SEC   = 0;      // Example: Daylight Savings Time
   ```
6. Compile and upload to your ESP32.

---

## 🚀 What's Next? (Future Upgrades)

This project is continuously evolving. Here is the roadmap for upcoming enhancements:

### 🔩 Hardware Upgrades
* **Battery Independence:** Integrate an 18650 Li-ion battery, TP4056 charging module, and MT3608 boost converter for a truly wire-free centerpiece.
* **Neopixel Edge-Lighting:** Add an addressable WS2812B LED strip beneath the sand basin. The ESP32 can sync the sand color with the current weather, time of day, or Pomodoro status.
* **Acoustic Ambience:** Incorporate a DFPlayer Mini and a 3W speaker to play Zen/Lo-Fi music, rain sounds, or an elegant hourly chime.
* **Glass Dome Enclosure:** Fit a custom acrylic or tempered glass dome to protect the sand from dust and humidity.


### 💾 Software/Firmware Upgrades
* **OTA (Over-The-Air) Updates:** Flash new firmware wirelessly through the web dashboard without dismantling the clock.
* **Kinetic Spirograph Patterns:** Upgrade the motor control math to draw complex geometric Mandala and Lissajous curves instead of simple circles.
* **Moon Phase Clock Face:** Calculate and render accurate lunar phases on the OLED alongside the date and time.
* **Kinetic Alarm Clock:** An alarm mode that vibrates the stepper motor rapidly and flashes the OLED to wake you up without needing a buzzer.

## 📝 License
This project is open-source. Feel free to fork, modify, and build your own ZenSand Clock!