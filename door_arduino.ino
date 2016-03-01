#include <RBD_Timer.h> // https://github.com/alextaujenis/RBD_Timer
#include <RBD_Light.h> // https://github.com/alextaujenis/RBD_Light

// The Bounce2 library http://playground.arduino.cc/Code/Bounce
#include <Bounce2.h>
#include <BounceAnalog.h> // a variation of Bounce2 with analog input

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
//const int pinInButtonInside =
const int pinAnalogInOutsideButton = 0;
const int pinAnalogInPhotosensor = 5; // right now unused!

// The relays are active-low, so we better define readable names for this
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// Active if the serial output should be compiled in.
//#define DEBUG

/** The states that the door can have*/
enum State {
  DOOR_DOWN
  , DOOR_UP
  , DOOR_MOVING_UP
  , DOOR_MOVING_DOWN
};
/** This is the door's actual state */
State state = DOOR_DOWN;
unsigned long lastMoveStart = 0;
const unsigned long moveDurationTotal =
#ifdef DEBUG
  2000;
#else
  18000L; // total movement takes this milliseconds; DEBUG: 2000
#endif
const unsigned long moveTurnaroundPause = 300; // extra waiting time when switching from one direction to the other
const unsigned long waitingTimeBeforeReclose =
#ifdef DEBUG
    5*1000L;
#else
    600*1000L; // don't forget the trailing "L"!!!
#endif
// 10 minutes before reclose; DEBUG: 5 seconds
const unsigned long waitingTimeBeforeReallyReclose =
#ifdef DEBUG
  3*1000L;
#else
  10*1000L; // 10 seconds of warning; DEBUG: 3 seconds
#endif

// The wrappers for the input buttons debouncing
Bounce inButtonDebounce;
Bounce inButtonDownDebounce;
BounceAnalog inButtonOutsideDebounce;
const unsigned long debounceDelay = 40;

// The wrapper for the warning light output
RBD::Light outWarnLightTimer;
RBD::Light outRoomLightTimer;
RBD::Light outArduinoLed;
RBD::Timer doorUpStartReclose;
RBD::Timer doorUpReallyReclose;
const unsigned long blinkTime = 200; // milliseconds

inline void outRoomLightSwitchOn() {
  outRoomLightTimer.blink(150, 150, 1);
}

// the setup routine runs once when you press reset:
void setup() {
  // initialize the digital pin as an output.
  pinMode(pinOutMotorOn, OUTPUT);
  pinMode(pinOutMotorUp, OUTPUT);
  outWarnLightTimer.setupPin(pinOutWarnLight);
  outWarnLightTimer.fade(blinkTime, blinkTime, blinkTime, blinkTime, 2);

  outRoomLightTimer.setupPin(pinOutRoomLight, true, true); // digital=true, inverted=true
  outRoomLightSwitchOn();
  outArduinoLed.setupPin(pinOutArduinoLedPin, true); // digital=true
  outArduinoLed.blink(2000, 2000); // In DOOR_DOWN, do some slow blinking to show we are alive

  digitalWrite(pinOutMotorOn, RELAY_OFF);
  digitalWrite(pinOutMotorUp, RELAY_OFF);

  pinMode(pinInButton, INPUT_PULLUP);
  inButtonDebounce.attach(pinInButton);
  inButtonDebounce.interval(debounceDelay);

  pinMode(pinInButtonDown, INPUT_PULLUP);
  inButtonDownDebounce.attach(pinInButtonDown);
  inButtonDownDebounce.interval(debounceDelay);

  // The input button that has a potentiometer behaviour (due to humidity)
  inButtonOutsideDebounce.attach(pinAnalogInOutsideButton);
  inButtonOutsideDebounce.interval(debounceDelay);
  inButtonOutsideDebounce.setCurrentAsMax(); // calibrate the current "high" value of the outside button

#ifdef DEBUG
  Serial.begin(9600);
#endif
  doorUpStartReclose.setTimeout(waitingTimeBeforeReclose);
  doorUpReallyReclose.setTimeout(waitingTimeBeforeReallyReclose);
  doorUpStartReclose.stop();
  doorUpReallyReclose.stop();
}

