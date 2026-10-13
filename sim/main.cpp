// Simulator für den Mac (SDL2): gleiche Spiellogik wie auf dem Gerät.
// Maus ziehen = wischen und zielen, Klick = tippen, B = BOOT-Taste, Leertaste = schütteln,
// C = Neigung zurücksetzen, S = Screenshot, Esc = Ende.
//
// Ohne Fenster:
//   hlsim --bench [Würfe]          Trefferquote des Demo-Bots je Torwart-Stufe
//   hlsim --dump <ordner> [Frames]  Demo-Spiel laufen lassen und jeden 15. Frame als BMP speichern
//   hlsim --screens <ordner>       Menübildschirme als BMP speichern
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "game.h"

using namespace hl;

struct FileStore : Store {
  std::string path;
  std::map<std::string, int> kv;
  explicit FileStore(std::string p) : path(std::move(p)) {
    if (FILE* f = fopen(path.c_str(), "r")) {
      char k[64]; int v;
      while (fscanf(f, "%63s %d", k, &v) == 2) kv[k] = v;
      fclose(f);
    }
  }
  int get(const char* k, int def) override { auto it = kv.find(k); return it == kv.end() ? def : it->second; }
  void set(const char* k, int v) override {
    kv[k] = v;
    if (path.empty()) return;
    if (FILE* f = fopen(path.c_str(), "w")) { for (auto& [a, b] : kv) fprintf(f, "%s %d\n", a.c_str(), b); fclose(f); }
  }
};

static uint16_t fb[W * H];

static void saveBmp(const char* path) {
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W * 2, H * 2, 24, SDL_PIXELFORMAT_RGB24);
  for (int y = 0; y < H * 2; y++)
    for (int x = 0; x < W * 2; x++) {
      uint16_t p = fb[(y / 2) * W + x / 2];
      uint8_t* d = (uint8_t*)s->pixels + y * s->pitch + x * 3;
      d[0] = uint8_t(((p >> 11) & 31) * 255 / 31); d[1] = uint8_t(((p >> 5) & 63) * 255 / 63); d[2] = uint8_t((p & 31) * 255 / 31);
    }
  SDL_SaveBMP(s, path);
  SDL_FreeSurface(s);
}

static void tap(Game& g, Input& in, int x, int y, uint32_t& ms) {
  in.nTouch = 1; in.touch[0] = {true, int16_t(x), int16_t(y), ms}; g.tick(in); ms += 33;
  in.touch[0] = {false, int16_t(x), int16_t(y), ms}; g.tick(in); ms += 33;
  in.nTouch = 0;
}

