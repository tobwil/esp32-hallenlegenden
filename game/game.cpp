#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "data.h"

namespace hl {

namespace {

// ---------- Geometrie: Blick von hinter dem Werfer aufs Tor ----------
constexpr float PXM = 28.f;                  // Pixel pro Meter in der Torebene
constexpr int GCX = 92, GLY = 150;           // Tormitte x, Torlinie y
constexpr int BAR_Y = GLY - 56;              // Unterkante Latte (2 m)
constexpr int HORIZON = BAR_Y;               // Kamera auf Lattenhöhe
constexpr int SHX = 38, SHY = 250, SHS = 2;  // Werfer: Fußpunkt (unter dem Bildrand), doppelte Größe
constexpr int KFY = 153;                     // Fußlinie des Torwarts
constexpr float BALL_R = 0.1f;

inline int gx2s(float x) { return GCX + int(std::lround(x * PXM)); }
inline int gy2s(float y) { return GLY - int(std::lround(y * PXM)); }
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float easeBack(float t) { t = clampf(t, 0, 1); const float c = 1.9f; return 1 + (c + 1) * std::pow(t - 1, 3.f) + c * std::pow(t - 1, 2.f); }
inline float segDist(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
  float t = l > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l, 0, 1) : 0;
  return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

struct Rng {
  uint32_t s;
  uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (u() >> 8) * (1.f / 16777216.f); }
  int i(int n) { return int(u() % uint32_t(n)); }
  float range(float a, float b) { return a + (b - a) * f(); }
  float gauss() { return (f() + f() + f() + f() - 2.f) * 1.22f; }
};

enum class St { Title, Team, Level, Howto, Intro, Aim, Throw, Flight, Outcome, Rival, Result };
enum class Res { None, Goal, Save, Post, Bar, Over, Wide, Time };
enum class KMode { Ready, Dive, Down, GetUp, Cheer, Sad };

struct Gesture {
  enum Type { None, Tap, Swipe } type = None;
  float x = 0, y = 0, dx = 0, dy = 0, len = 0, dur = 0, curv = 0;   // Start, Weg, Länge (px), Dauer (s), Krümmung
};

struct Particle { float x, y, vx, vy, life; uint16_t c; };
struct Fan { int16_t x, y; uint16_t shirt, skin; uint8_t ph; bool flag; };

const uint16_t C_BLACK = 0;
const char* const VOL_NAMES[4] = {"LAUT", "MITTEL", "LEISE", "AUS"};
const float VOL_LEVELS[4] = {0.8f, 0.45f, 0.2f, 0.f};
const RGB COL_BG = hex(0x07060b);

}  // namespace

struct Game::Impl {
  Synth& snd;
  Store& store;
  Game::Stats& stats;
  Rng rng;

  St st = St::Title;
  float stT = 0, t = 0, idleT = 0;
  bool demo = false, paused = false, benchMode = false;
  bool prevBoot = false;
  int vol = 3;   // Index in VOL_LEVELS, Standard: aus

  // Auswahl
  int team = 2, rival = 4, level = 0;
  Kit kitP{}, kitR{};
  Look shooter{}, keeper{};
  char shooterName[20] = "", keeperName[20] = "";

  // Sprites
  Sprite kReady[2], kHigh, kLow, kCheer[2], kSad, kStretch;
  Sprite kRot[13];   // gestreckt, 0..120° in 10°-Schritten
  Sprite sStand[2], sWind, sThrow, sCheer[2], sSad;
  std::vector<uint16_t> floor;   // vorberechneter Hallenboden (Zeilen HORIZON..H)
  std::vector<Fan> fans;

  // Steuerung
  float aimX = 0, aimY = 1.0f;
  bool tDown = false;
  std::vector<TouchSample> pts;
  Gesture queue[4];
  int nq = 0;
  float shakeCool = 0;

  // Spielstand
  int pGoals = 0, rGoals = 0, pShots = 0, rShots = 0;
  std::vector<int8_t> pHist, rHist;
  bool sudden = false, won = false;
  int fakesShot = 0, fakesSeen = 0;
  int zones[9] = {};
  float fakeAnim = 0;

  // Wurf
  float tx = 0, ty = 1, power = 0, spin = 0, flightT = 0, T = 0.3f, clock = 0;
  bool lob = false, slowmo = false;
  Res res = Res::None;
  bool innen = false;

  // Torwart
  struct Keeper {
    KMode mode = KMode::Ready;
    float x = 0, hx = 0, hy = 0.95f, sx = 0, sy = 0.95f, tx = 0, ty = 0, react = 0, t = 0, outAt = 9, out = 0, sway = 0;
    bool willOut = false, decided = false;
  } kp;

  // Ball nach dem Ziel
  float bx = 0, by = 0, bvx = 0, bvy = 0, br = 3, bLife = 0, bSpin = 0;

  // Effekte
  float shakeT = 0, shakeA = 0, netT = 9, netX = 0, netY = 0, postT = 0, excite = 0, flash = 0;
  std::vector<Particle> parts;
  char banner[24] = "";
  float bannerT = 9;
  int bannerScale = 2;
  RGB bannerTop{}, bannerBot{}, bannerSh{};
  char ticker[112] = "";
  float tickerT = 0;
  bool rivalGoal = false;

  // Demo-Bot
  float botWait = 1, botAimX = 0, botAimY = 1;
  int botAct = 0;   // 0 werfen, 1 täuschen, 2 Heber
  bool botFaked = false;

  Impl(Synth& s, Store& st_, Game::Stats& stats_, uint32_t seed) : snd(s), store(st_), stats(stats_) {
    rng.s = seed ? seed : 0xC0FFEE;
    team = clampi(store.get("team", 2), 0, N_TEAMS - 1);
    rival = (team + 4) % N_TEAMS;
    buildFloor();
    setupTeams();
    vol = clampi(store.get("vol", 3), 0, 3);
    snd.setVolume(VOL_LEVELS[vol]);
    snd.crowdLevel(0.06f);
  }
  static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

  // ---------------------------------------------------------------- Aufbau
  void setupTeams() {
    const Team& P = TEAMS[team];
    const Team& R = TEAMS[rival];
    kitP = {hex(P.c1), hex(P.c2), hex(GK_COLORS[team % 6])};
    kitR = {hex(R.c1), hex(R.c2), hex(GK_COLORS[(rival * 7 + 3) % 6])};
    if (colDist(kitR.gk, kitP.c1) < 90) kitR.gk = hex(GK_COLORS[(rival * 7 + 4) % 6]);
    shooter.gk = false; shooter.skin = rng.i(5); shooter.hair = hex(HAIR[rng.i(8)]); shooter.style = rng.i(7);
    shooter.beard = rng.f() < 0.3f; shooter.band = rng.f() < 0.4f; shooter.num = 2 + rng.i(40);
    keeper.gk = true; keeper.skin = rng.i(5); keeper.hair = hex(HAIR[rng.i(8)]); keeper.style = rng.i(7);
    keeper.beard = rng.f() < 0.35f; keeper.num = rng.f() < 0.5f ? 1 : 12 + rng.i(3) * 4;
    snprintf(shooterName, sizeof shooterName, "%s", SURNAMES[rng.i(N_SURNAMES)]);
    do snprintf(keeperName, sizeof keeperName, "%s", SURNAMES[rng.i(N_SURNAMES)]); while (!strcmp(keeperName, shooterName));
    buildSprites();
    buildFans();
  }

  void buildSprites() {
    for (int f = 0; f < 2; f++) {
      kReady[f] = makePlayer(keeper, kitR, Pose::Ready, f, false);
      kCheer[f] = makePlayer(keeper, kitR, Pose::Cheer, f * 2, false);
      sStand[f] = makePlayer(shooter, kitP, Pose::Stand, f, true);
      sCheer[f] = makePlayer(shooter, kitP, Pose::Cheer, f * 2, true);
    }
    kHigh = makePlayer(keeper, kitR, Pose::High, 0, false);
    kLow = makePlayer(keeper, kitR, Pose::Low, 0, false);
    kSad = makePlayer(keeper, kitR, Pose::Sad, 0, false);
    kStretch = makePlayer(keeper, kitR, Pose::Stretch, 0, false);
    for (int i = 0; i < 13; i++) kRot[i] = rotateSprite(kStretch, float(i * 10));
    sWind = makePlayer(shooter, kitP, Pose::Wind, 0, true);
    sThrow = makePlayer(shooter, kitP, Pose::Throw, 0, true);
    sSad = makePlayer(shooter, kitP, Pose::Sad, 0, true);
  }

