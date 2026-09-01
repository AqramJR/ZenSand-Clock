/*
 * ============================================================================
 *  SMART KINETIC SAND ART CLOCK — ESP32  (PREMIUM SMART-HOME EDITION)
 * ============================================================================
 *  Hardware:
 *    - ESP32 (NodeMCU-32S)
 *    - 1.3" OLED SH1106, I2C            -> U8g2 (full buffer, hardware I2C)
 *    - TTP223 capacitive touch          -> GPIO 4, Active HIGH
 *    - 28BYJ-48 + ULN2003               -> GPIO 13, 14, 12, 27, AccelStepper
 *    - DS1307 RTC, I2C                  -> RTClib (Adafruit)
 *
 *  Libraries required (Library Manager):
 *    - U8g2 by olikraus
 *    - AccelStepper by Mike McCauley
 *    - RTClib by Adafruit
 *    - ArduinoJson by Benoit Blanchon (v6.x)
 *    - (built into ESP32 core) Preferences.h, WiFi.h, time.h, WebServer.h,
 *      HTTPClient.h, Update.h
 *
 *  ============================================================================
 *  WHAT'S NEW IN THIS EDITION (6 features layered onto the original base)
 *  ============================================================================
 *   1. Local Web Dashboard   - WebServer.h, fully non-blocking (server.handleClient()
 *                              is called once per loop(), same as stepper.runSpeed()).
 *                              Mobile-friendly single-page HTML/CSS/JS control panel.
 *   2. Web Message Billboard - text box on the dashboard -> SystemState::BILLBOARD,
 *                              a millis()-driven horizontal scroll for ~5s.
 *   3. Live Weather Sync     - piggybacks on the existing WiFi/NTP state machine;
 *                              one extra (still non-blocking-to-the-motor) HTTPClient
 *                              call, parsed with ArduinoJson. New ClockFace::WEATHER.
 *   4. Pomodoro Zen Mode     - SystemState::POMODORO, 25-min work (RPM_MIN) / 5-min
 *                              break (RPM_MAX) cycle with a big countdown.
 *   5. Smart 12h Auto-Wipe   - fires automatically at 03:00 and 15:00 for a 2-minute
 *                              full-speed sweep, independent of the manual per-hour
 *                              auto-wipe setting already in Motor Settings.
 *   6. Easter-Egg Mini-Game  - 5 rapid taps inside the tap window launches a simple
 *                              non-blocking "Dino Jump" game; short tap = jump,
 *                              long-press or a loss exits back to the clock.
 *
 *  ============================================================================
 *  THIS REVISION ADDS
 *  ============================================================================
 *   7. OTA Firmware Update   - Update.h + a "/update" POST endpoint reachable
 *                              from the web dashboard. Standard synchronous
 *                              WebServer OTA pattern. NOTE: because WebServer
 *                              reads the whole multipart upload inside a single
 *                              server.handleClient() call, the stepper WILL
 *                              pause for the (short) duration of the upload +
 *                              flash write - this is an inherent limitation of
 *                              using the synchronous WebServer library for OTA,
 *                              not an oversight. yield() is called on every
 *                              received chunk so the watchdog never trips.
 *   8. Moon Phase Clock Face - ClockFace::MOON_PHASE. Phase is computed with a
 *                              lightweight Conway synodic-month approximation
 *                              (cheap integer/float math, no blocking calls,
 *                              no external library) and rendered as a real
 *                              illuminated disc using a per-scanline terminator
 *                              calculation (sqrt + cos, drawn with u8g2.drawHLine).
 *   9. WiFi Sync IP Display  - drawWifiSyncScreen() now prints WiFi.localIP()
 *                              on the OLED once sync succeeds, so the user can
 *                              find the dashboard without a Serial Monitor.
 *  10. "Always On" Label     - SLEEP_OPTIONS[0] (=0 minutes/never sleep) is now
 *                              rendered as "Always On" in both the OLED Display
 *                              Settings menu and the web dashboard status line.
 *
 *  ============================================================================
 *  THIS REVISION ALSO ADDS
 *  ============================================================================
 *  11. Zen Breathing Face    - ClockFace::ZEN_BREATHING. A sine-driven circle
 *                              that expands/contracts on an 8s inhale/exhale
 *                              cycle. Creative touch: updateZenBreathingMotor()
 *                              (called once per loop() pass, just like the
 *                              stepper itself) gently modulates motorRPM
 *                              +-30% around whatever speed the user had set,
 *                              faster at the inhale peak, slower on the
 *                              exhale - turning the wiper into a tactile
 *                              breathing pacer. The user's original RPM/dir/
 *                              run-state is saved on entry and restored
 *                              exactly on exit, so nothing else is disturbed.
 *  12. Generative Sand Art   - ClockFace::GENERATIVE_ART. A lightweight
 *                              falling-sand cellular automaton (64x32 logical
 *                              grid, 2px cells) that trickles grains downward
 *                              and piles them up, redrawn once per throttled
 *                              display refresh (same 200ms cadence as every
 *                              other face, so it never competes with the
 *                              stepper for CPU time). The digital time is
 *                              drawn on top inside a solid-filled panel so
 *                              it always stays crisp and readable.
 *  13. IP Wrap Fix           - drawWifiSyncScreen() now renders the synced
 *                              dashboard IP with a compact "IP: " prefix on
 *                              the small 5x7 font and centers/measures it
 *                              with getStrWidth() before drawing, so a full
 *                              IPv4 address ("IP: 255.255.255.255") always
 *                              fits inside the 128px screen width instead of
 *                              being clipped off the right edge.
 *
 *  Design notes (unchanged core philosophy):
 *    - ZERO calls to delay() anywhere in the steady-state loop() path. Every
 *      timed behaviour is driven off millis() state machines so the stepper
 *      and the touch polling are never blocked for more than a few
 *      milliseconds (the unavoidable I2C transfer time when we push a frame
 *      to the OLED). The ONE exception in the whole sketch is a short,
 *      clearly-commented delay() in handleUpdateResult(), used only after a
 *      successful OTA flash, immediately before ESP.restart() - at that point
 *      the MCU is about to fully reboot and re-initialise everything, so it
 *      has no bearing on the "smooth stepper during normal operation"
 *      guarantee.
 *    - AccelStepper::runSpeed() is called on every single pass through
 *      loop() so the sand wiper keeps perfectly smooth, constant-velocity
 *      motion regardless of what the UI/touch/web/HTTP logic is doing.
 *    - WebServer::handleClient() and the weather HTTPClient call are the two
 *      operations most likely to introduce latency; handleClient() is cheap
 *      and non-blocking by design, and the weather HTTP GET is only ever
 *      issued once per NTP sync (a rare, user-initiated or hourly event),
 *      immediately followed by stepper.runSpeed() calls before and after so
 *      any momentary stall is bounded and imperceptible on a slow sand wiper.
 *      The OTA upload endpoint is the sole exception, documented above.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <AccelStepper.h>
#include <RTClib.h>
#include <Preferences.h>   // NVS persistence
#include <WiFi.h>          // NTP sync + Web Dashboard + Weather
#include <time.h>          // configTime()/time()
#include <WebServer.h>     // Feature 1: local web dashboard
#include <HTTPClient.h>     // Feature 3: OpenWeatherMap / Open-Meteo
#include <ArduinoJson.h>    // Feature 3: JSON parsing
#include <Update.h>         // Feature 7: OTA firmware updates

// ============================================================================
//  PIN DEFINITIONS
// ============================================================================
static const uint8_t PIN_TOUCH   = 4;                 // TTP223, active HIGH

static const uint8_t PIN_STEP_IN1 = 13;
static const uint8_t PIN_STEP_IN2 = 14;
static const uint8_t PIN_STEP_IN3 = 12;
static const uint8_t PIN_STEP_IN4 = 27;

// ============================================================================
//  DISPLAY  (SH1106, full frame buffer, hardware I2C)
// ============================================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ============================================================================
//  RTC
// ============================================================================
RTC_DS1307 rtc;
bool rtcAvailable = false;

// ============================================================================
//  STEPPER  (28BYJ-48 / ULN2003, half-step mode -> 2048 steps/rev)
// ============================================================================
AccelStepper stepper(AccelStepper::HALF4WIRE,
                      PIN_STEP_IN1, PIN_STEP_IN3, PIN_STEP_IN2, PIN_STEP_IN4);

static const float STEPS_PER_REV = 2048.0f;

float    motorRPM      = 1.0f;   // default: 1 RPM
int8_t   motorDirection = 1;     // +1 = clockwise, -1 = counter-clockwise
bool     motorRunning   = true;

static const float RPM_STEP = 5.0f;
static const float RPM_MIN  = 1.0f;
static const float RPM_MAX  = 15.0f;

// ============================================================================
//  PERSISTENT SETTINGS (NVS via Preferences)
// ============================================================================
Preferences prefs;
static const char* NVS_NAMESPACE = "sandclock";

// ============================================================================
//  STATE MACHINE & MENUS
// ============================================================================
enum class SystemState : uint8_t {
  BOOT, MAIN_SCREEN, SETTINGS_MENU, MOTOR_MENU, DISPLAY_MENU, TIME_SET_MENU,
  ANIMATION, WIFI_SYNC, WIPE_ACTIVE,
  POMODORO,     // Feature 4
  BILLBOARD,    // Feature 2
  GAME          // Feature 6
};

enum class ClockFace : uint8_t {
  DIGITAL_HUGE, ANALOG_CLOCK, DATE_TIME, BINARY_CLOCK, RPM_VIEW,
  CARTOON_MODE, CARTOON_MOVIE_2,
  WEATHER,        // Feature 3
  MOON_PHASE,     // Feature 8
  ZEN_BREATHING,  // Feature 11 (this revision)
  GENERATIVE_ART, // Feature 12 (this revision)
  FACE_COUNT
};

SystemState currentState  = SystemState::BOOT;
SystemState stateBeforeAnimation = SystemState::MAIN_SCREEN;
ClockFace   currentFace   = ClockFace::DIGITAL_HUGE;

uint8_t settingsMenuIndex = 0;
uint8_t motorMenuIndex    = 0;
uint8_t displayMenuIndex  = 0;
uint8_t timeSetStep = 0; // 0 = hours, 1 = minutes
int8_t tempHour = 0;
int8_t tempMinute = 0;

// Settings menu now offers Pomodoro entry too.
const char* SETTINGS_ITEMS[] = { "1.Motor Settings", "2.Display Settings", "3.Set Time", "4.Sync WiFi Time", "5.Pomodoro Mode", "< Back" };
const uint8_t SETTINGS_ITEM_COUNT = 6;
static const uint8_t SETTINGS_IDX_MOTOR    = 0;
static const uint8_t SETTINGS_IDX_DISPLAY  = 1;
static const uint8_t SETTINGS_IDX_SET_TIME = 2;
static const uint8_t SETTINGS_IDX_WIFI     = 3;
static const uint8_t SETTINGS_IDX_POMODORO = 4;
static const uint8_t SETTINGS_IDX_BACK     = 5;

const char* MOTOR_ITEMS[] = { "Speed Up", "Speed Down", "Max RPM", "Toggle Direction", "Stop/Start Motor", "Auto-Wipe Hour", "< Back" };
const uint8_t MOTOR_ITEM_COUNT = 7;
static const uint8_t MOTOR_IDX_SPEED_UP   = 0;
static const uint8_t MOTOR_IDX_SPEED_DOWN = 1;
static const uint8_t MOTOR_IDX_MAX_RPM    = 2;
static const uint8_t MOTOR_IDX_TOGGLE_DIR = 3;
static const uint8_t MOTOR_IDX_TOGGLE_RUN = 4;
static const uint8_t MOTOR_IDX_AUTO_WIPE  = 5;
static const uint8_t MOTOR_IDX_BACK       = 6;

const char* DISPLAY_ITEMS[] = { "1.Brightness", "2.Auto-Sleep", "3.Time Format", "< Back" };
const uint8_t DISPLAY_ITEM_COUNT = 4;

const uint8_t SLEEP_OPTIONS[] = { 0, 1, 3, 5, 10 };
const uint8_t SLEEP_OPTION_COUNT = 5;
uint8_t sleepOptionIndex = 0;

uint8_t brightnessIndex = 2;
const uint8_t BRIGHTNESS_LEVELS[] = {10, 100, 255};
const char* BRIGHTNESS_NAMES[] = {"Low", "Med", "High"};

uint8_t currentAnimType = 0;

bool use12HourFormat = true;

// ---- Feature: Manual scheduled daily auto-wipe (Motor Settings entry) ----
static const uint8_t WIPE_HOUR_OFF = 255;
uint8_t wipeHourSetting = WIPE_HOUR_OFF;

// ---- save/load ----
void saveSettings() {
  prefs.begin(NVS_NAMESPACE, false); // read/write
  prefs.putFloat("rpm",    motorRPM);
  prefs.putChar("dir",     (int8_t)motorDirection);
  prefs.putUChar("bright", brightnessIndex);
  prefs.putUChar("sleep",  sleepOptionIndex);
  prefs.putBool("fmt12h",  use12HourFormat);
  prefs.putUChar("wipehr", wipeHourSetting);
  prefs.end();
}

void loadSettingsFull() {
  prefs.begin(NVS_NAMESPACE, true); // read-only
  motorRPM        = prefs.getFloat("rpm",    1.0f);
  motorDirection  = (int8_t)prefs.getChar("dir", 1);
  brightnessIndex = prefs.getUChar("bright", 2);
  sleepOptionIndex= prefs.getUChar("sleep",  0);
  use12HourFormat = prefs.getBool("fmt12h",  true);
  wipeHourSetting = prefs.getUChar("wipehr", WIPE_HOUR_OFF);
  prefs.end();
}

// ============================================================================
//  WI-FI / NTP TIME SYNC  (Feature 1 Web Dashboard, + Feature 3 Weather)
// ============================================================================
// !! Replace with your real network credentials before flashing. !!
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

const char* NTP_SERVER       = "pool.ntp.org";
const long  GMT_OFFSET_SEC   = 7200;   // Egypt is UTC+2
const int   DST_OFFSET_SEC   = 3600;   // Add 1 hour for DST offset

enum class WifiSyncPhase : uint8_t { CONNECTING, SUCCESS, FAILED };
WifiSyncPhase wifiSyncPhase       = WifiSyncPhase::CONNECTING;
unsigned long wifiSyncStartTime   = 0;
unsigned long wifiSyncResultTime  = 0;
bool          wifiConfigTimeCalled = false;

static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static const unsigned long WIFI_RESULT_DISPLAY_MS  = 2000;
static const unsigned long NTP_SANE_EPOCH          = 1700000000UL;

// Whether the web dashboard + server should stay up permanently instead of
// only during a sync. We keep WiFi on continuously so the dashboard is
// reachable at any time; this trades a bit of power for the "smart home"
// requirement. Toggle here if you prefer WiFi-off-until-sync behaviour.
static const bool KEEP_WIFI_ALWAYS_ON = true;

// ---- Feature 1: Web server ----
WebServer server(80);
bool webServerStarted = false;

// ---- Feature 3: Weather ----
const char* WEATHER_CITY    = "Cairo,EG";
const unsigned long WEATHER_REFRESH_MS = 30UL * 60UL * 1000UL; // refresh every 30 min

float   weatherTempC       = NAN;
String  weatherCondition   = "Unknown"; // "Clear","Clouds","Rain","Snow","Thunderstorm","Drizzle","Mist"/etc
bool    weatherValid       = false;
unsigned long lastWeatherFetch = 0;
bool    weatherFetchRequested = false; // set true to force a fetch on next WiFi_SYNC pass

// ============================================================================
//  Circadian Display Mode
// ============================================================================
static const uint8_t NIGHT_START_HOUR = 22;
static const uint8_t NIGHT_END_HOUR   = 6;
static const uint8_t NIGHT_CONTRAST   = 3;

unsigned long lastContrastCheck = 0;
static const unsigned long CONTRAST_CHECK_INTERVAL_MS = 30000;

bool greetingActive        = false;
unsigned long greetingStartTime = 0;
static const unsigned long GREETING_DURATION_MS = 2500;
char greetingText[20] = "";

// ============================================================================
//  Feature 5: SMART 12-HOUR AUTO-WIPE  (03:00 / 15:00, 2-minute sweep)
//  Kept fully independent from the manual per-hour "Auto-Wipe Hour" menu
//  setting so the two features never fight over the same state variables.
// ============================================================================
static const unsigned long SMART_WIPE_DURATION_MS = 2UL * 60UL * 1000UL; // 2 minutes
static const uint8_t SMART_WIPE_HOUR_A = 3;   // 03:00
static const uint8_t SMART_WIPE_HOUR_B = 15;  // 15:00

float         prevMotorRPM       = 1.0f;
int8_t        prevMotorDirection = 1;
bool          prevMotorRunning   = true;
SystemState   stateBeforeWipe    = SystemState::MAIN_SCREEN;
unsigned long wipeStartTime      = 0;
unsigned long currentWipeDurationMs = 25000; // set per-trigger (manual=25s, smart=2min)
int8_t        lastManualWipeDay  = -1;
int8_t        lastSmartWipeDay_A = -1;
int8_t        lastSmartWipeDay_B = -1;

// ============================================================================
//  Feature 4: POMODORO ZEN MODE
// ============================================================================
// Forward decls needed here because enterPomodoro() (defined a few lines
// below) calls these before their real bodies appear later in the file.
void applyMotorSpeedFwd();
void lastInteractionTimeFwd();

enum class PomodoroPhase : uint8_t { WORK, BREAK };
PomodoroPhase pomodoroPhase = PomodoroPhase::WORK;
unsigned long pomodoroPhaseStart = 0;
static const unsigned long POMODORO_WORK_MS  = 25UL * 60UL * 1000UL; // 25 min
static const unsigned long POMODORO_BREAK_MS = 5UL  * 60UL * 1000UL; // 5 min
float  pomodoroSavedRPM = 1.0f;
int8_t pomodoroSavedDir = 1;
bool   pomodoroSavedRunning = true;

void enterPomodoro() {
  pomodoroSavedRPM     = motorRPM;
  pomodoroSavedDir     = motorDirection;
  pomodoroSavedRunning = motorRunning;

  pomodoroPhase = PomodoroPhase::WORK;
  pomodoroPhaseStart = millis();
  motorRPM = RPM_MIN;
  motorRunning = true;
  applyMotorSpeedFwd(); // fwd-declared below, defined after applyMotorSpeed()
  currentState = SystemState::POMODORO;
  lastInteractionTimeFwd();
}

// (forward-decl helpers so enterPomodoro() above can call functions defined
//  later in the file without reordering the whole codebase)
void applyMotorSpeedFwd();
void lastInteractionTimeFwd();

void exitPomodoro() {
  motorRPM       = pomodoroSavedRPM;
  motorDirection = pomodoroSavedDir;
  motorRunning   = pomodoroSavedRunning;
  applyMotorSpeedFwd();
  currentState = SystemState::MAIN_SCREEN;
}

void updatePomodoro() {
  unsigned long elapsed = millis() - pomodoroPhaseStart;
  if (pomodoroPhase == PomodoroPhase::WORK) {
    if (elapsed >= POMODORO_WORK_MS) {
      pomodoroPhase = PomodoroPhase::BREAK;
      pomodoroPhaseStart = millis();
      motorRPM = RPM_MAX; // "wipe the sand clean" during break
      applyMotorSpeedFwd();
    }
  } else {
    if (elapsed >= POMODORO_BREAK_MS) {
      pomodoroPhase = PomodoroPhase::WORK;
      pomodoroPhaseStart = millis();
      motorRPM = RPM_MIN;
      applyMotorSpeedFwd();
    }
  }
}

// ============================================================================
//  Feature 2: WEB MESSAGE BILLBOARD
// ============================================================================
static const size_t BILLBOARD_MAX_LEN = 96;
char billboardText[BILLBOARD_MAX_LEN] = "";
int  billboardTextWidthPx = 0;
static const unsigned long BILLBOARD_DURATION_MS = 5000;
unsigned long billboardStartTime = 0;
int billboardScrollX = 128; // starts just off the right edge
static const int BILLBOARD_SCROLL_SPEED_PXPS = 60; // pixels per second
unsigned long lastBillboardFrameTime = 0;
SystemState stateBeforeBillboard = SystemState::MAIN_SCREEN;

void triggerBillboard(const String& msg) {
  strncpy(billboardText, msg.c_str(), BILLBOARD_MAX_LEN - 1);
  billboardText[BILLBOARD_MAX_LEN - 1] = '\0';
  stateBeforeBillboard = (currentState == SystemState::BILLBOARD) ? stateBeforeBillboard : currentState;
  currentState = SystemState::BILLBOARD;
  billboardStartTime = millis();
  lastBillboardFrameTime = millis();
  billboardScrollX = 128;
  billboardTextWidthPx = 0; // force recompute for the new text next frame
}

// ============================================================================
//  Feature 6: EASTER-EGG MINI-GAME  ("Dino Jump")
// ============================================================================
static const uint8_t GAME_TRIGGER_TAPS = 5; // exactly 5 rapid taps launches it

// Simple side-scrolling jump game: ground at y=56, dino at fixed x, cactus
// obstacles scroll right-to-left. A short tap makes the dino jump (a small
// non-blocking parabola driven off millis()). Touching a cactus ends the run.
bool    gameActive        = false;
bool    gameOver          = false;
int     dinoY             = 0;     // 0 = on ground, negative = height above ground
float   dinoVelocity      = 0;
bool    dinoJumping       = false;
static const float GAME_GRAVITY   = 0.6f;   // px per frame^2 (approx, frame ~ 16ms)
static const float GAME_JUMP_VEL  = -8.0f;

struct Obstacle { float x; bool active; };
static const uint8_t GAME_MAX_OBSTACLES = 3;
Obstacle obstacles[GAME_MAX_OBSTACLES];
float    gameSpeed = 3.0f;          // px per frame, ramps up over time
unsigned long gameStartTime = 0;
unsigned long lastGameFrameTime = 0;
unsigned long lastObstacleSpawn = 0;
uint16_t gameScore = 0;
SystemState stateBeforeGame = SystemState::MAIN_SCREEN;

void startGame() {
  gameActive = true;
  gameOver = false;
  dinoY = 0;
  dinoVelocity = 0;
  dinoJumping = false;
  gameSpeed = 3.0f;
  gameScore = 0;
  gameStartTime = millis();
  lastGameFrameTime = millis();
  lastObstacleSpawn = millis();
  for (uint8_t i = 0; i < GAME_MAX_OBSTACLES; i++) obstacles[i].active = false;
  stateBeforeGame = (currentState == SystemState::GAME) ? stateBeforeGame : currentState;
  currentState = SystemState::GAME;
}

void gameJump() {
  if (!gameActive || gameOver) return;
  if (!dinoJumping) {
    dinoJumping = true;
    dinoVelocity = GAME_JUMP_VEL;
  }
}

void exitGame() {
  gameActive = false;
  currentState = SystemState::MAIN_SCREEN;
}

void updateGame() {
  if (!gameActive) return;
  unsigned long now = millis();
  if (now - lastGameFrameTime < 16) return; // ~60fps logic tick
  lastGameFrameTime = now;

  if (gameOver) return; // frozen on the "Game Over" splash until user acts

  // Physics
  if (dinoJumping) {
    dinoY += (int)dinoVelocity;
    dinoVelocity += GAME_GRAVITY;
    if (dinoY >= 0) {
      dinoY = 0;
      dinoJumping = false;
      dinoVelocity = 0;
    }
  }

  // Difficulty ramp
  gameSpeed = 3.0f + (float)(now - gameStartTime) / 10000.0f; // speeds up over time
  if (gameSpeed > 8.0f) gameSpeed = 8.0f;

  // Spawn obstacles
  unsigned long spawnInterval = (unsigned long)map((int)gameSpeed, 3, 8, 1400, 700);
  if (now - lastObstacleSpawn >= spawnInterval) {
    lastObstacleSpawn = now;
    for (uint8_t i = 0; i < GAME_MAX_OBSTACLES; i++) {
      if (!obstacles[i].active) {
        obstacles[i].active = true;
        obstacles[i].x = 128;
        break;
      }
    }
  }

  // Move obstacles + collision check
  const int dinoX = 20;
  const int dinoW = 10;
  const int dinoGroundY = 56;
  for (uint8_t i = 0; i < GAME_MAX_OBSTACLES; i++) {
    if (!obstacles[i].active) continue;
    obstacles[i].x -= gameSpeed;
    if (obstacles[i].x < -8) {
      obstacles[i].active = false;
      gameScore++;
      continue;
    }
    // Collision: obstacle occupies [x, x+6] at ground level; dino occupies
    // [dinoX, dinoX+dinoW] and is airborne if dinoY < -4 (roughly cleared).
    bool overlapX = (obstacles[i].x < dinoX + dinoW) && (obstacles[i].x + 6 > dinoX);
    bool dinoLow   = (dinoY > -10); // not high enough to clear it
    if (overlapX && dinoLow) {
      gameOver = true;
    }
  }
  (void)dinoGroundY;
}

void drawGameScreen() {
  const int groundY = 56;
  u8g2.drawHLine(0, groundY, 128);

  if (gameOver) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(28, 28, "GAME OVER");
    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %u", gameScore);
    u8g2.setFont(u8g2_font_6x10_tr);
    int tw = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - tw) / 2, 42, buf);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(6, 60, "Hold to exit, tap to retry");
    return;
  }

  // Dino: simple blocky sprite
  const int dinoX = 20;
  int dinoTopY = groundY + dinoY - 12; // 12px tall
  u8g2.drawBox(dinoX, dinoTopY, 10, 12);
  u8g2.drawBox(dinoX + 7, dinoTopY - 4, 5, 5); // head bump

  // Obstacles (cacti)
  for (uint8_t i = 0; i < GAME_MAX_OBSTACLES; i++) {
    if (!obstacles[i].active) continue;
    int ox = (int)obstacles[i].x;
    u8g2.drawBox(ox, groundY - 10, 5, 10);
  }

  // Score
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 8, buf);
}

// ============================================================================
//  TOUCH TIMING CONSTANTS
// ============================================================================
static const unsigned long DEBOUNCE_MS    = 25;
static const unsigned long LONG_PRESS_MS  = 800;
static const unsigned long TAP_WINDOW_MS  = 250;
static const unsigned long MENU_TIMEOUT_MS = 10000;

bool          lastRawReading    = LOW;
bool          stableState       = LOW;
unsigned long lastDebounceTime  = 0;

bool          isPressed         = false;
unsigned long pressStartTime    = 0;
bool          longPressFired    = false;

uint8_t       tapCount          = 0;
bool          tapWindowOpen     = false;
unsigned long tapWindowStart    = 0;

bool          suppressThisPress = false;

// ============================================================================
//  DISPLAY TIMING / SLEEP / ANIMATION
// ============================================================================
unsigned long lastDisplayUpdate  = 0;
const unsigned long DISPLAY_INTERVAL_MS = 200;

unsigned long lastInteractionTime = 0;
bool          displayAsleep       = false;

unsigned long animStartTime      = 0;
unsigned long lastAnimFrameTime  = 0;
const unsigned long ANIM_FRAME_MS   = 60;
const unsigned long ANIM_DURATION_MS = 3000;
uint8_t animFrame = 0;

unsigned long bootStartTime = 0;
const unsigned long BOOT_DURATION_MS = 2000;


// ============================================================================
//  FORWARD DECLARATIONS
// ============================================================================
void pollTouch();
void onTouchPress();
void onTouchRelease();
void resolveTapSequence(uint8_t count);
void onLongPress();
void wakeDisplay();
void updateMotor();
void applyMotorSpeed();
void increaseSpeed();
void decreaseSpeed();
void setMaxRPM();
void toggleDirection();
void toggleMotorRunning();
void cycleClockFace();
void startAnimation();
void cycleAutoSleepValue();
void cycleBrightness();
void toggleTimeFormat();
void updateSleepTimer();
void renderDisplay();
void drawBootScreen();
void drawMainScreen();
void drawSettingsMenu();
void drawMotorMenu();
void drawDisplayMenu();
void drawTimeSetMenu();
void drawAnimationFrame();
void drawGenericMenu(const char* title, const char* const* items, uint8_t count, uint8_t selected, uint8_t maxVisible);
void drawCartoonMovie(const DateTime& now);
void drawCartoonMovie2(const DateTime& now);
void drawWeatherFace(const DateTime& now);
void drawWeatherIcon(int x, int y);
void exitSettingsMenus();
void startWifiSync();
void updateWifiSync();
void drawWifiSyncScreen();
void drawHLineClipped(int x, int y, int w);
void fetchWeather();

bool isNightHour(uint8_t hour);
void applyEffectiveContrast(uint8_t hour);
void applyEffectiveContrast();
void triggerGreeting();
const char* greetingForHour(uint8_t hour);
void drawGreetingOverlay();

void cycleAutoWipeHour();
void checkManualWipeSchedule(const DateTime& now);
void checkSmartWipeSchedule(const DateTime& now);
void startWipe(unsigned long durationMs);
void updateWipe();
void drawWipeActiveScreen();

void periodicTimeBasedChecks();

void startWebServer();
void handleWebRoot();
void handleWebState();
void handleWebSetFace();
void handleWebSetRPM();
void handleWebToggleDir();
void handleWebSetBrightness();
void handleWebSyncNow();
void handleWebBillboard();
void handleWebNotFound();

void drawBillboardScreen();
void drawPomodoroScreen();

// ---- Feature 8: Moon phase (this revision) ----
float getMoonPhase(const DateTime& now);
const char* moonPhaseName(float phase);
void drawMoonDisc(int cx, int cy, int r, float phase);
void drawMoonPhaseFace(const DateTime& now);

// ---- Feature 7: OTA (this revision) ----
void handleUpdateResult();
void handleUpdateUpload();

// ---- Feature 11: Zen Breathing face (this revision) ----
void updateZenBreathingMotor();
void drawZenBreathingFace(const DateTime& now);

// ---- Feature 12: Generative Falling-Sand face (this revision) ----
void updateSandGrid();
void drawSandGrid();
void drawGenerativeArtFace(const DateTime& now);

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TOUCH, INPUT);

  Wire.begin();

  u8g2.begin();
  u8g2.setBusClock(400000);

  if (!rtc.begin()) {
    Serial.println(F("RTC not found - clock faces will show 00:00:00"));
    rtcAvailable = false;
  } else {
    rtcAvailable = true;
    if (!rtc.isrunning()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  loadSettingsFull();
  applyEffectiveContrast();

  stepper.setMaxSpeed(4000.0f);
  applyMotorSpeed();

  randomSeed(micros());
  bootStartTime = millis();

  // Feature 1/3: WiFi + web dashboard stay up permanently so the dashboard
  // is reachable any time, instead of the old "WiFi off until sync" policy.
  if (KEEP_WIFI_ALWAYS_ON) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    WiFi.mode(WIFI_OFF);
  }

  lastInteractionTime = millis();
  lastContrastCheck   = millis();
  currentState = SystemState::BOOT;
}

// ============================================================================
//  MAIN LOOP  — no delay() anywhere in this steady-state path.
// ============================================================================
void loop() {
  // 1) Motor: must run every single pass for smooth constant-velocity motion.
  updateMotor();

  // 2) Touch input: fully non-blocking, millis()-based.
  pollTouch();

  // 3) Boot screen timeout.
  if (currentState == SystemState::BOOT &&
      millis() - bootStartTime >= BOOT_DURATION_MS) {
    currentState = SystemState::MAIN_SCREEN;
    triggerGreeting();
  }

  // 4) Animation overlay timeout.
  if (currentState == SystemState::ANIMATION &&
      millis() - animStartTime >= ANIM_DURATION_MS) {
    currentState = stateBeforeAnimation;
  }

  // 4.5) WiFi/NTP (+ weather) sync state machine.
  if (currentState == SystemState::WIFI_SYNC) {
    updateWifiSync();
  }

  // 4.6) Auto-wipe (manual per-hour OR smart 12h) full-speed sweep.
  if (currentState == SystemState::WIPE_ACTIVE) {
    updateWipe();
  }

  // 4.7) Pomodoro Zen Mode.
  if (currentState == SystemState::POMODORO) {
    updatePomodoro();
  }

  // 4.8) Billboard scroll timeout / auto-return.
  if (currentState == SystemState::BILLBOARD) {
    if (millis() - billboardStartTime >= BILLBOARD_DURATION_MS) {
      currentState = stateBeforeBillboard;
    }
  }

  // 4.9) Easter-egg mini-game physics tick.
  if (currentState == SystemState::GAME) {
    updateGame();
  }

  // 4.95) Feature 11: keep the motor speed synced to the breathing rhythm
  //       whenever the Zen Breathing face is showing on the main screen.
  //       Cheap (a handful of float ops), so it runs every loop() pass just
  //       like updateMotor() - it also transparently no-ops (and restores
  //       the user's normal RPM) the instant the face/state changes away.
  updateZenBreathingMotor();

  // 5) Shared hourly-granularity checks (contrast + both wipe schedules).
  if (millis() - lastContrastCheck >= CONTRAST_CHECK_INTERVAL_MS) {
    lastContrastCheck = millis();
    periodicTimeBasedChecks();
  }

  // 5.1) Auto-sleep bookkeeping.
  updateSleepTimer();

  // 5.2) Auto-exit settings menus after inactivity (also commits to NVS).
  if (currentState == SystemState::SETTINGS_MENU ||
      currentState == SystemState::MOTOR_MENU ||
      currentState == SystemState::DISPLAY_MENU ||
      currentState == SystemState::TIME_SET_MENU) {
    if (millis() - lastInteractionTime >= MENU_TIMEOUT_MS) {
      exitSettingsMenus();
    }
  }

  // 6) Feature 1: service the web dashboard (incl. Feature 7 OTA endpoint).
  //    WebServer::handleClient() only does real work when a client has an
  //    open/pending request, so on a typical pass through loop() this is a
  //    cheap socket poll - it never introduces a meaningful stall for the
  //    stepper. The one exception is an in-progress OTA upload, which is a
  //    rare, user-initiated event (see the notes at the top of the file and
  //    on handleUpdateUpload() below).
  if (webServerStarted) {
    server.handleClient();
  } else if (KEEP_WIFI_ALWAYS_ON && WiFi.status() == WL_CONNECTED) {
    startWebServer();
  }

  // 7) Throttled display refresh.
  unsigned long now = millis();
  unsigned long interval = (currentState == SystemState::ANIMATION) ? ANIM_FRAME_MS
                                                                     : DISPLAY_INTERVAL_MS;
  if (!displayAsleep && (now - lastDisplayUpdate >= interval)) {
    lastDisplayUpdate = now;
    renderDisplay();
  }

  // 8) Run the motor once more right after the (short) I2C/web transfer
  //    above, to minimise any timing skew.
  updateMotor();
}

// ============================================================================
//  SETTINGS EXIT HELPER
// ============================================================================
void exitSettingsMenus() {
  saveSettings();
  currentState = SystemState::MAIN_SCREEN;
}

// ============================================================================
//  MOTOR CONTROL
// ============================================================================
void updateMotor() {
  stepper.runSpeed();
}

void applyMotorSpeed() {
  float stepsPerSecond = (motorRPM * STEPS_PER_REV) / 60.0f;
  float signedSpeed = motorRunning ? (motorDirection * stepsPerSecond) : 0.0f;
  stepper.setSpeed(signedSpeed);
}

// Thin forwarders so Pomodoro block (declared earlier in the file, ahead of
// applyMotorSpeed()/lastInteractionTime's normal usage sites) can reach them
// without a full file reorder.
void applyMotorSpeedFwd() { applyMotorSpeed(); }
void lastInteractionTimeFwd() { lastInteractionTime = millis(); }

void increaseSpeed() {
  motorRPM = min(RPM_MAX, motorRPM + RPM_STEP);
  applyMotorSpeed();
}

void decreaseSpeed() {
  motorRPM = max(RPM_MIN, motorRPM - RPM_STEP);
  applyMotorSpeed();
}

void setMaxRPM() {
  motorRPM = RPM_MAX;
  applyMotorSpeed();
}

void toggleDirection() {
  motorDirection = -motorDirection;
  applyMotorSpeed();
}

void toggleMotorRunning() {
  motorRunning = !motorRunning;
  applyMotorSpeed();
}

// ============================================================================
//  TOUCH POLLING — time-window algorithm
// ============================================================================
void pollTouch() {
  bool reading = digitalRead(PIN_TOUCH);

  if (reading != lastRawReading) {
    lastDebounceTime = millis();
  }
  lastRawReading = reading;

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;
      if (stableState == HIGH) {
        onTouchPress();
      } else {
        onTouchRelease();
      }
    }
  }

  if (isPressed && !longPressFired && !suppressThisPress) {
    if (millis() - pressStartTime >= LONG_PRESS_MS) {
      longPressFired = true;
      onLongPress();
    }
  }

  if (tapWindowOpen && (millis() - tapWindowStart >= TAP_WINDOW_MS)) {
    tapWindowOpen = false;
    resolveTapSequence(tapCount);
    tapCount = 0;
  }
}

void onTouchPress() {
  lastInteractionTime = millis();

  if (displayAsleep) {
    wakeDisplay();
    suppressThisPress = true;
    isPressed = true;
    pressStartTime = millis();
    longPressFired = false;
    return;
  }

  isPressed = true;
  pressStartTime = millis();
  longPressFired = false;

  // In-game: a press is treated as an immediate jump input rather than
  // waiting for the tap-window to resolve, so the game feels responsive.
  if (currentState == SystemState::GAME) {
    if (gameOver) {
      // tap on the game-over screen retries
      startGame();
    } else {
      gameJump();
    }
  }
}

void onTouchRelease() {
  isPressed = false;

  if (suppressThisPress) {
    suppressThisPress = false;
    return;
  }

  if (longPressFired) {
    longPressFired = false;
    return;
  }

  tapCount++;
  tapWindowOpen = true;
  tapWindowStart = millis();
}

void resolveTapSequence(uint8_t count) {
  // Feature 6: exactly 5 rapid taps launches the Easter-egg game from any
  // normal clock/menu context (but not from inside another modal like
  // billboard/pomodoro/wipe, so those aren't accidentally interrupted).
  if (count == GAME_TRIGGER_TAPS &&
      (currentState == SystemState::MAIN_SCREEN ||
       currentState == SystemState::SETTINGS_MENU ||
       currentState == SystemState::MOTOR_MENU ||
       currentState == SystemState::DISPLAY_MENU)) {
    startGame();
    return;
  }

  switch (currentState) {
    case SystemState::MAIN_SCREEN:
      if (count == 2) {
        cycleClockFace();
      } else if (count == 3) {
        startAnimation();
      }
      break;

    case SystemState::ANIMATION:
      if (count == 2) {
        currentState = stateBeforeAnimation;
      }
      break;

    case SystemState::SETTINGS_MENU:
      settingsMenuIndex = (settingsMenuIndex + 1) % SETTINGS_ITEM_COUNT;
      break;

    case SystemState::MOTOR_MENU:
      motorMenuIndex = (motorMenuIndex + 1) % MOTOR_ITEM_COUNT;
      break;

    case SystemState::DISPLAY_MENU:
      displayMenuIndex = (displayMenuIndex + 1) % DISPLAY_ITEM_COUNT;
      break;

    case SystemState::TIME_SET_MENU:
      if (timeSetStep == 0) {
        tempHour = (tempHour + 1) % 24;
      } else {
        tempMinute = (tempMinute + 1) % 60;
      }
      break;

    case SystemState::GAME:
      // handled immediately in onTouchPress(); nothing further needed here.
      break;

    case SystemState::BILLBOARD:
      // any tap cancels the billboard early.
      currentState = stateBeforeBillboard;
      break;

    default:
      break;
  }
}

void onLongPress() {
  lastInteractionTime = millis();

  // A long-press always escapes the mini-game, billboard, or Pomodoro mode
  // back to the main clock, regardless of what sub-state it's in.
  if (currentState == SystemState::GAME) {
    exitGame();
    return;
  }
  if (currentState == SystemState::BILLBOARD) {
    currentState = stateBeforeBillboard;
    return;
  }
  if (currentState == SystemState::POMODORO) {
    exitPomodoro();
    return;
  }

  switch (currentState) {
    case SystemState::MAIN_SCREEN:
      currentState = SystemState::SETTINGS_MENU;
      settingsMenuIndex = 0;
      break;

    case SystemState::SETTINGS_MENU:
      if (settingsMenuIndex == SETTINGS_IDX_MOTOR) {
        currentState = SystemState::MOTOR_MENU; motorMenuIndex = 0;
      } else if (settingsMenuIndex == SETTINGS_IDX_DISPLAY) {
        currentState = SystemState::DISPLAY_MENU; displayMenuIndex = 0;
      } else if (settingsMenuIndex == SETTINGS_IDX_SET_TIME) {
        currentState = SystemState::TIME_SET_MENU;
        timeSetStep = 0;
        DateTime now = rtcAvailable ? rtc.now() : DateTime((uint32_t)0);
        tempHour = now.hour();
        tempMinute = now.minute();
      } else if (settingsMenuIndex == SETTINGS_IDX_WIFI) {
        startWifiSync();
      } else if (settingsMenuIndex == SETTINGS_IDX_POMODORO) {
        enterPomodoro();
      } else {
        exitSettingsMenus();
      }
      break;

    case SystemState::TIME_SET_MENU:
      if (timeSetStep == 0) {
        timeSetStep = 1;
      } else {
        if (rtcAvailable) {
          DateTime now = rtc.now();
          rtc.adjust(DateTime(now.year(), now.month(), now.day(), tempHour, tempMinute, 0));
        }
        currentState = SystemState::SETTINGS_MENU;
      }
      break;

    case SystemState::MOTOR_MENU:
      switch (motorMenuIndex) {
        case MOTOR_IDX_SPEED_UP:   increaseSpeed();     break;
        case MOTOR_IDX_SPEED_DOWN: decreaseSpeed();     break;
        case MOTOR_IDX_MAX_RPM:    setMaxRPM();         break;
        case MOTOR_IDX_TOGGLE_DIR: toggleDirection();   break;
        case MOTOR_IDX_TOGGLE_RUN: toggleMotorRunning();break;
        case MOTOR_IDX_AUTO_WIPE:  cycleAutoWipeHour();  break;
        case MOTOR_IDX_BACK:
          currentState = SystemState::SETTINGS_MENU;
          settingsMenuIndex = 0;
          break;
      }
      break;

    case SystemState::DISPLAY_MENU:
      switch (displayMenuIndex) {
        case 0: cycleBrightness(); break;
        case 1: cycleAutoSleepValue(); break;
        case 2: toggleTimeFormat(); break;
        case 3:
          currentState = SystemState::SETTINGS_MENU;
          settingsMenuIndex = 1;
          break;
      }
      break;

    default:
      break;
  }
}

// ============================================================================
//  ACTIONS
// ============================================================================
void cycleBrightness() {
  brightnessIndex = (brightnessIndex + 1) % 3;
  applyEffectiveContrast();
}

void cycleClockFace() {
  uint8_t next = (static_cast<uint8_t>(currentFace) + 1) % static_cast<uint8_t>(ClockFace::FACE_COUNT);
  currentFace = static_cast<ClockFace>(next);
}

void startAnimation() {
  stateBeforeAnimation = currentState;
  currentState = SystemState::ANIMATION;
  animStartTime = millis();
  lastAnimFrameTime = millis();
  animFrame = 0;
  currentAnimType = random(0, 3);
}

void cycleAutoSleepValue() {
  sleepOptionIndex = (sleepOptionIndex + 1) % SLEEP_OPTION_COUNT;
}

void toggleTimeFormat() {
  use12HourFormat = !use12HourFormat;
}

// ============================================================================
//  WI-FI NTP SYNC  (+ Feature 3 weather fetch piggybacked on the same event)
// ============================================================================
void startWifiSync() {
  currentState        = SystemState::WIFI_SYNC;
  wifiSyncPhase        = WifiSyncPhase::CONNECTING;
  wifiSyncStartTime    = millis();
  wifiConfigTimeCalled = false;
  weatherFetchRequested = true; // ask updateWifiSync() to also grab weather

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void updateWifiSync() {
  if (wifiSyncPhase == WifiSyncPhase::CONNECTING) {

    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiConfigTimeCalled) {
        configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
        wifiConfigTimeCalled = true;
      }

      time_t nowEpoch = time(nullptr);
      if (nowEpoch > (time_t)NTP_SANE_EPOCH) {
        struct tm timeinfo;
        localtime_r(&nowEpoch, &timeinfo);
        if (rtcAvailable) {
          rtc.adjust(DateTime((uint16_t)(timeinfo.tm_year + 1900),
                               (uint8_t)(timeinfo.tm_mon + 1),
                               (uint8_t)timeinfo.tm_mday,
                               (uint8_t)timeinfo.tm_hour,
                               (uint8_t)timeinfo.tm_min,
                               (uint8_t)timeinfo.tm_sec));
        }

        // Feature 3: grab weather once, right after time lands, while we
        // already have a live connection - saves a second reconnect later.
        if (weatherFetchRequested) {
          fetchWeather();
          weatherFetchRequested = false;
        }

        // Leave WiFi connected if the dashboard wants it kept alive;
        // otherwise power the radio down to save energy.
        if (!KEEP_WIFI_ALWAYS_ON) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
        }
        wifiSyncPhase      = WifiSyncPhase::SUCCESS;
        wifiSyncResultTime = millis();
      } else if (millis() - wifiSyncStartTime >= WIFI_CONNECT_TIMEOUT_MS) {
        if (!KEEP_WIFI_ALWAYS_ON) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
        }
        wifiSyncPhase      = WifiSyncPhase::FAILED;
        wifiSyncResultTime = millis();
      }

    } else if (millis() - wifiSyncStartTime >= WIFI_CONNECT_TIMEOUT_MS) {
      if (!KEEP_WIFI_ALWAYS_ON) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      }
      wifiSyncPhase      = WifiSyncPhase::FAILED;
      wifiSyncResultTime = millis();
    }

  } else {
    if (millis() - wifiSyncResultTime >= WIFI_RESULT_DISPLAY_MS) {
      currentState      = SystemState::SETTINGS_MENU;
      settingsMenuIndex = SETTINGS_IDX_WIFI;
    }
  }
}

// ============================================================================
//  Feature 3: LIVE WEATHER SYNC (Open-Meteo - No API Key Needed)
// ----------------------------------------------------------------------------
//  A single blocking-ish HTTPClient GET (typically well under a second on a
//  good link) issued only during an already-connected WiFi/NTP sync, which
//  itself only happens on user request or the periodic hourly check. The
//  stepper is nudged immediately before and after via updateMotor() so any
//  latency here never accumulates into visible stutter on the sand wiper.
// ============================================================================
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  updateMotor(); // keep the wiper moving right up to the HTTP call

  HTTPClient http;
  // Direct weather endpoint for Cairo (no API key required).
  String url = "http://api.open-meteo.com/v1/forecast?latitude=30.0444&longitude=31.2357&current_weather=true";

  http.begin(url);
  http.setTimeout(6000); // bounded worst case
  int code = http.GET();

  if (code == HTTP_CODE_OK) {
    String payload = http.getString();

    StaticJsonDocument<768> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      weatherTempC = doc["current_weather"]["temperature"] | NAN;
      int wCode = doc["current_weather"]["weathercode"] | -1;

      // Map the numeric weather code to a condition name used by the icon
      // renderer and dashboard.
      if (wCode == 0) weatherCondition = "Clear";
      else if (wCode >= 1 && wCode <= 3) weatherCondition = "Clouds";
      else if (wCode == 45 || wCode == 48) weatherCondition = "Mist";
      else if (wCode >= 51 && wCode <= 57) weatherCondition = "Drizzle";
      else if (wCode >= 61 && wCode <= 67) weatherCondition = "Rain";
      else if (wCode >= 71 && wCode <= 77) weatherCondition = "Snow";
      else if (wCode >= 80 && wCode <= 82) weatherCondition = "Rain";
      else if (wCode >= 95 && wCode <= 99) weatherCondition = "Thunderstorm";
      else weatherCondition = "Unknown";

      weatherValid = true;
      lastWeatherFetch = millis();
    } else {
      weatherValid = false;
    }
  } else {
    weatherValid = false;
  }

  http.end();
  updateMotor(); // and again immediately after, closing the bracket
}

// ============================================================================
//  Circadian Display Mode
// ============================================================================
bool isNightHour(uint8_t hour) {
  return (hour >= NIGHT_START_HOUR) || (hour < NIGHT_END_HOUR);
}

void applyEffectiveContrast(uint8_t hour) {
  uint8_t target = isNightHour(hour) ? NIGHT_CONTRAST : BRIGHTNESS_LEVELS[brightnessIndex];
  u8g2.setContrast(target);
}

void applyEffectiveContrast() {
  DateTime now = rtcAvailable ? rtc.now() : DateTime((uint32_t)0);
  applyEffectiveContrast(now.hour());
}

const char* greetingForHour(uint8_t hour) {
  if (hour >= 5  && hour < 12) return "Good Morning!";
  if (hour >= 12 && hour < 17) return "Good Afternoon!";
  if (hour >= 17 && hour < 22) return "Good Evening!";
  return "Good Night!";
}

void triggerGreeting() {
  DateTime now = rtcAvailable ? rtc.now() : DateTime((uint32_t)0);
  strncpy(greetingText, greetingForHour(now.hour()), sizeof(greetingText) - 1);
  greetingText[sizeof(greetingText) - 1] = '\0';
  greetingActive     = true;
  greetingStartTime  = millis();
}

void drawGreetingOverlay() {
  u8g2.setFont(u8g2_font_ncenB10_tr);
  int tw = u8g2.getStrWidth(greetingText);
  u8g2.drawStr((128 - tw) / 2, 30, greetingText);

  DateTime now = rtcAvailable ? rtc.now() : DateTime((uint32_t)0);
  int h = now.hour();
  bool isPM = h >= 12;
  char buf[12];
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h, now.minute(), isPM ? "PM" : "AM");
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d", h, now.minute());
  }
  u8g2.setFont(u8g2_font_6x10_tr);
  int tw2 = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - tw2) / 2, 48, buf);
}

// ============================================================================
//  AUTO-WIPE (manual per-hour setting + Feature 5 smart 12h schedule)
//  Unified into one generic startWipe(duration)/updateWipe() pair so both
//  triggers share the exact same non-blocking sweep-and-restore machinery.
// ============================================================================
void cycleAutoWipeHour() {
  if (wipeHourSetting == WIPE_HOUR_OFF) {
    wipeHourSetting = 0;
  } else if (wipeHourSetting >= 23) {
    wipeHourSetting = WIPE_HOUR_OFF;
  } else {
    wipeHourSetting++;
  }
}

void checkManualWipeSchedule(const DateTime& now) {
  if (wipeHourSetting == WIPE_HOUR_OFF) return;
  if (currentState == SystemState::WIPE_ACTIVE) return;
  if (now.hour() == wipeHourSetting && now.day() != lastManualWipeDay) {
    startWipe(25000); // 25s quick sweep for the manual/user-configured slot
    lastManualWipeDay = now.day();
  }
}

// Feature 5: fixed twice-daily 2-minute deep-clean sweep at 03:00 / 15:00,
// independent of whatever hour the user picked for the manual setting above.
void checkSmartWipeSchedule(const DateTime& now) {
  if (currentState == SystemState::WIPE_ACTIVE) return;

  if (now.hour() == SMART_WIPE_HOUR_A && now.day() != lastSmartWipeDay_A) {
    startWipe(SMART_WIPE_DURATION_MS);
    lastSmartWipeDay_A = now.day();
    return;
  }
  if (now.hour() == SMART_WIPE_HOUR_B && now.day() != lastSmartWipeDay_B) {
    startWipe(SMART_WIPE_DURATION_MS);
    lastSmartWipeDay_B = now.day();
  }
}

void startWipe(unsigned long durationMs) {
  prevMotorRPM       = motorRPM;
  prevMotorDirection = motorDirection;
  prevMotorRunning   = motorRunning;

  motorRPM       = RPM_MAX;
  motorDirection = 1;
  motorRunning   = true;
  applyMotorSpeed();

  currentWipeDurationMs = durationMs;
  stateBeforeWipe      = currentState;
  currentState         = SystemState::WIPE_ACTIVE;
  wipeStartTime        = millis();
  lastInteractionTime  = millis();
}

void updateWipe() {
  if (millis() - wipeStartTime >= currentWipeDurationMs) {
    motorRPM       = prevMotorRPM;
    motorDirection = prevMotorDirection;
    motorRunning   = prevMotorRunning;
    applyMotorSpeed();
    currentState = (stateBeforeWipe == SystemState::WIPE_ACTIVE)
                     ? SystemState::MAIN_SCREEN
                     : stateBeforeWipe;
  }
}

void drawWipeActiveScreen() {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(6, 18, "AUTO-WIPE CYCLE");
  u8g2.drawHLine(0, 22, 128);

  const int cx = 64, cy = 40, r = 14;
  float angle = (millis() / 4.0f) * (PI / 180.0f) * 30.0f;
  u8g2.drawCircle(cx, cy, r);
  u8g2.drawLine(cx, cy, cx + (int)(cos(angle) * r), cy + (int)(sin(angle) * r));

  unsigned long elapsed = millis() - wipeStartTime;
  unsigned long remainingMs = (elapsed >= currentWipeDurationMs) ? 0 : (currentWipeDurationMs - elapsed);
  char buf[26];
  if (remainingMs >= 60000) {
    snprintf(buf, sizeof(buf), "Resetting sand... %lum", (remainingMs / 60000UL) + 1);
  } else {
    snprintf(buf, sizeof(buf), "Resetting sand... %lus", (remainingMs / 1000UL) + 1);
  }
  u8g2.setFont(u8g2_font_5x7_tr);
  int tw = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - tw) / 2, 61, buf);
}

// ============================================================================
//  Shared throttle: contrast + both wipe schedules.
// ============================================================================
void periodicTimeBasedChecks() {
  if (!rtcAvailable) return;
  DateTime now = rtc.now();
  applyEffectiveContrast(now.hour());
  checkManualWipeSchedule(now);
  checkSmartWipeSchedule(now);

  // Feature 3: opportunistic weather refresh every WEATHER_REFRESH_MS while
  // already connected, so the WEATHER face doesn't need a manual sync to
  // stay reasonably fresh.
  if (KEEP_WIFI_ALWAYS_ON && WiFi.status() == WL_CONNECTED &&
      (millis() - lastWeatherFetch >= WEATHER_REFRESH_MS)) {
    fetchWeather();
  }
}

// ============================================================================
//  AUTO-SLEEP
// ============================================================================
void updateSleepTimer() {
  uint8_t minutes = SLEEP_OPTIONS[sleepOptionIndex];
  if (minutes == 0 || displayAsleep) return;

  unsigned long timeoutMs = (unsigned long)minutes * 60000UL;
  if (millis() - lastInteractionTime >= timeoutMs) {
    displayAsleep = true;
    u8g2.setPowerSave(1);
  }
}

void wakeDisplay() {
  if (displayAsleep) {
    displayAsleep = false;
    u8g2.setPowerSave(0);
    applyEffectiveContrast();
    if (currentState == SystemState::MAIN_SCREEN) {
      triggerGreeting();
    }
  }
  lastInteractionTime = millis();
}

// ============================================================================
//  Small helper: draw a horizontal line safely clipped to the left edge.
// ============================================================================
void drawHLineClipped(int x, int y, int w) {
  if (w <= 0) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (w <= 0) return;
  u8g2.drawHLine((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w);
}

// ============================================================================
//  CARTOON MOVIE MODE 1 — "Abyssal Voyage"
// ============================================================================
void drawCartoonMovie(const DateTime& now) {
  unsigned long t = millis();

  int farOffset = (t / 60) % 32;
  for (int i = -1; i < 5; i++) {
    int bx = i * 32 - farOffset;
    u8g2.drawTriangle(bx, 64, bx + 16, 54, bx + 32, 64);
    u8g2.drawTriangle(bx + 8, 0, bx + 24, 8, bx + 40, 0);
  }

  int nearOffset = (t / 25) % 40;
  for (int i = -1; i < 4; i++) {
    int bx = i * 40 - nearOffset;
    u8g2.drawTriangle(bx, 64, bx + 20, 48, bx + 40, 64);
  }

  for (int i = 0; i < 6; i++) {
    unsigned long bubbleT = t + (unsigned long)i * 733UL;
    int by = 64 - (int)((bubbleT / 20) % 70);
    int bx = 10 + i * 20 + (int)(sin(bubbleT / 200.0f + i) * 4);
    u8g2.drawCircle(bx, by, (i % 2) + 1);
  }

  for (int i = 0; i < 4; i++) {
    int baseX = 15 + i * 30;
    float sway = sin(t / 400.0f + i * 1.3f) * 6.0f;
    u8g2.drawLine(baseX, 64, baseX + (int)sway, 54);
    u8g2.drawLine(baseX + (int)sway, 54, baseX + (int)(sway * 1.5f), 46);
  }

  for (int i = 0; i < 2; i++) {
    unsigned long fishT = (t + (unsigned long)i * 4000UL) % 8000UL;
    int fx = map(fishT, 0, 8000, 140, -20);
    int fy = 20 + i * 15;
    u8g2.drawTriangle(fx, fy, fx + 8, fy - 3, fx + 8, fy + 3);
    u8g2.drawLine(fx + 8, fy, fx + 12, fy - 3);
    u8g2.drawLine(fx + 8, fy, fx + 12, fy + 3);
  }

  int subX = 64;
  int subY = 34 + (int)(sin(t / 500.0f) * 3);

  u8g2.drawRBox(subX - 20, subY - 6, 40, 12, 6);
  u8g2.drawCircle(subX - 8, subY, 3);
  u8g2.drawCircle(subX + 4, subY, 3);
  u8g2.drawBox(subX - 4, subY - 12, 6, 7);
  u8g2.drawLine(subX - 1, subY - 12, subX - 1, subY - 16);
  u8g2.drawLine(subX - 1, subY - 16, subX + 4, subY - 16);
  u8g2.drawTriangle(subX + 20, subY - 6, subX + 20, subY + 6, subX + 28, subY);

  float propAngle = (t / 8.0f) * (PI / 180.0f) * 40.0f;
  float pc = cos(propAngle), ps = sin(propAngle);
  int propX = subX - 22, propY = subY;
  u8g2.drawLine(propX, propY, propX - (int)(pc * 5), propY - (int)(ps * 5));
  u8g2.drawLine(propX, propY, propX + (int)(pc * 5), propY + (int)(ps * 5));

  char timeBuf[12];
  int h = now.hour();
  bool isPM = h >= 12;
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d%s", h, now.minute(), now.second(), isPM ? "P" : "A");
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h, now.minute(), now.second());
  }

  u8g2.setFont(u8g2_font_6x10_tr);
  int tw = u8g2.getStrWidth(timeBuf);
  int panelW = tw + 20;
  int panelX = (128 - panelW) / 2;

  u8g2.setDrawColor(0);
  u8g2.drawBox(panelX, 53, panelW, 11);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(panelX, 53, panelW, 11);

  int pulseR = (int)((t % 1000UL) / 250UL);
  u8g2.drawDisc(panelX + 6, 58, 1);
  if (pulseR > 0) {
    u8g2.drawCircle(panelX + 6, 58, pulseR + 1);
  }

  u8g2.drawStr(panelX + 14, 61, timeBuf);
}

// ============================================================================
//  CARTOON MOVIE MODE 2 — "Star Voyage"
// ============================================================================
void drawCartoonMovie2(const DateTime& now) {
  unsigned long t = millis();

  const uint8_t STARS_PER_LAYER = 8;
  for (uint8_t layer = 0; layer < 3; layer++) {
    unsigned long speedDiv = (layer == 0) ? 90 : (layer == 1) ? 45 : 20;
    int offset = (t / speedDiv) % 128;
    for (uint8_t i = 0; i < STARS_PER_LAYER; i++) {
      int baseX = (i * 37 + layer * 13) % 128;
      int y = 4 + ((i * 11 + layer * 7) % 28);
      int x = ((baseX - offset) % 128 + 128) % 128;
      u8g2.drawPixel(x, y);
      if (layer == 2) {
        u8g2.drawPixel((x + 1) % 128, y);
      }
    }
  }

  int planetX = (int)((t / 30) % 200) - 40;
  int planetY = 12;
  int planetR = 9;
  u8g2.drawCircle(planetX, planetY, planetR);
  u8g2.drawCircle(planetX, planetY, planetR - 3);
  u8g2.drawLine(planetX - planetR - 4, planetY - 3, planetX + planetR + 4, planetY + 3);
  u8g2.drawLine(planetX - planetR - 4, planetY - 2, planetX + planetR + 4, planetY + 4);

  int shipX = 60;
  int shipY = 40 + (int)(sin(t / 300.0f) * 4);

  u8g2.drawRBox(shipX - 14, shipY - 4, 28, 8, 3);
  u8g2.drawCircle(shipX + 4, shipY - 5, 4);
  u8g2.drawTriangle(shipX - 14, shipY - 4, shipX - 14, shipY + 4, shipX - 22, shipY);

  if ((t / 80) % 2 == 0) {
    u8g2.drawDisc(shipX - 16, shipY, 2);
  } else {
    u8g2.drawDisc(shipX - 17, shipY, 3);
  }

  for (int p = 0; p < 3; p++) {
    int trailX = shipX - 20 - p * 6 - (int)((t / 15) % 6);
    if (trailX > 0) {
      u8g2.drawPixel(trailX, shipY + (p % 2 == 0 ? -1 : 1));
    }
  }

  unsigned long cometCycle = t % 6000UL;
  if (cometCycle < 600) {
    int cx = map(cometCycle, 0, 600, 128, -20);
    int cy = 5 + (int)(cometCycle * 0.08f);
    u8g2.drawLine(cx, cy, cx + 10, cy - 5);
  }

  char timeBuf[12];
  int h = now.hour();
  bool isPM = h >= 12;
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d%s", h, now.minute(), now.second(), isPM ? "P" : "A");
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h, now.minute(), now.second());
  }

  u8g2.setFont(u8g2_font_6x10_tr);
  int tw = u8g2.getStrWidth(timeBuf);
  int hudX = (128 - tw) / 2 - 4;
  int hudW = tw + 8;

  u8g2.setDrawColor(0);
  u8g2.drawBox(hudX, 53, hudW, 11);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(hudX, 53, hudW, 11);
  u8g2.drawStr(hudX + 4, 61, timeBuf);
}

// ============================================================================
//  Feature 3: WEATHER CLOCK FACE
// ============================================================================
void drawWeatherIcon(int x, int y) {
  // Small 24x24-ish glyph drawn with primitives - condition-dependent.
  String c = weatherCondition;
  if (c == "Clear") {
    u8g2.drawDisc(x + 10, y + 10, 7);
    for (int i = 0; i < 8; i++) {
      float a = i * (PI / 4.0f);
      int x1 = x + 10 + (int)(cos(a) * 10);
      int y1 = y + 10 + (int)(sin(a) * 10);
      int x2 = x + 10 + (int)(cos(a) * 13);
      int y2 = y + 10 + (int)(sin(a) * 13);
      u8g2.drawLine(x1, y1, x2, y2);
    }
  } else if (c == "Rain" || c == "Drizzle" || c == "Thunderstorm") {
    u8g2.drawRBox(x, y, 22, 12, 4);
    for (int i = 0; i < 3; i++) {
      int dx = x + 5 + i * 6;
      u8g2.drawLine(dx, y + 14, dx - 2, y + 20);
    }
    if (c == "Thunderstorm") {
      u8g2.drawTriangle(x + 10, y + 14, x + 6, y + 20, x + 12, y + 20);
    }
  } else if (c == "Snow") {
    u8g2.drawRBox(x, y, 22, 12, 4);
    for (int i = 0; i < 3; i++) {
      int dx = x + 5 + i * 6;
      u8g2.drawDisc(dx, y + 18, 1);
    }
  } else { // Clouds / Mist / Fog / default
    u8g2.drawRBox(x, y + 2, 22, 12, 5);
    u8g2.drawCircle(x + 6, y + 4, 5);
    u8g2.drawCircle(x + 14, y + 2, 6);
  }
}

void drawWeatherFace(const DateTime& now) {
  u8g2.setFont(u8g2_font_ncenB08_tr);

  char dateBuf[16];
  snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", now.day(), now.month(), now.year());
  u8g2.drawStr(2, 10, dateBuf);

  int h = now.hour();
  bool isPM = h >= 12;
  char timeBuf[12];
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s", h, now.minute(), isPM ? "PM" : "AM");
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, now.minute());
  }
  int tw = u8g2.getStrWidth(timeBuf);
  u8g2.drawStr(128 - tw - 2, 10, timeBuf);

  u8g2.drawHLine(0, 13, 128);

  if (!weatherValid) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(10, 40, "Weather: sync WiFi");
    u8g2.drawStr(10, 54, "to fetch conditions");
    return;
  }

  drawWeatherIcon(6, 22);

  char tempBuf[12];
  snprintf(tempBuf, sizeof(tempBuf), "%.0fC", weatherTempC);
  u8g2.setFont(u8g2_font_logisoso22_tf);
  u8g2.drawStr(38, 44, tempBuf);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(6, 62, WEATHER_CITY);
  u8g2.drawStr(70, 62, weatherCondition.c_str());
}

// ============================================================================
//  Feature 8 (this revision): MOON PHASE CLOCK FACE
// ----------------------------------------------------------------------------
//  getMoonPhase() uses Conway's lightweight synodic-month approximation:
//  a handful of integer/float ops, no RTC-heavy math, no external astronomy
//  library, and no blocking calls - safe to call on every display refresh.
//  It returns a value in [0, 1): 0.0 = New Moon, 0.5 = Full Moon, wrapping
//  back to 0.0 at the next New Moon (~29.53 days later).
//
//  drawMoonDisc() renders the actual illuminated shape (not just an icon)
//  by drawing the outline circle and then, scanline by scanline, computing
//  where the terminator (day/night boundary, itself half an ellipse) falls
//  and filling only the lit side with u8g2.drawHLine calls. This is O(2r)
//  simple arithmetic per frame - trivial for an ESP32 and safe to run at
//  the normal 200ms display-refresh cadence.
// ============================================================================
float getMoonPhase(const DateTime& now) {
  int year  = now.year();
  int month = now.month();
  int day   = now.day();

  if (month < 3) {
    year--;
    month += 12;
  }
  ++month;

  long c = (long)(365.25 * year);
  long e = (long)(30.6 * month);
  double jd = (double)c + (double)e + (double)day - 694039.09; // days since a known new moon reference
  jd /= 29.5305882;                                            // synodic (New-Moon-to-New-Moon) month length
  double phase = jd - floor(jd);                                // fractional part -> 0..1
  if (phase < 0.0) phase += 1.0;                                 // guard against negative fmod-style results
  return (float)phase;
}

const char* moonPhaseName(float phase) {
  if (phase < 0.03f || phase >= 0.97f) return "New Moon";
  if (phase < 0.22f) return "Wax Crescent";
  if (phase < 0.28f) return "1st Quarter";
  if (phase < 0.47f) return "Wax Gibbous";
  if (phase < 0.53f) return "Full Moon";
  if (phase < 0.72f) return "Wan Gibbous";
  if (phase < 0.78f) return "Last Quarter";
  return "Wan Crescent";
}

void drawMoonDisc(int cx, int cy, int r, float phase) {
  u8g2.drawCircle(cx, cy, r);

  // Illuminated fraction, 0 (new) .. 1 (full) .. 0 (new again), symmetric
  // around the full moon regardless of waxing/waning.
  float illumFrac = (1.0f - cosf(2.0f * PI * phase)) / 2.0f;
  bool  waxing    = (phase < 0.5f); // waxing = lit side grows on the right

  for (int dy = -r; dy <= r; dy++) {
    float wf = sqrtf((float)(r * r - dy * dy)); // half-width of the disc at this row
    int   w  = (int)wf;
    if (w <= 0) continue;

    // Terminator offset for this row: ranges from +w (new, nothing lit) to
    // -w (full, everything lit), crossing 0 at the quarters (straight edge).
    float tf = wf * (1.0f - 2.0f * illumFrac);
    int   t  = (int)tf;

    int x1, x2;
    if (waxing) {
      x1 = t;  x2 = w;   // lit region grows in from the right edge
    } else {
      x1 = -w; x2 = -t;  // lit region shrinks toward the left edge
    }
    if (x1 > x2) continue; // nothing lit on this scanline

    drawHLineClipped(cx + x1, cy + dy, x2 - x1 + 1);
  }
}

void drawMoonPhaseFace(const DateTime& now) {
  u8g2.setFont(u8g2_font_ncenB08_tr);

  char dateBuf[16];
  snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", now.day(), now.month(), now.year());
  u8g2.drawStr(2, 10, dateBuf);

  int h = now.hour();
  bool isPM = h >= 12;
  char timeBuf[12];
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s", h, now.minute(), isPM ? "PM" : "AM");
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, now.minute());
  }
  int tw = u8g2.getStrWidth(timeBuf);
  u8g2.drawStr(128 - tw - 2, 10, timeBuf);
  u8g2.drawHLine(0, 13, 128);

  float phase = getMoonPhase(now);
  drawMoonDisc(28, 40, 18, phase);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(54, 32, moonPhaseName(phase));

  char pctBuf[16];
  float illumPct = ((1.0f - cosf(2.0f * PI * phase)) / 2.0f) * 100.0f;
  snprintf(pctBuf, sizeof(pctBuf), "%.0f%% lit", illumPct);
  u8g2.drawStr(54, 46, pctBuf);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(54, 58, "Sync WiFi for date");
}

// ============================================================================
//  Feature 11 (this revision): ZEN BREATHING CLOCK FACE
// ----------------------------------------------------------------------------
//  A single sine wave drives an 8s breathe-in/breathe-out cycle (4s each,
//  smoothly - not a hard box-breathing cut). breathVal goes 0 -> 1 -> 0 once
//  per cycle; 0 = fully exhaled, 1 = peak inhale. The SAME breathVal is used
//  both to size the on-screen circle (Feature request: "smoothly expand for
//  inhalation, contract for exhalation") and, in updateZenBreathingMotor()
//  below, to gently modulate the stepper's RPM - turning the sand wiper's
//  motion itself into a tactile breathing pace-setter. Nothing here ever
//  calls delay(); the whole thing is a millis()-driven state machine that
//  is cheap enough to evaluate every single loop() pass.
// ============================================================================
bool          zenActive      = false;
float         zenBaseRPM     = 1.0f;
int8_t        zenBaseDir     = 1;
bool          zenBaseRunning = true;
unsigned long zenCycleStart  = 0;
static const unsigned long ZEN_CYCLE_MS = 8000UL; // 4s inhale + 4s exhale

// Shared helper so the motor-sync logic and the on-screen animation always
// agree on exactly the same breathing phase.
static float zenBreathValue(unsigned long cycleStart) {
  unsigned long elapsed = (millis() - cycleStart) % ZEN_CYCLE_MS;
  float phase = (float)elapsed / (float)ZEN_CYCLE_MS;
  return (1.0f - cosf(2.0f * PI * phase)) / 2.0f; // 0 (exhaled) .. 1 (inhaled) .. 0
}

void updateZenBreathingMotor() {
  bool shouldBeActive = (currentFace == ClockFace::ZEN_BREATHING &&
                          currentState == SystemState::MAIN_SCREEN);

  if (shouldBeActive && !zenActive) {
    // Just entered the face: remember the user's normal motor settings so
    // they can be restored exactly once breathing mode ends.
    zenBaseRPM     = motorRPM;
    zenBaseDir     = motorDirection;
    zenBaseRunning = motorRunning;
    zenCycleStart  = millis();
    zenActive      = true;
  } else if (!shouldBeActive && zenActive) {
    // Left the face (menu opened, face switched, etc.) - restore exactly.
    motorRPM       = zenBaseRPM;
    motorDirection = zenBaseDir;
    motorRunning   = zenBaseRunning;
    applyMotorSpeed();
    zenActive = false;
  }

  if (!zenActive) return;

  float breathVal = zenBreathValue(zenCycleStart);

  // Slower on exhale (breathVal -> 0), slightly faster on inhale
  // (breathVal -> 1): +-30% around whatever RPM the user had set.
  float modulated = zenBaseRPM * (0.7f + 0.6f * breathVal);
  motorRPM       = constrain(modulated, RPM_MIN, RPM_MAX);
  motorDirection = zenBaseDir;
  motorRunning   = true;
  applyMotorSpeed();
}

void drawZenBreathingFace(const DateTime& now) {
  // Falls back to a locally-timed phase if updateZenBreathingMotor() hasn't
  // (yet) marked the mode active this pass, so the very first rendered
  // frame still looks correct.
  unsigned long cycleStart = zenActive ? zenCycleStart : millis();
  float breathVal = zenBreathValue(cycleStart);
  unsigned long elapsed = (millis() - cycleStart) % ZEN_CYCLE_MS;
  bool inhaling = elapsed < (ZEN_CYCLE_MS / 2);

  const int cx = 64, cy = 36;
  const int minR = 9, maxR = 24;
  int r = minR + (int)((maxR - minR) * breathVal);

  u8g2.drawCircle(cx, cy, r);
  if (r > 5) u8g2.drawCircle(cx, cy, r - 5);
  if (r > 10) u8g2.drawDisc(cx, cy, 2);

  u8g2.setFont(u8g2_font_6x10_tr);
  const char* label = inhaling ? "Breathe In" : "Breathe Out";
  int tw = u8g2.getStrWidth(label);
  u8g2.drawStr((128 - tw) / 2, 60, label);

  // Small, unobtrusive time readout in the corner - this face is about the
  // breathing, not the clock.
  int h = now.hour();
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
  }
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, now.minute());
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 8, timeBuf);
}

// ============================================================================
//  Feature 12 (this revision): GENERATIVE FALLING-SAND CLOCK FACE
// ----------------------------------------------------------------------------
//  A tiny falling-sand cellular automaton: a low-resolution logical grid
//  (64 wide x 32 tall, each cell drawn as a 2x2 screen pixel block, exactly
//  covering the 128x64 OLED) where occupied cells try to fall straight down
//  each tick, or diagonally if blocked, piling up realistically. New grains
//  trickle in from a random column near the top every ~150ms.
//
//  Non-blocking by construction: updateSandGrid()/drawSandGrid() are only
//  ever called from inside drawGenerativeArtFace(), which itself only runs
//  from renderDisplay() - already throttled to the same DISPLAY_INTERVAL_MS
//  (200ms) cadence as every other face in loop(). It never gets its own
//  timer or blocking wait, so it costs the stepper nothing beyond what any
//  other face already costs.
// ============================================================================
static const uint8_t SAND_GRID_W  = 64;
static const uint8_t SAND_GRID_H  = 32;
static const uint8_t SAND_CELL_PX = 2; // 64*2 = 128px wide, 32*2 = 64px tall

uint8_t       sandGrid[SAND_GRID_H][SAND_GRID_W];
bool          sandGridInitialized = false;
unsigned long lastSandSpawn = 0;
static const unsigned long SAND_SPAWN_INTERVAL_MS = 150;

void updateSandGrid() {
  if (!sandGridInitialized) {
    memset(sandGrid, 0, sizeof(sandGrid));
    sandGridInitialized = true;
  }

  // Trickle a new grain in from a random column near the top.
  if (millis() - lastSandSpawn >= SAND_SPAWN_INTERVAL_MS) {
    lastSandSpawn = millis();
    uint8_t spawnX = (uint8_t)random(0, SAND_GRID_W);
    if (sandGrid[0][spawnX] == 0) sandGrid[0][spawnX] = 1;
  }

  // Bottom-up pass so a grain never falls twice within the same tick.
  for (int y = SAND_GRID_H - 2; y >= 0; y--) {
    for (int x = 0; x < SAND_GRID_W; x++) {
      if (sandGrid[y][x] == 0) continue;

      if (sandGrid[y + 1][x] == 0) {
        sandGrid[y + 1][x] = 1;
        sandGrid[y][x] = 0;
        continue;
      }

      bool leftFree  = (x > 0) && (sandGrid[y + 1][x - 1] == 0);
      bool rightFree = (x < SAND_GRID_W - 1) && (sandGrid[y + 1][x + 1] == 0);

      if (leftFree && rightFree) {
        if (random(0, 2) == 0) sandGrid[y + 1][x - 1] = 1;
        else                   sandGrid[y + 1][x + 1] = 1;
        sandGrid[y][x] = 0;
      } else if (leftFree) {
        sandGrid[y + 1][x - 1] = 1;
        sandGrid[y][x] = 0;
      } else if (rightFree) {
        sandGrid[y + 1][x + 1] = 1;
        sandGrid[y][x] = 0;
      }
      // else: fully blocked, the grain settles here and the pile grows.
    }
  }

  // Keep the flow perpetual instead of letting the whole screen fill solid:
  // once the bottom two rows are nearly packed, clear them so grains keep
  // trickling forever rather than the animation grinding to a halt.
  int filled = 0;
  for (int x = 0; x < SAND_GRID_W; x++) {
    if (sandGrid[SAND_GRID_H - 1][x]) filled++;
  }
  if (filled > SAND_GRID_W - 4) {
    memset(sandGrid[SAND_GRID_H - 1], 0, SAND_GRID_W);
    memset(sandGrid[SAND_GRID_H - 2], 0, SAND_GRID_W);
  }
}

void drawSandGrid() {
  for (uint8_t y = 0; y < SAND_GRID_H; y++) {
    for (uint8_t x = 0; x < SAND_GRID_W; x++) {
      if (sandGrid[y][x]) {
        u8g2.drawBox(x * SAND_CELL_PX, y * SAND_CELL_PX, SAND_CELL_PX, SAND_CELL_PX);
      }
    }
  }
}

void drawGenerativeArtFace(const DateTime& now) {
  updateSandGrid();
  drawSandGrid();

  // Overlay the digital time in a solid-filled panel on top of the sand, so
  // falling grains can never make it hard to read.
  int h = now.hour();
  bool isPM = h >= 12;
  if (use12HourFormat) {
    h = h % 12;
    if (h == 0) h = 12;
  }
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, now.minute());

  u8g2.setFont(u8g2_font_logisoso22_tf);
  int tw = u8g2.getStrWidth(buf);
  int panelW = tw + 14;
  int panelH = 28;
  int panelX = (128 - panelW) / 2;
  int panelY = 18;

  u8g2.setDrawColor(0);
  u8g2.drawBox(panelX, panelY, panelW, panelH);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(panelX, panelY, panelW, panelH);
  u8g2.drawStr(panelX + 7, panelY + 22, buf);

  if (use12HourFormat) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(panelX + panelW - 12, panelY + 8, isPM ? "PM" : "AM");
  }
}

// ============================================================================
//  Feature 4: POMODORO SCREEN
// ============================================================================
void drawPomodoroScreen() {
  unsigned long total = (pomodoroPhase == PomodoroPhase::WORK) ? POMODORO_WORK_MS : POMODORO_BREAK_MS;
  unsigned long elapsed = millis() - pomodoroPhaseStart;
  unsigned long remaining = (elapsed >= total) ? 0 : (total - elapsed);
  unsigned long remMin = remaining / 60000UL;
  unsigned long remSec = (remaining / 1000UL) % 60UL;

  u8g2.setFont(u8g2_font_ncenB08_tr);
  const char* label = (pomodoroPhase == PomodoroPhase::WORK) ? "FOCUS" : "SAND BREAK";
  int tw0 = u8g2.getStrWidth(label);
  u8g2.drawStr((128 - tw0) / 2, 12, label);
  u8g2.drawHLine(0, 15, 128);

  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", remMin, remSec);
  u8g2.setFont(u8g2_font_logisoso28_tf);
  int tw = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - tw) / 2, 48, buf);

  u8g2.setFont(u8g2_font_5x7_tr);
  const char* hint = (pomodoroPhase == PomodoroPhase::WORK) ? "Sand moves slow while you focus" : "Full-speed wipe - stretch a bit";
  int tw2 = u8g2.getStrWidth(hint);
  u8g2.drawStr(constrain((128 - tw2) / 2, 0, 128), 60, hint);
}

// ============================================================================
//  Feature 2: BILLBOARD SCREEN
// ============================================================================
void drawBillboardScreen() {
  unsigned long now = millis();
  unsigned long dt = now - lastBillboardFrameTime;
  lastBillboardFrameTime = now;

  u8g2.setFont(u8g2_font_ncenB14_tr);
  if (billboardTextWidthPx == 0) {
    billboardTextWidthPx = u8g2.getStrWidth(billboardText);
  }

  float pxMove = (BILLBOARD_SCROLL_SPEED_PXPS * (float)dt) / 1000.0f;
  billboardScrollX -= (int)pxMove;
  if (billboardScrollX < -billboardTextWidthPx) {
    billboardScrollX = 128; // loop the scroll while the 5s window is open
  }

  u8g2.drawStr(billboardScrollX, 40, billboardText);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 62, "New message from dashboard");
}

// ============================================================================
//  RENDERING
// ============================================================================
void renderDisplay() {
  u8g2.clearBuffer();

  if (greetingActive) {
    if (currentState != SystemState::MAIN_SCREEN) {
      greetingActive = false;
    } else if (millis() - greetingStartTime < GREETING_DURATION_MS) {
      drawGreetingOverlay();
      u8g2.sendBuffer();
      return;
    } else {
      greetingActive = false;
    }
  }

  switch (currentState) {
    case SystemState::BOOT:           drawBootScreen();      break;
    case SystemState::MAIN_SCREEN:    drawMainScreen();      break;
    case SystemState::SETTINGS_MENU:  drawSettingsMenu();    break;
    case SystemState::MOTOR_MENU:     drawMotorMenu();       break;
    case SystemState::DISPLAY_MENU:   drawDisplayMenu();     break;
    case SystemState::TIME_SET_MENU:  drawTimeSetMenu();     break;
    case SystemState::ANIMATION:      drawAnimationFrame();  break;
    case SystemState::WIFI_SYNC:      drawWifiSyncScreen();  break;
    case SystemState::WIPE_ACTIVE:    drawWipeActiveScreen();break;
    case SystemState::POMODORO:       drawPomodoroScreen();  break;
    case SystemState::BILLBOARD:      drawBillboardScreen(); break;
    case SystemState::GAME:           drawGameScreen();      break;
  }
  u8g2.sendBuffer();
}

void drawTimeSetMenu() {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(2, 10, "SET TIME");
  u8g2.drawHLine(0, 13, 128);

  char hBuf[4], mBuf[4];
  snprintf(hBuf, sizeof(hBuf), "%02d", tempHour);
  snprintf(mBuf, sizeof(mBuf), "%02d", tempMinute);

  u8g2.setFont(u8g2_font_logisoso32_tf);
  u8g2.drawStr(12, 48, hBuf);
  u8g2.drawStr(56, 46, ":");
  u8g2.drawStr(72, 48, mBuf);

  if (timeSetStep == 0) {
    u8g2.drawHLine(10, 52, 42);
  } else {
    u8g2.drawHLine(70, 52, 42);
  }

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 63, "Tap = +1 | Hold = Next/Save");
}

// ============================================================================
//  Feature 9 (this revision): WiFi sync screen now shows the dashboard IP.
// ============================================================================
void drawWifiSyncScreen() {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(14, 20, "WiFi Time Sync");
  u8g2.drawHLine(0, 24, 128);

  u8g2.setFont(u8g2_font_6x10_tr);
  if (wifiSyncPhase == WifiSyncPhase::CONNECTING) {
    uint8_t dots = (uint8_t)(((millis() - wifiSyncStartTime) / 400) % 4);
    char buf[24];
    snprintf(buf, sizeof(buf), "Connecting%.*s", dots, "...");
    u8g2.drawStr(10, 40, buf);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 60, "Fetching time + weather...");
  } else if (wifiSyncPhase == WifiSyncPhase::SUCCESS) {
    u8g2.drawStr(18, 40, "Time Synced!");

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 51, weatherValid ? "Weather updated too." : "Weather fetch failed.");

    // Feature 9/13: show the dashboard IP so the user can reach it without a
    // Serial Monitor. WiFi.localIP().toString() is cheap and one-shot -
    // only computed while this screen is briefly shown after a sync.
    //
    // Fix (Feature 13, this revision): the old "Dashboard: 192.168.100.100"
    // string on the 6x10 font ran well past 128px and got clipped off the
    // right edge. A short "IP: " prefix on the smaller 5x7 font keeps even
    // the longest possible IPv4 address ("IP: 255.255.255.255") comfortably
    // under 128px, and the width is measured with getStrWidth() before
    // drawing so it's always centered - falling back to a flush-left draw
    // in the (practically impossible) case it's still too wide to center.
    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      char ipBuf[24];
      snprintf(ipBuf, sizeof(ipBuf), "IP: %s", ip.c_str());

      u8g2.setFont(u8g2_font_5x7_tr);
      int tw = u8g2.getStrWidth(ipBuf);
      int x = (tw < 128) ? (128 - tw) / 2 : 0;
      u8g2.drawStr(x, 58, ipBuf);
    }
  } else {
    u8g2.drawStr(20, 40, "Sync Failed");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 55, "Check SSID/password");
    u8g2.drawStr(0, 63, "or signal range.");
  }
}

void drawBootScreen() {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(20, 20, "Kinetic Sand Art");

  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(28, 42, "Booting...");

  unsigned long elapsed = millis() - bootStartTime;
  int barWidth = map(constrain(elapsed, 0, BOOT_DURATION_MS), 0, BOOT_DURATION_MS, 0, 108);
  u8g2.drawFrame(10, 50, 108, 8);
  u8g2.drawBox(10, 50, barWidth, 8);
}

void drawMainScreen() {
  DateTime now = rtcAvailable ? rtc.now() : DateTime((uint32_t)0);

  int displayHour = now.hour();
  bool isPM = displayHour >= 12;
  if (use12HourFormat) {
    displayHour = displayHour % 12;
    if (displayHour == 0) displayHour = 12;
  }

  switch (currentFace) {
    case ClockFace::DIGITAL_HUGE: {
      char buf[6];
      if (use12HourFormat) {
        snprintf(buf, sizeof(buf), "%d:%02d", displayHour, now.minute());
      } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", displayHour, now.minute());
      }

      u8g2.setFont(u8g2_font_logisoso32_tf);
      u8g2.drawStr(4, 46, buf);

      char secBuf[4];
      snprintf(secBuf, sizeof(secBuf), "%02d", now.second());

      if (use12HourFormat) {
        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(102, 32, secBuf);
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(102, 48, isPM ? "PM" : "AM");
      } else {
        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(100, 46, secBuf);
      }
      break;
    }

    case ClockFace::ANALOG_CLOCK: {
      const int cx = 64, cy = 34, r = 28;
      u8g2.drawCircle(cx, cy, r);
      float secAngle  = (now.second() / 60.0f) * 2 * PI - HALF_PI;
      float minAngle  = ((now.minute() + now.second() / 60.0f) / 60.0f) * 2 * PI - HALF_PI;
      float hourAngle = (((now.hour() % 12) + now.minute() / 60.0f) / 12.0f) * 2 * PI - HALF_PI;
      u8g2.drawLine(cx, cy, cx + cos(hourAngle) * (r * 0.5f), cy + sin(hourAngle) * (r * 0.5f));
      u8g2.drawLine(cx, cy, cx + cos(minAngle)  * (r * 0.8f), cy + sin(minAngle)  * (r * 0.8f));
      u8g2.drawLine(cx, cy, cx + cos(secAngle)  * (r * 0.9f), cy + sin(secAngle)  * (r * 0.9f));
      u8g2.drawDisc(cx, cy, 2);
      break;
    }

    case ClockFace::DATE_TIME: {
      char dateBuf[16];
      snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", now.day(), now.month(), now.year());
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.drawStr((128 - u8g2.getStrWidth(dateBuf)) / 2, 25, dateBuf);

      char timeBuf[16];
      if (use12HourFormat) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d %s", displayHour, now.minute(), now.second(), isPM ? "PM" : "AM");
        u8g2.setFont(u8g2_font_ncenB10_tr);
      } else {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", displayHour, now.minute(), now.second());
        u8g2.setFont(u8g2_font_ncenB14_tr);
      }
      u8g2.drawStr((128 - u8g2.getStrWidth(timeBuf)) / 2, 55, timeBuf);
      break;
    }

    case ClockFace::BINARY_CLOCK: {
      int h = now.hour();
      int m = now.minute();
      int s = now.second();
      for (int i = 0; i < 6; i++) {
        if (h & (1 << (5 - i))) u8g2.drawDisc(20 + i * 18, 16, 6); else u8g2.drawCircle(20 + i * 18, 16, 6);
        if (m & (1 << (5 - i))) u8g2.drawDisc(20 + i * 18, 34, 6); else u8g2.drawCircle(20 + i * 18, 34, 6);
        if (s & (1 << (5 - i))) u8g2.drawDisc(20 + i * 18, 52, 6); else u8g2.drawCircle(20 + i * 18, 52, 6);
      }
      break;
    }

    case ClockFace::RPM_VIEW: {
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(30, 14, "Motor Status");
      char buf[24];
      snprintf(buf, sizeof(buf), "RPM: %.1f", motorRPM);
      u8g2.setFont(u8g2_font_ncenB14_tr);
      u8g2.drawStr(10, 36, buf);
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(10, 54, motorRunning ? "State: RUNNING" : "State: STOPPED");
      u8g2.drawStr(10, 64, motorDirection > 0 ? "Dir: CW" : "Dir: CCW");
      break;
    }

    case ClockFace::CARTOON_MODE: {
      drawCartoonMovie(now);
      break;
    }

    case ClockFace::CARTOON_MOVIE_2: {
      drawCartoonMovie2(now);
      break;
    }

    case ClockFace::WEATHER: {
      drawWeatherFace(now);
      break;
    }

    case ClockFace::MOON_PHASE: {
      drawMoonPhaseFace(now);
      break;
    }

    case ClockFace::ZEN_BREATHING: {
      drawZenBreathingFace(now);
      break;
    }

    case ClockFace::GENERATIVE_ART: {
      drawGenerativeArtFace(now);
      break;
    }

    default:
      break;
  }
}

void drawGenericMenu(const char* title, const char* const* items, uint8_t count, uint8_t selected, uint8_t maxVisible) {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(2, 10, title);
  u8g2.drawHLine(0, 13, 128);

  u8g2.setFont(u8g2_font_6x10_tr);

  int startIndex = 0;
  if (selected > maxVisible - 1) {
    startIndex = selected - (maxVisible - 1);
  }

  for (uint8_t i = startIndex; i < count; i++) {
    int displayIdx = i - startIndex;
    if (displayIdx >= maxVisible) break;

    int y = 26 + displayIdx * 12;
    if (i == selected) {
      u8g2.drawStr(0, y, ">");
    }
    u8g2.drawStr(10, y, items[i]);
  }
}

void drawSettingsMenu() {
  drawGenericMenu("SETTINGS", SETTINGS_ITEMS, SETTINGS_ITEM_COUNT, settingsMenuIndex, 4);
}

void drawMotorMenu() {
  drawGenericMenu("MOTOR SETTINGS", MOTOR_ITEMS, MOTOR_ITEM_COUNT, motorMenuIndex, 3);

  char wipeStr[4];
  if (wipeHourSetting == WIPE_HOUR_OFF) {
    strcpy(wipeStr, "Off");
  } else {
    snprintf(wipeStr, sizeof(wipeStr), "%02d", wipeHourSetting);
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f %s %s W:%s",
           motorRPM,
           motorDirection > 0 ? "CW" : "CCW",
           motorRunning ? "ON" : "OFF",
           wipeStr);

  u8g2.setFont(u8g2_font_5x7_tr);
  int textWidth = u8g2.getStrWidth(buf);
  int xPos = 128 - textWidth - 2;

  u8g2.setDrawColor(0);
  u8g2.drawBox(xPos - 4, 54, textWidth + 8, 10);
  u8g2.setDrawColor(1);
  u8g2.drawStr(xPos, 62, buf);
}

// ============================================================================
//  Feature 10 (this revision): "Always On" label when sleep option is 0.
// ============================================================================
void drawDisplayMenu() {
  drawGenericMenu("DISPLAY SETTINGS", DISPLAY_ITEMS, DISPLAY_ITEM_COUNT, displayMenuIndex, 3);

  char sleepBuf[10];
  if (SLEEP_OPTIONS[sleepOptionIndex] == 0) {
    strcpy(sleepBuf, "AlwaysOn");
  } else {
    snprintf(sleepBuf, sizeof(sleepBuf), "%dm", SLEEP_OPTIONS[sleepOptionIndex]);
  }

  char buf[40];
  snprintf(buf, sizeof(buf), "Br:%s Zz:%s Fmt:%s",
           BRIGHTNESS_NAMES[brightnessIndex],
           sleepBuf,
           use12HourFormat ? "12H" : "24H");

  u8g2.setFont(u8g2_font_5x7_tr);
  int textWidth = u8g2.getStrWidth(buf);
  int xPos = 128 - textWidth - 2;

  u8g2.setDrawColor(0);
  u8g2.drawBox(xPos - 4, 54, textWidth + 8, 10);
  u8g2.setDrawColor(1);
  u8g2.drawStr(xPos, 62, buf);
}

void drawAnimationFrame() {
  if (millis() - lastAnimFrameTime >= ANIM_FRAME_MS) {
    lastAnimFrameTime = millis();
    animFrame++;
  }

  if (currentAnimType == 0) {
    u8g2.setFont(u8g2_font_5x7_tr);
    for (int i = 0; i < 6; i++) {
      int x = i * 20 + 10;
      int y = (animFrame * (i % 3 + 2)) % 70 - 10;

      char c1 = (animFrame + i) % 2 == 0 ? '1' : '0';
      char c2 = (animFrame + i + 1) % 2 == 0 ? '0' : '1';

      u8g2.drawGlyph(x, y, c1);
      u8g2.drawGlyph(x, y - 8, c2);
    }
  }
  else if (currentAnimType == 1) {
    const int cx = 64, cy = 32, size = 15;
    float angle = animFrame * 0.1;
    float s = sin(angle), c = cos(angle);

    int pts[8][2];
    for (int i = 0; i < 8; i++) {
      float x = (i & 1 ? size : -size);
      float y = (i & 2 ? size : -size);
      float z = (i & 4 ? size : -size);
      float xy = x * c - y * s;
      float yy = x * s + y * c;
      float xz = xy * c - z * s;
      float zz = xy * s + z * c;
      pts[i][0] = cx + (int)(xz * 0.8);
      pts[i][1] = cy + (int)(yy * 0.8);
    }
    for (int i = 0; i < 4; i++) {
      u8g2.drawLine(pts[i][0], pts[i][1], pts[i + 4][0], pts[i + 4][1]);
      u8g2.drawLine(pts[i][0], pts[i][1], pts[(i + 1) % 4][0], pts[(i + 1) % 4][1]);
      u8g2.drawLine(pts[i + 4][0], pts[i + 4][1], pts[(i + 1) % 4 + 4][0], pts[(i + 1) % 4 + 4][1]);
    }
  }
  else if (currentAnimType == 2) {
    const int cx = 64, cy = 32;
    for (int i = 0; i < 30; i++) {
      float angle = i * 0.5 - (animFrame * 0.1);
      float radius = i * 1.5;
      int x = cx + (int)(cos(angle) * radius);
      int y = cy + (int)(sin(angle) * radius);
      u8g2.drawPixel(x, y);
      if (i > 0) u8g2.drawCircle(x, y, 2);
    }
  }
}

// ============================================================================
//  Feature 1: WEB DASHBOARD
// ----------------------------------------------------------------------------
//  All handlers are short, synchronous request/response functions; they only
//  ever run from inside server.handleClient(), which itself is only invoked
//  once per loop() pass and never blocks waiting for a client - it services
//  whatever request is already pending and returns immediately otherwise.
//  The one deliberate exception is the OTA upload handler (Feature 7 below),
//  which necessarily consumes the full firmware body across successive
//  server.handleClient() calls (WebServer's own read loop drives that, not
//  ours) - see the note above handleUpdateUpload().
// ============================================================================
const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sand Art Clock</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: -apple-system, Segoe UI, Roboto, sans-serif; background:#0e0f12; color:#f2f2f2;
         margin:0; padding:20px; max-width:480px; margin-left:auto; margin-right:auto; }
  h1 { font-size:1.3em; margin-bottom:4px; }
  .sub { color:#8a8f98; font-size:0.85em; margin-bottom:20px; }
  .card { background:#1a1c20; border-radius:14px; padding:16px 18px; margin-bottom:14px;
          box-shadow: 0 1px 3px rgba(0,0,0,0.4); }
  .card h2 { font-size:0.95em; margin:0 0 10px 0; color:#c9cdd4; text-transform:uppercase; letter-spacing:0.05em; }
  select, input[type=text], input[type=file] { width:100%; padding:10px; border-radius:8px; border:1px solid #333;
         background:#111318; color:#f2f2f2; font-size:1em; box-sizing:border-box; }
  input[type=range] { width:100%; }
  .row { display:flex; gap:10px; margin-top:10px; flex-wrap:wrap; }
  button { flex:1; padding:12px; border:none; border-radius:10px; background:#3b6cff; color:#fff;
           font-size:0.95em; font-weight:600; cursor:pointer; }
  button.secondary { background:#2a2d34; }
  button.danger { background:#c93b3b; }
  button:active { opacity:0.75; }
  .val { text-align:right; color:#8a8f98; font-size:0.85em; margin-top:4px; }
  .status { font-size:0.8em; color:#8a8f98; margin-top:14px; text-align:center; }
  .warn { font-size:0.75em; color:#e0a030; margin-top:8px; }
</style>
</head>
<body>
  <h1>Kinetic Sand Art Clock</h1>
  <div class="sub">Local dashboard - controls apply instantly</div>

  <div class="card">
    <h2>Clock Face</h2>
    <select id="face" onchange="setFace()">
      <option value="0">Digital</option>
      <option value="1">Analog</option>
      <option value="2">Date + Time</option>
      <option value="3">Binary</option>
      <option value="4">Motor RPM</option>
      <option value="5">Cartoon: Abyssal Voyage</option>
      <option value="6">Cartoon: Star Voyage</option>
      <option value="7">Weather</option>
      <option value="8">Moon Phase</option>
      <option value="9">Zen Breathing</option>
      <option value="10">Generative Sand Art</option>
    </select>
  </div>

  <div class="card">
    <h2>Motor</h2>
    <input type="range" id="rpm" min="1" max="10" step="0.5" oninput="rpmLive()" onchange="setRPM()">
    <div class="val" id="rpmVal">-- RPM</div>
    <div class="row">
      <button class="secondary" onclick="toggleDir()">Toggle Direction</button>
    </div>
  </div>

  <div class="card">
    <h2>Display</h2>
    <select id="bright" onchange="setBright()">
      <option value="0">Low</option>
      <option value="1">Medium</option>
      <option value="2">High</option>
    </select>
  </div>

  <div class="card">
    <h2>Billboard Message</h2>
    <input type="text" id="msg" maxlength="90" placeholder="Type a message to scroll on the clock...">
    <div class="row">
      <button onclick="sendMsg()">Send to Clock</button>
    </div>
  </div>

  <div class="card">
    <div class="row">
      <button onclick="syncNow()">Sync WiFi Time + Weather</button>
    </div>
  </div>

  <div class="card">
    <h2>Firmware Update (OTA)</h2>
    <form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" accept=".bin">
      <div class="row">
        <button type="submit" class="danger">Upload &amp; Flash</button>
      </div>
    </form>
    <div class="warn">Do not close this page or power off the clock during the update.</div>
  </div>

  <div class="status" id="status">Loading state...</div>

<script>
function refresh() {
  fetch('/state').then(r => r.json()).then(s => {
    document.getElementById('face').value = s.face;
    document.getElementById('rpm').value = s.rpm;
    document.getElementById('rpmVal').innerText = s.rpm.toFixed(1) + ' RPM';
    document.getElementById('bright').value = s.brightness;
    var sleepTxt = (s.sleep === 0) ? 'Always On' : (s.sleep + 'm');
    document.getElementById('status').innerText =
      'Dir: ' + (s.dir > 0 ? 'CW' : 'CCW') +
      ' | Motor: ' + (s.running ? 'ON' : 'OFF') +
      ' | Sleep: ' + sleepTxt +
      (s.weatherValid ? (' | ' + s.temp.toFixed(0) + 'C ' + s.condition) : '');
  }).catch(()=>{});
}
function rpmLive() {
  document.getElementById('rpmVal').innerText =
    parseFloat(document.getElementById('rpm').value).toFixed(1) + ' RPM';
}
function setFace() {
  fetch('/setFace?value=' + document.getElementById('face').value);
}
function setRPM() {
  fetch('/setRPM?value=' + document.getElementById('rpm').value);
}
function toggleDir() {
  fetch('/toggleDir').then(refresh);
}
function setBright() {
  fetch('/setBrightness?value=' + document.getElementById('bright').value);
}
function sendMsg() {
  var m = document.getElementById('msg').value;
  if (!m) return;
  fetch('/billboard?msg=' + encodeURIComponent(m)).then(()=>{
    document.getElementById('msg').value = '';
  });
}
function syncNow() {
  fetch('/syncNow');
}
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)HTML";

void handleWebRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleWebState() {
  StaticJsonDocument<256> doc;
  doc["face"] = static_cast<uint8_t>(currentFace);
  doc["rpm"] = motorRPM;
  doc["dir"] = motorDirection;
  doc["running"] = motorRunning;
  doc["brightness"] = brightnessIndex;
  doc["sleep"] = SLEEP_OPTIONS[sleepOptionIndex]; // Feature 10: 0 = Always On
  doc["weatherValid"] = weatherValid;
  doc["temp"] = weatherValid ? weatherTempC : 0.0f;
  doc["condition"] = weatherCondition;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWebSetFace() {
  if (server.hasArg("value")) {
    int v = server.arg("value").toInt();
    if (v >= 0 && v < static_cast<int>(ClockFace::FACE_COUNT)) {
      currentFace = static_cast<ClockFace>(v);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleWebSetRPM() {
  if (server.hasArg("value")) {
    float v = server.arg("value").toFloat();
    motorRPM = constrain(v, RPM_MIN, RPM_MAX);
    applyMotorSpeed();
  }
  server.send(200, "text/plain", "OK");
}

void handleWebToggleDir() {
  toggleDirection();
  server.send(200, "text/plain", "OK");
}

void handleWebSetBrightness() {
  if (server.hasArg("value")) {
    int v = server.arg("value").toInt();
    if (v >= 0 && v < 3) {
      brightnessIndex = (uint8_t)v;
      applyEffectiveContrast();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleWebSyncNow() {
  startWifiSync();
  server.send(200, "text/plain", "OK");
}

void handleWebBillboard() {
  if (server.hasArg("msg")) {
    triggerBillboard(server.arg("msg"));
  }
  server.send(200, "text/plain", "OK");
}

void handleWebNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ============================================================================
//  Feature 7 (this revision): OTA FIRMWARE UPDATE
// ----------------------------------------------------------------------------
//  Standard ESP32 WebServer OTA pattern: a POST handler (handleUpdateResult,
//  called once after the whole body has arrived) plus an upload callback
//  (handleUpdateUpload, called repeatedly by WebServer as chunks of the
//  multipart body arrive). WebServer.h reads the request body itself inside
//  server.handleClient(), so the stepper legitimately pauses for the
//  duration of the upload - there is no way to interleave stepper.runSpeed()
//  calls mid-chunk-read with this library. What we CAN and DO guarantee:
//    - yield() on every chunk so the watchdog (and WiFi stack) stay serviced
//      and the ESP32 never resets mid-flash.
//    - updateMotor() called once per chunk boundary as a courtesy, so on a
//      fast link with small chunks the wiper still gets occasional ticks.
//    - the whole thing is short-lived: typical sketches are well under a
//      megabyte, so the pause is on the order of a few seconds, once, only
//      when the user explicitly initiates a firmware upload.
// ============================================================================
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA: starting update, file=%s\n", upload.filename.c_str());
    // UPDATE_SIZE_UNKNOWN lets Update.h use the max available OTA partition
    // size, since the browser doesn't tell us the final size up front.
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
    updateMotor(); // courtesy nudge between chunks
    yield();        // feed the watchdog + WiFi/TCP stack
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { // true = set the new sketch as bootable
      Serial.printf("OTA: success, %u bytes written\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
    updateMotor();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end(false);
    Serial.println(F("OTA: upload aborted"));
  }
}

void handleUpdateResult() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "Update OK - rebooting..." : "Update FAILED - not rebooting");

  if (ok) {
    // NOTE: this is the single, deliberate delay() in the whole sketch. It
    // only runs once, after a successful firmware flash, to give the
    // WebServer time to flush the HTTP response over TCP before the MCU
    // tears everything down and reboots. Since ESP.restart() immediately
    // follows, the stepper's "always smooth" guarantee (which is about
    // steady-state operation) is not affected - the whole system, stepper
    // included, is about to reinitialize from scratch anyway.
    delay(1000);
    ESP.restart();
  }
}

void startWebServer() {
  server.on("/", HTTP_GET, handleWebRoot);
  server.on("/state", HTTP_GET, handleWebState);
  server.on("/setFace", HTTP_GET, handleWebSetFace);
  server.on("/setRPM", HTTP_GET, handleWebSetRPM);
  server.on("/toggleDir", HTTP_GET, handleWebToggleDir);
  server.on("/setBrightness", HTTP_GET, handleWebSetBrightness);
  server.on("/syncNow", HTTP_GET, handleWebSyncNow);
  server.on("/billboard", HTTP_GET, handleWebBillboard);
  // Feature 7: OTA endpoint. handleUpdateResult runs once the full body has
  // been received; handleUpdateUpload runs repeatedly as chunks arrive.
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.onNotFound(handleWebNotFound);
  server.begin();
  webServerStarted = true;
  Serial.print(F("Dashboard ready at http://"));
  Serial.println(WiFi.localIP());
}
