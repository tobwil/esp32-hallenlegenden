#include "sprites.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace hl {

static const RGB OUTLINE = hex(0x0b0910);
static const RGB SKIN[5][2] = {{hex(0xf3c9a2), hex(0xd9a47c)}, {hex(0xe6b085), hex(0xc98d62)}, {hex(0xc98c5c), hex(0xa96d43)},
                               {hex(0x9a6440), hex(0x7a4a2c)}, {hex(0x6e4428), hex(0x52301b)}};
static const char* DIGITS[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
                                 "111100111001111", "111100111101111", "111001001010010", "111101111101111", "111101111001111"};

// Gelenkpunkte relativ zum Fußpunkt; l = linke Bildseite, r = rechte Bildseite. k Knie, f Fuß, e Ellbogen, h Hand, b Rumpfversatz
Joints poseJoints(Pose p, int fr) {
  switch (p) {
    case Pose::Ready:   // Torwart in Grundstellung: Knie gebeugt, Hände hoch
      return {{-6, -9}, {-7, 0}, {5, -9}, {6, 0}, {-10, -29}, {-12, -36 - fr}, {9, -29}, {11, -36 - fr}, {0, 3}};
    case Pose::Stretch:   // gestreckt zum Hechten, wird gedreht
      return {{-3, -11}, {-4, 0}, {5, -12}, {9, -5}, {-4, -41}, {-4, -49}, {4, -41}, {4, -49}, {0, 0}};
    case Pose::High:   // Sprung, Arme nach oben
      return {{-4, -14}, {-5, -6}, {4, -14}, {5, -6}, {-6, -41}, {-7, -49}, {5, -41}, {6, -49}, {0, 0}};
    case Pose::Low:   // Spagat für flache Bälle
      return {{-8, -6}, {-15, 0}, {7, -6}, {14, 0}, {-10, -22}, {-15, -17}, {9, -22}, {14, -17}, {0, 6}};
    case Pose::Cheer:
      return {{-3, -11 - fr}, {-4, -fr}, {3, -11 - fr}, {4, -fr}, {-8, -38 - fr}, {-11, -47 - 2 * fr}, {7, -38 - fr}, {10, -47 - 2 * fr}, {0, -fr}};
    case Pose::Sad:
      return {{-3, -11}, {-4, 0}, {3, -11}, {4, 0}, {-7, -24}, {-7, -17}, {6, -24}, {6, -17}, {0, 1}};
    case Pose::Stand:   // Werfer von hinten, Ball in der rechten Hand
      return {{-3, -11}, {-4, 0}, {3, -11}, {4, 0}, {-8, -25}, {-9, -19}, {9, -27}, {11, -23 - fr}, {0, fr}};
    case Pose::Wind:   // Wurfarm hinter dem Kopf
      return {{-3, -12}, {-5, 0}, {4, -10}, {6, 0}, {-9, -31}, {-11, -35}, {11, -37}, {10, -45}, {0, 0}};
    case Pose::Throw:   // Arm schnellt nach vorn
      return {{-3, -11}, {-4, 0}, {4, -13}, {5, -6}, {-8, -26}, {-9, -21}, {4, -37}, {-1, -41}, {1, 0}};
  }
  return poseJoints(Pose::Ready, 0);
}