int main(int argc, char** argv) {
  Synth synth;
  if (argc > 1 && !strcmp(argv[1], "--bench")) {
    FileStore st("");
    Game g(synth, st, 42);
    int n = argc > 2 ? atoi(argv[2]) : 600;
    const char* names[3] = {"Kreisliga", "Oberliga", "Bundesliga"};
    for (int l = 0; l < 3; l++) {
      g.bench(l, n);
      auto s = g.stats();
      printf("%-10s Würfe %4d  Tore %5.1f%%  Paraden %5.1f%%  daneben %5.1f%%\n", names[l], s.shots, 100.f * s.goals / s.shots,
             100.f * s.saves / s.shots, 100.f * s.misses / s.shots);
    }
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "--dump")) {
    FileStore st("");
    Game g(synth, st, 7);
    g.startDemo();
    Input in;
    int frames = argc > 3 ? atoi(argv[3]) : 600;
    char path[512];
    for (int i = 0; i < frames; i++) {
      in.ms = uint32_t(i * 33);
      g.tick(in);
      if (i % 15 == 0) { g.render(fb); snprintf(path, sizeof path, "%s/f%04d.bmp", argv[2], i); saveBmp(path); }
    }
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "--screens")) {
    FileStore st("");
    Game g(synth, st, 3);
    Input in;
    uint32_t ms = 0;
    char path[512];
    auto shot = [&](const char* name) { g.render(fb); snprintf(path, sizeof path, "%s/%s.bmp", argv[2], name); saveBmp(path); };
    for (int i = 0; i < 20; i++) { g.tick(in); ms += 33; }
    shot("1_titel");
    tap(g, in, 92, 150, ms); shot("2_verein");
    tap(g, in, 92, 100, ms); shot("3_stufe");
    tap(g, in, 92, 80, ms);
    for (int i = 0; i < 20; i++) { g.tick(in); ms += 33; }
    shot("4_anleitung");
    tap(g, in, 92, 150, ms);
    for (int i = 0; i < 20; i++) { g.tick(in); ms += 33; }
    shot("5_intro");
    for (int i = 0; i < 50; i++) { g.tick(in); ms += 33; }
    // Wurf: vom Ball nach rechts oben ins Tor wischen, Fadenkreuz während des Wischens
    in.nTouch = 1; in.touch[0] = {true, 70, 200, ms}; g.tick(in); ms += 33;
    in.touch[0] = {true, 92, 160, ms}; g.tick(in); ms += 33;
    in.touch[0] = {true, 120, 108, ms}; g.tick(in); ms += 33;
    in.nTouch = 0;
    shot("6_zielen");
    in.nTouch = 1; in.touch[0] = {false, 120, 108, ms}; g.tick(in); ms += 33; in.nTouch = 0;
    for (int i = 0; i < 6; i++) { g.tick(in); ms += 33; }
    shot("7_flug");
    for (int i = 0; i < 12; i++) { g.tick(in); ms += 33; }
    shot("8_ergebnis");
    return 0;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  const int scale = 2;   // Fenster in Gerätegröße (368×448)
  SDL_Window* win = SDL_CreateWindow("Hallen-Legenden Mini", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * scale, H * scale, SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren, W * scale, H * scale);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);

  SDL_AudioSpec want{}, have{};
  want.freq = Synth::RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
  want.callback = [](void* u, Uint8* stream, int len) { static_cast<Synth*>(u)->render((int16_t*)stream, len / 2); };
  want.userdata = &synth;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);

  std::string home = getenv("HOME") ? getenv("HOME") : ".";
  FileStore store(home + "/.hallenlegenden-mini.txt");
  Game game(synth, store, uint32_t(SDL_GetTicks() * 2654435761u + 1));

  float tiltX = 0, tiltY = 0;   // Grad
  bool mouseDown = false, running = true, shake = false;
  int shotN = 0;
  Uint64 last = SDL_GetPerformanceCounter();
  double acc = 0;
  Input in;
  while (running) {
    in.nTouch = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
        if (e.key.keysym.sym == SDLK_c) tiltX = tiltY = 0;
        if (e.key.keysym.sym == SDLK_s) { char p[64]; snprintf(p, sizeof p, "screenshot_%02d.bmp", shotN++); saveBmp(p); printf("%s\n", p); }
      }
      auto addTouch = [&](bool down, int x, int y) {
        if (in.nTouch < 32) in.touch[in.nTouch++] = {down, int16_t(x / scale), int16_t(y / scale), SDL_GetTicks()};
      };
      if (e.type == SDL_MOUSEBUTTONDOWN) { mouseDown = true; addTouch(true, e.button.x, e.button.y); }
      if (e.type == SDL_MOUSEMOTION && mouseDown) addTouch(true, e.motion.x, e.motion.y);
      if (e.type == SDL_MOUSEBUTTONUP) { mouseDown = false; addTouch(false, e.button.x, e.button.y); }
    }
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    const float dt = 1.f / 60.f;
    if (k[SDL_SCANCODE_LEFT]) tiltX -= 40 * dt;
    if (k[SDL_SCANCODE_RIGHT]) tiltX += 40 * dt;
    if (k[SDL_SCANCODE_UP]) tiltY += 40 * dt;
    if (k[SDL_SCANCODE_DOWN]) tiltY -= 40 * dt;
    tiltX = fmaxf(-35, fminf(35, tiltX)); tiltY = fmaxf(-35, fminf(35, tiltY));
    shake = k[SDL_SCANCODE_SPACE];
    // Neigung nach rechts (rechte Kante runter) → ax negativ, obere Kante runter → ay negativ
    in.ax = -sinf(tiltX * 3.14159f / 180.f);
    in.ay = -sinf(tiltY * 3.14159f / 180.f);
    in.az = cosf(tiltX * 3.14159f / 180.f) * cosf(tiltY * 3.14159f / 180.f);
    if (shake) in.az += (SDL_GetTicks() / 50) % 2 ? 1.5f : -1.5f;
    in.boot = k[SDL_SCANCODE_B];
    in.ms = SDL_GetTicks();

    Uint64 now = SDL_GetPerformanceCounter();
    acc += double(now - last) / SDL_GetPerformanceFrequency();
    last = now;
    if (acc >= Game::DT) {
      acc -= Game::DT;
      if (acc > 0.2) acc = 0;
      game.tick(in);
      game.render(fb);
      SDL_UpdateTexture(tex, nullptr, fb, W * 2);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
    } else {
      SDL_Delay(2);
    }
  }
  SDL_Quit();
  return 0;
}
