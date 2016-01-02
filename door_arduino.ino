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
//const int outRoomLight = 4;

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
const long moveDurationTotal = 18000; // total movement takes this milliseconds
const long moveTurnaroundPause = 300; // extra waiting time when switching from one direction to the other

Bounce inButtonDebounce;
Bounce inButtonDownDebounce;
unsigned long debounceDelay = 50;

// the setup routine runs once when you press reset:
void setup() {
  // initialize the digital pin as an output.
  pinMode(outMotorOn, OUTPUT);
  pinMode(outMotorUp, OUTPUT);
  pinMode(outWarnLight, OUTPUT);
  //pinMode(outRoomLight, OUTPUT);

  digitalWrite(outMotorOn, RELAY_OFF);
  digitalWrite(outMotorUp, RELAY_OFF);
  digitalWrite(outWarnLight, RELAY_OFF);
  //digitalWrite(outRoomLight, RELAY_OFF);

  pinMode(inButton, INPUT_PULLUP);
  inButtonDebounce.attach(inButton);
  inButtonDebounce.interval(debounceDelay);

  pinMode(inButtonDown, INPUT_PULLUP);
  inButtonDownDebounce.attach(inButtonDown);
  inButtonDownDebounce.interval(debounceDelay);

#ifdef DEBUG
  Serial.begin(9600);
#endif
}

// the loop routine runs over and over again forever:
void loop() {
  const unsigned long now = millis();

  const bool onInButtonChanged = inButtonDebounce.update();
  const bool onInButtonPressed = onInButtonChanged && (inButtonDebounce.read() == LOW);

  const bool onInButtonDownChanged = inButtonDownDebounce.update();
  const bool onInButtonDownPressed = onInButtonDownChanged && (inButtonDownDebounce.read() == LOW);

#ifdef DEBUG
  if (onInButtonChanged) {
    Serial.print("onInButtonChanged, now = ");
    Serial.print(inButtonDebounce.read());
    Serial.print(" at ");
    Serial.println(now);
  }
  if (onInButtonDownChanged) {
    Serial.print("onInButtonDownChanged, now = ");
    Serial.print(inButtonDownDebounce.read());
    Serial.print(" at ");
    Serial.println(now);
  }
#endif

  switch (state) {
    case DOOR_DOWN:
      digitalWrite(outMotorOn, RELAY_OFF);
      digitalWrite(outMotorUp, RELAY_OFF);
      digitalWrite(outWarnLight, RELAY_OFF);
      if (onInButtonPressed) {
        state = DOOR_MOVING_UP;
        lastMoveStart = now;
      }
      break;

    case DOOR_UP:
      digitalWrite(outMotorOn, RELAY_OFF);
      digitalWrite(outMotorUp, RELAY_OFF);
      digitalWrite(outWarnLight, RELAY_OFF);
      if (onInButtonPressed || onInButtonDownPressed) {
        state = DOOR_MOVING_DOWN;
        lastMoveStart = now;
      }
      break;

    case DOOR_MOVING_DOWN:
      digitalWrite(outMotorOn, RELAY_ON);
      digitalWrite(outMotorUp, RELAY_OFF);
      digitalWrite(outWarnLight, RELAY_ON);
      if (now - lastMoveStart > moveDurationTotal) {
        state = DOOR_DOWN;
      }
      if (onInButtonPressed) {
        state = DOOR_MOVING_UP;
        digitalWrite(outMotorOn, RELAY_OFF);
        delay(moveTurnaroundPause);
        const unsigned long moveRemaining = moveDurationTotal - (now - lastMoveStart);
        lastMoveStart = now - moveRemaining + moveTurnaroundPause;
      }
      break;

    case DOOR_MOVING_UP:
      digitalWrite(outMotorOn, RELAY_ON);
      digitalWrite(outMotorUp, RELAY_ON);
      digitalWrite(outWarnLight, RELAY_ON);
      if (now - lastMoveStart > moveDurationTotal) {
        state = DOOR_UP;
      }
      if (onInButtonDownPressed) {
        state = DOOR_MOVING_DOWN;
        digitalWrite(outMotorOn, RELAY_OFF);
        delay(moveTurnaroundPause);
        const unsigned long moveRemaining = moveDurationTotal - (now - lastMoveStart);
        lastMoveStart = now - moveRemaining + moveTurnaroundPause;
      }
      break;
  }
}

