#include "DHT.h"
#include <Keypad.h>
#include <LiquidCrystal.h>

// --- MODE SWITCH ---
#define TEST_MODE          // Uncomment for test mode (seconds instead of hours)
//#define REAL_MODE        // Comment this line for test mode

// Pins
const int soilSensorPin = A0;
const int lightSensorPin = A1;
const int DHTPin = 2;
const int redPin = 9;
const int greenPin = 10;
const int bluePin = 11;
const int resetButtonPin = 7;   // RESET BUTTON PIN

#define DHTTYPE DHT11
DHT dht(DHTPin, DHTTYPE);

// Keypad setup
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {22,23,24,25};
byte colPins[COLS] = {26,27,28,29};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// LCD setup
LiquidCrystal lcd(30, 31, 32, 33, 34, 35);

String inputString = "";

// Sensor ranges
int soilLower = 0;
float tempLower = 0;
float tempUpper = 0;
int lightThreshold = 0;

// Nighttime floor for light sensor
const int nightLevel = 50;

// Timing
unsigned long previousMillis = 0;

// --- MODIFIED: interval is now user-defined ---
unsigned long userInterval = 15000;  // user sets this
unsigned long interval = 15000;      // used by the loop

bool firstLoop = true;

// Sunlight tracking
unsigned long sunlightSeconds = 0;
unsigned long darkDuration = 0;
bool sunlightSatisfied = false;

// Adjustable night duration
#ifdef TEST_MODE
const unsigned long nightResetSeconds = 60;
#else
const unsigned long nightResetSeconds = 7200;
#endif

// Sunlight goal
unsigned long sunlightGoalSeconds = 0;

// --- Variables for reset hold detection ---
unsigned long resetPressStart = 0;
const unsigned long resetHoldTime = 5000; // 5 seconds hold

// --- LED blinking globals ---
const int MAX_ISSUES = 4;                 // Max simultaneous issues
float blinkColors[MAX_ISSUES][3];         // RGB values for each active issue
int blinkCount = 0;                        // Current color index
const unsigned long ledOnTime = 800;      // LED on duration
const unsigned long ledOffTime = 400;     // LED off duration
bool ledState = false;                     // LED currently ON/OFF
unsigned long ledPreviousMillis = 0;
int activeIssues = 0;                      // Number of active issues

// Flags for active issues
bool highTemp = false;
bool lowTemp = false;
bool drySoil = false;
bool lowLightFlag = false;

// -----------------------
// Check Reset Button
// -----------------------
void checkResetButton() {
  if (digitalRead(resetButtonPin) == LOW) { // Button pressed
    if (resetPressStart == 0) {
      resetPressStart = millis(); // start timing
    } else if (millis() - resetPressStart >= resetHoldTime) {
      Serial.println("\n--- RESET BUTTON HELD FOR 5 SECONDS ---");
      setLED(0,0,0); // turn off LED
      delay(200);
      setup(); // restart system
    }
  } else {
    resetPressStart = 0; // button released, reset timer
  }
}

// -----------------------
// Keypad Input Function
// -----------------------
int getKeypadValue(String prompt, int maxDigits = 3, String lcdLabel = "") {
  inputString = "";
  Serial.println();
  Serial.print("🌿 Smart Plant Monitor with RGB LED Alerts (°F) 🌿\n");
  Serial.print(prompt + ": ");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(lcdLabel);
  lcd.setCursor(0,1);
  lcd.print(inputString);

  char lastKey = NO_KEY;

  while (true) {
    checkResetButton(); // check reset during input

    char key = keypad.getKey();
    if (key && key != lastKey) {
      lastKey = key;

      if (key >= '0' && key <= '9') {
        if (inputString.length() < maxDigits) {
          inputString += key;
          Serial.print(key);
          lcd.setCursor(0,1);
          lcd.print("                ");
          lcd.setCursor(0,1);
          lcd.print(inputString);
        }
      }
      else if (key == 'B') { // Backspace
        if (inputString.length() > 0) {
          inputString.remove(inputString.length() - 1);
          Serial.println();
          Serial.print(prompt + ": " + inputString);
          lcd.setCursor(0,1);
          lcd.print("                ");
          lcd.setCursor(0,1);
          lcd.print(inputString);
        }
      }
      else if (key == 'C') { // Clear → restart all inputs
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Smart Plant Monitor");
        return -1;
      }
      else if (key == 'A') { // Enter
        if (inputString.length() > 0) {
          int value = inputString.toInt();
          Serial.println();
          Serial.print("✅ You entered: "); Serial.println(value);
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print(lcdLabel + ": " + String(value));
          delay(1000);
          lcd.clear();
          while (keypad.getKey() == 'A') { delay(10); }
          return value;
        }
      }
    }
    else if (!key) {
      lastKey = NO_KEY;
    }
  }
}

