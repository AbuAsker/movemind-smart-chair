#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- Pins ----------------
const int seatSensor = A0;
const int backSensor = A1;
const int buzzer = 2; // ACTIVE LOW
const int ledG = 12;
const int ledY = 11;
const int ledR = 10;

// ---------------- Settings ----------------
const unsigned long SIT_LIMIT          = 60000;   // 60 sec sitting limit
const unsigned long BREAK_TIME         = 12000;   // 12 sec standing break
const unsigned long BACK_GRACE         = 5000;    // 5 sec back off threshold
const unsigned long SHORT_STAND_GRACE  = 4000;    // 4 sec allowed short stand
const unsigned long BEEP_INTERVAL      = 3000;    // beep once every 3 sec
const unsigned long BREAK_INTERRUPTION_GRACE = 1000; // 1 sec allowed noise while standing

// ---------------- States ----------------
enum State {
  IDLE,
  SITTING,
  LATE_ALERT,
  BACK_ALERT,
  BREAK_STATE,
  SHORT_STAND
};

State state = IDLE;

// ---------------- Timers / Accumulators ----------------
unsigned long sitAccumulated   = 0;
unsigned long sitSegmentStart  = 0;
unsigned long standStart       = 0;
unsigned long lastBeep         = 0;
unsigned long backLiftStart    = 0;
unsigned long shortStandStart  = 0;
unsigned long breakSitStart    = 0;   // NEW: used in BREAK_STATE to filter noise

// ===============================================================
//                   STABILITY FILTER FUNCTIONS
// ===============================================================
bool stableBelowThreshold(int pin, int threshold) {
  for (int i = 0; i < 20; i++) {
    if (analogRead(pin) >= threshold) return false;
    delay(2);
  }
  return true;
}

bool stableAboveThreshold(int pin, int threshold) {
  for (int i = 0; i < 20; i++) {
    if (analogRead(pin) <= threshold) return false;
    delay(2);
  }
  return true;
}

bool seatPressedStable() {
  return stableBelowThreshold(seatSensor, 50);
}

bool seatReleasedStable() {
  return stableAboveThreshold(seatSensor, 120);
}

bool backPressedStable() {
  return stableBelowThreshold(backSensor, 50);
}

// ===============================================================
//                     UTILITY FUNCTIONS
// ===============================================================
void ledsOff() {
  digitalWrite(ledG, LOW);
  digitalWrite(ledY, LOW);
  digitalWrite(ledR, LOW);
}

void buzzerOff() {
  digitalWrite(buzzer, HIGH);
}

void shortBeep() {
  digitalWrite(buzzer, LOW);
  delay(120);
  digitalWrite(buzzer, HIGH);
}

// Sitting LEDs
void updateSittingLeds(unsigned long elapsed) {
  ledsOff();
  if (elapsed < SIT_LIMIT / 2) {
    digitalWrite(ledG, HIGH);
  } else if (elapsed < (SIT_LIMIT * 3UL) / 4UL) {
    digitalWrite(ledY, HIGH);
  } else {
    digitalWrite(ledR, HIGH);
  }
}

// Break LEDs
void updateBreakLeds(unsigned long standElapsed) {
  ledsOff();
  if (standElapsed < BREAK_TIME / 3) {
    digitalWrite(ledR, HIGH);
  } else if (standElapsed < (BREAK_TIME * 2UL) / 3UL) {
    digitalWrite(ledY, HIGH);
  } else {
    digitalWrite(ledG, HIGH);
  }
}

// ===============================================================
//                           SETUP
// ===============================================================
void setup() {

  Serial.begin(9600);

  pinMode(buzzer, OUTPUT);
  pinMode(ledG, OUTPUT);
  pinMode(ledY, OUTPUT);
  pinMode(ledR, OUTPUT);

  buzzerOff();
  ledsOff();

  lcd.init();
  lcd.backlight();
  lcd.print("Posture System");
  delay(1500);
  lcd.clear();
}

