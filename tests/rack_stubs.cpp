/* Minimal stubs so the test binary can link without libRack.

Including plugin.hpp pulls in rack.hpp, whose component-library color
constants are initialized at static-init time by calling into NanoVG. Those
two functions are the only libRack symbols the test binary ends up needing;
without them the process segfaults before main(). Nothing under test touches
colors, so returning zeroes is fine. */
#include <nanovg.h>

extern "C" {

NVGcolor nvgRGB(unsigned char r, unsigned char g, unsigned char b) {
  return nvgRGBA(r, g, b, 255);
}

NVGcolor nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
  NVGcolor c;
  c.r = r / 255.0f;
  c.g = g / 255.0f;
  c.b = b / 255.0f;
  c.a = a / 255.0f;
  return c;
}
}