  void buildFans() {
    fans.clear();
    const RGB home1 = kitP.c1, home2 = kitP.c2, away = kitR.c1;
    for (int row = 0; row < 10; row++) {
      int y = 31 + row * 5 + (row > 5 ? row - 5 : 0);
      for (int x = (row & 1) * 2 - 2; x < W + 4; x += 4 + rng.i(2)) {
        if (rng.f() < 0.06f) continue;
        float r = rng.f();
        RGB shirt = r < 0.5f ? home1 : r < 0.68f ? home2 : r < 0.76f ? away : r < 0.88f ? hex(0x2a2a33) : hex(0xd8d4cc);
        float fog = 0.42f + 0.06f * row;
        static const uint32_t skins[5] = {0xf3c9a2, 0xe6b085, 0xc98c5c, 0x9a6440, 0x6e4428};
        Fan f;
        f.x = int16_t(x + rng.i(2)); f.y = int16_t(y);
        f.shirt = c565(shade(shirt, fog));
        f.skin = c565(shade(hex(skins[rng.i(5)]), fog));
        f.ph = uint8_t(rng.i(256));
        f.flag = rng.f() < 0.025f;
        fans.push_back(f);
      }
    }
  }

  void buildFloor() {
    floor.assign(size_t(W) * (H - HORIZON), 0);
    const RGB behind = hex(0x262a3c), orange = hex(0xd68b3e), white = hex(0xf2efe6);
    for (int y = HORIZON; y < H; y++) {
      uint16_t* row = &floor[size_t(y - HORIZON) * W];
      if (y < GLY) {
        // Boden hinter dem Tor: dunkler, zur Wand hin noch dunkler, mit Dielen in Perspektive
        float d = 476.f / float(std::max(1, y - HORIZON));
        RGB c = shade(behind, 0.7f + 0.3f * float(y - HORIZON) / float(GLY - HORIZON));
        if (int(d / 0.6f) & 1) c = shade(c, 0.93f);
        for (int x = 0; x < W; x++) row[x] = c565(c);
        continue;
      }
      float dcam = 476.f / float(y - HORIZON);   // Abstand von der Kamera in Metern
      RGB base = (int(dcam / 0.35f) & 1) ? orange : shade(orange, 0.95f);
      float sc = 238.f / dcam;                   // Pixel pro Meter in dieser Zeile
      for (int x = 0; x < W; x++) {
        float wx = (x - GCX) / sc;               // Weltkoordinate quer
        RGB c = base;
        float seam = std::fmod(std::fabs(wx) + 0.3f, 0.6f);
        if (seam < 0.6f / sc * 1.2f) c = shade(c, 0.9f);
        float vig = 1.f - 0.25f * std::pow(std::fabs(x - GCX) / 92.f, 2.f);
        c = shade(c, vig);
        row[x] = c565(c);
      }
    }
    // Torlinie und Torraumlinie (4 m Torwartgrenze als kleine Marke)
    for (int x = 0; x < W; x++) floor[size_t(GLY - HORIZON) * W + x] = c565(white);
    int y4 = HORIZON + int(476.f / 4.5f);
    for (int x = GCX - 4; x <= GCX + 4; x++) floor[size_t(y4 - HORIZON) * W + x] = c565(shade(white, 0.9f));
  }

  // ---------------------------------------------------------------- Ablauf
  void go(St s) { st = s; stT = 0; }

  void cycleVolume() {
    vol = (vol + 1) % 4;
    store.set("vol", vol);
    snd.setVolume(VOL_LEVELS[vol]);
    snd.click();
  }

  void say(const char* fmt, const char* a = "", int x = 0, int y = 0) {
    snprintf(ticker, sizeof ticker, fmt, a, x, y);
    tickerT = 0;
  }

  void showBanner(const char* txt, int scale, RGB top, RGB bot, RGB sh) {
    snprintf(banner, sizeof banner, "%s", txt);
    bannerScale = scale; bannerTop = top; bannerBot = bot; bannerSh = sh; bannerT = 0;
  }

  void startMatch() {
    pGoals = rGoals = pShots = rShots = 0;
    pHist.clear(); rHist.clear();
    sudden = false; fakesSeen = 0;
    std::memset(zones, 0, sizeof zones);
    parts.clear();
    if (!demo) {
      do rival = rng.i(N_TEAMS); while (rival == team);
    }
    setupTeams();
    snd.crowdLevel(0.08f);
    snprintf(ticker, sizeof ticker, "7M-DUELL: %s GEGEN %s", TEAMS[team].k, TEAMS[rival].k);
    tickerT = 0;
    go(St::Intro);
  }

  void startDemo() {
    demo = true;
    level = 1;
    team = rng.i(N_TEAMS);
    do rival = rng.i(N_TEAMS); while (rival == team);
    startMatch();
  }

  void startShot() {
    fakesShot = 0; fakeAnim = 0; res = Res::None; innen = false; lob = false; spin = 0; slowmo = false;
    kp = Keeper();
    kp.willOut = rng.f() < LEVELS[level].pOut;
    kp.outAt = rng.range(0.5f, 1.6f);
    kp.sway = rng.range(0, 6.28f);
    bLife = 0; netT = 9;
    clock = 3.0f;
    botWait = rng.range(0.6f, 1.6f); botFaked = false;
    float r = rng.f();
    botAct = r < 0.15f ? 1 : 0;
    float cx = rng.f() < 0.5f ? -1.f : 1.f;
    botAimX = cx * rng.range(0.7f, 1.3f);
    botAimY = rng.f() < 0.5f ? rng.range(0.25f, 0.6f) : rng.range(1.4f, 1.85f);
    if (pShots < 2 && !demo) say("ZUM TOR WISCHEN - WO DU LOSLÄSST, FLIEGT ER HIN");
    else if (!demo) snprintf(ticker, sizeof ticker, "%s #%d tritt an. %d:%d", shooterName, shooter.num, pGoals, rGoals), tickerT = 0;
    snd.whistle(false);
    go(St::Aim);
  }

  int zoneOf(float x, float y) const {
    int col = x < -0.5f ? 0 : x > 0.5f ? 2 : 1, row = y < 0.67f ? 0 : y > 1.33f ? 2 : 1;
    return row * 3 + col;
  }

  // Torwart entscheidet beim Abwurf, wohin er geht
  void keeperDecide() {
    const Level& L = LEVELS[level];
    kp.decided = true;
    if (kp.mode != KMode::Ready) return;   // liegt noch von der Täuschung
    float readP = L.pRead * (1.f - 0.6f * std::fabs(spin)) * (lob ? 1.3f : 1.f);
    int total = 0;
    for (int z : zones) total += z;
    float gx, gy;
    if (rng.f() < readP) {
      gx = tx + rng.gauss() * 0.2f; gy = ty + rng.gauss() * 0.2f;
    } else if (total >= 2 && rng.f() < L.pMem) {
      int k = rng.i(total), z = 0;
      while (k >= zones[z]) k -= zones[z++];
      gx = (z % 3 - 1) * 1.0f + rng.gauss() * 0.15f;
      gy = 0.35f + (z / 3) * 0.65f + rng.gauss() * 0.15f;
    } else {
      static const float w[9] = {2, 1, 2, 1, 0.6f, 1, 2, 1, 2};
      float sum = 0;
      for (float v : w) sum += v;
      float k = rng.f() * sum;
      int z = 0;
      while (z < 8 && k >= w[z]) k -= w[z++];
      gx = (z % 3 - 1) * 1.0f; gy = 0.35f + (z / 3) * 0.65f;
    }
    diveTo(gx, gy, L.react * (lob ? 1.6f : 1.f));
  }