namespace {

struct Pix { RGB c; bool on; };

struct Gen {
  std::vector<Pix> p = std::vector<Pix>(SW * SH, Pix{{0, 0, 0}, false});   // auf dem Heap: der Loop-Task hat wenig Stack
  void R(int x, int y, int w, int h, RGB c) {
    for (int yy = 0; yy < h; yy++)
      for (int xx = 0; xx < w; xx++) {
        int X = AX + x + xx, Y = AY + y + yy;
        if (X >= 0 && Y >= 0 && X < SW && Y < SH) p[Y * SW + X] = {c, true};
      }
  }
  void brush(float x1, float y1, float x2, float y2, int w, RGB c) {
    int n = int(std::fmax(std::fmax(std::fabs(x2 - x1), std::fabs(y2 - y1)), 1.f));
    for (int i = 0; i <= n; i++) {
      float k = float(i) / n;
      R(int(std::lround(x1 + (x2 - x1) * k)) - (w >> 1), int(std::lround(y1 + (y2 - y1) * k)) - (w >> 1), w, w, c);
    }
  }
  struct Seg { float x1, y1, x2, y2; int w; RGB c; };
  void limb(const Seg* s, int n, bool outline) {
    if (outline)
      for (int i = 0; i < n; i++) brush(s[i].x1, s[i].y1, s[i].x2, s[i].y2, s[i].w + 2, OUTLINE);
    for (int i = 0; i < n; i++) brush(s[i].x1, s[i].y1, s[i].x2, s[i].y2, s[i].w, s[i].c);
  }
  void digits(int n, int x, int y, RGB col) {
    char buf[8];
    int len = snprintf(buf, sizeof buf, "%d", n);
    for (int k = 0; k < len; k++) {
      const char* m = DIGITS[buf[k] - '0'];
      for (int i = 0; i < 15; i++)
        if (m[i] == '1') R(x + k * 4 + i % 3, y + i / 3, 1, 1, col);
    }
  }
};

int digitsW(int n) { return (n >= 10 ? 2 : 1) * 4 - 1; }

}  // namespace

