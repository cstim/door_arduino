#include <RBD_Timer.h> // https://github.com/alextaujenis/RBD_Timer
#include <RBD_Light.h> // https://github.com/cstim/RBD_Light

// The Bounce2 library https://github.com/thomasfredericks/Bounce2
#include <Bounce2.h>
#include <BounceAnalog.h> // a variation of Bounce2 with analog input

// for disabling the serial monitor below
#include <wiring_private.h>

/*
  Garage-Door State machine 2

  Christian Stimming, 2016, Hamburg, Germany
 */

// Names for the output pins:
const int pinOutMotorOn = 7;
const int pinOutMotorUp = 6;
const int pinOutWarnLight = 5;
const int pinOutRoomLight = 4;
const int pinOutArduinoLedPin = 13;

const int pinInButton = 3;
const int pinInButtonDown = 2;
const int pinInLightswitch = 0;
const int pinAnalogInOutsideButton = 0;

// Active this for the numerical values during debugging
//#define DEBUG
// Active this for the serial output to be compiled in.
//#define DEBUGOUTPUT
// Activate this for blinking for loop performance measurement
//#define COUNTBLINKER

// /////////////////////////////

/// A management class for a recorded timestamp that should not be older than
/// some constraint
class MyTimestamp {
    unsigned long m_value;
  public:
    MyTimestamp()
        : m_value(0)
    {}
    void setValue(unsigned long value)
    {
        m_value = value;
    }
    unsigned long getValue() const { return m_value; }
};


// The relays are active-low, so we better define readable names for this
#define RELAY_ON LOW
#define RELAY_OFF HIGH

/** The states that the door can have*/
enum State {
  DOOR_DOWN
  , DOOR_UP
  , DOOR_MOVING_UP
  , DOOR_MOVING_DOWN
  , DOOR_MOVING_DOWN_PAUSED
};
/** This is the door's actual state */
State state = DOOR_DOWN;
const unsigned long c_moveDurationTotal =
#ifdef DEBUG
  5000L;
#else
  17000L; // total movement takes this milliseconds; DEBUGOUTPUT: 2000
#endif
MyTimestamp g_lastMoveStart;
unsigned long g_lastMovePartCompleted = 0;
const unsigned long c_moveTurnaroundPause = 300; // extra waiting time when switching from one direction to the other
const unsigned long c_movingDownPause = 800; // If the lightswitch was blocked, wait for this time
const unsigned long c_waitingTimeBeforeReclose =
#ifdef DEBUG
    10*1000L;
#else
    // Don't forget the trailing "L"!!!
    600*1000L; // 10 minutes (600 seconds) before reclose during daylight
#endif
const unsigned long c_waitingTimeBeforeReallyReclose =
#ifdef DEBUG
  5*1000L;
#else
  10*1000L; // 10 seconds of warning
#endif

static inline unsigned int currentMoveCompletedToPercent() {
  return 100L * g_lastMovePartCompleted / c_moveDurationTotal;
}

// The wrappers for the input buttons debouncing
Bounce inButtonDebounce;
Bounce inButtonDownDebounce;
BounceAnalog inButtonOutsideDebounce;
const unsigned long c_debounceDelay = 40;
unsigned long g_lastLightswitchBlocked = 0;

// The wrapper for the warning light output
RBD::Light outWarnLightTimer;
RBD::Light outRoomLightTimer;
RBD::Light outArduinoLed;
const unsigned long c_blinkTime = 200; // milliseconds

// The timers for starting a reclosing
RBD::Timer doorUpStartReclose;
RBD::Timer doorUpReallyReclose;
RBD::Timer doorDownPausing;

// The variable to ensure printing only every 8th time some debug output
int g_continuous_printing = 0;

inline void outRoomLightSwitchOn() {
  outRoomLightTimer.blink(150, 150, 1);
}

