// Hallen-Legenden Mini: Siebenmeter-Duell. Plattformunabhängig (Simulator und ESP32).
// Die Plattform ruft tick() mit 30 Hz auf, liefert Neigung, Touch und Tasten und zeigt danach den Framebuffer.
#pragma once
#include <cstdint>

#include "gfx.h"
#include "sprites.h"
#include "synth.h"

namespace hl {

struct TouchSample { bool down; int16_t x, y; uint32_t ms; };   // Spielkoordinaten (184×224)

struct Input {
  float ax = 0, ay = 0, az = 1;   // Beschleunigung in g, Bildschirmachsen: x nach rechts, y nach oben, z aus dem Bildschirm
  TouchSample touch[32];          // alle Abtastwerte seit dem letzten Frame, zeitlich sortiert
  int nTouch = 0;
  bool boot = false;              // BOOT-Taste gedrückt
  uint32_t ms = 0;
};

struct Store {   // dauerhafte Werte (NVS auf dem Gerät, Datei im Simulator)
  virtual int get(const char* key, int def) = 0;
  virtual void set(const char* key, int v) = 0;
  virtual ~Store() = default;
};

class Game {
 public:
  static constexpr float DT = 1.f / 30.f;

  Game(Synth& synth, Store& store, uint32_t seed);
  void tick(const Input& in);   // ein Frame Logik
  void render(uint16_t* fb);    // in den 184×224-Puffer zeichnen
  bool wantsDim() const;        // lange nichts passiert: Display dimmen

  // Für Tests und den Simulator
  void startDemo();
  struct Stats { int shots = 0, goals = 0, saves = 0, misses = 0; };
  Stats stats() const { return stats_; }
  void bench(int level, int shots);   // Bot wirft schnell gegen den Torwart, ohne Grafik
  const char* debugInfo() const;      // Zustand und letzte Geste (serielle Ausgabe)

 private:
  struct Impl;
  Impl* p_;
  Stats stats_;
};

}  // namespace hl
