#pragma once

// Standalone playback is the default. Set to 1 only for diagnostic captures.
#ifndef DAW_SERIAL_DIAGNOSTICS
#define DAW_SERIAL_DIAGNOSTICS 0
#endif

// Never flush or retry diagnostic bytes. Missing logs are preferable to a
// stalled audio pipeline when the PC stops reading its USB serial port.
class BestEffortSerialLog : public Stream {
public:
  BestEffortSerialLog(Stream &output, bool (*ready)()) : output(output), ready(ready) {}
  using Print::write;
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t *data, size_t length) override {
    if (!data || !length) return 0;
    if (!DAW_SERIAL_DIAGNOSTICS || !ready()) return length;
    int room = output.availableForWrite();
    if (room <= 0 || length > (size_t)room) return length;
    output.write(data, length); // USB timeout is also bounded in setup().
    return length; // Drop any unwritten remainder; do not request retries.
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {} // A serial flush can wait for the absent host.
private:
  Stream &output;
  bool (*ready)();
};