// the setup routine runs once when you press reset:
void setup() {
  // initialize the digital pin as an output.
  pinMode(pinOutMotorOn, OUTPUT);
  pinMode(pinOutMotorUp, OUTPUT);
  outWarnLightTimer.setupPin(pinOutWarnLight);
  outWarnLightTimer.fade(c_blinkTime, c_blinkTime, c_blinkTime, c_blinkTime, 2);

  outRoomLightTimer.setupPin(pinOutRoomLight, true, true); // digital=true, inverted=true
  outRoomLightSwitchOn();
  outArduinoLed.setupPin(pinOutArduinoLedPin, true); // digital=true
  outArduinoLed.blink(2000, 2000); // In DOOR_DOWN, do some slow blinking to show we are alive

  digitalWrite(pinOutMotorOn, RELAY_OFF);
  digitalWrite(pinOutMotorUp, RELAY_OFF);

  pinMode(pinInButton, INPUT_PULLUP);
  inButtonDebounce.attach(pinInButton);
  inButtonDebounce.interval(c_debounceDelay);

  pinMode(pinInButtonDown, INPUT_PULLUP);
  inButtonDownDebounce.attach(pinInButtonDown);
  inButtonDownDebounce.interval(c_debounceDelay);

  pinMode(pinInLightswitch, INPUT_PULLUP);

  // The input button that has a potentiometer behaviour (due to humidity)
  inButtonOutsideDebounce.attach(pinAnalogInOutsideButton);
  inButtonOutsideDebounce.interval(c_debounceDelay);
  inButtonOutsideDebounce.setCurrentAsMax(); // calibrate the current "high" value of the outside button

#ifdef DEBUGOUTPUT
  Serial.begin(9600);
#endif
  doorUpStartReclose.setTimeout(c_waitingTimeBeforeReclose);
  doorUpReallyReclose.setTimeout(c_waitingTimeBeforeReallyReclose);
  doorDownPausing.setTimeout(c_movingDownPause);
  doorUpStartReclose.stop();
  doorUpReallyReclose.stop();
  doorDownPausing.stop();
}

inline void motorMoveUp(bool shouldMoveUp) {
  digitalWrite(pinOutMotorUp, shouldMoveUp ? RELAY_ON : RELAY_OFF);
}
inline void motorMoving(bool shouldMove) {
  digitalWrite(pinOutMotorOn, shouldMove ? RELAY_ON : RELAY_OFF);
}

inline void transitionTo_MOVING_DOWN_PAUSED() {
    state = DOOR_MOVING_DOWN_PAUSED;
    motorMoving(false);
    outWarnLightTimer.fade(c_blinkTime, c_blinkTime, c_blinkTime, c_blinkTime);
    doorDownPausing.restart();
}

#ifdef COUNTBLINKER
int blinkCounter = 0;
bool blinkerWasOn = false;
#endif