Sprite makePlayer(const Look& L, const Kit& K, Pose pose, int fr, bool back) {
  Gen g;
  const Joints J = poseJoints(pose, fr);
  const RGB skin = SKIN[L.skin][0], skinS = SKIN[L.skin][1];
  const RGB jer = L.gk ? K.gk : K.c1, jerS = shade(jer, 0.7f), jerH = mix(jer, hex(0xffffff), 0.3f);
  const RGB trim = L.gk ? shade(K.gk, 0.5f) : K.c2, sho = L.gk ? shade(K.gk, 0.45f) : K.c2;
  const RGB sock = lum(K.c1) > 0.8f ? K.c2 : K.c1, shoe = L.gk ? hex(0xf2f2f2) : (L.num % 3 ? hex(0xf2f2f2) : K.c2);
  const RGB numCol = lum(jer) > 0.6f ? shade(trim, 0.9f) : (colDist(trim, jer) > 120 ? trim : hex(0xffffff));
  const int bx = J.b[0], by = J.b[1];
  const int hipY = -19 + by, shY = -32 + by;

  // Beine: links im Bild leicht im Schatten (Licht kommt von rechts wie im Original)
  auto leg = [&](const int* k, const int* f, bool left) {
    const float hx = bx + (left ? -3 : 3);
    const RGB sk = left ? skinS : skin, so = left ? shade(sock, 0.85f) : sock, pa = left ? shade(sho, 0.8f) : sho;
    const float mx = (k[0] + f[0]) / 2.f, my = (k[1] + f[1]) / 2.f;
    if (L.gk) {
      Gen::Seg s[2] = {{hx, float(hipY + 1), float(k[0]), float(k[1]), 4, pa}, {float(k[0]), float(k[1]), float(f[0]), float(f[1] - 2), 4, pa}};
      g.limb(s, 2, false);
    } else {
      Gen::Seg s[3] = {{hx, float(hipY + 1), float(k[0]), float(k[1]), 4, sk}, {float(k[0]), float(k[1]), mx, my, 3, sk}, {mx, my, float(f[0]), float(f[1] - 2), 3, so}};
      g.limb(s, 3, false);
      if (!back) g.R(k[0], k[1] - 1, 1, 1, mix(skin, hex(0xffffff), 0.3f));
    }
    g.R(f[0] - 2, f[1] - 3, 5, 3, shade(shoe, left ? 0.85f : 1.f));
    g.R(f[0] - 2, f[1] - 1, 5, 1, hex(0x55555c));
    g.R(left ? f[0] - 2 : f[0] + 2, f[1] - 3, 1, 2, trim);
  };
  leg(J.lk, J.lf, true);
  leg(J.rk, J.rf, false);

  // Hose
  g.R(bx - 6, hipY - 2, 12, 6, sho);
  g.R(bx - 6, hipY - 2, 2, 6, shade(sho, 0.78f));
  g.R(bx + 5, hipY - 1, 1, 4, trim);
  g.R(bx - 6, hipY - 1, 1, 4, trim);
  g.R(bx, hipY + 2, 1, 2, shade(sho, 0.6f));

  // Rumpf als Trapez: breite Schultern, schmale Taille
  const int tH = hipY - 1 - shY;
  for (int r = 0; r < tH; r++) {
    float k = float(r) / std::max(1, tH - 1);
    int l = int(std::lround(-7 + 2 * k)), rr = int(std::lround(6 - 2 * k)), y = shY + r;
    g.R(bx + l, y, rr - l + 1, 1, jer);
    g.R(bx + l, y, 2, 1, jerS);
    g.R(bx + rr, y, 1, 1, r < tH * 0.6f ? jerH : jer);
  }
  g.R(bx - 7, shY, 14, 1, trim);
  g.R(bx - 5, hipY - 2, 10, 1, shade(jer, 0.8f));
  if (!back) {   // Kragen vorn
    g.R(bx - 1, shY, 3, 2, skinS);
    g.R(bx, shY + 2, 1, 1, trim);
  }
  const int nw = digitsW(L.num);
  g.digits(L.num, bx - nw / 2, shY + (back ? 3 : 4), numCol);

  // Kopf
  const int hy = shY - 10;
  g.R(bx - 2, shY - 2, 4, 3, skinS);   // Hals
  g.R(bx - 3, hy, 6, 9, skin);
  g.R(bx - 4, hy + 1, 8, 7, skin);
  g.R(bx - 5, hy + 3, 1, 3, skinS);   // Ohren
  g.R(bx + 4, hy + 3, 1, 3, skin);
  g.R(bx - 4, hy + 1, 1, 7, skinS);
  g.R(bx - 2, hy + 8, 4, 1, skinS);
  const RGB Hc = L.hair, HL = mix(Hc, hex(0xffffff), 0.25f);
  if (!back) {
    g.R(bx - 3, hy + 4, 2, 1, hex(0xf4f0ea));
    g.R(bx + 1, hy + 4, 2, 1, hex(0xf4f0ea));
    g.R(bx - 2, hy + 4, 1, 1, hex(0x1a1010));
    g.R(bx + 1, hy + 4, 1, 1, hex(0x1a1010));
    g.R(bx - 3, hy + 3, 2, 1, shade(Hc, 0.75f));
    g.R(bx + 1, hy + 3, 2, 1, shade(Hc, 0.75f));
    g.R(bx - 1, hy + 5, 1, 2, shade(skinS, 0.9f));
    g.R(bx - 2, hy + 7, 4, 1, shade(skinS, 0.8f));
    g.R(bx + 1, hy + 1, 2, 1, mix(skin, hex(0xffffff), 0.35f));
    switch (L.style) {
      case 0: g.R(bx - 4, hy - 1, 8, 2, Hc); g.R(bx - 4, hy + 1, 1, 3, Hc); g.R(bx + 3, hy + 1, 1, 2, Hc); g.R(bx - 1, hy - 1, 3, 1, HL); break;
      case 1: g.R(bx - 3, hy - 1, 6, 1, Hc); g.R(bx - 4, hy, 8, 1, Hc); break;
      case 2: g.R(bx - 4, hy - 2, 8, 3, Hc); g.R(bx - 5, hy, 2, 8, Hc); g.R(bx + 3, hy, 2, 8, Hc); g.R(bx - 4, hy + 1, 8, 1, trim); g.R(bx - 2, hy - 2, 3, 1, HL); break;
      case 3: g.R(bx - 1, hy + 1, 2, 1, mix(skin, hex(0xffffff), 0.45f)); break;
      case 4: g.R(bx - 5, hy - 3, 10, 4, Hc); g.R(bx - 6, hy - 1, 2, 6, Hc); g.R(bx + 4, hy - 1, 2, 6, Hc); g.R(bx - 3, hy - 3, 3, 1, HL); break;
      case 6: g.R(bx - 4, hy - 2, 8, 3, Hc); g.R(bx - 5, hy, 2, 10, Hc); g.R(bx + 3, hy, 2, 10, Hc); g.R(bx - 2, hy - 2, 3, 1, HL); break;
      default: g.R(bx - 3, hy - 1, 6, 2, Hc); g.R(bx - 1, hy - 3, 2, 2, Hc); break;
    }
    if (L.beard) { g.R(bx - 3, hy + 6, 6, 3, shade(Hc, 0.9f)); g.R(bx - 1, hy + 7, 2, 1, skinS); }
  } else {
    switch (L.style) {
      case 0: g.R(bx - 4, hy - 1, 8, 7, Hc); g.R(bx - 3, hy - 1, 6, 1, HL); break;
      case 1: g.R(bx - 4, hy - 1, 8, 6, shade(Hc, 0.95f)); break;
      case 2: g.R(bx - 4, hy - 2, 8, 3, Hc); g.R(bx - 5, hy, 10, 9, Hc); g.R(bx - 4, hy + 1, 8, 1, trim); break;
      case 3: g.R(bx - 1, hy + 1, 3, 1, mix(skin, hex(0xffffff), 0.4f)); break;
      case 4: g.R(bx - 5, hy - 3, 10, 9, Hc); g.R(bx - 6, hy - 1, 12, 6, Hc); g.R(bx - 3, hy - 3, 3, 1, HL); break;
      case 6: g.R(bx - 4, hy - 2, 8, 3, Hc); g.R(bx - 5, hy, 10, 11, Hc); break;
      default: g.R(bx - 4, hy - 1, 8, 6, Hc); g.R(bx - 1, hy - 3, 2, 2, Hc); break;
    }
  }

  // Arme zuletzt, mit eigener Kontur, damit sie sich vom Rumpf absetzen
  auto arm = [&](const int* e, const int* h, bool left) {
    const float sx = bx + (left ? -6 : 6), sy = shY + 2;
    const float ex = e[0] + bx, ey = e[1] + by, hx = h[0] + bx, hy2 = h[1] + by;
    const float mx = sx + (ex - sx) * 0.45f, my = sy + (ey - sy) * 0.45f;
    const RGB sk = left ? skinS : skin, je = left ? jerS : jer;
    if (L.gk) {
      Gen::Seg s[2] = {{sx, sy, ex, ey, 3, je}, {ex, ey, hx, hy2, 3, je}};
      g.limb(s, 2, true);
      g.R(int(hx) - 2, int(hy2) - 2, 4, 4, hex(0xececec));
      g.R(int(hx) - 2, int(hy2) + 1, 4, 1, hex(0xbdbdbd));
    } else {
      Gen::Seg s[3] = {{sx, sy, ex, ey, 3, sk}, {sx, sy, mx, my, 4, je}, {ex, ey, hx, hy2, 2, sk}};
      g.limb(s, 3, true);
      g.R(int(hx) - 1, int(hy2) - 1, 3, 3, sk);
      if (L.band) g.R(int(ex + (hx - ex) * 0.7f) - 1, int(ey + (hy2 - ey) * 0.7f), 3, 1, trim);
    }
  };
  arm(J.le, J.lh, true);
  arm(J.re, J.rh, false);

  // äußere Kontur
  Sprite s;
  s.w = SW; s.h = SH; s.ax = AX; s.ay = AY; s.cx = AX; s.cy = AY - BODY_CY;
  s.px.assign(SW * SH, 0);
  s.a.assign(SW * SH, 0);
  const uint16_t oc = c565(OUTLINE);
  for (int y = 0; y < SH; y++)
    for (int x = 0; x < SW; x++) {
      int i = y * SW + x;
      if (g.p[i].on) { s.px[i] = c565(g.p[i].c); s.a[i] = 1; continue; }
      auto n = [&](int xx, int yy) { return xx >= 0 && yy >= 0 && xx < SW && yy < SH && g.p[yy * SW + xx].on; };
      if (n(x - 1, y) || n(x + 1, y) || n(x, y - 1) || n(x, y + 1)) { s.px[i] = oc; s.a[i] = 1; }
    }
  return s;
}

