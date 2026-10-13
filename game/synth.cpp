#include "synth.h"

#include <cmath>

namespace hl {

static constexpr float TWO_PI = 6.2831853f;

uint32_t Synth::rnd() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return seed_;
}

void Synth::add(const Voice& v) {
  std::lock_guard<std::mutex> l(m_);
  int best = 0;
  float bestLeft = 1e9f;
  for (int i = 0; i < 14; i++) {
    if (!v_[i].on) { best = i; bestLeft = -1; break; }
    float left = v_[i].delay + v_[i].dur - v_[i].t;
    if (left < bestLeft) { bestLeft = left; best = i; }
  }
  v_[best] = v;
  v_[best].on = true;
  v_[best].t = -v.delay;
}

float Synth::sample(Voice& v, float dt) {
  v.t += dt;
  if (v.t < 0) return 0;
  if (v.t >= v.dur) { v.on = false; return 0; }
  const float k = v.t / v.dur;
  float f = v.f0 == v.f1 ? v.f0 : v.f0 * std::pow(v.f1 / v.f0, k);
  if (v.lfoD > 0) f += v.lfoD * std::sin(TWO_PI * v.lfoF * v.t);
  float s;
  if (v.wave == NOISE) {
    s = float(int32_t(rnd())) / 2147483648.f;
  } else {
    v.ph += f * dt;
    v.ph -= std::floor(v.ph);
    switch (v.wave) {
      case SINE: s = std::sin(TWO_PI * v.ph); break;
      case SQUARE: s = v.ph < 0.5f ? 0.8f : -0.8f; break;
      case TRI: s = 4.f * std::fabs(v.ph - 0.5f) - 1.f; break;
      default: s = 2.f * v.ph - 1.f; break;
    }
  }
  if (v.filt != NONE) {
    float fc = v.cut0 == v.cut1 ? v.cut0 : v.cut0 * std::pow(v.cut1 / v.cut0, k);
    if (fc > 2400) fc = 2400;   // Chamberlin-Filter bleibt bei 16 kHz bis etwa hier stabil
    const float F = 2.f * std::sin(3.14159265f * fc / RATE), damp = 1.f / v.q;
    v.lp += F * v.bp;
    const float hp = s - v.lp - damp * v.bp;
    v.bp += F * hp;
    s = v.filt == LP ? v.lp : v.filt == BP ? v.bp : hp;
  }
  float g = v.t < v.attack ? v.t / v.attack : 1.f;
  if (v.hold) { if (v.dur - v.t < 0.03f) g *= (v.dur - v.t) / 0.03f; }
  else g *= std::exp(-5.f * (v.t - v.attack > 0 ? v.t - v.attack : 0) / v.dur);
  if (v.amD > 0) g *= 1.f - v.amD * (0.5f + 0.5f * std::sin(TWO_PI * v.amF * v.t + v.f0));
  return s * g * v.vol;
}

void Synth::render(int16_t* out, int n) {
  std::lock_guard<std::mutex> l(m_);
  const float dt = 1.f / RATE;
  for (int i = 0; i < n; i++) {
    // Grundrauschen der Halle: tiefpassgefiltertes Rauschen
    crowdCur_ += (crowd_ - crowdCur_) * 0.00005f;
    float nz = float(int32_t(rnd())) / 2147483648.f;
    amb_lp_ += (nz - amb_lp_) * 0.09f;
    amb_lp2_ += (amb_lp_ - amb_lp2_) * 0.09f;
    float s = amb_lp2_ * 1.6f * crowdCur_;
    for (auto& v : v_)
      if (v.on) s += sample(v, dt);
    s *= master_;
    s = s * (27.f + s * s) / (27.f + 9.f * s * s);   // weiche Begrenzung
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    out[i] = int16_t(s * 28000.f);
  }
}

void Synth::crowdLevel(float v) { std::lock_guard<std::mutex> l(m_); crowd_ = v; }

void Synth::whistle(bool longOne) {
  Voice v; v.wave = TRI; v.f0 = v.f1 = 2950; v.lfoF = 42; v.lfoD = 260; v.hold = true;
  v.attack = 0.01f; v.dur = longOne ? 0.75f : 0.32f; v.vol = 0.2f;
  add(v);
}

void Synth::throwThud(float power) {
  Voice a; a.wave = SINE; a.f0 = 160; a.f1 = 55; a.dur = 0.13f; a.vol = 0.5f; add(a);
  Voice b; b.wave = NOISE; b.filt = LP; b.cut0 = b.cut1 = 700; b.dur = 0.06f; b.vol = 0.35f; add(b);
  Voice w; w.wave = NOISE; w.filt = BP; w.cut0 = 1400; w.cut1 = 350; w.q = 2; w.attack = 0.03f; w.dur = 0.3f; w.vol = 0.2f + 0.25f * power; add(w);
}