  void diveTo(float gx, float gy, float react) {
    kp.mode = KMode::Dive;
    kp.sx = kp.x; kp.sy = 0.95f;
    kp.hx = kp.sx; kp.hy = kp.sy;
    kp.tx = clampf(gx, -1.75f, 1.75f); kp.ty = clampf(gy, 0.1f, 2.0f);
    kp.react = react; kp.t = 0;
  }

  void updateKeeper(float dt) {
    const Level& L = LEVELS[level];
    kp.t += dt;
    switch (kp.mode) {
      case KMode::Ready: {
        kp.sway += dt * 1.7f;
        kp.x = 0.12f * std::sin(kp.sway);
        bool aiming = st == St::Aim || st == St::Throw || st == St::Flight;
        if (kp.willOut && aiming && stT > kp.outAt) kp.out = std::min(1.f, kp.out + dt * 3.f);
        break;
      }
      case KMode::Dive: {
        if (kp.t < kp.react) break;
        float dx = kp.tx - kp.hx, dy = kp.ty - kp.hy, d = std::hypot(dx, dy);
        float step = L.speed * dt;
        if (d > step) { kp.hx += dx / d * step; kp.hy += dy / d * step; }
        else { kp.hx = kp.tx; kp.hy = kp.ty; }
        float rx = kp.hx - kp.sx, ry = kp.hy - kp.sy, rd = std::hypot(rx, ry);
        if (rd > L.reach) { kp.hx = kp.sx + rx / rd * L.reach; kp.hy = kp.sy + ry / rd * L.reach; }
        if ((d <= step || rd > L.reach || kp.t > kp.react + 0.55f) && st != St::Flight) {
          // nach dem Hechtsprung liegt er kurz am Boden (nur wenn kein Ball unterwegs ist)
          if (std::fabs(kp.hx - kp.sx) > 0.5f || kp.hy < 0.5f) { kp.mode = KMode::Down; kp.t = 0; }
          else { kp.mode = KMode::GetUp; kp.t = 0; }
        }
        break;
      }
      case KMode::Down:
        if (kp.t > 0.75f) { kp.mode = KMode::GetUp; kp.t = 0; }
        break;
      case KMode::GetUp:
        kp.x = lerpf(kp.hx > kp.sx ? kp.sx + 0.4f : kp.sx - 0.4f, 0, clampf(kp.t / 0.5f, 0, 1));
        if (std::fabs(kp.hx - kp.sx) < 0.5f) kp.x = 0;
        if (kp.t > 0.5f) { kp.mode = KMode::Ready; kp.t = 0; kp.x = 0; }
        break;
      default: break;
    }
  }

  // Hält der Torwart den Ball am Punkt (x, y)?
  bool keeperSaves(float x, float y) {
    const float outB = kp.out * 0.1f;
    switch (kp.mode) {
      case KMode::Ready: {
        if (lob && kp.out > 0.5f && y > 1.45f) return false;   // Heber über den herausgekommenen Torwart
        if (segDist(x, y, kp.x, 0, kp.x, 1.85f) < 0.27f + outB) return true;
        if (std::hypot(x - (kp.x - 0.42f), y - 1.5f) < 0.27f + outB) return true;
        if (std::hypot(x - (kp.x + 0.42f), y - 1.5f) < 0.27f + outB) return true;
        return false;
      }
      case KMode::Dive: {
        if (lob && kp.out > 0.5f && y > 1.45f) return false;
        if (kp.t < kp.react) return segDist(x, y, kp.x, 0, kp.x, 1.85f) < 0.25f;
        float dx = kp.hx - kp.sx, dy = kp.hy - kp.sy, d = std::hypot(dx, dy);
        float ux = d > 0.01f ? dx / d : 0, uy = d > 0.01f ? dy / d : 1;
        if (std::hypot(x - kp.hx, y - kp.hy) < 0.3f + outB) return true;
        return segDist(x, y, kp.hx, kp.hy, kp.hx - ux * 1.7f, std::max(0.f, kp.hy - uy * 1.7f)) < 0.22f + outB;
      }
      case KMode::Down: {
        float side = kp.hx > kp.sx ? 1.f : -1.f;
        return segDist(x, y, kp.hx, 0.25f, kp.hx - side * 1.7f, 0.25f) < 0.3f;
      }
      case KMode::GetUp:
        return segDist(x, y, kp.x, 0, kp.x, 1.3f) < 0.25f;
      default: return false;
    }
  }

  void doFake() {
    fakesShot++;
    fakeAnim = 0.28f;
    snd.tick();
    const Level& L = LEVELS[level];
    float bite = L.pBite * std::pow(0.6f, float(fakesSeen));
    fakesSeen++;
    if (kp.mode == KMode::Ready && rng.f() < bite) {
      float side = rng.f() < 0.5f ? -1.f : 1.f;
      diveTo(side * 1.2f, rng.f() < 0.5f ? 0.4f : 1.6f, 0.08f);
      say("%s fällt auf die Täuschung rein!", keeperName);
    }
  }

  // Zielpunkt in der Torebene aus einer Bildschirmposition; endet die Geste unter der Torlinie,
  // wird die Wischrichtung bis auf Hüfthöhe verlängert
  void aimFromSwipe(float x0, float y0, float x1, float y1, float& ax, float& ay) {
    if (y1 > GLY - 4 && y1 < y0 - 4) {
      const float yt = float(gy2s(1.0f));
      x1 = x0 + (x1 - x0) * (y0 - yt) / (y0 - y1);
      y1 = yt;
    }
    ax = clampf((x1 - GCX) / PXM, -1.95f, 1.95f);
    ay = clampf((GLY - y1) / PXM, 0.05f, 2.4f);
  }

  void doThrow(const Gesture& g) {
    float ax, ay;
    aimFromSwipe(g.x, g.y, g.x + g.dx, g.y + g.dy, ax, ay);
    const float speed = g.len / std::max(g.dur, 0.03f);   // px/s
    lob = speed < 200.f && g.dur > 0.3f;                    // ganz langsam gewischt: Heber
    power = clampf((speed - 200.f) / 600.f, 0.05f, 1.f);
    spin = (!lob && std::fabs(g.curv) > 0.1f) ? clampf(g.curv * 3.5f, -1, 1) : 0;
    float sigma = 0.06f + std::fabs(spin) * 0.08f + (speed > 1400.f ? 0.25f : 0.f);
    tx = ax + rng.gauss() * sigma + spin * 0.12f;
    ty = clampf(ay + rng.gauss() * sigma, 0.12f, 3.0f);
    float v = lob ? 8.5f : 12.f + 18.f * power;
    T = 7.5f / v;
    flightT = 0;
    stats.shots++;
    bool decisive = sudden || pShots == 4;
    slowmo = decisive && !benchMode;
    go(St::Throw);
  }

  void resolve() {
    const float ax = std::fabs(tx);
    const bool inside = ax < 1.5f - BALL_R && ty < 2.0f - BALL_R;
    const bool postHit = !inside && ax >= 1.5f - BALL_R - 0.04f && ax <= 1.5f + BALL_R + 0.04f && ty <= 2.0f + BALL_R;
    const bool barHit = !inside && !postHit && ty >= 2.0f - BALL_R - 0.04f && ty <= 2.0f + BALL_R + 0.04f && ax <= 1.5f + BALL_R;
    zones[zoneOf(clampf(tx, -1.5f, 1.5f), clampf(ty, 0, 2))]++;
    if ((inside || postHit || barHit) && keeperSaves(tx, ty)) res = Res::Save;
    else if (inside) res = Res::Goal;
    else if (postHit || barHit) {
      if (rng.f() < 0.25f) { res = Res::Goal; innen = true; }
      else res = postHit ? Res::Post : Res::Bar;
    } else res = ty > 2.0f ? Res::Over : Res::Wide;
    finishShot();
  }

