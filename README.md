# Hallen-Legenden Mini

Siebenmeter-Duell im Look von [Hallen-Legenden](https://github.com/tobwil/HallenLegenden) für das Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2).

## Steuerung

| Geste | Wirkung |
|---|---|
| vom Ball zum Tor wischen | Wurf, er fliegt dorthin, wo der Finger loslässt |
| schnell wischen | harter Wurf (zu schnell streut) |
| im Bogen wischen | Dreher |
| ganz langsam wischen | Heber (lohnt sich, wenn der Torwart herauskommt) |
| tippen | Wurftäuschung |
| Gerät schütteln nach einem Tor | Jubel |
| BOOT-Taste | im Spiel Pause, im Titel Ton umschalten, sonst zurück zum Titel |

Der Ton startet ausgeschaltet. Umschalten geht im Titel und in der Pause über „TON: …“.

## Aufbau

```
game/       Spiel, plattformunabhängig (C++17): Renderer 184×224, Figuren, Synthesizer, Siebenmeter-Logik
sim/        Simulator für den Mac (SDL2)
firmware/   Gerät: Display (QSPI), Touch, IMU, Audio, NVS
tools/      mkfont.py: Pixelschrift → 8×8-Bitmap (game/font8.h)
```

## Simulator

```bash
make -C sim && sim/hlsim
```

Maus ziehen = wischen, Klick = tippen, B = BOOT, Leertaste = schütteln, S = Screenshot.
`sim/hlsim --bench` misst die Trefferquote des Demo-Bots je Torwart-Stufe (zum Ausbalancieren).

## Gerät

```bash
.venv/bin/pio run -t upload
```

Das Original-Image der Waveshare-Demo liegt unter `../backup/factory_flash_16MB.bin`. So spielst du es zurück:

```bash
.venv/bin/esptool -p /dev/cu.usbmodem13101 -b 921600 write-flash 0 ../backup/factory_flash_16MB.bin
```

## Stolperfallen auf der Hardware

- **Serielle Ausgabe darf nicht blockieren:** Liest am Rechner niemand mit, läuft der USB-Puffer voll und jedes `Serial.print` wartet, das Spiel friert sekundenlang ein. Deshalb `Serial.setTxTimeoutMs(0)`, und das Protokoll gibt es nur noch auf den seriellen Befehl `log`.
- **Kein `-O2`:** GCC legte dabei Farbkonstanten in den Literal-Pool im Flash-Code und las sie byteweise. Das führt zum LoadStoreError beim Start. Mit dem Standard `-Os` passiert das nicht.
- **QMI8658 braucht einen Softreset** (`0x60 = 0xB0`) vor der Konfiguration, sonst liefert er alte Werte.
- **Display:** CO5300 mit Spaltenversatz 16. Der Puffer wird bandweise per `writeBytes` geschoben (Big Endian, vorher getauscht). Die Bildpuffer liegen im internen RAM bzw. PSRAM.
- **Runde Display-Ecken:** Text hält mindestens 16 px (intern) Abstand zum Rand (`SAFE` in `game.cpp`).

## Lizenzen

- Pixelschrift in `game/font8.h`: abgeleitet von Press Start 2P (© CodeMan38), SIL Open Font License 1.1, siehe [licenses/OFL-1.1.txt](licenses/OFL-1.1.txt).
- ES8311-Treiber (`firmware/es8311*`): Espressif, Apache-2.0.