// the loop routine runs over and over again forever:
void loop() {
  auto currentMillis = millis();

#ifdef COUNTBLINKER
  // value of 3000 is approx half a second = this loop is processed approx. 6000 times per second
  if (blinkCounter > 3000) {
    if (blinkerWasOn)
      outRoomLightTimer.off();
    else
      outRoomLightTimer.on();
    blinkerWasOn = !blinkerWasOn;
    blinkCounter = 0;
  }
  blinkCounter = blinkCounter+1;
#endif

  // Update for the input buttons
  const bool onInButtonOutsideChanged = inButtonOutsideDebounce.update();
  const bool onInButtonOutsidePressed = onInButtonOutsideChanged && (inButtonOutsideDebounce.read() == LOW);

  const bool onInButtonChanged = inButtonDebounce.update();
  const bool onInButtonPressed = onInButtonChanged && (inButtonDebounce.read() == LOW);

  const bool onInButtonDownChanged = inButtonDownDebounce.update();
  const bool onInButtonDownPressed = onInButtonDownChanged && (inButtonDownDebounce.read() == LOW);

  const bool onInLightswitchBlocked = (digitalRead(pinInLightswitch) == HIGH);
  if (onInLightswitchBlocked)
  {
      g_lastLightswitchBlocked = currentMillis;
  }

  // Update for the timed output LED
  outWarnLightTimer.update();
  outRoomLightTimer.update();
  outArduinoLed.update();

#ifdef DEBUGOUTPUT
  int val;
  g_continuous_printing++;
  if ((g_continuous_printing & 0xFFF) == 0) {
    Serial.print(" state = ");
    Serial.print(state);
    Serial.print(" analogInButton= ");
    val = analogRead(pinAnalogInOutsideButton);
    Serial.print(val);
    Serial.print(" inLightswitch = ");
    Serial.println(onInLightswitchBlocked);
  }

  if (onInButtonOutsideChanged) {
    Serial.print("onInButtonOutsideChanged  -- ");
  }
  if (onInButtonChanged) {
    Serial.print("onInButtonChanged, now = ");
    Serial.print(inButtonDebounce.read());
    Serial.print(" at millis=");
    Serial.println(currentMillis);
  }
  if (onInButtonDownChanged) {
    Serial.print("onInButtonDownChanged, now = ");
    Serial.print(inButtonDownDebounce.read());
    Serial.print(" at millis=");
    Serial.println(currentMillis);
  }
#endif

  switch (state) {
    case DOOR_DOWN:
      // Door down/closed: Make sure motor is off
      motorMoving(false);
      // State change: Only when the Up-Button is pressed
      if (onInButtonPressed || onInButtonOutsidePressed) {
        state = DOOR_MOVING_UP;
        motorMoveUp(true);
        g_lastMoveStart.setValue(currentMillis);
#ifdef DEBUGOUTPUT
        Serial.print(currentMillis);
        Serial.println(": state DOWN -> MOVING_UP");
#endif
        outWarnLightTimer.on();
        outRoomLightSwitchOn();
      }
      break;

    case DOOR_UP:
      // Door up/open: Make sure motor is off
      motorMoving(false);
      // Special rule on Up-Button, if the reclose blinking is ongoing: Cancel the reclose
      if (doorUpReallyReclose.isActive() && (onInButtonPressed || onInLightswitchBlocked)) {
        doorUpReallyReclose.stop();
        doorUpStartReclose.restart();
        outWarnLightTimer.off();
      } else
      // State change can be because of multiple things
      if (onInButtonPressed || onInButtonOutsidePressed || onInButtonDownPressed || doorUpReallyReclose.onExpired()) {
        doorUpStartReclose.stop();
        doorUpReallyReclose.stop();
        g_lastMoveStart.setValue(currentMillis);
        g_lastMovePartCompleted = 0;
        outRoomLightSwitchOn();
        if (onInButtonDownPressed) {
            inButtonOutsideDebounce.setCurrentAsMax(); // calibrate the current "high" value of the outside button
        }
        motorMoveUp(false);
        if (onInLightswitchBlocked || currentMillis <= g_lastLightswitchBlocked + c_movingDownPause) {
          // Oh, we had somebody blocking the lightswitch, so go into PAUSED mode immediately
          transitionTo_MOVING_DOWN_PAUSED();
#ifdef DEBUGOUTPUT
          Serial.print(currentMillis);
          Serial.print(": state DOOR_UP -> MOVING_DOWN_PAUSED due to lightswitchBlocked. lastMoveStart=");
          Serial.println(g_lastMoveStart.getValue());
#endif
          //delay(1); // maybe this fixes a potential wrong state transition
          break;
        } else {
          // Any button was pressed or timer expired => State change: Now move downwards
          state = DOOR_MOVING_DOWN;
          outWarnLightTimer.on();
          doorDownPausing.stop();
#ifdef DEBUGOUTPUT
          Serial.print(currentMillis);
          Serial.print(": state DOOR_UP -> MOVING_DOWN; lastMoveStart=");
          Serial.println(g_lastMoveStart.getValue());
#endif
          break;
        }
      }
      // While the door is open, check for the timer timeout of re-closing
      if (doorUpStartReclose.onExpired()) {
        doorUpReallyReclose.restart();
        outWarnLightTimer.blink(c_blinkTime, c_blinkTime);
        outRoomLightSwitchOn();
      }
      break;

    case DOOR_MOVING_DOWN:
      // Lightswitch blocked? Transition to MOVING_DOWN_PAUSED and jump out of this state
      if (onInLightswitchBlocked) {
        // Light switch is blocked => State change: Go into PAUSED state
        transitionTo_MOVING_DOWN_PAUSED();
        g_lastMovePartCompleted = currentMillis - g_lastMoveStart.getValue();
#ifdef DEBUGOUTPUT
        Serial.print(currentMillis);
        Serial.print(": state MOVING_DOWN -> PAUSED due to lightswitchBlocked. lastMovePartCompleted[%] = ");
        Serial.println(currentMoveCompletedToPercent());
#endif
        break; // jump out of this state
      }

      // Reached end of movement in DOOR_DOWN position? Jump out of this state
      if (currentMillis - g_lastMoveStart.getValue() > c_moveDurationTotal) {
#ifdef DEBUGOUTPUT
        Serial.print("State change to DOOR_DOWN with lastMoveStart=");
        Serial.println(g_lastMoveStart.getValue());
#endif
        state = DOOR_DOWN;
        motorMoving(false);
        outWarnLightTimer.off();
        outArduinoLed.blink(2000, 2000); // In DOOR_DOWN, do some slow blinking to show we are alive
#ifdef DEBUGOUTPUT
        Serial.print(currentMillis);
        Serial.print(": state MOVING_DOWN -> DOOR_DOWN; lastMoveStart=");
        Serial.print(g_lastMoveStart.getValue());
        Serial.print(" diff=");
        Serial.print(currentMillis - g_lastMoveStart.getValue());
        Serial.print(" moveTotal=");
        Serial.println(c_moveDurationTotal);
#endif
        break; // jump out of this state
      }

      // State change: Pressed button for changing direction?
      if (onInButtonPressed || onInButtonOutsidePressed) {
        // Some up-button was pressed => State change: Now move up again
        state = DOOR_MOVING_UP;
        motorMoving(false);
        motorMoveUp(true);
        g_lastMovePartCompleted = currentMillis - g_lastMoveStart.getValue();
        const unsigned long moveRemaining = c_moveDurationTotal - g_lastMovePartCompleted;
        delay(c_moveTurnaroundPause);
        currentMillis = millis();
        g_lastMoveStart.setValue(currentMillis - moveRemaining);
#ifdef DEBUGOUTPUT
        Serial.print("state MOVING_DOWN -> MOVING_UP. lastMovePartCompleted[%] = ");
        Serial.println(currentMoveCompletedToPercent());
#endif
        break; // jump out of this state
      }
      
      // We stay in MOVE_DOWN state: Door moving downwards/closing
      //motorMoveUp(false);
      motorMoving(true);

      break;

    case DOOR_MOVING_DOWN_PAUSED:

      // State change: Pressed button for changing direction?
      if (onInButtonPressed || onInButtonOutsidePressed) {
        // Some button was pressed => State change: Now move up again
        state = DOOR_MOVING_UP;
        if (g_lastMovePartCompleted > 0) { // only if we have moved away from DOOR_UP
          motorMoveUp(true);
        }
        outWarnLightTimer.on();
        const unsigned long moveRemaining = c_moveDurationTotal - g_lastMovePartCompleted;
        g_lastMoveStart.setValue(currentMillis - moveRemaining);
#ifdef DEBUGOUTPUT
        Serial.print("state PAUSED -> MOVING_UP; lastMovePartCompleted[%] = ");
        Serial.print(currentMoveCompletedToPercent());
        Serial.print(" lastMoveStart=");
        Serial.println(g_lastMoveStart.getValue());
#endif
        delay(1); // otherwise we get a wrong state transition
        break; // jump out of this state
      } else if (onInLightswitchBlocked) {
        // Light switch is still blocked: Still waiting for pause time
        doorDownPausing.restart();
      } else if (doorDownPausing.onExpired()) {
        // Light switch was free again for long enough => State change: Continue moving downwards
        state = DOOR_MOVING_DOWN;
        outWarnLightTimer.on();
        g_lastMoveStart.setValue(currentMillis - g_lastMovePartCompleted);
#ifdef DEBUGOUTPUT
        Serial.print(currentMillis);
        Serial.print(": state PAUSED -> MOVING_DOWN; lastMovePartCompleted[%] = ");
        Serial.print(currentMoveCompletedToPercent());
        Serial.print("; lastMoveStart=");
        Serial.println(g_lastMoveStart.getValue());
#endif
        delay(1); // otherwise we get a wrong state transition
      }
      break;

    case DOOR_MOVING_UP:
      // Reached end of movement in DOOR_UP position? Jump out of this state
      if (currentMillis - g_lastMoveStart.getValue() > c_moveDurationTotal) {
        // We have reached the DOOR_UP position
        state = DOOR_UP;
        motorMoving(false);
        motorMoveUp(false);
        outWarnLightTimer.off();
        inButtonOutsideDebounce.setCurrentAsMax(); // calibrate the current "high" value of the outside button

        doorUpStartReclose.setTimeout(c_waitingTimeBeforeReclose);
#ifdef DEBUGOUTPUT
        Serial.print("state MOVING_UP -> DOOR_UP; Set restarting timer to timeout=");
        Serial.println(c_waitingTimeBeforeReclose);
#endif
        doorUpStartReclose.restart();
        outArduinoLed.blink(400, 400); // in DOOR_UP state we blink somewhat faster
        break; // jump out of this state
      }

      // State change: Pressed button for changing direction?
      if (onInButtonDownPressed) {
        const unsigned long movePartCompleted = currentMillis - g_lastMoveStart.getValue();
        transitionTo_MOVING_DOWN_PAUSED();
        motorMoveUp(false);
        g_lastMovePartCompleted = c_moveDurationTotal - movePartCompleted;
#ifdef DEBUGOUTPUT
        Serial.print(currentMillis);
        Serial.println(": state MOVING_UP -> MOVING_DOWN_PAUSED; lastMovePartCompleted[%] = ");
        Serial.println(currentMoveCompletedToPercent());
#endif
        break; // jump out of this state
      }

      // Door moving upwards/opening
      //motorMoveUp(true);
      motorMoving(true);

      break;
  }
}