Sprite rotateSprite(const Sprite& s, float deg) {
  const int N = 68, C = N / 2;
  const float r = deg * 3.14159265f / 180.f, cs = std::cos(r), sn = std::sin(r);
  Sprite o;
  o.w = o.h = N;
  o.cx = o.cy = C;
  o.px.assign(N * N, 0);
  o.a.assign(N * N, 0);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float dx = x - C + 0.5f, dy = y - C + 0.5f;
      int sx = int(std::floor(s.cx + dx * cs + dy * sn)), sy = int(std::floor(s.cy - dx * sn + dy * cs));
      if (sx < 0 || sy < 0 || sx >= s.w || sy >= s.h) continue;
      int i = sy * s.w + sx;
      if (!s.a[i]) continue;
      o.px[y * N + x] = s.px[i];
      o.a[y * N + x] = 1;
    }
  float fx = s.ax - s.cx, fy = s.ay - s.cy;
  o.ax = C + int(std::lround(fx * cs - fy * sn));
  o.ay = C + int(std::lround(fx * sn + fy * cs));
  return o;
}

Sprite jerseyIcon(RGB c1, RGB c2) {
  Sprite s;
  s.w = s.h = 24; s.ax = 12; s.ay = 23; s.cx = 12; s.cy = 12;
  s.px.assign(24 * 24, 0);
  s.a.assign(24 * 24, 0);
  auto R = [&](int x, int y, int w, int h, RGB c) {
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++)
        if (xx >= 0 && yy >= 0 && xx < 24 && yy < 24) { s.px[yy * 24 + xx] = c565(c); s.a[yy * 24 + xx] = 1; }
  };
  R(3, 4, 18, 19, OUTLINE); R(0, 5, 24, 8, OUTLINE);
  R(4, 5, 16, 17, c1); R(1, 6, 5, 6, c1); R(18, 6, 5, 6, c1);
  R(4, 5, 4, 17, shade(c1, 0.8f)); R(16, 6, 1, 12, mix(c1, hex(0xffffff), 0.3f)); R(9, 5, 6, 2, c2); R(10, 7, 4, 1, shade(c1, 0.6f));
  R(1, 10, 5, 2, c2); R(18, 10, 5, 2, c2);
  const RGB nc = lum(c1) > 0.6f ? c2 : (colDist(c1, c2) > 100 ? c2 : hex(0xffffff));
  for (int k = 0; k < 2; k++) {
    const char* m = DIGITS[k == 0 ? 1 : 0];
    for (int i = 0; i < 15; i++)
      if (m[i] == '1') R(8 + k * 4 + i % 3, 11 + i / 3, 1, 1, nc);
  }
  return s;
}

void drawBall(Canvas& c, int x, int y, int r, float spin) {
  // weißer Handball mit zwei geschwungenen Nähten (blau und rot), dreht sich mit spin
  const uint16_t out = c565(OUTLINE), white = c565(hex(0xf4f4f0)), red = c565(hex(0xe8382f)), blue = c565(hex(0x2a56c6));
  c.disc(x, y, r + 1, out);
  c.disc(x, y, r, white);
  if (r <= 2) { c.pset(x + (int(spin * 2) & 1), y, blue); return; }
  const float cs = std::cos(spin), sn = std::sin(spin), k = 1.f / (1.3f * r);
  for (int yy = -r; yy <= r; yy++)
    for (int xx = -r; xx <= r; xx++) {
      if (xx * xx + yy * yy > r * r) continue;
      float u = xx * cs + yy * sn, v = -xx * sn + yy * cs;
      if (std::fabs(u - 0.35f * r + v * v * k) < 0.75f) c.pset(x + xx, y + yy, blue);
      else if (std::fabs(u + 0.35f * r - v * v * k) < 0.75f) c.pset(x + xx, y + yy, red);
    }
  c.pset(x - r / 2, y - r / 2, c565(hex(0xffffff)));
}

}  // namespace hl
