// Please read Bounce2.h for information about the liscence and authors

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif
#include "BounceAnalog.h"

#define DEBOUNCED_STATE 0
#define UNSTABLE_STATE  1
#define STATE_CHANGED   3


BounceAnalog::BounceAnalog()
    : previous_millis(0)
    , interval_millis(10)
    , state(0)
    , pin(0)
    , m_analogMin(0)
    , m_analogMax(1023)
{}

void BounceAnalog::setCurrentAsMax()
{
    int val = analogRead(pin);
    m_analogMax = val;
}

void BounceAnalog::attach(int pin) {
    this->pin = pin;
    const int value = analogRead(pin);
    bool read = (value > (m_analogMax - m_analogMin) / 2);
    state = 0;
    if (read) {
        state = _BV(DEBOUNCED_STATE) | _BV(UNSTABLE_STATE);
    }
#ifdef BOUNCE_LOCK_OUT
    previous_millis = 0;
#else
    previous_millis = millis();
#endif
}

void BounceAnalog::attach(int pin, int mode){
  pinMode(pin, mode);
  
  this->attach(pin);
}

void BounceAnalog::interval(uint16_t interval_millis)
{
    this->interval_millis = interval_millis;
}

bool BounceAnalog::update()
{
#ifdef BOUNCE_LOCK_OUT
    state &= ~_BV(STATE_CHANGED);
    // Ignore everything if we are locked out
    if (millis() - previous_millis >= interval_millis) {
        bool currentState = digitalRead(pin);
        if ((bool)(state & _BV(DEBOUNCED_STATE)) != currentState) {
            previous_millis = millis();
            state ^= _BV(DEBOUNCED_STATE);
            state |= _BV(STATE_CHANGED);
        }
    }
    return state & _BV(STATE_CHANGED);
#else
    // Read the state of the switch in a temporary variable.
    const int value = analogRead(pin);
    bool currentState = (value > (m_analogMax - m_analogMin) / 2);
    state &= ~_BV(STATE_CHANGED);

    // If the reading is different from last reading, reset the debounce counter
    if ( currentState != (bool)(state & _BV(UNSTABLE_STATE)) ) {
        previous_millis = millis();
        state ^= _BV(UNSTABLE_STATE);
    } else
        if ( millis() - previous_millis >= interval_millis ) {
            // We have passed the threshold time, so the input is now stable
            // If it is different from last state, set the STATE_CHANGED flag
            if ((bool)(state & _BV(DEBOUNCED_STATE)) != currentState) {
                previous_millis = millis();
                state ^= _BV(DEBOUNCED_STATE);
                state |= _BV(STATE_CHANGED);
            }
        }

    return state & _BV(STATE_CHANGED);
#endif
}

bool BounceAnalog::read()
{
    return state & _BV(DEBOUNCED_STATE);
}

bool BounceAnalog::rose()
{
    return ( state & _BV(DEBOUNCED_STATE) ) && ( state & _BV(STATE_CHANGED));
}

bool BounceAnalog::fell()
{
    return !( state & _BV(DEBOUNCED_STATE) ) && ( state & _BV(STATE_CHANGED));
}
