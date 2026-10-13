// Kleiner Software-Renderer: RGB565-Framebuffer in halber Auflösung (184×224), Pixel-Schrift, Sprites.
// Plattformunabhängig, läuft gleich im Simulator und auf dem ESP32.
#pragma once
#include <cstdint>
#include <vector>

namespace hl {

constexpr int W = 184, H = 224;   // interne Auflösung, das Display zeigt sie 2× vergrößert (368×448)

struct RGB { uint8_t r, g, b; };
constexpr RGB hex(uint32_t v) { return {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }
inline uint16_t c565(RGB c) { return uint16_t(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3)); }
RGB shade(RGB c, float f);
RGB mix(RGB a, RGB b, float t);
float lum(RGB c);
float colDist(RGB a, RGB b);

// Sprite: Farben + Deckkraft (0/1) mit Fußpunkt (ax, ay) und Körpermitte (cx, cy)
struct Sprite {
  int w = 0, h = 0, ax = 0, ay = 0, cx = 0, cy = 0;
  std::vector<uint16_t> px;
  std::vector<uint8_t> a;
};

class Canvas {
 public:
  explicit Canvas(uint16_t* buf) : px(buf) {}
  uint16_t* px;

  void clear(uint16_t c);
  void fill(int x, int y, int w, int h, uint16_t c);
  void fillA(int x, int y, int w, int h, uint16_t c, int alpha);   // alpha 0..255
  void dim(int x, int y, int w, int h, int alpha);                  // abdunkeln Richtung Schwarz
  void pset(int x, int y, uint16_t c) { if (unsigned(x) < unsigned(W) && unsigned(y) < unsigned(H)) px[y * W + x] = c; }
  void blend(int x, int y, uint16_t c, int alpha);
  void line(int x0, int y0, int x1, int y1, uint16_t c);
  void disc(int cx, int cy, int r, uint16_t c);
  void ring(int cx, int cy, int r, uint16_t c);
  // Sprite am Fußpunkt (x, y) zeichnen, optional gespiegelt und ganzzahlig vergrößert
  void blit(const Sprite& s, int x, int y, bool flip = false, int scale = 1);
  // Sprite mit der Körpermitte an (x, y)
  void blitCenter(const Sprite& s, int x, int y, bool flip = false);
  void shadow(const Sprite& s, int x, int y, int alpha);   // dunkle Silhouette (Schatten)

  // Text (UTF-8, Großbuchstaben und Umlaute), 8×8-Pixelschrift
  int text(const char* s, int x, int y, uint16_t c, int scale = 1);
  void textC(const char* s, int cx, int y, uint16_t c, int scale = 1);
  void textShadowC(const char* s, int cx, int y, uint16_t c, uint16_t sh, int scale = 1);
  static int textW(const char* s, int scale = 1);
  // Große Überschrift im Hallen-Legenden-Stil: Verlauf, dunkle Kontur, Schlagschatten
  void headline(const char* s, int cx, int y, int scale, RGB top, RGB bottom, RGB shadowCol);

  // Bildschirm verschieben (Screenshake), Ränder schwarz
  void shift(int dx, int dy);
};

}  // namespace hl
