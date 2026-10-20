"""Exercise the actual firmware gesture state machine with timed GPIO input."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino').read_text()
engine = source[source.index('enum ButtonEvent {'):source.index('// ================================================================\n// ENCODERS')]
definitions = '\n'.join(line for line in source.splitlines() if line.startswith((
    '#define BUTTON_DEBOUNCE_MS ', '#define LONG_PRESS_MS ',
    '#define DOUBLE_CLICK_MS ', '#define ENC1_SW ', '#define ENC2_SW ')))
stub = r'''
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
'''
tests = r'''
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
'''
(ROOT / 'validation/button_checks_v6.cpp').write_text(stub + definitions + '\n' + engine + tests)
print('Extracted actual button state machine and gesture timing constants.')
