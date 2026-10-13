#include "gfx.h"

#include <cmath>
#include <cstring>

#include "font8.h"

namespace hl {

static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

RGB shade(RGB c, float f) {
  return {uint8_t(clampi(int(c.r * f + 0.5f), 0, 255)), uint8_t(clampi(int(c.g * f + 0.5f), 0, 255)),
          uint8_t(clampi(int(c.b * f + 0.5f), 0, 255))};
}
RGB mix(RGB a, RGB b, float t) {
  return {uint8_t(clampi(int(a.r + (b.r - a.r) * t + 0.5f), 0, 255)), uint8_t(clampi(int(a.g + (b.g - a.g) * t + 0.5f), 0, 255)),
          uint8_t(clampi(int(a.b + (b.b - a.b) * t + 0.5f), 0, 255))};
}
float lum(RGB c) { return (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255.f; }
float colDist(RGB a, RGB b) {
  float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return std::sqrt(dr * dr + dg * dg + db * db);
}

static inline uint16_t blend565(uint16_t d, uint16_t s, int a) {
  int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
  int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
  dr += ((sr - dr) * a) >> 8; dg += ((sg - dg) * a) >> 8; db += ((sb - db) * a) >> 8;
  return uint16_t((dr << 11) | (dg << 5) | db);
}

void Canvas::clear(uint16_t c) {
  for (int i = 0; i < W * H; i++) px[i] = c;
}

void Canvas::fill(int x, int y, int w, int h, uint16_t c) {
  int x0 = clampi(x, 0, W), x1 = clampi(x + w, 0, W), y0 = clampi(y, 0, H), y1 = clampi(y + h, 0, H);
  for (int yy = y0; yy < y1; yy++) {
    uint16_t* p = px + yy * W;
    for (int xx = x0; xx < x1; xx++) p[xx] = c;
  }
}

void Canvas::fillA(int x, int y, int w, int h, uint16_t c, int alpha) {
  int x0 = clampi(x, 0, W), x1 = clampi(x + w, 0, W), y0 = clampi(y, 0, H), y1 = clampi(y + h, 0, H);
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) px[yy * W + xx] = blend565(px[yy * W + xx], c, alpha);
}

void Canvas::dim(int x, int y, int w, int h, int alpha) { fillA(x, y, w, h, 0, alpha); }

void Canvas::blend(int x, int y, uint16_t c, int alpha) {
  if (unsigned(x) < unsigned(W) && unsigned(y) < unsigned(H)) px[y * W + x] = blend565(px[y * W + x], c, alpha);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    pset(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::disc(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r + r) pset(cx + x, cy + y, c);
}

void Canvas::ring(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      int d = x * x + y * y;
      if (d <= r * r + r && d > (r - 1) * (r - 1) + (r - 1)) pset(cx + x, cy + y, c);
    }
}

void Canvas::blit(const Sprite& s, int x, int y, bool flip, int scale) {
  for (int sy = 0; sy < s.h; sy++)
    for (int sx = 0; sx < s.w; sx++) {
      int i = sy * s.w + sx;
      if (!s.a[i]) continue;
      int dx = flip ? (s.w - 1 - sx) - (s.w - 1 - s.ax) : sx - s.ax;
      int bx = x + dx * scale, by = y + (sy - s.ay) * scale;
      if (scale == 1) pset(bx, by, s.px[i]);
      else fill(bx, by, scale, scale, s.px[i]);
    }
}

void Canvas::blitCenter(const Sprite& s, int x, int y, bool flip) {
  for (int sy = 0; sy < s.h; sy++)
    for (int sx = 0; sx < s.w; sx++) {
      int i = sy * s.w + sx;
      if (!s.a[i]) continue;
      int dx = flip ? (s.cx - sx) : sx - s.cx;
      pset(x + dx, y + sy - s.cy, s.px[i]);
    }
}

void Canvas::shadow(const Sprite& s, int x, int y, int alpha) {
  // flacher Schatten: Silhouette gestaucht auf den Boden gelegt
  for (int sy = 0; sy < s.h; sy += 3)
    for (int sx = 0; sx < s.w; sx++) {
      if (!s.a[sy * s.w + sx]) continue;
      int dy = (s.h - 1 - sy) / 6;
      blend(x + sx - s.ax + dy / 2, y + 1 + (s.ay - sy) / 12 - dy / 3 + 1, 0, alpha);
    }
}

// ---------- Schrift ----------
static uint32_t nextCp(const char*& s) {
  uint8_t c = uint8_t(*s++);
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *s) { uint32_t cp = ((c & 0x1F) << 6) | (uint8_t(*s++) & 0x3F); return cp; }
  while ((uint8_t(*s) & 0xC0) == 0x80) s++;
  return '?';
}
static const uint8_t* glyph(uint32_t cp) {
  if (cp >= 32 && cp <= 126) return font::kGlyphs[cp - 32];
  for (int i = 0; i < 7; i++)
    if (font::kExtra[i] == cp) return font::kGlyphs[95 + i];
  return font::kGlyphs['?' - 32];
}

