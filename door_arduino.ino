#include <RBD_Timer.h> // https://github.com/alextaujenis/RBD_Timer
#include <RBD_Light.h> // https://github.com/alextaujenis/RBD_Light

// The Bounce2 library http://playground.arduino.cc/Code/Bounce
#include <Bounce2.h>

/*
  Garage-Door State machine 2

  Christian Stimming, 2016, Hamburg, Germany
 */

// Names for the output pins:
const int outMotorOn = 7;
const int outMotorUp = 6;
const int outWarnLight = 5;
const int outRoomLight = 4;
const int outArduinoLedPin = 13;

const int inButton = 3;
const int inButtonDown = 2;
//const int inButtonInside =

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
long lastMoveStart = 0;
const long moveDurationTotal =
#ifdef DEBUG
  2000;
#else
  18000; // total movement takes this milliseconds; DEBUG: 2000
#endif
const long moveTurnaroundPause = 300; // extra waiting time when switching from one direction to the other

// The wrappers for the input buttons debouncing
Bounce inButtonDebounce;
Bounce inButtonDownDebounce;
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
  pinMode(outMotorOn, OUTPUT);
  pinMode(outMotorUp, OUTPUT);
  outWarnLightTimer.setupPin(outWarnLight);
  outWarnLightTimer.fade(blinkTime, blinkTime, blinkTime, blinkTime, 2);

  outRoomLightTimer.setupPin(outRoomLight, true, true); // digital=true, inverted=true
  outRoomLightSwitchOn();
  outArduinoLed.setupPin(outArduinoLedPin, true); // digital=true
  outArduinoLed.blink(2000, 2000); // In DOOR_DOWN, do some slow blinking to show we are alive

  digitalWrite(outMotorOn, RELAY_OFF);
  digitalWrite(outMotorUp, RELAY_OFF);

  pinMode(inButton, INPUT_PULLUP);
  inButtonDebounce.attach(inButton);
  inButtonDebounce.interval(debounceDelay);

  pinMode(inButtonDown, INPUT_PULLUP);
  inButtonDownDebounce.attach(inButtonDown);
  inButtonDownDebounce.interval(debounceDelay);

#ifdef DEBUG
  Serial.begin(9600);
#endif
  doorUpStartReclose.setTimeout(
#ifdef DEBUG
    5*1000
#else
    10*60*1000
#endif
  ); // 10 minutes before reclose; DEBUG: 5 seconds
  doorUpReallyReclose.setTimeout(
#ifdef DEBUG
  3*1000
#else
  10*1000
#endif
  ); // 10 seconds of warning; DEBUG: 3 seconds
  doorUpStartReclose.stop();
  doorUpReallyReclose.stop();
}

// the loop routine runs over and over again forever:
void loop() {

  // Update for the input buttons
  const bool onInButtonChanged = inButtonDebounce.update();
  const bool onInButtonPressed = onInButtonChanged && (inButtonDebounce.read() == LOW);

  const bool onInButtonDownChanged = inButtonDownDebounce.update();
  const bool onInButtonDownPressed = onInButtonDownChanged && (inButtonDownDebounce.read() == LOW);

  // Update for the timed output LED
  outWarnLightTimer.update();
  outRoomLightTimer.update();
  outArduinoLed.update();

#ifdef DEBUG
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
      digitalWrite(outMotorOn, RELAY_OFF);
      digitalWrite(outMotorUp, RELAY_OFF);
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
      digitalWrite(outMotorOn, RELAY_OFF);
      digitalWrite(outMotorUp, RELAY_OFF);
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
      digitalWrite(outMotorOn, RELAY_ON);
      digitalWrite(outMotorUp, RELAY_OFF);
      // Check for timeout so that we are down/closed
      if (millis() - lastMoveStart > moveDurationTotal) {
        state = DOOR_DOWN;
        outWarnLightTimer.off();
        outArduinoLed.blink(2000, 2000); // In DOOR_DOWN, do some slow blinking to show we are alive
      }
      // State change: Pressed button for changing direction?
      if (onInButtonPressed) {
        state = DOOR_MOVING_UP;
        digitalWrite(outMotorOn, RELAY_OFF);
        delay(moveTurnaroundPause);
        const unsigned long moveRemaining = moveDurationTotal - (millis() - lastMoveStart);
        lastMoveStart = millis() - moveRemaining + moveTurnaroundPause;
      }
      break;

    case DOOR_MOVING_UP:
      // Door moving upwards/opening
      digitalWrite(outMotorOn, RELAY_ON);
      digitalWrite(outMotorUp, RELAY_ON);
      // Check for timeout so that we are up/opened
      if (millis() - lastMoveStart > moveDurationTotal) {
        state = DOOR_UP;
        outWarnLightTimer.off();
        doorUpStartReclose.restart();
        outArduinoLed.blink(500, 500); // in DOOR_UP state we blink somewhat faster
      }
      // State change: Pressed button for changing direction?
      if (onInButtonDownPressed) {
        state = DOOR_MOVING_DOWN;
        digitalWrite(outMotorOn, RELAY_OFF);
        delay(moveTurnaroundPause);
        const unsigned long moveRemaining = moveDurationTotal - (millis() - lastMoveStart);
        lastMoveStart = millis() - moveRemaining + moveTurnaroundPause;
      }
      break;
  }
}