  void finishShot() {
    const bool goal = res == Res::Goal;
    pShots++;
    pHist.push_back(goal ? 1 : 0);
    if (goal) pGoals++;
    if (goal) stats.goals++;
    else if (res == Res::Save) stats.saves++;
    else stats.misses++;

    const int sx = gx2s(tx), sy = gy2s(ty);
    bx = float(sx); by = float(sy); br = 3; bLife = 1.2f;
    const RGB gold = hex(0xffd23f), orange = hex(0xf08a24), red = hex(0xe8382f), dred = hex(0x7a1410);
    switch (res) {
      case Res::Goal:
        bvx = 0; bvy = 0;
        netT = 0; netX = float(sx); netY = float(sy);
        snd.net(); snd.cheer(1.f);
        excite = 1.f; flash = 0.25f;
        confetti(sx, sy, 50);
        showBanner(innen ? "INNENPFOSTEN!" : "TOR!", innen ? 1 : 3, gold, orange, hex(0x8a3a00));
        if (innen) bannerScale = 2;
        if (std::strlen(banner) * 8 * bannerScale > 180) bannerScale = 1;
        snprintf(ticker, sizeof ticker, "Tor durch %s! Es steht %d:%d.", shooterName, pGoals, rGoals); tickerT = 0;
        kp.mode = kp.mode == KMode::Dive ? KMode::Down : KMode::Sad; kp.t = 0;
        if (kp.mode == KMode::Down) kp.t = -1.f;
        break;
      case Res::Save:
        bvx = rng.range(-70, 70); bvy = rng.range(-60, -20); br = 3;
        snd.save(); snd.groan();
        showBanner("PARADE!", 2, hex(0x9ff0ff), hex(0x2a8ad8), hex(0x0a2a5a));
        snprintf(ticker, sizeof ticker, "%s hält gegen %s!", keeperName, shooterName); tickerT = 0;
        excite = 0.25f;
        break;
      case Res::Post: case Res::Bar:
        bvx = (tx > 0 ? -1 : 1) * rng.range(20, 60); bvy = res == Res::Bar ? 30 : rng.range(-40, 10);
        snd.post(); snd.groan();
        shakeT = 0.35f; shakeA = 3; postT = 0.4f;
        showBanner(res == Res::Post ? "PFOSTEN!" : "LATTE!", 2, hex(0xffffff), hex(0xb8b8c8), hex(0x303040));
        snprintf(ticker, sizeof ticker, "%s! %s trifft nur Aluminium.", res == Res::Post ? "Pfosten" : "Latte", shooterName); tickerT = 0;
        break;
      case Res::Over: case Res::Wide:
        bvx = (tx > 0 ? 1 : -1) * 30.f; bvy = res == Res::Over ? -70.f : -10.f;
        snd.groan();
        showBanner(res == Res::Over ? "DRÜBER!" : "VORBEI!", 2, red, dred, hex(0x2a0505));
        snprintf(ticker, sizeof ticker, "%s setzt den Ball %s.", shooterName, res == Res::Over ? "drüber" : "daneben"); tickerT = 0;
        break;
      case Res::Time:
        bLife = 0;
        snd.whistle(true);
        showBanner("ZEIT!", 2, red, dred, hex(0x2a0505));
        snprintf(ticker, sizeof ticker, "Zu lange gezögert, %s!", shooterName); tickerT = 0;
        break;
      default: break;
    }
    if (res != Res::Goal && res != Res::Time && kp.mode == KMode::Ready) { kp.mode = KMode::Cheer; kp.t = 0; }
    if (res == Res::Save && kp.mode == KMode::Dive) { kp.t = 99; }
    go(St::Outcome);
  }

  bool decided() {
    if (!sudden) {
      int remP = 5 - pShots, remR = 5 - rShots;
      if (pGoals > rGoals + remR) { won = true; return true; }
      if (rGoals > pGoals + remP) { won = false; return true; }
      if (pShots == 5 && rShots == 5) sudden = true;
      return false;
    }
    if (pShots == rShots && pGoals != rGoals) { won = pGoals > rGoals; return true; }
    return false;
  }

  void endMatch() {
    char k[16];
    snprintf(k, sizeof k, "games%d", level);
    if (!demo) store.set(k, store.get(k, 0) + 1);
    if (won) {
      snd.fanfare(); snd.cheer(1.2f);
      confetti(GCX, 60, 90);
      excite = 1;
      if (!demo) {
        snprintf(k, sizeof k, "wins%d", level);
        store.set(k, store.get(k, 0) + 1);
        int s = store.get("streak", 0) + 1;
        store.set("streak", s);
        if (s > store.get("best", 0)) store.set("best", s);
      }
    } else {
      snd.sadTune(); snd.groan();
      if (!demo) store.set("streak", 0);
    }
    snd.crowdLevel(0.06f);
    go(St::Result);
  }

  void confetti(int x, int y, int n) {
    const uint16_t cols[4] = {c565(kitP.c1), c565(kitP.c2), c565(hex(0xffd23f)), c565(hex(0xffffff))};
    for (int i = 0; i < n && parts.size() < 220; i++) {
      float a = rng.range(-3.0f, -0.15f), v = rng.range(40, 140);
      parts.push_back({float(x), float(y), std::cos(a) * v, std::sin(a) * v, rng.range(1.2f, 2.4f), cols[rng.i(4)]});
    }
  }

  // ---------------------------------------------------------------- Eingabe
  void handleTouch(const Input& in) {
    for (int i = 0; i < in.nTouch; i++) {
      const TouchSample& s = in.touch[i];
      if (s.down && !tDown) {
        tDown = true; pts.clear(); pts.push_back(s);
        idleT = 0;
      } else if (s.down && tDown) {
        if (pts.size() < 96) pts.push_back(s);
        else pts.back() = s;
        aimFromSwipe(pts.front().x, pts.front().y, s.x, s.y, aimX, aimY);
      } else if (!s.down && tDown) {
        tDown = false;
        pts.push_back({false, pts.back().x, pts.back().y, s.ms});
        Gesture g = recognize();
        if (g.type != Gesture::None && nq < 4) queue[nq++] = g;
      }
    }
  }

  char dbg[96] = "";
  Gesture recognize() {
    Gesture g;
    const TouchSample& a = pts.front();
    const TouchSample& b = pts.back();
    g.x = a.x; g.y = a.y;
    g.dx = float(b.x - a.x); g.dy = float(b.y - a.y);
    g.len = std::hypot(g.dx, g.dy);
    g.dur = float(b.ms - a.ms) / 1000.f;
    if (g.len < 10.f) {
      g.type = g.dur < 0.45f ? Gesture::Tap : Gesture::None;
      snprintf(dbg, sizeof dbg, "st=%d tap=%d at %.0f,%.0f dur=%.2f n=%d", int(st), g.type == Gesture::Tap, g.x, g.y, g.dur, int(pts.size()));
      return g;
    }
    g.type = Gesture::Swipe;
    float best = 0;
    for (const auto& p : pts) {
      float cr = (g.dx * float(p.y - a.y) - g.dy * float(p.x - a.x)) / g.len;   // Abstand zur Sehne, mit Vorzeichen
      if (std::fabs(cr) > std::fabs(best)) best = cr;
    }
    g.curv = -best / g.len;   // > 0: Bogen nach rechts
    snprintf(dbg, sizeof dbg, "st=%d swipe len=%.0f dx=%.0f dy=%.0f dur=%.2f n=%d", int(st), g.len, g.dx, g.dy, g.dur, int(pts.size()));
    return g;
  }

  bool popGesture(Gesture& g) {
    if (!nq) return false;
    g = queue[0];
    for (int i = 1; i < nq; i++) queue[i - 1] = queue[i];
    nq--;
    return true;
  }

