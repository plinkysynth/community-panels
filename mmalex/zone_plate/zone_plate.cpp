/*
@Name: Zone Plate
@Author: mmalex
@Documentation: https://github.com/plinkysynth/community-panels/tree/main/mmalex/zone_plate
@Tags: visuals
@Preferred Panels: all
@_artwork_multiply: true
@Description: Slowly scrolling RGB zone plates over the 16x16 LEDs.
*/
#define PANEL_PAD_COLOR BLUE

static int triangle(int v) { v>>=3; v &= 127; return v < 64? v * 2 : 256 - v * 2; }

struct zone_plate : panel_t {
  int phase = 0;
  void on_ui(int dt) override {
    phase++;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
      int dx = x * 2 - 15, dy = y * 2 - 15, z = dx * dx + dy * dy;
      set_led(x, y, LED_RGB(triangle(z * 3 + phase), 
        triangle(z * 5 + phase * 2), triangle(z * 7 + phase * 3)));
    }
  }
};
