
#include <cassert>
#include <cstdint>
#include <iostream>
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
int rawLevel = HIGH;
uint32_t nowMs = 0;
int digitalRead(uint8_t) { return rawLevel; }
void pinMode(uint8_t, int) {}
uint32_t millis() { return nowMs; }
#define ENC1_SW     38
#define ENC2_SW     47
#define BUTTON_DEBOUNCE_MS 30
#define LONG_PRESS_MS 650
#define DOUBLE_CLICK_MS 300
enum ButtonEvent {
  NO_EVENT,
  CLICK_EVENT,
  LONG_EVENT,
  DOUBLE_CLICK_EVENT
};

struct Button {
  uint8_t pin;
  bool detectDouble = false;
  bool stable = HIGH, rawLast = HIGH;
  bool longSent = false, clickPending = false, secondPress = false;
  uint32_t changedAt = 0, pressedAt = 0, releasedAt = 0;

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    stable = rawLast = digitalRead(pin);
    changedAt = millis();
    longSent = clickPending = secondPress = false;
  }

  ButtonEvent update(uint32_t now) {
    bool raw = digitalRead(pin);
    if (raw != rawLast) { rawLast = raw; changedAt = now; }
    if (now - changedAt < BUTTON_DEBOUNCE_MS) return NO_EVENT;
    if (raw != stable) {
      stable = raw;
      if (stable == LOW) {
        pressedAt = now;
        longSent = false;
        secondPress = detectDouble && clickPending && now - releasedAt <= DOUBLE_CLICK_MS;
        // A late second press must not swallow the preceding single click.
        if (detectDouble && clickPending && !secondPress) {
          clickPending = false;
          return CLICK_EVENT;
        }
      } else if (!longSent) {
        if (!detectDouble) return CLICK_EVENT;
        if (secondPress) {
          clickPending = secondPress = false;
          return DOUBLE_CLICK_EVENT;
        }
        clickPending = true;
        releasedAt = now;
      }
    }
    if (stable == LOW && !longSent && now - pressedAt >= LONG_PRESS_MS) {
      longSent = true;
      clickPending = secondPress = false;
      return LONG_EVENT;
    }
    // Wait for the double-click window before invoking the single action.
    if (detectDouble && clickPending && stable == HIGH && now - releasedAt >= DOUBLE_CLICK_MS) {
      clickPending = false;
      return CLICK_EVENT;
    }
    return NO_EVENT;
  }
};

Button enc1Button{ENC1_SW};
Button enc2Button{ENC2_SW, true};


Button fresh(bool doubles) {
  rawLevel = HIGH; nowMs = 0;
  Button button{ENC2_SW, doubles}; button.begin(); return button;
}
ButtonEvent sample(Button &button, int level, uint32_t time) {
  rawLevel = level; nowMs = time; return button.update(time);
}
void firstClick(Button &button) {
  assert(sample(button, LOW, 0) == NO_EVENT);
  assert(sample(button, LOW, 30) == NO_EVENT);
  assert(sample(button, HIGH, 80) == NO_EVENT);
}
int main() {
  {
    auto button = fresh(true); firstClick(button);
    assert(sample(button, HIGH, 110) == NO_EVENT);
    assert(sample(button, HIGH, 409) == NO_EVENT);
    assert(sample(button, HIGH, 410) == CLICK_EVENT);
    assert(sample(button, HIGH, 1000) == NO_EVENT);
  }
  {
    auto button = fresh(true); firstClick(button);
    assert(sample(button, HIGH, 110) == NO_EVENT);
    assert(sample(button, LOW, 200) == NO_EVENT);
    assert(sample(button, LOW, 230) == NO_EVENT);
    assert(sample(button, HIGH, 280) == NO_EVENT);
    assert(sample(button, HIGH, 310) == DOUBLE_CLICK_EVENT);
    assert(sample(button, HIGH, 1000) == NO_EVENT);
  }
  {
    auto button = fresh(true);
    assert(sample(button, LOW, 0) == NO_EVENT);
    assert(sample(button, LOW, 30) == NO_EVENT);
    assert(sample(button, LOW, 679) == NO_EVENT);
    assert(sample(button, LOW, 680) == LONG_EVENT);
    assert(sample(button, LOW, 1000) == NO_EVENT);
    assert(sample(button, HIGH, 1050) == NO_EVENT);
    assert(sample(button, HIGH, 1080) == NO_EVENT);
    assert(sample(button, HIGH, 1500) == NO_EVENT);
    assert(sample(button, LOW, 1600) == NO_EVENT);
    assert(sample(button, LOW, 1630) == NO_EVENT);
    assert(sample(button, HIGH, 1680) == NO_EVENT);
    assert(sample(button, HIGH, 1710) == NO_EVENT);
    assert(sample(button, HIGH, 2010) == CLICK_EVENT);
  }
  {
    auto button = fresh(true); firstClick(button);
    assert(sample(button, HIGH, 110) == NO_EVENT);
    assert(sample(button, LOW, 450) == NO_EVENT);
    assert(sample(button, LOW, 480) == CLICK_EVENT);
    assert(sample(button, HIGH, 530) == NO_EVENT);
    assert(sample(button, HIGH, 560) == NO_EVENT);
    assert(sample(button, HIGH, 860) == CLICK_EVENT);
  }
  {
    auto button = fresh(true); firstClick(button);
    assert(sample(button, HIGH, 110) == NO_EVENT);
    assert(sample(button, LOW, 200) == NO_EVENT);
    assert(sample(button, LOW, 230) == NO_EVENT);
    assert(sample(button, LOW, 880) == LONG_EVENT);
    assert(sample(button, HIGH, 900) == NO_EVENT);
    assert(sample(button, HIGH, 930) == NO_EVENT);
    assert(sample(button, HIGH, 1500) == NO_EVENT);
  }
  {
    auto button = fresh(false); firstClick(button);
    assert(sample(button, HIGH, 110) == CLICK_EVENT);
    assert(sample(button, HIGH, 1000) == NO_EVENT);
  }
  {
    auto button = fresh(true);
    assert(sample(button, LOW, 0) == NO_EVENT);
    assert(sample(button, HIGH, 10) == NO_EVENT);
    assert(sample(button, LOW, 20) == NO_EVENT);
    assert(sample(button, HIGH, 25) == NO_EVENT);
    assert(sample(button, HIGH, 1000) == NO_EVENT);
  }
  {
    auto button = fresh(true);
    assert(sample(button, LOW, UINT32_MAX - 100) == NO_EVENT);
    assert(sample(button, LOW, UINT32_MAX - 70) == NO_EVENT);
    assert(sample(button, HIGH, UINT32_MAX - 20) == NO_EVENT);
    assert(sample(button, HIGH, 9) == NO_EVENT);
    assert(sample(button, HIGH, 308) == NO_EVENT);
    assert(sample(button, HIGH, 309) == CLICK_EVENT);
  }
  std::cout << "PASS: actual button debounce, deferred single click, double click, hold, re-arm, late second click and timer wrap\n";
}