  void bot() {
    if (st != St::Aim || nq >= 3) return;
    if (stT < botWait) return;
    if (botAct == 1 && !botFaked) {
      Gesture g; g.type = Gesture::Tap; queue[nq++] = g; botFaked = true;
      botWait = stT + rng.range(0.15f, 0.6f);
      botAimX = -botAimX;
      return;
    }
    Gesture g; g.type = Gesture::Swipe;
    const bool lobIt = kp.out > 0.6f && rng.f() < 0.7f;
    if (lobIt) botAimY = 1.7f;
    g.x = 64; g.y = 200;
    g.dx = gx2s(botAimX) - g.x; g.dy = gy2s(botAimY) - g.y;
    g.len = std::hypot(g.dx, g.dy);
    g.dur = lobIt ? g.len / 150.f : g.len / rng.range(550.f, 1000.f);
    g.curv = (!lobIt && rng.f() < 0.2f) ? rng.range(-0.2f, 0.2f) : 0;
    aimX = botAimX; aimY = botAimY;
    queue[nq++] = g;
  }

  // ---------------------------------------------------------------- Takt
  void tick(const Input& in) {
    float dt = DT;
    t += dt; idleT += dt;
    if (in.nTouch) idleT = 0;

    // Demo verlassen bei Berührung
    if (demo && !benchMode && in.nTouch) {
      for (int i = 0; i < in.nTouch; i++)
        if (in.touch[i].down) { demo = false; tDown = false; nq = 0; snd.crowdLevel(0.06f); go(St::Title); idleT = 0; return; }
    }
    if (!demo) handleTouch(in);

    // BOOT-Taste: Pause im Spiel, im Menü zurück zum Titel
    if (in.boot && !prevBoot) {
      idleT = 0;
      if (st >= St::Intro && st <= St::Rival && !demo) { paused = !paused; snd.click(); }
      else if (st == St::Title) cycleVolume();
      else { paused = false; demo = false; go(St::Title); }
    }
    prevBoot = in.boot;
    if (paused) {
      Gesture g;
      while (popGesture(g))
        if (g.type == Gesture::Tap) {
          if (g.y >= 146 && g.y < 170) cycleVolume();
          else { paused = false; snd.click(); }
        }
      return;
    }

    // Schütteln zum Jubeln
    float mag = std::sqrt(in.ax * in.ax + in.ay * in.ay + in.az * in.az);
    shakeCool -= dt;
    if (!demo && mag > 1.9f && shakeCool <= 0 && ((st == St::Outcome && res == Res::Goal) || (st == St::Result && won))) {
      confetti(rng.i(W), 40, 25); snd.cheer(0.6f); excite = 1; shakeCool = 0.35f;
    }

    if (demo) bot();
    stT += dt;
    Gesture g;

    switch (st) {
      case St::Title:
        while (popGesture(g))
          if (g.type == Gesture::Tap) {
            if (g.y >= 182 && g.y < 206) cycleVolume();
            else { snd.click(); go(St::Team); }
          }
        updateKeeper(dt);
        if (idleT > 15.f && !benchMode) startDemo();
        break;
      case St::Team:
        while (popGesture(g)) {
          if (g.type == Gesture::Swipe && std::fabs(g.dx) > std::fabs(g.dy)) { team = (team + (g.dx < 0 ? 1 : N_TEAMS - 1)) % N_TEAMS; snd.tick(); }
          else if (g.type == Gesture::Tap) {
            if (g.x < 46) { team = (team + N_TEAMS - 1) % N_TEAMS; snd.tick(); }
            else if (g.x > W - 46) { team = (team + 1) % N_TEAMS; snd.tick(); }
            else { store.set("team", team); snd.click(); go(St::Level); }
          }
        }
        break;
      case St::Level:
        while (popGesture(g))
          if (g.type == Gesture::Tap)
            for (int i = 0; i < 3; i++)
              if (g.y >= 62 + i * 48 && g.y < 62 + i * 48 + 42) { level = i; snd.click(); go(St::Howto); }
        break;
      case St::Howto:
        while (popGesture(g))
          if (g.type == Gesture::Tap && stT > 0.4f) { snd.click(); startMatch(); }
        break;
      case St::Intro:
        updateKeeper(dt);
        if (stT > 1.8f) startShot();
        break;
      case St::Aim: {
        updateKeeper(dt);
        if (fakeAnim > 0) fakeAnim -= dt;
        clock -= dt;
        while (popGesture(g)) {
          if (g.type == Gesture::Tap && fakeAnim <= 0) doFake();
          else if (g.type == Gesture::Swipe && g.dy < -15) { doThrow(g); break; }
        }
        if (st == St::Aim && clock <= 0) { res = Res::Time; stats.shots++; zones[4] += 0; finishShot(); }
        break;
      }
      case St::Throw:
        updateKeeper(dt);
        if (stT > 0.1f) {
          snd.throwThud(power);
          keeperDecide();
          go(St::Flight);
        }
        break;
      case St::Flight: {
        float k = flightT / T;
        float sdt = (slowmo && k > 0.35f) ? dt * 0.4f : dt;
        flightT += sdt;
        updateKeeper(sdt);
        if (flightT >= T) resolve();
        break;
      }
      case St::Outcome:
        updateKeeper(dt);
        if (stT > 1.9f) {
          if (decided()) { endMatch(); break; }
          go(St::Rival);
          rivalGoal = rng.f() < LEVELS[level].rivalConv;
        }
        break;
      case St::Rival:
        if (stT > 0.8f && stT - dt <= 0.8f) {
          rShots++;
          rHist.push_back(rivalGoal ? 1 : 0);
          if (rivalGoal) { rGoals++; snd.groan(); snd.net(); }
          else { snd.save(); snd.cheer(0.6f); excite = 0.6f; }
        }
        if (stT > 1.9f) {
          if (decided()) endMatch();
          else startShot();
        }
        break;
      case St::Result:
        while (popGesture(g))
          if (g.type == Gesture::Tap && stT > 1.0f) { snd.click(); startMatch(); }
        if (demo && stT > 4.f) startMatch();
        break;
    }

    // Effekte
    excite = std::max(0.f, excite - dt * 0.35f);
    if (shakeT > 0) shakeT -= dt;
    if (postT > 0) postT -= dt;
    if (flash > 0) flash -= dt;
    netT += dt; bannerT += dt; tickerT += dt;
    if (bLife > 0) {
      bLife -= dt;
      if (res == Res::Goal) { br = std::max(2.f, br - dt * 2); }
      else { bvy += 160 * dt; bx += bvx * dt; by += bvy * dt; bSpin += dt * 10; }
    }
    for (auto& p : parts) { p.vy += 60 * dt; p.vx *= 0.985f; p.x += p.vx * dt + std::sin(p.life * 9) * 0.3f; p.y += p.vy * dt; p.life -= dt; }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0 || p.y > H; }), parts.end());
  }

  // ---------------------------------------------------------------- Zeichnen
  void drawStands(Canvas& c) {
    for (int y = 21; y < HORIZON - 8; y++) c.fill(0, y, W, 1, c565(mix(hex(0x0e0b14), hex(0x1c1826), float(y - 21) / 60.f)));
    // Hallenlicht oben
    for (int i = 0; i < 6; i++) c.fill(10 + i * 32, 22, 10, 1, c565(hex(0x9a96a0)));
    const float ex = excite;
    for (const auto& f : fans) {
      int jump = 0;
      if (ex > 0.05f) jump = int(ex * 2.5f * std::fabs(std::sin(t * 11.f + f.ph)));
      int y = f.y - jump;
      c.fill(f.x - 1, y + 2, 4, 3, f.shirt);
      c.fill(f.x, y, 2, 2, f.skin);
      if (ex > 0.5f && (f.ph & 3) == 0) { c.pset(f.x - 1, y - 1, f.skin); c.pset(f.x + 2, y - 1, f.skin); }
      if (f.flag) c.fill(f.x - 2, y - 3 + (int(t * 3 + f.ph) & 1), 7, 2, c565(shade(kitP.c1, 0.9f)));
    }
    // LED-Bande
    const int ay = HORIZON - 8;
    c.fill(0, ay, W, 8, c565(hex(0x1c080c)));
    const int tw = Canvas::textW(ADS);
    int off = int(t * 18.f) % tw;
    const uint16_t led = c565(hex(0xd8322a));
    c.text(ADS, -off, ay, led);
    c.text(ADS, tw - off, ay, led);
  }

  void drawNet(Canvas& c) {
    const uint16_t nc = c565(hex(0xd0d0d8));
    const int x0 = 56, x1 = 128, y0 = BAR_Y + 4, y1 = GLY;
    float amp = 0;
    if (netT < 1.2f) amp = 5.f * std::exp(-netT * 3.f) * (0.6f + 0.4f * std::cos(netT * 26.f));
    auto put = [&](int x, int y, int a) {
      if (amp > 0.1f) {
        float dx = x - netX, dy = y - netY, d2 = dx * dx + dy * dy;
        float k = amp * std::exp(-d2 / 260.f);
        float d = std::sqrt(d2) + 0.01f;
        x += int(std::lround(dx / d * k * 0.5f));
        y += int(std::lround(dy / d * k * 0.5f + k * 0.4f));
      }
      c.blend(x, y, nc, a);
    };
    for (int x = x0; x <= x1; x += 4)
      for (int y = y0; y <= y1; y++) put(x, y, 90);
    for (int y = y0; y <= y1; y += 4)
      for (int x = x0; x <= x1; x++) put(x, y, 90);
    // Seitennetze und Dach
    for (int k = 0; k < 3; k++) {
      int xl = 51 + k * 2, xr = 133 - k * 2;
      for (int y = BAR_Y + 1 + k; y <= y1; y += 1) { c.blend(xl, y, nc, 50); c.blend(xr, y, nc, 50); }
    }
    for (int x = 51; x <= 133; x += 3) c.blend(x, BAR_Y + 2, nc, 60);
  }

  void drawFrame(Canvas& c) {
    const uint16_t red = c565(hex(0xe8382f)), white = c565(hex(0xf4f4f0)), out = c565(hex(0x0b0910));
    int wob = postT > 0 ? ((int(t * 60) & 1) ? 1 : -1) : 0;
    auto post = [&](int x) {
      c.fill(x - 1 + wob, BAR_Y - 4, 5, GLY - BAR_Y + 5, out);
      for (int y = BAR_Y - 3; y <= GLY; y++) c.fill(x + wob, y, 3, 1, ((y - BAR_Y) / 6) & 1 ? white : red);
    };
    post(GCX - 45);
    post(GCX + 43);
    c.fill(GCX - 46 + wob, BAR_Y - 4, 93, 5, out);
    for (int x = GCX - 45; x <= GCX + 45; x++) c.fill(x + wob, BAR_Y - 3, 1, 3, ((x - GCX + 45) / 6) & 1 ? white : red);
    // Pfostenschatten auf dem Boden
    c.fillA(GCX - 46, GLY + 1, 6, 2, 0, 90);
    c.fillA(GCX + 42, GLY + 1, 6, 2, 0, 90);
  }

  void drawKeeper(Canvas& c) {
    const int oy = int(kp.out * 5.f);
    auto shadowAt = [&](int x, int w) { c.fillA(x - w / 2, KFY + oy - 1, w, 3, 0, 100); };
    switch (kp.mode) {
      case KMode::Ready: {
        int x = gx2s(kp.x);
        shadowAt(x, 16);
        c.blit(kReady[int(t * 2.5f) & 1], x, KFY + oy);
        break;
      }
      case KMode::Cheer: {
        int x = gx2s(kp.x);
        shadowAt(x, 14);
        c.blit(kCheer[int(t * 6) & 1], x, KFY + oy - (int(t * 6) & 1));
        break;
      }
      case KMode::Sad: {
        int x = gx2s(kp.x);
        shadowAt(x, 14);
        c.blit(kSad, x, KFY + oy);
        break;
      }
      case KMode::GetUp: {
        int x = gx2s(kp.x);
        shadowAt(x, 16);
        c.blit(kp.t < 0.25f ? kLow : kReady[0], x, KFY + oy);
        break;
      }
      case KMode::Dive: case KMode::Down: {
        if (kp.mode == KMode::Dive && kp.t < kp.react) { int x = gx2s(kp.x); shadowAt(x, 16); c.blit(kReady[0], x, KFY + oy); break; }
        float dx = kp.hx - kp.sx, dy = kp.hy - kp.sy;
        float d = std::hypot(dx, dy);
        if (kp.mode == KMode::Dive && d < 0.35f) { int x = gx2s(kp.x); c.blit(kReady[0], x, KFY + oy); break; }
        float ang = std::atan2(dx, dy) * 57.2958f;   // 0 = nach oben, 90 = nach rechts
        if (kp.mode == KMode::Down) ang = dx >= 0 ? 95.f : -95.f;
        float ux = std::sin(ang / 57.2958f), uy = std::cos(ang / 57.2958f);
        float hx = kp.hx, hy = kp.mode == KMode::Down ? 0.2f - uy * 0.82f : kp.hy;
        if (kp.mode == KMode::Dive && std::fabs(ang) < 22.f && hy > 1.3f) {
          int lift = int((hy - 1.75f) * PXM);
          shadowAt(gx2s(hx), 12);
          c.blit(kHigh, gx2s(hx), KFY + oy - std::max(0, lift));
          break;
        }
        if (kp.mode == KMode::Dive && std::fabs(ang) > 150.f) {
          shadowAt(gx2s(hx), 22);
          c.blit(kLow, gx2s(hx), KFY + oy);
          break;
        }
        float a = clampf(std::fabs(ang), 0, 120);
        int idx = int(std::lround(a / 10.f));
        float cx = hx - ux * 0.82f, cy = hy - uy * 0.82f;
        int sxp = gx2s(cx), syp = gy2s(cy) + oy;
        shadowAt(sxp, 28);
        c.blitCenter(kRot[idx], sxp, syp, ang < 0);
        break;
      }
    }
  }

  void handPos(Pose p, int fr, int& x, int& y) {
    Joints j = poseJoints(p, fr);
    x = SHX + j.rh[0] * SHS;
    y = SHY + j.rh[1] * SHS;
  }

  void drawShooter(Canvas& c) {
    const Sprite* s = &sStand[int(t * 2) & 1];
    Pose hp = Pose::Stand;
    int fr = int(t * 2) & 1;
    bool ball = false;
    if (st == St::Aim || st == St::Intro) {
      ball = true;
      if (fakeAnim > 0) { s = &sWind; hp = Pose::Wind; fr = 0; }
    } else if (st == St::Throw) { s = &sWind; hp = Pose::Wind; ball = true; fr = 0; }
    else if (st == St::Flight) s = &sThrow;
    else if (st == St::Outcome) s = res == Res::Goal ? &sCheer[int(t * 6) & 1] : &sSad;
    c.blit(*s, SHX, SHY, false, SHS);
    if (ball) {
      int x, y;
      handPos(hp, fr, x, y);
      drawBall(c, x, y - 2, 5, 0);
    }
  }

  void ballScreen(float s, float& x, float& y, float& r) {
    int hx, hy;
    handPos(Pose::Wind, 0, hx, hy);
    float e = 1.f - std::pow(1.f - s, 1.7f);
    float ex = float(gx2s(tx)), ey = float(gy2s(ty));
    x = lerpf(float(hx), ex, e) + spin * 22.f * std::sin(3.14159f * s);
    y = lerpf(float(hy - 2), ey, e) - (lob ? 46.f * std::sin(3.14159f * s) : 0.f);
    r = lerpf(5.f, 2.6f, e);
  }

  void drawBallFlight(Canvas& c) {
    if (st == St::Flight) {
      float s = clampf(flightT / T, 0, 1), x, y, r;
      ballScreen(s, x, y, r);
      float e = 1.f - std::pow(1.f - s, 1.7f);
      int shy = int(lerpf(222.f, float(GLY + 2), e));
      c.fillA(int(x) - int(r), shy, int(r) * 2 + 1, 2, 0, 110);
      drawBall(c, int(x), int(y), int(std::lround(r)), flightT * 25.f);
    } else if (st == St::Outcome && bLife > 0 && res != Res::Time) {
      drawBall(c, int(bx), int(by), int(std::lround(br)), bSpin);
    }
  }

  void drawCrosshair(Canvas& c) {
    if (st != St::Aim) return;
    if (!tDown || pts.size() < 2) {
      // Hinweis: Pfeil über dem Ball, solange nicht gewischt wird
      if (int(t * 2) & 1) {
        int hx, hy;
        handPos(Pose::Stand, 0, hx, hy);
        const uint16_t col = c565(hex(0xffd23f));
        for (int k = 0; k < 4; k++) c.fill(hx - k, hy - 14 + k, 2 * k + 1, 1, col);
        c.fill(hx, hy - 10, 1, 4, col);
      }
      return;
    }
    int sx = gx2s(aimX), sy = gy2s(aimY);
    const uint16_t col = c565(hex(0xffffff));
    int a = 150 + int(100 * std::sin(t * 8));
    for (int k = 3; k <= 6; k++) { c.blend(sx - k, sy, col, a); c.blend(sx + k, sy, col, a); c.blend(sx, sy - k, col, a); c.blend(sx, sy + k, col, a); }
    c.ring(sx, sy, 4, col);
    c.pset(sx, sy, col);
    for (size_t i = 1; i < pts.size(); i++) c.line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, c565(hex(0xfff3c0)));
  }

  // Die Display-Ecken sind rund und das Gehäuse deckt den Rand ab: Text bleibt im sicheren Bereich
  static constexpr int SAFE = 16;

  void drawHud(Canvas& c) {
    c.fill(0, 0, W, 25, c565(COL_BG));
    const Team& P = TEAMS[team];
    const Team& R = TEAMS[rival];
    const uint16_t txt = c565(hex(0xd8d0b8));
    c.fill(SAFE + 2, 5, 2, 8, c565(kitP.c1));
    c.text(P.k, SAFE + 6, 5, txt);
    c.fill(W - SAFE - 4, 5, 2, 8, c565(kitR.c1));
    c.text(R.k, W - SAFE - 6 - Canvas::textW(R.k), 5, txt);
    char sc[12];
    snprintf(sc, sizeof sc, "%d:%d", pGoals, rGoals);
    c.textC(sc, W / 2, 5, c565(hex(0xffd23f)));
    auto dots = [&](const std::vector<int8_t>& h, int x0, int dir) {
      int n = std::max(5, int(h.size()));
      int start = std::max(0, n - 5);
      for (int i = 0; i < 5; i++) {
        int idx = start + i;
        uint16_t col = idx < int(h.size()) ? (h[idx] ? c565(hex(0x3ddc84)) : c565(hex(0xe8382f))) : c565(hex(0x3a3844));
        c.disc(x0 + dir * i * 7, 18, 2, col);
      }
    };
    dots(pHist, SAFE + 6, 1);
    dots(rHist, W - SAFE - 7, -1);
    if (sudden) c.textC("SD", W / 2, 15, c565(hex(0xe8382f)));
    if (st == St::Aim) {
      int w = int(clampf(clock / 3.f, 0, 1) * (W - 2 * SAFE));
      c.fill(SAFE, 23, w, 1, clock < 1.f ? c565(hex(0xe8382f)) : c565(hex(0x8a7a40)));
    }
  }

  void drawTicker(Canvas& c) {
    const int y0 = H - 14;
    c.fill(0, y0, W, 14, c565(COL_BG));
    int w = Canvas::textW(ticker);
    const uint16_t col = c565(hex(0xc8bfa8));
    const int x0 = SAFE + 4, avail = W - 2 * SAFE - 8;
    if (w <= avail) c.text(ticker, W / 2 - w / 2, y0 + 3, col);
    else {
      int off = int(std::max(0.f, tickerT - 0.8f) * 40.f) % (w + 40);
      c.text(ticker, x0 - off, y0 + 3, col);
      c.text(ticker, x0 - off + w + 40, y0 + 3, col);
      c.fill(0, y0, x0, 14, c565(COL_BG));
      c.fill(x0 + avail, y0, W - x0 - avail, 14, c565(COL_BG));
    }
    c.fill(x0 - 4, y0 + 2, 2, 10, c565(hex(0xe8382f)));
  }

  void drawBanner(Canvas& c) {
    if (bannerT > 1.7f || !banner[0]) return;
    float k = easeBack(bannerT / 0.35f);
    int y = int(lerpf(-30.f, 44.f, k));
    if (bannerT > 1.45f) y -= int((bannerT - 1.45f) * 500.f);
    c.headline(banner, W / 2, y, bannerScale, bannerTop, bannerBot, bannerSh);
  }

  void drawParticles(Canvas& c) {
    for (const auto& p : parts) {
      if (int(p.life * 12) & 1) c.fill(int(p.x), int(p.y), 2, 1, p.c);
      else c.fill(int(p.x), int(p.y), 1, 2, p.c);
    }
  }

  void drawArena(Canvas& c, bool withShooter) {
    c.fill(0, 0, W, 21, c565(COL_BG));
    drawStands(c);
    std::memcpy(c.px + HORIZON * W, floor.data(), floor.size() * 2);
    drawNet(c);
    if (st == St::Outcome && res == Res::Goal && bLife > 0) drawBall(c, int(bx), int(by), int(std::lround(br)), 0);
    drawFrame(c);
    drawKeeper(c);
    if (!(st == St::Outcome && res == Res::Goal)) drawBallFlight(c);
    if (withShooter) drawShooter(c);
  }

  void drawVolButton(Canvas& c, int y) {
    char b[24];
    snprintf(b, sizeof b, "TON: %s", VOL_NAMES[vol]);
    int w = Canvas::textW(b) + 16;
    drawPanel(c, W / 2 - w / 2, y, w, 18, vol == 3 ? hex(0x5a5466) : hex(0x3ddc84));
    c.textC(b, W / 2, y + 5, vol == 3 ? c565(hex(0x8a8496)) : c565(hex(0xf2efe6)));
  }

  void drawPanel(Canvas& c, int x, int y, int w, int h, RGB border) {
    c.fillA(x, y, w, h, c565(hex(0x0b0910)), 215);
    c.fill(x, y, w, 1, c565(border));
    c.fill(x, y + h - 1, w, 1, c565(shade(border, 0.6f)));
    c.fill(x, y, 1, h, c565(border));
    c.fill(x + w - 1, y, 1, h, c565(shade(border, 0.6f)));
  }

  void render(uint16_t* fb) {
    Canvas c(fb);
    const uint16_t dimTxt = c565(hex(0x8a8496)), gold = c565(hex(0xffd23f)), wht = c565(hex(0xf2efe6));
    switch (st) {
      case St::Title: {
        drawArena(c, false);
        c.dim(0, 0, W, H, 150);
        c.headline("HALLEN-", W / 2, 22, 2, hex(0xffe27a), hex(0xf0a030), hex(0x8a4a00));
        c.headline("LEGENDEN", W / 2, 42, 2, hex(0xffe27a), hex(0xf0a030), hex(0x8a4a00));
        c.headline("MINI", W / 2, 62, 2, hex(0xff6a5a), hex(0xc01a10), hex(0x3a0505));
        c.textShadowC("7M-DUELL", W / 2, 104, wht, C_BLACK);
        if (int(t * 2) & 1) c.textShadowC("TIPPEN!", W / 2, 166, gold, C_BLACK);
        int best = store.get("best", 0);
        if (best > 0) { char b[24]; snprintf(b, sizeof b, "BESTE SERIE: %d", best); c.textC(b, W / 2, 140, dimTxt); }
        drawVolButton(c, 186);
        c.textC("(C) 2026 tobwil", W / 2, 212, c565(hex(0x5a5466)));
        break;
      }
      case St::Team: {
        drawArena(c, false);
        c.dim(0, 0, W, H, 190);
        c.textShadowC("DEIN VEREIN", W / 2, 20, gold, C_BLACK);
        const Team& T = TEAMS[team];
        Sprite icon = jerseyIcon(hex(T.c1), hex(T.c2));
        c.blit(icon, W / 2, 112, false, 3);
        c.textShadowC(T.k, W / 2, 124, wht, C_BLACK, 2);
        c.textShadowC(T.city, W / 2, 148, wht, C_BLACK);
        c.textShadowC(T.nick, W / 2, 160, c565(hex(0xc8bfa8)), C_BLACK);
        c.textShadowC("<", 18, 80, gold, C_BLACK, 2);
        c.textShadowC(">", W - 18, 80, gold, C_BLACK, 2);
        c.textC("WISCHEN: WECHSELN", W / 2, 190, dimTxt);
        c.textC("TIPPEN: WÄHLEN", W / 2, 202, dimTxt);
        break;
      }
      case St::Level: {
        drawArena(c, false);
        c.dim(0, 0, W, H, 190);
        c.textShadowC("DER TORWART", W / 2, 20, gold, C_BLACK);
        c.textShadowC("SPIELT IN DER...", W / 2, 34, c565(hex(0xc8bfa8)), C_BLACK);
        const RGB borders[3] = {hex(0x3ddc84), hex(0xffd23f), hex(0xe8382f)};
        for (int i = 0; i < 3; i++) {
          int y = 62 + i * 48;
          drawPanel(c, 8, y, W - 16, 42, borders[i]);
          c.textShadowC(LEVELS[i].name, W / 2, y + 6, c565(borders[i]), C_BLACK, Canvas::textW(LEVELS[i].name, 2) <= W - 20 ? 2 : 1);
          c.textC(LEVELS[i].desc, W / 2, y + 24, c565(hex(0xc8bfa8)));
          char k[12], b[24];
          snprintf(k, sizeof k, "wins%d", i);
          int wins = store.get(k, 0);
          if (wins) { snprintf(b, sizeof b, "SIEGE: %d", wins); c.textC(b, W / 2, y + 33, dimTxt); }
        }
        break;
      }
      case St::Howto: {
        drawArena(c, false);
        c.dim(0, 0, W, H, 200);
        c.textShadowC("SO GEHT'S", W / 2, 16, gold, C_BLACK);
        const char* lines[] = {"ZUM TOR WISCHEN", " wo du loslässt,", " fliegt er hin", "SCHNELL = HART", "", "IM BOGEN WISCHEN", " Dreher", "GANZ LANGSAM", " Heber", "TIPPEN", " Wurftäuschung"};
        for (int i = 0; i < 11; i++) c.text(lines[i], 14, 36 + i * 12, lines[i][0] == ' ' ? c565(hex(0xc8bfa8)) : wht);
        if (int(t * 2) & 1) c.textShadowC("LOS!", W / 2, 204, gold, C_BLACK);
        break;
      }
      case St::Result: {
        drawArena(c, false);
        c.dim(0, 21, W, H - 31, 120);
        drawHud(c);
        if (won) c.headline("SIEG!", W / 2, 44, 3, hex(0xffe27a), hex(0xf0a030), hex(0x8a4a00));
        else c.headline("VERLOREN", W / 2, 48, 2, hex(0xff6a5a), hex(0xb01810), hex(0x3a0505));
        char b[40];
        snprintf(b, sizeof b, "%s %d:%d %s", TEAMS[team].k, pGoals, rGoals, TEAMS[rival].k);
        c.textShadowC(b, W / 2, 92, wht, C_BLACK, 1);
        snprintf(b, sizeof b, "TREFFER %d/%d", pGoals, pShots);
        c.textShadowC(b, W / 2, 108, c565(hex(0xc8bfa8)), C_BLACK);
        snprintf(b, sizeof b, "SERIE: %d", store.get("streak", 0));
        if (!demo) c.textShadowC(b, W / 2, 122, c565(hex(0xc8bfa8)), C_BLACK);
        if (stT > 1.0f && (int(t * 2) & 1)) c.textShadowC(demo ? "DEMO" : "NOCHMAL: TIPPEN", W / 2, 150, gold, C_BLACK);
        if (!demo) c.textC("BOOT: MENÜ", W / 2, 166, dimTxt);
        drawParticles(c);
        snprintf(ticker, sizeof ticker, "%s", won ? "Die Halle steht Kopf! Schüttel das Gerät!" : "Kopf hoch, gleich nochmal!");
        drawTicker(c);
        break;
      }
      default: {
        drawArena(c, true);
        if (slowmo && st == St::Flight && flightT / T > 0.35f) { c.fill(0, 21, W, 5, C_BLACK); c.fill(0, H - 16, W, 6, C_BLACK); }
        if (flash > 0) c.fillA(0, 21, W, H - 31, c565(hex(0xffffff)), int(flash * 200));
        drawCrosshair(c);
        drawParticles(c);
        if (shakeT > 0) {
          int a = int(shakeA * shakeT / 0.35f + 0.5f);
          if (a > 0) c.shift(rng.i(2 * a + 1) - a, rng.i(2 * a + 1) - a);
        }
        drawBanner(c);
        if (st == St::Rival) drawRival(c);
        if (st == St::Intro) {
          c.headline("7M-DUELL", W / 2, 46, 2, hex(0xffe27a), hex(0xf0a030), hex(0x8a4a00));
          char b[24];
          snprintf(b, sizeof b, "%s - %s", TEAMS[team].k, TEAMS[rival].k);
          c.textShadowC(b, W / 2, 70, wht, C_BLACK);
          c.textShadowC(LEVELS[level].name, W / 2, 82, c565(hex(0xc8bfa8)), C_BLACK);
        }
        drawHud(c);
        drawTicker(c);
        if (paused) {
          c.dim(0, 21, W, H - 31, 170);
          c.headline("PAUSE", W / 2, 80, 2, hex(0xffffff), hex(0xb8b8c8), hex(0x303040));
          c.textC("TIPPEN: WEITER", W / 2, 120, wht);
          c.textC("BOOT: MENÜ", W / 2, 134, dimTxt);
          drawVolButton(c, 150);
        }
        break;
      }
    }
    if (demo && st != St::Title) {
      c.fillA(4, 26, 42, 11, C_BLACK, 160);
      if (int(t * 2) & 1) c.text("DEMO", 8, 28, c565(hex(0xe8382f)));
    }
  }

  void drawRival(Canvas& c) {
    c.dim(0, 21, W, H - 31, 150);
    const Team& R = TEAMS[rival];
    drawPanel(c, 20, 66, W - 40, 82, hex(R.c1));
    Sprite icon = jerseyIcon(hex(R.c1), hex(R.c2));
    c.blit(icon, W / 2, 104, false, 1);
    char b[32];
    snprintf(b, sizeof b, "%s WIRFT", R.k);
    c.textShadowC(b, W / 2, 112, c565(hex(0xf2efe6)), C_BLACK);
    if (stT > 0.8f) {
      if (rivalGoal) c.headline("TOR", W / 2, 126, 2, hex(0xff6a5a), hex(0xb01810), hex(0x3a0505));
      else c.headline("GEHALTEN!", W / 2, 128, 1, hex(0x9ff0ff), hex(0x2a8ad8), hex(0x0a2a5a));
    } else {
      int dots = int(stT * 5) % 4;
      c.textC(dots == 0 ? "" : dots == 1 ? "." : dots == 2 ? ".." : "...", W / 2, 128, c565(hex(0xc8bfa8)));
    }
  }
};

// ---------------------------------------------------------------- öffentliche Schnittstelle
Game::Game(Synth& synth, Store& store, uint32_t seed) : p_(new Impl(synth, store, stats_, seed)) {}

void Game::tick(const Input& in) { p_->tick(in); }
void Game::render(uint16_t* fb) { p_->render(fb); }
const char* Game::debugInfo() const { return p_->dbg; }
bool Game::wantsDim() const { return p_->idleT > 180.f; }

void Game::startDemo() { p_->startDemo(); }

void Game::bench(int level, int shots) {
  Input in;
  p_->benchMode = true;
  startDemo();
  p_->level = level;
  stats_ = Stats();
  while (stats_.shots < shots) p_->tick(in);
  p_->benchMode = false;
  p_->demo = false;
}

}  // namespace hl
