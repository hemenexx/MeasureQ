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

- **ESP32-C3-SuperMini** – hlavní MCU, WiFi/ESP-NOW vysílání dat do hubu,
  většinu času v deep sleep (viz [Napájecí režim node](#napájecí-režim-node-deep-sleep--spínání-senzoru))
- **ENS160 + AHT21** (I2C kombo modul) – CO2/eCO2, TVOC, teplota, vlhkost
- **P-MOSFET load switch** – spíná napájení senzoru přes GPIO (senzor je
  vypnutý, dokud ESP32 spí – viz níže)
- **XL63070** (modul na bázi TI TPS63070) – automatický buck-boost
  převodník, drží 3.3V ze 2× AAA i s klesajícím napětím baterií (viz
  [Napájecí modul XL63070](#napájecí-modul-xl63070-tps63070))
- **Napájecí vypínač** – mezi baterií a XL63070 (fyzický spínač, bez GPIO)
- Napájení: 2× AAA baterie (přes XL63070)

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

## Piny (node, ESP32-C3-SuperMini) – zapojení fyzicky NEOVĚŘENO (kontinuitou/multimetrem)

| Signál | Pin | Poznámka |
|---|---|---|
| I2C SDA | GPIO4 | mimo strapping piny (2/8/9) a JTAG (5/6/7) |
| I2C SCL | GPIO3 | mimo strapping piny (2/8/9) a JTAG (5/6/7) |
| Napájení senzoru (MOSFET gate) | GPIO10 | aktivní LOW = senzor zapnutý; GPIO0/1 záměrně ponechány volné pro budoucí ADC měření napětí baterie |

GPIO8 a GPIO9 se pro I2C záměrně nepoužívají – jsou to strapping piny
(ovlivňují bootovací mód) a navíc GPIO8 je sdílené se zabudovanou LED a
GPIO9 s tlačítkem BOOT. Zdroj: [Last Minute Engineers – ESP32-C3 Super
Mini Pinout Reference](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/).

**Pull-up rezistory na I2C:** SDA i SCL potřebují pull-up proti 3.3V (I2C
je open-drain sběrnice). ENS160+AHT21 kombo moduly (klony DFRobot Gravity
SEN0515) je mívají zapájené přímo na desce – nutno zkontrolovat na
konkrétním kusu (vizuálně, nebo multimetrem odpor SDA↔3.3V a SCL↔3.3V).
Pokud tam nejsou, přidat externě 2× 4.7kΩ – **na spínanou (MOSFETem)
větev 3.3V senzoru, ne na trvale živou 3.3V ESP32** (viz
[Napájecí režim node](#napájecí-režim-node-deep-sleep--spínání-senzoru)) –
jinak by senzor mohl být částečně zpětně napájený přes I2C linky i s
vypnutým MOSFETem. Interní pull-up ESP32-C3 (~45kΩ) je na spolehlivý provoz
moc slabý, nespoléhat na něj samotný. Stačí jedna sada pro celou sběrnici
(ne per-čip, i když ENS160 a AHT21 jsou dva čipy na jednom modulu).

### Zapojení konkrétního senzorového modulu

Koupený modul má 8 pinů: `VIN, 3V3, GND, SCL, SDA, ADD, CS, INT` (na desce
samotné, dle fotky, značené i s alternativní SPI funkcí:
`SCL/SCLK, SDA/MOSI, ADD/MISO, CS, INT`) – potvrzuje, že jde o ENS160
s volitelným I2C/SPI rozhraním (+ AHT21 na stejné I2C sběrnici, bez
vlastních konfiguračních pinů, pevná adresa). Pojmenování pinů na desce
vizuálně odpovídá ENS160 datasheetu (2026-08-07, foto modulu).

| Pin senzoru | Připojit na | Proč |
|---|---|---|
| 3V3 | **spínaná** 3.3V (výstup MOSFETu) | napájení – VIN jde přes palubní LDO, který potřebuje >3.3V na vstupu (typicky ~4.3V+), takže se z 3.3V zdroje nepoužívá |
| VIN | nezapojeno | viz výše |
| GND | GND | – |
| SCL | GPIO3 (ESP32-C3) | I2C clock |
| SDA | GPIO4 (ESP32-C3) | I2C data |
| ADD | **spínaná** 3.3V (stejný net jako 3V3) | HIGH → I2C adresa ENS160 `0x53` (LOW by dalo `0x52`) – podle [ENS160 datasheetu](https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf); nutno sedět s adresou použitou v `ScioSense_ENS16x` knihovně v kódu |
| CS | **spínaná** 3.3V (stejný net jako 3V3) | HIGH = I2C režim (LOW by přepnul čip do SPI) – nenechávat volně viset |
| INT | nezapojeno | firmware zatím jen polluje senzor, přerušení nevyužívá |

**Proč ADD/CS na spínanou větev, ne na trvale živou 3.3V:** pokud by ADD/CS
(nebo pull-upy) zůstaly na 3.3V, která běží i když je senzor "vypnutý" MOSFETem,
mohl by senzor being částečně napájený zpětně přes ochranné diody těchto
pinů – MOSFET by tak reálně nešetřil tolik energie, kolik by se čekalo.
Když všechny piny senzoru (VCC, ADD, CS, případné pull-upy) visí na stejné
spínané větvi, při vypnutí MOSFETu je celý modul skutečně bez napětí.

Zapojení fyzicky NEOVĚŘENO – založeno na ENS160 datasheetu, ne na testu s
konkrétním kusem modulu.

**Další potvrzené specifikace ze zápisku k produktu:** rozhraní I2C i SPI
(potvrzuje výše), MOX senzor pro až 4 nezávislé plyny (TVOC, eCO2, AQI
výstupy), integrovaná automatická baseline korekce, provozní rozsah
-40 až +85°C / 5-95% RH. `VDD1 1.71-1.98V` je interní napájecí větev čipu
(z interního LDO), ne externí napájecí požadavek – modul se napájí přes
3V3 pin jako obvykle, tohle nic nemění na zapojení.

## Napájecí modul XL63070 (TPS63070)

Koupený modul je označený `XL63070`, ne `XL6007` – to je důležitý rozdíl,
byla to prvně chybně zaměněná součástka. `XL63070` moduly jsou postavené na
čipu **TPS63070** (Texas Instruments), skutečném buck-boost převodníku, ne
na čistě boost čipu XL6007 (ten by pro 2× AAA nebyl vhodný – potřebuje
vstup min. ~3.6V, což by čerstvé 2× AAA sotva splnily a s vybíjením by
brzy přestal fungovat).

Potvrzené specifikace (z popisků prodejce/výrobce modulu, ne z fyzického
testu):

- Vstupní napětí: 2V–16V (2× AAA, i vybité ~1V/článek = 2V, je na spodní
  hraně rozsahu)
- Výstupní napětí: 2.5V–9V, konkrétní modul volitelný 3.3V/5V/9V
  (mechanismus volby na koupeném kusu - pravděpodobně pájecí propojka/jumper
  - zatím NEOVĚŘENO, potřeba foto fyzického modulu)
- Pracovní režim PWM/PFM (automatické přepínání) – PFM zvyšuje účinnost
  při malém odběru, což sedí na to, že modul většinu času napájí jen
  hluboce spící ESP32 (řádově µA) mezi krátkými špičkami při měření/vysílání
- Max. výstupní proud 2A (víc než dost pro ESP32-C3 + senzor)
- Nízký ripple (~8mV při 3.3V výstupu)

Buck-boost topologie (na rozdíl od prostého boost) znamená, že modul umí
držet 3.3V i kdyby vstup z baterií byl chvíli nad i pod touto hodnotou -
pro 2× AAA (nominál ~3V, postupně klesá) je to vhodnější volba.

**Zapojení (obecný princip, přesné pájecí body NEOVĚŘENY):**

| Pin modulu | Připojit na |
|---|---|
| VIN+ | + z baterií (přes napájecí vypínač) |
| VIN- / GND | - z baterií |
| VOUT+ | 3.3V napájecí větev (ESP32-C3, MOSFET source, senzor) |
| VOUT- / GND | společná GND |

Output je potřeba nastavit/potvrdit na 3.3V (u víceúčelových modulů typicky
pájecí propojka nebo DIP switch) - upřesnit až podle fotky konkrétního kusu.

## Napájecí režim node (deep sleep + spínání senzoru)

ENS160 má provozní proud **~29mA**. Doba zahřívání po zapnutí napájení, než
dá platné hodnoty, se v dostupných zdrojích rozchází:

- SparkFun/DFRobot hookup guide (nezávislé zdroje, odvozené z datasheetu):
  **~3 minuty**
- Popisek konkrétního koupeného modulu: **"< 1 minute warm-up"** + navíc
  samostatně **"< 1 hour start"** (pravděpodobně čas na plné ustálení
  automatické baseline korekce, ne blokující požadavek před první hodnotou)

Zvoleno **1 minuta** (`SENSOR_WARMUP_MS` v kódu) podle popisku konkrétního
modulu – NEOVĚŘENO na HW. Pokud by po nasazení první hodnoty po probuzení
byly nestabilní/nesmyslné, prodloužit zpět směrem ke 3 minutám.

Proud senzoru výrazně převyšuje spotřebu ESP32-C3 v deep sleep (řádově µA),
takže klíčové pro výdrž baterie je **hlavně to, jak dlouho běží senzor**,
ne samotný deep sleep ESP32.

Proto: `src/node/main.cpp` zapíná senzor přes P-MOSFET (GPIO10, aktivní
LOW) jen na dobu zahřátí + čtení, pak ho vypne a teprve potom odešle data
přes ESP-NOW a jde spát (`esp_deep_sleep_start()` s timer wakeup).
Poradové číslo paketu (`packetSeq`) je v `RTC_DATA_ATTR` paměti, která na
rozdíl od běžných globálních proměnných deep sleep přežije.

Odhad výdrže 2× AAA (~1000mAh reálně přes XL63070) podle intervalu probouzení
(1 min zahřívání ENS160 na každý cyklus):

| Interval probouzení | Podíl doby zapnutí senzoru | Průměrný proud | Odhad výdrže |
|---|---|---|---|
| 15 min | ~6.7 % | ~1.9mA | ~3 týdny |
| 30 min | ~3.3 % | ~1.0mA | ~6 týdnů |
| 60 min | ~1.7 % | ~0.5mA | ~3 měsíce |

(Pokud by se zahřívání muselo vrátit na 3 minuty, viz předchozí verze
tabulky v historii commitů – řádově 3× horší výdrž při stejném intervalu.)

Výchozí hodnota v kódu je `MEASURE_INTERVAL_US = 15 minut` – uprav podle
požadované rovnováhy mezi čerstvostí dat a výdrží baterie.

**MOSFET zapojení (návrh, NEOVĚŘENO na HW):** P-kanálový logic-level MOSFET
(např. AO3401A, SOT-23) – Source na trvale živou 3.3V (výstup XL6007), Drain
na spínanou 3.3V větev senzoru, Gate na GPIO10 (ESP32 3.3V logika dá dostatečné
Vgs na sepnutí) + pull-up rezistor ~10kΩ z Gate na trvale živou 3.3V (zajistí
výchozí vypnutý stav, dokud firmware GPIO explicitně nenastaví). Volitelně
sériový rezistor ~100-220Ω mezi GPIO a Gate.

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
- **P-MOSFET load switch pro senzor** – konkrétní součástka (navrženo
  AO3401A) a hodnoty rezistorů jsou jen odhad, needs ověřit na HW. Pokud
  bude ADD/CS/pull-upy nakonec zapojeno na trvale živou 3.3V místo spínané
  větve, přepočítat dopad na výdrž baterie (viz
  [Napájecí režim node](#napájecí-režim-node-deep-sleep--spínání-senzoru)).
- **Interval probouzení node** (`MEASURE_INTERVAL_US`, výchozí 15 min) a
  doba zahřívání senzoru (`SENSOR_WARMUP_MS`, výchozí 3 min) – zatím
  odhad z datasheetu, doladit podle reálné výdrže/potřeby čerstvosti dat.
- **IDLE režim ENS160** – čip má kromě STANDARD i IDLE/DEEP_SLEEP režimy s
  nižší spotřebou, možná rychlejším náběhem než studený start – zatím
  nevyužito (firmware čip vždy úplně vypíná/zapíná přes MOSFET). Přesné
  proudy pro IDLE se nepodařilo dohledat z veřejných zdrojů, chtělo by to
  změřit na reálném HW.
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
