// Pixel-Figuren im Stil von Hallen-Legenden (p03_sprites.js), hier von vorn (Torwart) und von hinten (Werfer).
// Prozedural wie im Original: Gelenkpunkte je Pose, Gliedmaßen als Pinselstriche, Schattierung und dunkle Kontur.
#pragma once
#include "gfx.h"

namespace hl {

constexpr int SW = 36, SH = 56, AX = 18, AY = 53;   // Sprite-Leinwand und Fußpunkt wie im Original
constexpr int BODY_CY = 26;                          // Körpermitte über dem Fußpunkt (Drehpunkt beim Hechten)

struct Kit { RGB c1, c2, gk; };

struct Look {
  bool gk = false;
  int skin = 1;        // 0..4
  RGB hair = hex(0x4a2e18);
  int style = 0;       // Frisur 0..6
  bool beard = false;
  bool band = false;   // Schweißband
  int num = 7;
};

enum class Pose { Ready, Stretch, High, Low, Cheer, Sad, Stand, Wind, Throw };

struct Joints { int lk[2], lf[2], rk[2], rf[2], le[2], lh[2], re[2], rh[2], b[2]; };
Joints poseJoints(Pose p, int fr);

// back = Ansicht von hinten (Werfer), sonst von vorn (Torwart)
Sprite makePlayer(const Look& L, const Kit& K, Pose p, int fr, bool back);
// um die Körpermitte drehen (Grad, im Uhrzeigersinn), Pixel bleiben scharf
Sprite rotateSprite(const Sprite& s, float deg);
// Trikot-Symbol (24×24) für die Vereinswahl
Sprite jerseyIcon(RGB c1, RGB c2);
// Handball (Durchmesser in Pixeln)
void drawBall(Canvas& c, int x, int y, int r, float spin);

}  // namespace hl
