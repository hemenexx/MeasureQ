# MeasureQ – poznámky k firmwaru

Průběžně aktualizovaný přehled hardwaru, zjištěných faktů a softwarové
architektury. Projekt založen 2026-08-07, zatím bez fyzického HW – všechny
piny a části API knihoven jsou nutně jen předpoklad, dokud se neověří na
reálné desce (viz [Otevřené otázky / TODO](#otevřené-otázky--todo)).

## Cíl projektu

Bezdrátové měřicí zařízení: 3 samostatné baterkové měřicí krabičky posílají
data přes ESP-NOW do jedné centrální jednotky, která je v reálném čase
zobrazuje na displejích.

## Hardware – přehled

### Měřicí krabička (node) – 3×

- **ESP32-C3-SuperMini** – hlavní MCU, WiFi/ESP-NOW vysílání dat do hubu
- **ENS160 + AHT21** (I2C kombo modul) – CO2/eCO2, TVOC, teplota, vlhkost
- **XL6007** – boost převodník, zvyšuje napětí ze 2× AAA baterií na 3.3V
- Napájení: 2× AAA baterie (přes XL6007)

### Centrální jednotka (hub) – 1×

- **ESP32 30pin** (generický DOIT DEVKIT V1 klon) – příjem dat přes
  ESP-NOW, zobrazování
- **3× TM1637** 0.36" 4-místný 7-segmentový displej – po jednom pro každou
  měřicí krabičku, připojeno přímo na GPIO (žádný GPIO expander, na rozdíl
  od MIDIQ – hub má dost volných pinů)
- **3× tlačítko** (jedno pod každým displejem) – cykluje zobrazovanou
  veličinu (teplota/vlhkost/CO2/TVOC) pro daný displej/node
- **1× napájecí tlačítko** – zatím předpoklad: fyzický spínač přímo v
  napájecí větvi (bez GPIO), viz TODO níže
- Napájení: USB-C přímo do ESP32

## Piny (node, ESP32-C3-SuperMini) – NEOVĚŘENO na HW

| Signál | Pin |
|---|---|
| I2C SDA | 8 |
| I2C SCL | 9 |

## Piny (hub, ESP32 30pin) – NEOVĚŘENO na HW

| Signál | Pin |
|---|---|
| Display 0 (node 0) CLK / DIO | 25 / 26 |
| Display 1 (node 1) CLK / DIO | 27 / 14 |
| Display 2 (node 2) CLK / DIO | 12 / 13 |
| Tlačítko 0 / 1 / 2 | 32 / 33 / 4 |

## Softwarová architektura

### Struktura PlatformIO projektu

Jeden `platformio.ini`, dvě environments:

- `env:node` – kompiluje `src/common/` + `src/node/` (build_src_filter
  vylučuje `src/hub/`)
- `env:hub` – kompiluje `src/common/` + `src/hub/` (build_src_filter
  vylučuje `src/node/`)

Stejný vzor jako by šlo použít i pro sdílení kódu mezi více firmware cíli
v jednom repozitáři – zde využito, protože node a hub běží na jiném HW,
ale sdílí komunikační protokol.

### ESP-NOW protokol (`src/common/MeasureQProtocol.h`)

`MeasurementPacket` – packed struct (nodeId, seq, teplota, vlhkost, CO2,
TVOC, napětí baterie), posílaná z node do hubu. Struktura musí zůstat
bit-identická na obou stranách.

Hub v `setup()` vypíše na Serial vlastní MAC adresu – tu je nutné ručně
zkopírovat do `hubMac[]` v `src/node/main.cpp` na všech 3 krabičkách.
Každá krabička má navíc vlastní `NODE_ID` (0/1/2), který se musí ručně
nastavit před flashem.

### TM1637Display (`src/hub/TM1637Display.*`)

Nízkoúrovňový TM1637 bit-bang protokol a vysokoúrovňové API
(`showDigits`/`showNumber`/`showAllSegments`/`off`) převzaté z MIDIQ
projektu (`c:\Users\Dan\Documents\PlatformIO\Projects\MIDIQ\src\TM1637Display.*`).
Rozdíl: MIDIQ verze jde přes MCP23S17 SPI GPIO expander (kvůli nedostatku
pinů na MIDIQ desce), MeasureQ hub má dost volných GPIO, takže tahle verze
píše přímo přes `pinMode`/`digitalWrite`.

`invertedMount` konstruktor parametr (segmentové zrcadlení pro displej
namontovaný vzhůru nohama, jako u MIDIQ) je zachovaný, ale u MeasureQ
zatím nepoužitý (`false`) – orientace montáže není ověřená.

## Otevřené otázky / TODO

- **Board definice** – `esp32-c3-devkitm-1` a `esp32doit-devkit-v1` v
  `platformio.ini` jsou nejbližší generické desky, ne nutně přesná shoda s
  koupenými klony. Ověřit po prvním flashi.
- **Piny** (node I2C, hub displeje/tlačítka) – čistě předpoklad, viz tabulky
  výše. Upravit podle skutečného zapojení.
- **Knihovna ENS160** (`sciosense/ScioSense_ENS16x`) – API
  (`begin()`, `setOperationMode()`, `getECO2()`, `getTVOC()`) ověřeno jen
  z dokumentace/webu, ne z reálné kompilace proti nainstalované verzi.
- **Napájecí tlačítko na hubu** – zatím firmware s ním vůbec nepočítá
  (předpoklad: čistě fyzický spínač v napájecí větvi). Pokud má jít o
  "soft power" řízené firmwarem, je potřeba přidat GPIO a logiku.
- **Deep sleep na node** – zatím `delay()` mezi měřeními, baterie (2× AAA)
  by z toho těžila přechodem na `esp_deep_sleep`.
- **Desetinná místa na displeji** – `TM1637Display` nemá API pro desetinnou
  tečku, teplota/vlhkost se tak momentálně zobrazují jako celé číslo ×10
  (např. `236` = 23.6 °C) bez vizuálního oddělovače. Řešit až podle
  reálné potřeby čitelnosti.
- **Napětí baterie** – `batteryVoltage` v paketu se zatím neplní (chybí
  ADC měření na node).
- **Sdílený CLK pro TM1637** – MIDIQ z důvodu nedostatku pinů sdílí CLK
  mezi displeji; MeasureQ hub má dost pinů, takže zatím každý displej má
  vlastní CLK i DIO (6 pinů celkem). Lze zrevidovat, pokud by se GPIO
  hubu z jiného důvodu nedostávalo.
