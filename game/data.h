// Vereine, Namen und Werbebanden aus Hallen-Legenden (Fantasienamen, siehe p01_core.js im Hauptspiel)
#pragma once
#include "gfx.h"

namespace hl {

struct Team { const char* k; const char* city; const char* nick; uint32_t c1, c2; };

// die 1. Liga des Hauptspiels, Saison 2026/27
static const Team TEAMS[] = {
    {"MAG", "Magdeburg", "Domstürmer", 0x0a8a3e, 0xe30613},   {"FLE", "Flensburg", "Fördeblitz", 0x0b3d91, 0xe2001a},
    {"KIE", "Kiel", "Leuchttürme", 0xf4f4f0, 0x16161a},       {"BER", "Berlin", "Hauptstadtbären", 0x0f8a4a, 0xffffff},
    {"HAN", "Hannover", "Leineritter", 0xd0021b, 0x141414},   {"MEL", "Melsungen", "Fuldataler", 0xc8102e, 0xf2f2f2},
    {"GUM", "Gummersbach", "Oberbergler", 0x0057a8, 0xffffff}, {"ERL", "Erlangen", "Regnitzer", 0x1b2f6b, 0xf39200},
    {"KUR", "Rhein-Neckar", "Kurpfälzer", 0xffd200, 0x0a2d6e}, {"GÖP", "Göppingen", "Hohenstaufen", 0x00863f, 0xf5f5f5},
    {"STU", "Stuttgart", "Neckarwilde", 0xb0122c, 0xffffff},  {"BAL", "Balingen", "Zollernalb", 0xf07d00, 0x141414},
    {"LEM", "Lemgo", "Lippe-Hanse", 0x0050a0, 0xf4f4f4},      {"HAM", "Hamburg", "Hafenriesen", 0x1d1d24, 0x3ba3e8},
    {"EIS", "Eisenach", "Wartburgritter", 0x1356a8, 0xffffff}, {"BIE", "Bietigheim", "Enzwölfe", 0xe30613, 0x13306b},
    {"WET", "Wetzlar", "Lahnadler", 0x008d4c, 0xf4f4f4},      {"WUP", "Wuppertal", "Bergwölfe", 0x6aa82f, 0x141414},
};
constexpr int N_TEAMS = sizeof(TEAMS) / sizeof(TEAMS[0]);

static const uint32_t GK_COLORS[] = {0xffd23f, 0x3fd0ff, 0x9cff57, 0xff8bd1, 0xff9f1c, 0xc58bff};
static const uint32_t HAIR[] = {0x2a1a10, 0x4a2e18, 0x7a4a20, 0xc9a050, 0xe3c27a, 0x1a1a1a, 0x8a3a18, 0xb8b0a0};

static const char* const SURNAMES[] = {
    "Becker", "Hoffmann", "Krüger", "Lehmann", "Brandt", "Vogel", "Hartmann", "Seidel", "Franke", "Lorenz", "Arnold",
    "Ziegler", "Böhm", "Kaiser", "Keller", "Roth", "Graf", "Jäger", "Winter", "Busch", "Engel", "Horn", "Petersen",
    "Jansen", "Kruse", "Möller", "Wendt", "Fuchs", "Kraft", "Larsen", "Hansen", "Dahl", "Holm", "Strand", "Eriksen",
    "Novak", "Horvat", "Kovac", "Babic", "Juric", "Garcia", "Ruiz", "Moreau", "Costa", "Silva", "Magnusson", "Jonsson",
    "Gislason", "Svensson", "Lund", "Nagy", "Toth", "Visser", "Bakker", "Frost", "Ravn", "Skov", "Lindberg"};
constexpr int N_SURNAMES = sizeof(SURNAMES) / sizeof(SURNAMES[0]);

// Werbebanden wie im Original (laufen als LED-Bande durch)
static const char* const ADS = "LÄUFER BRÄU   SIEBENMETER SPORT   FUCHSBAU REISEN   HARZ & HAFT   KREISLÄUFER KAFFEE   ";

struct Level {
  const char* name;
  const char* desc;
  float react;     // Reaktionszeit nach dem Abwurf (s)
  float speed;     // Hechtgeschwindigkeit der Hand (m/s)
  float reach;     // maximale Reichweite von der Körpermitte (m)
  float pRead;     // liest die echte Wurfrichtung
  float pMem;      // tippt auf deine Lieblingsecke
  float pBite;     // fällt auf die Täuschung herein
  float pOut;      // kommt raus (anfällig für den Heber)
  float rivalConv; // Trefferquote des Gegners
};
static const Level LEVELS[3] = {
    {"KREISLIGA", "Der Torwart ist müde", 0.24f, 5.0f, 1.70f, 0.10f, 0.20f, 0.70f, 0.15f, 0.60f},
    {"OBERLIGA", "Er merkt sich was", 0.17f, 6.2f, 1.80f, 0.25f, 0.45f, 0.45f, 0.22f, 0.72f},
    {"BUNDESLIGA", "Er liest dich", 0.12f, 7.4f, 1.90f, 0.42f, 0.65f, 0.25f, 0.28f, 0.82f},
};

}  // namespace hl