// ===============================================================
//                           LOOP
// ===============================================================
void loop() {

  Serial.print("Seat=");
  Serial.print(analogRead(seatSensor));
  Serial.print(" | Back=");
  Serial.println(analogRead(backSensor));

  switch (state) {

    // -----------------------------------------------------------
    //                        IDLE STATE
    // -----------------------------------------------------------
    case IDLE:
      ledsOff();
      buzzerOff();

      lcd.setCursor(0, 0);
      lcd.print("Waiting...      ");
      lcd.setCursor(0, 1);
      lcd.print("                ");

      if (seatPressedStable()) {
        state = SITTING;
        sitAccumulated  = 0;
        sitSegmentStart = millis();
        backLiftStart   = 0;
        lcd.clear();
        lcd.print("Sitting Started");
        delay(200);
      }
      break;

    // -----------------------------------------------------------
    //                        SITTING STATE
    // -----------------------------------------------------------
    case SITTING: {
      unsigned long elapsed = sitAccumulated + (millis() - sitSegmentStart);

      lcd.setCursor(0, 0);
      lcd.print("Sit: ");
      lcd.print(elapsed / 1000);
      lcd.print("s      ");

      buzzerOff();
      updateSittingLeds(elapsed);

      // ---- Short Stand Before Time Exceeded ----
      if (seatReleasedStable()) {
        sitAccumulated += (millis() - sitSegmentStart);
        if (elapsed <= SIT_LIMIT) {
          state = SHORT_STAND;
          shortStandStart = millis();
        } else {
          state = BREAK_STATE;
          standStart = 0;
        }
        lcd.clear();
        break;
      }

      // ---- Time Exceeded ----
      if (elapsed > SIT_LIMIT) {
        state = LATE_ALERT;
        lcd.clear();
        break;
      }

      // ---- Back Posture Detection ----
      if (!backPressedStable()) {
        if (backLiftStart == 0) backLiftStart = millis();
        if (millis() - backLiftStart > BACK_GRACE) {
          state = BACK_ALERT;
          lcd.clear();
        }
      } else {
        backLiftStart = 0;
      }

    } break;

    // -----------------------------------------------------------
    //                      SHORT STAND STATE
    // -----------------------------------------------------------
    case SHORT_STAND: {

      ledsOff();
      digitalWrite(ledY, HIGH);
      buzzerOff();

      unsigned long standElapsed = millis() - shortStandStart;

      lcd.setCursor(0, 0);
      lcd.print("Short Stand     ");
      lcd.setCursor(0, 1);
      lcd.print("<=4s to resume  ");

      if (seatPressedStable()) {
        if (standElapsed <= SHORT_STAND_GRACE) {
          state = SITTING;
          sitSegmentStart = millis();
        } else {
          state = IDLE;
        }
        lcd.clear();
        break;
      }

      if (standElapsed > SHORT_STAND_GRACE) {
        state = IDLE;
        lcd.clear();
      }

    } break;

    // -----------------------------------------------------------
    //                      LATE ALERT STATE
    // -----------------------------------------------------------
    case LATE_ALERT: {

      unsigned long elapsed = sitAccumulated + (millis() - sitSegmentStart);

      lcd.setCursor(0, 0);
      lcd.print("Time Exceeded!  ");
      lcd.setCursor(0, 1);
      lcd.print("Sit: ");
      lcd.print(elapsed / 1000);
      lcd.print("s      ");

      ledsOff();
      digitalWrite(ledR, HIGH);

      if (millis() - lastBeep > BEEP_INTERVAL) {
        shortBeep();
        lastBeep = millis();
      }

      if (seatReleasedStable()) {
        state = BREAK_STATE;
        standStart = 0;
        breakSitStart = 0;
        lcd.clear();
        break;
      }

      if (!backPressedStable()) {
        if (backLiftStart == 0) backLiftStart = millis();
        if (millis() - backLiftStart > BACK_GRACE) {
          state = BACK_ALERT;
          lcd.clear();
        }
      } else {
        backLiftStart = 0;
      }

    } break;

    // -----------------------------------------------------------
    //                   BACK POSTURE ALERT STATE
    // -----------------------------------------------------------
    case BACK_ALERT:

      lcd.setCursor(0, 0);
      lcd.print("Put Back!       ");
      lcd.setCursor(0, 1);
      lcd.print("                ");

      ledsOff();
      digitalWrite(ledY, HIGH);

      if (millis() - lastBeep > BEEP_INTERVAL) {
        shortBeep();
        lastBeep = millis();
      }

      if (backPressedStable()) {
        state = SITTING;
        backLiftStart = 0;
        lcd.clear();
      }

      if (seatReleasedStable()) {
        state = BREAK_STATE;
        standStart = 0;
        breakSitStart = 0;
        lcd.clear();
      }

      break;

    // -----------------------------------------------------------
    //                       BREAK STATE
    //  Noise-immune standing timer
    // -----------------------------------------------------------
    case BREAK_STATE: {

      bool standingStable = seatReleasedStable();

      if (standingStable) {
        // Clean standing: break is valid
        breakSitStart = 0;  // clear interruption timer

        if (standStart == 0) {
          standStart = millis();
        }

        unsigned long standElapsed = millis() - standStart;
        long remaining = (long)(BREAK_TIME - standElapsed);
        if (remaining < 0) remaining = 0;
        unsigned long remainingSec = remaining / 1000;

        lcd.setCursor(0, 0);
        lcd.print("Keep Standing   ");
        lcd.setCursor(0, 1);
        lcd.print("Remain: ");
        lcd.print(remainingSec);
        lcd.print("s     ");

        buzzerOff();
        updateBreakLeds(standElapsed);

        if (standElapsed >= BREAK_TIME) {
          state = IDLE;
          standStart = 0;
          breakSitStart = 0;
          lcd.clear();
        }

      } else {
        // Possible noise or actual sitting

        if (standStart != 0) {
          // Break already in progress → filter interruption
          if (breakSitStart == 0) {
            breakSitStart = millis();
          }

          unsigned long interruption = millis() - breakSitStart;

          if (interruption < BREAK_INTERRUPTION_GRACE) {
            // === Treat as NOISE: keep counting break ===
            unsigned long standElapsed = millis() - standStart;
            long remaining = (long)(BREAK_TIME - standElapsed);
            if (remaining < 0) remaining = 0;
            unsigned long remainingSec = remaining / 1000;

            lcd.setCursor(0, 0);
            lcd.print("Keep Standing   ");
            lcd.setCursor(0, 1);
            lcd.print("Remain: ");
            lcd.print(remainingSec);
            lcd.print("s     ");

            buzzerOff();
            updateBreakLeds(standElapsed);

            if (standElapsed >= BREAK_TIME) {
              state = IDLE;
              standStart = 0;
              breakSitStart = 0;
              lcd.clear();
            }

          } else {
            // === Real sit during break: reset timer ===
            standStart = 0;
            breakSitStart = 0;

            lcd.setCursor(0, 0);
            lcd.print("Stand Required  ");
            lcd.setCursor(0, 1);
            lcd.print("Please Stand    ");

            ledsOff();
            digitalWrite(ledR, HIGH);

            if (millis() - lastBeep > BEEP_INTERVAL) {
              shortBeep();
              lastBeep = millis();
            }
          }

        } else {
          // Break hasn't started yet (user never stood correctly)
          breakSitStart = 0;
          standStart = 0;

          lcd.setCursor(0, 0);
          lcd.print("Stand Required  ");
          lcd.setCursor(0, 1);
          lcd.print("Please Stand    ");

          ledsOff();
          digitalWrite(ledR, HIGH);

          if (millis() - lastBeep > BEEP_INTERVAL) {
            shortBeep();
            lastBeep = millis();
          }
        }
      }

    } break;
  }
}
