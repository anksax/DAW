#pragma once

#if defined(CONFIG_DISABLE_HAL_LOCKS) && CONFIG_DISABLE_HAL_LOCKS
#error "Shared TFT/SD SPI requires Arduino HAL transaction locks"
#endif

// Include Adafruit_GFX and esp_heap_caps before this header. Only the UI task
// owns these pixels; physical SPI transfers are performed separately.
class TFTCanvas : public Adafruit_GFX {
public:
  static constexpr int WIDTH_PX = 240, HEIGHT_PX = 320, STRIPE_ROWS = 8;
  static constexpr int STRIPES = HEIGHT_PX / STRIPE_ROWS;
  TFTCanvas() : Adafruit_GFX(WIDTH_PX, HEIGHT_PX) {}
  ~TFTCanvas() { if (pixels) heap_caps_free(pixels); }
  bool begin() {
    if (!pixels) pixels = (uint16_t *)heap_caps_calloc(WIDTH_PX * HEIGHT_PX,
      sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels) markAll();
    return pixels != nullptr;
  }
  uint16_t *getBuffer() const { return pixels; }
  void markAll() { for (auto &value : dirty) value = true; }
  int nextDirty() const {
    for (int offset = 0; offset < STRIPES; ++offset) {
      int stripe = (nextStripe + offset) % STRIPES;
      if (dirty[stripe]) return stripe;
    }
    return -1;
  }
  void sent(int stripe) {
    if (stripe < 0 || stripe >= STRIPES) return;
    dirty[stripe] = false;
    nextStripe = (stripe + 1) % STRIPES;
  }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!pixels || x < 0 || y < 0 || x >= WIDTH_PX || y >= HEIGHT_PX) return;
    pixels[y * WIDTH_PX + x] = color;
    dirty[y / STRIPE_ROWS] = true;
  }
  void fillScreen(uint16_t color) override {
    if (!pixels) return;
    for (int i = 0; i < WIDTH_PX * HEIGHT_PX; ++i) pixels[i] = color;
    markAll();
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override {
    if (!pixels || y < 0 || y >= HEIGHT_PX || width <= 0) return;
    int first = x < 0 ? 0 : x;
    int last = x + width > WIDTH_PX ? WIDTH_PX : x + width;
    if (first >= last) return;
    for (int i = first; i < last; ++i) pixels[y * WIDTH_PX + i] = color;
    dirty[y / STRIPE_ROWS] = true;
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override {
    if (!pixels || x < 0 || x >= WIDTH_PX || height <= 0) return;
    int first = y < 0 ? 0 : y;
    int last = y + height > HEIGHT_PX ? HEIGHT_PX : y + height;
    for (int row = first; row < last; ++row) {
      pixels[row * WIDTH_PX + x] = color;
      dirty[row / STRIPE_ROWS] = true;
    }
  }
private:
  uint16_t *pixels = nullptr;
  bool dirty[STRIPES] = {};
  int nextStripe = 0;
};