void Synth::net() {
  Voice a; a.wave = NOISE; a.filt = BP; a.cut0 = 2200; a.cut1 = 700; a.q = 1.2f; a.attack = 0.01f; a.dur = 0.45f; a.vol = 0.45f; add(a);
  Voice b; b.wave = NOISE; b.filt = HP; b.cut0 = b.cut1 = 2400; b.dur = 0.25f; b.vol = 0.12f; add(b);
}

void Synth::post() {
  Voice a; a.wave = SINE; a.f0 = a.f1 = 880; a.dur = 0.7f; a.vol = 0.28f; add(a);
  Voice b; b.wave = SINE; b.f0 = b.f1 = 1318; b.dur = 0.5f; b.vol = 0.16f; add(b);
  Voice c; c.wave = TRI; c.f0 = c.f1 = 2490; c.dur = 0.25f; c.vol = 0.08f; add(c);
  Voice d; d.wave = NOISE; d.filt = LP; d.cut0 = d.cut1 = 1800; d.dur = 0.03f; d.vol = 0.5f; add(d);
}

void Synth::save() {
  Voice a; a.wave = NOISE; a.filt = BP; a.cut0 = a.cut1 = 1100; a.q = 1.5f; a.dur = 0.08f; a.vol = 0.55f; add(a);
  Voice b; b.wave = SINE; b.f0 = 220; b.f1 = 90; b.dur = 0.12f; b.vol = 0.4f; add(b);
}

void Synth::cheer(float s) {
  Voice a; a.wave = NOISE; a.filt = BP; a.cut0 = 900; a.cut1 = 1300; a.q = 0.6f; a.attack = 0.25f; a.dur = 2.4f * s; a.vol = 0.5f * s; a.amF = 7; a.amD = 0.3f; add(a);
  Voice b; b.wave = NOISE; b.filt = HP; b.cut0 = b.cut1 = 2000; b.attack = 0.3f; b.dur = 1.9f * s; b.vol = 0.12f * s; b.amF = 11; b.amD = 0.4f; add(b);
  for (int i = 0; i < 3; i++) {   // ein paar „Stimmen“ im Jubel
    Voice v; v.wave = SAW; v.filt = LP; v.cut0 = v.cut1 = 900; v.f0 = 260.f + 90.f * i + float(rnd() % 40); v.f1 = v.f0 * 1.15f;
    v.lfoF = 5.5f; v.lfoD = 6; v.attack = 0.15f; v.dur = 1.2f; v.delay = 0.05f * i; v.vol = 0.05f * s; add(v);
  }
}

void Synth::groan() {
  Voice a; a.wave = NOISE; a.filt = LP; a.cut0 = 600; a.cut1 = 300; a.attack = 0.2f; a.dur = 1.3f; a.vol = 0.45f; a.amF = 3; a.amD = 0.2f; add(a);
  Voice b; b.wave = SAW; b.filt = LP; b.cut0 = b.cut1 = 700; b.f0 = 220; b.f1 = 150; b.attack = 0.15f; b.dur = 1.1f; b.vol = 0.07f; add(b);
  Voice c; c.wave = SAW; c.filt = LP; c.cut0 = c.cut1 = 600; c.f0 = 165; c.f1 = 112; c.attack = 0.2f; c.dur = 1.1f; c.vol = 0.06f; add(c);
}

void Synth::click() { Voice v; v.wave = SQUARE; v.f0 = v.f1 = 1400; v.dur = 0.035f; v.vol = 0.12f; add(v); }
void Synth::tick() { Voice v; v.wave = SQUARE; v.f0 = v.f1 = 1000; v.dur = 0.025f; v.vol = 0.07f; add(v); }

void Synth::fanfare() {
  const float notes[4] = {523, 659, 784, 1047};
  for (int i = 0; i < 4; i++) {
    Voice v; v.wave = SQUARE; v.f0 = v.f1 = notes[i]; v.hold = true; v.attack = 0.005f;
    v.dur = i == 3 ? 0.45f : 0.13f; v.delay = 0.13f * i; v.vol = 0.11f; add(v);
    Voice b = v; b.wave = TRI; b.f0 = b.f1 = notes[i] / 2; b.vol = 0.12f; add(b);
  }
}

void Synth::sadTune() {
  const float notes[3] = {392, 330, 262};
  for (int i = 0; i < 3; i++) {
    Voice v; v.wave = TRI; v.f0 = v.f1 = notes[i]; v.hold = true; v.dur = i == 2 ? 0.55f : 0.25f; v.delay = 0.28f * i; v.vol = 0.16f; add(v);
  }
}

}  // namespace hl
