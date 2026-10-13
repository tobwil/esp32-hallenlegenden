// Kleiner Synthesizer für die Hallen-Geräusche: Pfiff, Wurf, Netz, Pfosten, Raunen, Jubel.
// Alles wird zur Laufzeit erzeugt (wie p02_audio.js im Original), keine Samples im Flash.
// render() läuft im Audio-Thread der Plattform, die Auslöser im Spiel-Thread; ein Mutex trennt beide.
#pragma once
#include <cstdint>
#include <mutex>

namespace hl {

class Synth {
 public:
  static constexpr int RATE = 16000;

  void render(int16_t* out, int n);   // Mono, 16 Bit

  // Spielgeräusche
  void whistle(bool longOne = false);
  void throwThud(float power);
  void net();
  void post();
  void save();
  void cheer(float strength);
  void groan();
  void click();
  void tick();
  void fanfare();
  void sadTune();
  void crowdLevel(float v);   // Grundrauschen der Halle (0..1)
  void setVolume(float v) { std::lock_guard<std::mutex> l(m_); master_ = v; }

 private:
  enum Wave : uint8_t { SINE, SQUARE, TRI, SAW, NOISE };
  enum Filt : uint8_t { NONE, LP, BP, HP };
  struct Voice {
    bool on = false;
    Wave wave = SINE;
    Filt filt = NONE;
    float f0 = 440, f1 = 440;     // Frequenz, exponentiell von f0 nach f1
    float cut0 = 1000, cut1 = 1000, q = 0.7f;
    float vol = 0.3f, attack = 0.005f, dur = 0.2f, delay = 0;
    bool hold = false;            // true: flach halten und am Ende kurz ausblenden, sonst exponentiell abklingen
    float lfoF = 0, lfoD = 0;     // Vibrato
    float amF = 0, amD = 0;       // Lautstärke-Schwankung (Publikum)
    // Laufzustand
    float t = 0, ph = 0, lp = 0, bp = 0;
  };
  void add(const Voice& v);
  float sample(Voice& v, float dt);
  uint32_t rnd();

  std::mutex m_;
  Voice v_[14];
  float master_ = 0.8f, crowd_ = 0.15f, crowdCur_ = 0.15f;
  float amb_lp_ = 0, amb_lp2_ = 0;
  uint32_t seed_ = 0x1234567;
};

}  // namespace hl