int Canvas::textW(const char* s, int scale) {
  int n = 0;
  while (*s) { nextCp(s); n++; }
  return n * 8 * scale;
}

int Canvas::text(const char* s, int x, int y, uint16_t c, int scale) {
  int x0 = x;
  while (*s) {
    const uint8_t* g = glyph(nextCp(s));
    for (int r = 0; r < 8; r++)
      for (int b = 0; b < 8; b++)
        if (g[r] & (0x80 >> b)) {
          if (scale == 1) pset(x + b, y + r, c);
          else fill(x + b * scale, y + r * scale, scale, scale, c);
        }
    x += 8 * scale;
  }
  return x - x0;
}

void Canvas::textC(const char* s, int cx, int y, uint16_t c, int scale) { text(s, cx - textW(s, scale) / 2, y, c, scale); }

void Canvas::textShadowC(const char* s, int cx, int y, uint16_t c, uint16_t sh, int scale) {
  int x = cx - textW(s, scale) / 2;
  text(s, x + scale, y + scale, sh, scale);
  text(s, x, y, c, scale);
}

void Canvas::headline(const char* s, int cx, int y, int scale, RGB top, RGB bottom, RGB shadowCol) {
  const uint16_t outline = c565(hex(0x0b0910));
  int x0 = cx - textW(s, scale) / 2;
  const int h = 8 * scale;
  // erst Kontur und Schatten, dann der Verlauf darüber
  for (int pass = 0; pass < 3; pass++) {
    const char* p = s;
    int x = x0;
    while (*p) {
      const uint8_t* g = glyph(nextCp(p));
      for (int r = 0; r < 8; r++)
        for (int b = 0; b < 8; b++) {
          if (!(g[r] & (0x80 >> b))) continue;
          int px0 = x + b * scale, py0 = y + r * scale;
          if (pass == 0) {
            fill(px0 - 1, py0 - 1, scale + 2, scale + 2 + scale, outline);
          } else if (pass == 1) {
            fill(px0, py0 + scale, scale, scale, c565(shadowCol));
          } else {
            for (int k = 0; k < scale; k++) {
              float t = float(r * scale + k) / float(h - 1);
              RGB col = mix(top, bottom, t);
              if (r * scale + k == 1) col = mix(col, hex(0xffffff), 0.5f);   // Glanzkante oben
              fill(px0, py0 + k, scale, 1, c565(col));
            }
          }
        }
      x += 8 * scale;
    }
  }
}

void Canvas::shift(int dx, int dy) {
  if (!dx && !dy) return;
  if (dy > 0) std::memmove(px + dy * W, px, size_t(W * (H - dy)) * 2), std::memset(px, 0, size_t(W * dy) * 2);
  if (dy < 0) std::memmove(px, px - dy * W, size_t(W * (H + dy)) * 2), std::memset(px + (H + dy) * W, 0, size_t(-dy * W) * 2);
  if (dx) {
    for (int y = 0; y < H; y++) {
      uint16_t* r = px + y * W;
      if (dx > 0) { std::memmove(r + dx, r, size_t(W - dx) * 2); std::memset(r, 0, size_t(dx) * 2); }
      else { std::memmove(r, r - dx, size_t(W + dx) * 2); std::memset(r + W + dx, 0, size_t(-dx) * 2); }
    }
  }
}

}  // namespace hl