// the loop routine runs over and over again forever:
void loop() {

  // Update for the input buttons
  const bool onInButtonOutsideChanged = inButtonOutsideDebounce.update();
  const bool onInButtonOutsidePressed = onInButtonOutsideChanged && (inButtonOutsideDebounce.read() == LOW);

  const bool onInButtonChanged = inButtonDebounce.update();
  const bool onInButtonPressed = onInButtonOutsidePressed || (onInButtonChanged && (inButtonDebounce.read() == LOW));

  const bool onInButtonDownChanged = inButtonDownDebounce.update();
  const bool onInButtonDownPressed = onInButtonDownChanged && (inButtonDownDebounce.read() == LOW);

  // Update for the timed output LED
  outWarnLightTimer.update();
  outRoomLightTimer.update();
  outArduinoLed.update();

#ifdef DEBUG
  int val;
  Serial.print("Photosensor = ");
  val = analogRead(inAnalogPinPhotosensor);
  Serial.println(val);

  if (onInButtonOutsideChanged) {
    Serial.print("onInButtonOutsideChanged  -- ");
  }
  if (onInButtonChanged) {
    Serial.print("onInButtonChanged, now = ");
    Serial.print(inButtonDebounce.read());
    Serial.print(" at ");
    Serial.println(millis());
  }
  if (onInButtonDownChanged) {
    Serial.print("onInButtonDownChanged, now = ");
    Serial.print(inButtonDownDebounce.read());
    Serial.print(" at ");
    Serial.println(millis());
  }
#endif

  switch (state) {
    case DOOR_DOWN:
      // Door down/closed: Make sure motor is off
      digitalWrite(pinOutMotorOn, RELAY_OFF);
      digitalWrite(pinOutMotorUp, RELAY_OFF);
      // State change: Only when the Up-Button is pressed
      if (onInButtonPressed) {
        state = DOOR_MOVING_UP;
        lastMoveStart = millis();
        outWarnLightTimer.on();
        outRoomLightSwitchOn();
      }
      break;

    case DOOR_UP:
      // Door up/open: Make sure motor is off
      digitalWrite(pinOutMotorOn, RELAY_OFF);
      digitalWrite(pinOutMotorUp, RELAY_OFF);
      // Special rule on Up-Button, if the reclose blinking is ongoing: Cancel the reclose
      if (doorUpReallyReclose.isActive() && onInButtonPressed) {
        doorUpReallyReclose.stop();
        doorUpStartReclose.restart();
        outWarnLightTimer.off();
      } else
      // State change can be because of multiple things
      if (onInButtonPressed || onInButtonDownPressed || doorUpReallyReclose.onExpired()) {
        state = DOOR_MOVING_DOWN;
        lastMoveStart = millis();
        outWarnLightTimer.on();
        doorUpStartReclose.stop();
        doorUpReallyReclose.stop();
        outRoomLightSwitchOn();
      }
      // While the door is open, check for the timer timeout of re-closing
      if (doorUpStartReclose.onExpired()) {
        doorUpReallyReclose.restart();
        outWarnLightTimer.blink(blinkTime, blinkTime);
        outRoomLightSwitchOn();
      }
      break;

    case DOOR_MOVING_DOWN:
      // Door moving downwards/closing
      digitalWrite(pinOutMotorOn, RELAY_ON);
      digitalWrite(pinOutMotorUp, RELAY_OFF);
      // Check for timeout so that we are down/closed
      if (millis() - lastMoveStart > moveDurationTotal) {
        state = DOOR_DOWN;
        outWarnLightTimer.off();
        outArduinoLed.blink(2000, 2000); // In DOOR_DOWN, do some slow blinking to show we are alive
      }
      // State change: Pressed button for changing direction?
      if (onInButtonPressed) {
        state = DOOR_MOVING_UP;
        digitalWrite(pinOutMotorOn, RELAY_OFF);
        delay(moveTurnaroundPause);
        const unsigned long moveRemaining = moveDurationTotal - (millis() - lastMoveStart);
        lastMoveStart = millis() - moveRemaining + moveTurnaroundPause;
      }
      break;

    case DOOR_MOVING_UP:
      // Door moving upwards/opening
      digitalWrite(pinOutMotorOn, RELAY_ON);
      digitalWrite(pinOutMotorUp, RELAY_ON);
      // Check for timeout so that we are up/opened
      if (millis() - lastMoveStart > moveDurationTotal) {
        state = DOOR_UP;
        outWarnLightTimer.off();
        doorUpStartReclose.restart();
        outArduinoLed.blink(500, 500); // in DOOR_UP state we blink somewhat faster
        inButtonOutsideDebounce.setCurrentAsMax(); // calibrate the current "high" value of the outside button
      }
      // State change: Pressed button for changing direction?
      if (onInButtonDownPressed) {
        state = DOOR_MOVING_DOWN;
        digitalWrite(pinOutMotorOn, RELAY_OFF);
        delay(moveTurnaroundPause);
        const unsigned long moveRemaining = moveDurationTotal - (millis() - lastMoveStart);
        lastMoveStart = millis() - moveRemaining + moveTurnaroundPause;
      }
      break;
  }
}