// -----------------------
// Setup
// -----------------------
void setup() {
  resetPressStart = 0; // clear previous press

  Serial.begin(9600);
  pinMode(soilSensorPin, INPUT);
  pinMode(lightSensorPin, INPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(resetButtonPin, INPUT_PULLUP);
  dht.begin();
  lcd.begin(16,2);

  // Reset all measurements and LED
  sunlightSeconds = 0;
  darkDuration = 0;
  sunlightSatisfied = false;
  setLED(0,0,0);
  previousMillis = millis();

  while (true) {
    checkResetButton(); // allow reset during setup

    int val = -1;

    // --- Soil ---
    do {
      val = getKeypadValue("Enter minimum soil moisture level (0-100)",3,"Min Soil %");
      if (val == -1) continue;
      if (val < 0 || val > 100) Serial.println("❌ Soil Level must be 0-100%");
    } while (val < 0 || val > 100);
    soilLower = val;

    // --- Lower Temp ---
    do {
      val = getKeypadValue("Enter LOWER optimal temperature (°F, 0-150)",3,"Lower Temp");
      if (val == -1) continue;
      if (val < 0 || val > 150) Serial.println("❌ Temp must be 0-150°F");
    } while (val < 0 || val > 150);
    tempLower = val;

    // --- Upper Temp ---
    do {
      val = getKeypadValue("Enter UPPER optimal temperature (°F, >= lower, <=150)",3,"Upper Temp");
      if (val == -1) continue;
      if (val < tempLower || val > 150) Serial.println("❌ Upper temp must be ≥ lower and ≤150");
    } while (val < tempLower || val > 150);
    tempUpper = val;

    // --- Light ---
    do {
      val = getKeypadValue("Enter minimum optimal light level (0-1023)",4,"Min Light 0-1023");
      if (val == -1) continue;
      if (val < 0 || val > 1023) Serial.println("❌ Light must be 0-1023");
    } while (val < 0 || val > 1023);
    lightThreshold = val;

    // --- Sunlight Goal ---
#ifdef TEST_MODE
    do {
      val = getKeypadValue("Enter sunlight goal (seconds, 1-100)",3,"Light Goal (s)");
      if (val == -1) continue;
      if (val < 1 || val > 100) Serial.println("❌ Goal must be 1-100 seconds");
    } while (val < 1 || val > 100);
    sunlightGoalSeconds = val;
#else
    do {
      val = getKeypadValue("Enter sunlight goal (hours, 1-12)",3,"Light Goal (hrs)");
      if (val == -1) continue;
      if (val < 1 || val > 12) Serial.println("❌ Goal must be 1-12 hours");
    } while (val < 1 || val > 12);
    sunlightGoalSeconds = val * 3600UL;
#endif

    // --- Monitoring Interval ---
#ifdef TEST_MODE
    do {
      val = getKeypadValue("Enter monitor interval (seconds, 1–300)", 3, "Interval (s)");
      if (val == -1) continue;
      if (val < 1 || val > 300) Serial.println("❌ Interval must be 1–300 seconds");
    } while (val < 1 || val > 300);
    userInterval = val * 1000UL;
#else
    do {
      val = getKeypadValue("Enter monitor interval (minutes, 1–60)", 2, "Interval(min)");
      if (val == -1) continue;
      if (val < 1 || val > 60) Serial.println("❌ Interval must be 1–60 minutes");
    } while (val < 1 || val > 60);
    userInterval = val * 60000UL;
#endif

    interval = userInterval; // apply user interval

#ifdef TEST_MODE
    Serial.print("📏 Monitoring every ");
    Serial.print(interval / 1000);
    Serial.println(" seconds.");
#else
    Serial.print("📏 Monitoring every ");
    Serial.print(interval / 60000);
    Serial.println(" minutes.");
#endif

    break;
  }

  firstLoop = true;
  previousMillis = millis() - interval;

#ifdef TEST_MODE
  Serial.print("---- START MONITORING EVERY ");
  Serial.print(interval / 1000);
  Serial.println(" SECONDS ----");
#else
  Serial.print("---- START MONITORING EVERY ");
  Serial.print(interval / 60000);
  Serial.println(" MINUTES ----");
#endif
}

// -----------------------
// Helper function to set RGB LED
// -----------------------
void setLED(float r, float g, float b) {
  analogWrite(redPin, (int)(r * 255));
  analogWrite(greenPin, (int)(g * 255));
  analogWrite(bluePin, (int)(b * 255));
}

// -----------------------
// Main Loop
// -----------------------
void loop() {
  checkResetButton();

  unsigned long currentMillis = millis();

  // --- Measurement Interval Check ---
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    int soilValue = analogRead(soilSensorPin);
    int soilPercent = map(soilValue, 1023, 0, 0, 100);

    float temperatureC = dht.readTemperature();
    float temperatureF = (temperatureC * 9.0 / 5.0) + 32.0;

    int lightValue = analogRead(lightSensorPin);

    // --- Sunlight / Dark Duration Logic ---
    if (!firstLoop) {
      if (lightValue > lightThreshold) {
        sunlightSeconds += interval / 1000;
        darkDuration = 0;
      } else if (lightValue < nightLevel) {
        darkDuration += interval / 1000;
      } else {
        darkDuration = 0;
      }

      if (darkDuration >= nightResetSeconds) {
        sunlightSeconds = 0;
        sunlightSatisfied = false;
        darkDuration = 0;
        Serial.println("🌙 Night detected. Resetting daily sunlight counter.");
      }

      if (sunlightSeconds >= sunlightGoalSeconds) {
        sunlightSatisfied = true;
        Serial.println("✅ Sunlight goal reached!");
      }
    }

    // --- Print Sensor Readings ---
    Serial.print("🌱 Soil Moisture: "); Serial.print(soilPercent); Serial.print("% (Optimal ≥ "); Serial.print(soilLower); Serial.print("%)");
    if (soilPercent < soilLower) Serial.print("  --> TOO DRY!");
    Serial.println();

    Serial.print("🌡️ Temperature: "); Serial.print(temperatureF,2); Serial.print(" °F (Optimal "); Serial.print(tempLower,2); Serial.print(" - "); Serial.print(tempUpper,2); Serial.print(" °F)");
    if (temperatureF < tempLower || temperatureF > tempUpper) Serial.print("  --> OUT OF OPTIMAL TEMP RANGE!");
    Serial.println();

    Serial.print("💡 Light Level: "); Serial.print(lightValue); Serial.print(" (Optimal ≥ "); Serial.print(lightThreshold); Serial.print(")");
    if (lightValue < nightLevel) Serial.print(" 🌙 Night detected");
    else if (lightValue < lightThreshold && lightValue > nightLevel && !sunlightSatisfied) Serial.print("  --> TOO DARK!");
    Serial.println();

#ifdef TEST_MODE
    Serial.print("☀️ Sunlight accumulated today: "); Serial.print(sunlightSeconds); Serial.print(" seconds / goal ("); Serial.print(sunlightGoalSeconds); Serial.println(" seconds)");
#else
    Serial.print("☀️ Sunlight accumulated today: "); Serial.print(sunlightSeconds/3600.0,2); Serial.print(" hrs / goal ("); Serial.print(sunlightGoalSeconds/3600UL); Serial.println(" hrs)");
#endif

    Serial.println("--------------------------------------");

    // --- Determine active issues ---
    highTemp = (temperatureF > tempUpper);
    lowTemp  = (temperatureF < tempLower);
    drySoil  = (soilPercent < soilLower);
    lowLightFlag = (lightValue < lightThreshold && lightValue > nightLevel && !sunlightSatisfied);

    // --- Handle LED based on active issues ---
    activeIssues = 0;
    int idx = 0;

    if (highTemp)  { blinkColors[idx][0]=1; blinkColors[idx][1]=0; blinkColors[idx][2]=0; idx++; activeIssues++; } // red
    if (lowTemp)   { blinkColors[idx][0]=0; blinkColors[idx][1]=0; blinkColors[idx][2]=1; idx++; activeIssues++; } // blue
    if (drySoil)   { blinkColors[idx][0]=1; blinkColors[idx][1]=1; blinkColors[idx][2]=0; idx++; activeIssues++; } // yellow
    if (lowLightFlag) { blinkColors[idx][0]=1; blinkColors[idx][1]=0; blinkColors[idx][2]=1; idx++; activeIssues++; } // purple

    blinkCount = 0;
    ledState = false;
    ledPreviousMillis = millis();

    // --- Single issue static LED ---
    if (activeIssues == 1) {
      setLED(blinkColors[0][0], blinkColors[0][1], blinkColors[0][2]);
    }
    // --- No issues ---
    if (activeIssues == 0) setLED(0,0,0);

    firstLoop = false;
  }

  // --- Non-blocking LED blink for multiple issues ---
  if (activeIssues > 1) {
    unsigned long currentMillis = millis();
    if (ledState) {
      if (currentMillis - ledPreviousMillis >= ledOnTime) {
        setLED(0,0,0); // turn off
        ledState = false;
        ledPreviousMillis = currentMillis;
        blinkCount++;
        if (blinkCount >= activeIssues) blinkCount = 0;
      }
    } else {
      if (currentMillis - ledPreviousMillis >= ledOffTime) {
        setLED(blinkColors[blinkCount][0], blinkColors[blinkCount][1], blinkColors[blinkCount][2]);
        ledState = true;
        ledPreviousMillis = currentMillis;
      }
    }
  }
}
