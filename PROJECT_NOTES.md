# MeasureQ – poznámky k firmwaru

Průběžně aktualizovaný přehled hardwaru, zjištěných faktů a softwarové
architektury. Projekt založen 2026-08-07, zatím bez fyzického HW – všechny
piny a části API knihoven jsou nutně jen předpoklad, dokud se neověří na
reálné desce (viz [Otevřené otázky / TODO](#otevřené-otázky--todo)).

## Cíl projektu

Bezdrátové měřicí zařízení: 3 samostatné baterkové měřicí krabičky posílají
data přes ESP-NOW do jedné centrální jednotky (hub), která je přeposílá do
**Google Sheets** přes WiFi.

**ZMĚNA NÁVRHU (2026-08-08):** původně měl mít hub fyzické displeje
(3× TM1637) a tlačítka pro zobrazení hodnot přímo na místě. Místo toho se
teď všechna data zaznamenávají do Google Sheets (přes Google Apps Script
jako jednoduché HTTP rozhraní) – žádné fyzické zobrazení, sledování přes
telefon/počítač. Hub tak zůstává jen jako "relay" mezi bateriovými
krabičkami (ESP-NOW, žádný přístup k internetu) a WiFi/internetem.

## Hardware – přehled

### Měřicí krabička (node) – 3×

- **ESP32-C3-SuperMini** – hlavní MCU, WiFi/ESP-NOW vysílání dat do hubu,
  většinu času v deep sleep (viz [Napájecí režim node](#napájecí-režim-node-deep-sleep--spínání-senzoru))
- **ENS160 + AHT21** (I2C kombo modul) – CO2/eCO2, TVOC, teplota, vlhkost
- **PNP tranzistor BC557** – high-side spínač 3.3V větve senzoru řízený
  GPIO (senzor je bez napájení, dokud ESP32 spí – viz níže)
- **XL63070** (modul na bázi TI TPS63070) – automatický buck-boost
  převodník, drží 3.3V ze 2× AAA i s klesajícím napětím baterií (viz
  [Napájecí modul XL63070](#napájecí-modul-xl63070-tps63070))
- **Napájecí vypínač** – mezi baterií a XL63070 (fyzický spínač, bez GPIO)
- Napájení: 2× AAA baterie (přes XL63070)

### Centrální jednotka (hub) – 1×

- **ESP32 30pin** (generický DOIT DEVKIT V1 klon) – příjem dat přes
  ESP-NOW od node krabiček, přeposlání do Google Sheets přes WiFi (HTTP
  GET na Google Apps Script web app)
- Žádné displeje ani tlačítka – čistě "relay" mezi ESP-NOW (bateriové
  krabičky) a WiFi/internetem
- Napájení: USB-C přímo do ESP32 (potřebuje být trvale připojený k WiFi)

**WiFi heslo a URL Google Sheets** se nekomitují do gitu – viz
`src/hub/secrets.h.example` (šablona) a postup nastavení Google Apps
Scriptu v sekci [Google Sheets logování](#google-sheets-logování) níže.

## Piny (node, ESP32-C3-SuperMini)

| Signál | Pin | Poznámka |
|---|---|---|
| I2C SDA | GPIO4 | **POTVRZENO kontinuitou multimetrem (2026-08-08)** – původní předpoklad byl správně; viz [Zjištění z prvního testu na reálném HW](#zjištění-z-prvního-testu-na-reálném-hw-2026-08-08) o zmatku kolem měření |
| I2C SCL | GPIO3 | POTVRZENO kontinuitou multimetrem (2026-08-08) |
| Napájení senzoru (báze PNP tranzistoru BC557, přes R1) | GPIO10 | aktivní LOW = senzor zapnutý; GPIO0/1 záměrně ponechány volné pro budoucí ADC měření napětí baterie |

**Pozor – GPIO4 je defaultně JTAG pin (MTMS) na ESP32-C3** (dřívější
poznámka v tomhle souboru mylně uváděla JTAG piny jako 5/6/7, správně je to
4/5/6/7). V běžném Arduino/PlatformIO programu bez aktivně zapnutého JTAG
debuggeru by to nemělo vadit (piny fungují jako normální GPIO), ale
zůstává to otevřená otázka, pokud by I2C na GPIO3/4 dál nefungovalo i po
vyloučení ostatních příčin – zvážit přesun na jiný pin (např. GPIO0/1).

GPIO8 a GPIO9 se pro I2C záměrně nepoužívají – jsou to strapping piny
(ovlivňují bootovací mód) a navíc GPIO8 je sdílené se zabudovanou LED a
GPIO9 s tlačítkem BOOT. Zdroj: [Last Minute Engineers – ESP32-C3 Super
Mini Pinout Reference](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/).

**Pull-up rezistory na I2C:** SDA i SCL potřebují pull-up proti 3.3V (I2C
je open-drain sběrnice). **POTVRZENO měřením (2026-08-07):** multimetr mezi
SDA/SCL a 3V3 na koupeném modulu ukázal ~4MΩ – to je o tři řády víc, než
by měl mít skutečný I2C pull-up (typicky 2.2–10kΩ), takže jde jen o
zbytkový svod (např. přes ochranné diody čipu), ne o funkční pull-up.
**Modul pull-upy nemá** – nutné přidat externě **R3 a R4 (4.7kΩ)**.

Protože finální topologie je PNP high-side (spíná se 3.3V senzoru, ne
GND), R3/R4 patří na **spínanou** 3.3V větev (za Q1, stejný net jako
3V3/ADD/CS senzoru) – ne na trvale živou 3.3V ESP32. Jinak by mohl senzor
zůstat částečně napájený přes pull-upy i s vypnutým Q1. Viz
[Napájecí režim node](#napájecí-režim-node-deep-sleep--spínání-senzoru).

Interní pull-up ESP32-C3 (~45kΩ) je na spolehlivý provoz moc slabý,
nespoléhat na něj samotný. Stačí jedna sada pro celou sběrnici (ne
per-čip, i když ENS160 a AHT21 jsou dva čipy na jednom modulu).

### Zapojení konkrétního senzorového modulu

Koupený modul má 8 pinů: `VIN, 3V3, GND, SCL, SDA, ADD, CS, INT` (na desce
samotné, dle fotky, značené i s alternativní SPI funkcí:
`SCL/SCLK, SDA/MOSI, ADD/MISO, CS, INT`) – potvrzuje, že jde o ENS160
s volitelným I2C/SPI rozhraním (+ AHT21 na stejné I2C sběrnici, bez
vlastních konfiguračních pinů, pevná adresa). Pojmenování pinů na desce
vizuálně odpovídá ENS160 datasheetu (2026-08-07, foto modulu).

| Pin senzoru | Připojit na | Proč |
|---|---|---|
| 3V3 | **spínaná** 3.3V (Kolektor Q1) | napájení – VIN jde přes palubní LDO, který potřebuje >3.3V na vstupu (typicky ~4.3V+), takže se z 3.3V zdroje nepoužívá |
| VIN | nezapojeno | viz výše |
| GND | trvale živá GND (společná s ESP32) | u high-side spínače (Q1 = PNP) se GND nikdy nepřerušuje - viz [Napájecí režim node](#napájecí-režim-node-deep-sleep--spínání-senzoru) |
| SCL | GPIO3 (ESP32-C3) | I2C clock – POTVRZENO kontinuitou (2026-08-08) |
| SDA | GPIO4 (ESP32-C3) | I2C data – POTVRZENO kontinuitou (2026-08-08) |
| ADD | **spínaná** 3.3V (stejný net jako 3V3) | HIGH → I2C adresa ENS160 `0x53` (LOW by dalo `0x52`) – podle [ENS160 datasheetu](https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf); nutno sedět s adresou použitou v `ScioSense_ENS16x` knihovně v kódu |
| CS | **spínaná** 3.3V (stejný net jako 3V3) | HIGH = I2C režim (LOW by přepnul čip do SPI) – nenechávat volně viset |
| INT | nezapojeno | firmware zatím jen polluje senzor, přerušení nevyužívá |

**Proč high-side (spíná se 3.3V senzoru, ne GND):** zvoleno místo
low-side NPN varianty, protože PNP (BC557) je i tak dostupný a high-side
nemá nevýhodu, kterou low-side má – u NPN low-side by zbytkové napětí
Vce(sat) na tranzistoru posunulo GND senzoru vůči GND ESP32, což by
teoreticky mohlo narušit logické úrovně na sdílené I2C sběrnici. U
PNP high-side zůstává GND všude společná, takže k tomu nedochází; místo
toho se "ztrácí" jen pár desetin voltu na straně napájení senzoru
(3.3V − Vce(sat) ≈ 3.1–3.2V), což senzor typicky toleruje bez problémů.

Zapojení fyzicky NEOVĚŘENO – založeno na ENS160 datasheetu, ne na testu s
konkrétním kusem modulu.

### Zjištění z prvního testu na reálném HW (2026-08-08)

Po zapojení první krabičky (senzor + Q1 + pull-upy na breadboardu) a nahrání
`src/node_test/main.cpp` senzor **stále nekomunikuje po I2C** (potvrzeno LED
indikátorem – viz níže – i opakovaným `[E][Wire.cpp:513] requestFrom():
i2cRead returned Error -1` v Serial logu při prvních pokusech). Co už je
vyloučené jako příčina (ověřeno multimetrem):

- **Piny SDA/SCL** – potvrzeno kontinuitou: SDA=GPIO4, SCL=GPIO3, sedí s
  kódem. (Cestou k tomuhle závěru došlo k dočasnému omylu – SDA/SCL byly na
  chvíli v kódu prohozené kvůli chybnému čtení kontinuity, kdy se popletl
  pin "3.3V" s pinem "3" na desce senzoru. Opraveno zpět.)
- **Pull-up rezistory R3/R4 (4.7kΩ)** – potvrzeno napěťovým měřením: na
  SDA i SCL je při zapnutém senzoru stabilních ~3.3V (ne plovoucí/nulové).
- **GND kontinuita** mezi ESP32 a senzorovým modulem – potvrzeno.
- **Kontinuita signálních vodičů** (ESP32 GPIO ↔ piny senzoru) – potvrzeno,
  žádný přerušený spoj na breadboardu.
- **CS a ADD piny senzoru na 3.3V** – potvrzeno správně podle ENS160
  datasheetu (CSn HIGH = I2C režim, LOW by přepnul na SPI; ADD HIGH = I2C
  adresa `0x53`). Není to invertované.

**LED indikátor stavu (`src/node_test/main.cpp`):** vestavěná LED na
ESP32-C3-SuperMini (GPIO8, aktivní LOW) bliká, když firmware úspěšně
komunikuje s oběma senzory (AHT21 i ENS160), a trvale svítí, když ne –
užitečné pro rychlou vizuální kontrolu, obzvlášť dokud nebyl vyřešený
problém s neviditelným Serial výstupem (viz níže).

**VÝSLEDEK (2026-08-08): I2C funguje, hodnoty potvrzené na reálném HW.**
S finálním zapojením (SDA=GPIO4, SCL=GPIO3 – stejné jako úplně původní
předpoklad) LED bliká a Serial Monitor ukazuje stabilní čtení:

```
Teplota:  32.1 C   Vlhkost:  33.2 %   eCO2:   400 ppm   TVOC:    24 ppb
```

(eCO2 ~400ppm = normální atmosférická baseline, TVOC nízké = čistý vzduch,
vlhkost ~33% běžná pokojová hodnota; teplota 32°C o něco vyšší než pokojová
– pravděpodobně mírné samo-zahřívání v uzavřené krabičce od MOX ohřívače
ENS160, netestováno mimo krabičku).

**Skutečná příčina původního `Error -1` zůstává nejistá** (zapojení bylo
mezitím vícekrát fyzicky sondováno multimetrem, takže je možné, že šlo o
nespolehlivý kontakt na breadboardu, který se "opravil" sám). Zato se
podařilo najít a opravit **samostatný, nezávislý problém**, který
znemožňoval cokoliv vidět na Serial Monitoru (viz
[Chybějící Serial výstup](#chybějící-serial-výstup---arduino_usb_cdc_on_boot-2026-08-08)
níže) – i po vyřešení I2C komunikace (LED blikala) nešlo dlouho ověřit
skutečné hodnoty, protože `Serial.print()` fyzicky nikam neodcházelo.

### Chybějící Serial výstup - ARDUINO_USB_CDC_ON_BOOT (2026-08-08)

I když LED indikátor potvrzoval, že firmware běží a I2C komunikuje, po
dlouhou dobu se nedařilo na Serial Monitoru (ani mém automatizovaném, ani
tom spuštěném přímo uživatelem ve VS Code terminálu) zobrazit vůbec nic
kromě ROM bootloader hlášky.

**Příčina:** ESP32-C3-SuperMini nemá externí USB-UART čip (CP2102/CH340) –
má jen vestavěný nativní USB-Serial/JTAG řadič. ROM bootloader hlášky vždy
jdou přes tenhle řadič (proto byly vždy vidět), ale bez explicitních build
flagů Arduino framework defaultně mapuje `Serial` na klasickou UART0
periferii (fyzické piny GPIO20/21), které na tomhle konkrétním boardu
nikam nevedou. `Serial.print()` volání tedy v kódu proběhla v pořádku
(proto LED blikala normálně), ale data fyzicky nikdy nedorazila k USB.

**Oprava:** přidán `build_flags` do `[node_common]` v `platformio.ini`:

```ini
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

Tím se `Serial` přesměruje na stejný vestavěný USB-Serial/JTAG řadič, který
už beztak firmware používá pro ROM hlášky a flashování. Platí pro `env:node`
i `env:node-test` (obě běží na stejné desce).

### Mereni CO2/TVOC vypnuto - self-heating vyšetřování (2026-08-08)

Po prvním porovnání s reálným teploměrem (naměřeno 32.8°C na senzoru vs
~26°C na stolním teploměru) se ukázalo, že AHT21 na tomhle kombo modulu
měří výrazně vyšší teplotu, než je skutečnost. Dan primárně potřebuje
**přesnou teplotu/vlhkost** – eCO2/TVOC (MOX senzor) je až druhotné.

**Hypotéza č.1 (self-heating od MOX ohřívače ENS160, 200-300°C):** logicky
zdůvodněná a potvrzená jako obecně známý jev u těchhle kombo modulů
(zdroj: [Zbotic - ENS160+AHT21 combo](https://zbotic.in/ens160-aht21-combo-air-quality-and-climate-in-one-board/)).
Modul má dokonce vyfrézovanou izolační drážku v PCB kolem jednoho z čipů
(pravděpodobně kolem ENS160), což naznačuje, že si toho byl vědom i
výrobce.

**Test:** vypnuto volání `ens160.startStandardMeasure()` (jediné, co
spouští MOX ohřívač – bez něj čip zůstává v IDLE, potvrzeno jako
"low-power" stav v knihovně). ENS160 se stále detekuje (`init()`), jen
nikdy nezačne aktivně měřit.

**Výsledek prvního testu (senzor bežící dlouho v kuse): teplota se
prakticky NEZMĚNILA** (32.5-32.6°C i s vypnutým ohřívačem) – hypotéza č.1
tedy **buď neplatí, nebo není jediná příčina**.

**Výsledek testu po vychladnutí (2026-08-08, odpojeno ~5-10 min, pak
znovu zapojeno):** hned po startu 28.4°C, po ustálení (~5 min běhu)
**stabilně 28.7-28.9°C** (vs ~26°C na stolním teploměru, tedy offset
**~2.5-2.9°C**) – vlhkost kolísala 37.7-41.5%, coz vypadá jako realne
kolisani vzduchu v mistnosti, ne vada senzoru. **Klíčové zjištění: teplota
se ustálila na stabilní hodnotě, NEROSTLA donekonečna** jako při dlouhém
nepřetržitém běhu. To potvrzuje, že drtivá většina toho původního 6-7°C
posunu byla nashromážděné teplo z dlouhé testovací session (mnoho
re-flashů za sebou bez skutečného vychladnutí), ne vlastnost samotného
modulu. Zbylý ~2.5-2.9°C offset i s vypnutým ENS160 ohřívačem je
pravděpodobně od **ESP32-C3 samotného** (CPU, napěťový regulátor) nebo
obecně provozu desky, ne od ENS160.

**Rozhodnutí:** i 2.5-2.9°C je pro Danovy účely moc (chce znát skutečnou
teplotu, ne "nějak zvýšenou o neznámou hodnotu") – jde se směrem
**samostatného senzoru AHT21/AHT20/AHT21B** (viz níže), umístěného na
kabelu mimo desku ESP32-C3/breadboard, ne nalepeného přímo na ni.
Dodatečné zjištění po odsunutí modulu dál od desky (stále v jednom celku
s ENS160): offset se nezměnil – ENS160 a AHT21 jsou na stejné malé
destičce a sdílí napájení přes Q1, takže je nejde softwarově oddělit;
zbytkové teplo je od ENS160 klidového proudu (I2C aktivní, i bez
`startStandardMeasure()`), ne od vzdálenosti k ESP32-C3.

**PROVIZORNÍ softwarová kalibrace (2026-08-08),** dokud nedorazí
samostatný senzor – `TEMP_CALIBRATION_OFFSET_C` a
`HUMIDITY_CALIBRATION_OFFSET_PCT` v `src/node/main.cpp` i
`src/node_test/main.cpp`:

| Veličina | Syrová hodnota | Reference | Offset |
|---|---|---|---|
| Teplota | 28.9°C | 26.4°C (stolní teploměr) | **-2.5°C** |
| Vlhkost | 39.4% | 44% (referenční vlhkoměr) | **+4.6%** |

Vlhkostní offset je jen hrubý odhad – relativní vlhkost je fyzikálně
svázaná s teplotou (teplejší vzduch pojme víc vodní páry při stejném
% RH), takže pevný aditivní offset je přesný jen poblíž podmínek, za
kterých byl změřen, ne obecně. Pro účely tohoto projektu (ne laboratorní
přesnost) to stačí. **Až dorazí samostatný senzor, oba offsety
přepočítat/smazat** – měly by být blízko nule, protože zdroj zkreslení
(sdílená deska s ENS160) zmizí.

**Rozhodnutí (nezávisle na výsledku self-heating testu):** měření
CO2/TVOC (`ens160.startStandardMeasure()`) zůstává **záměrně vypnuté** v
`src/node/main.cpp` i `src/node_test/main.cpp` – `packet.co2Ppm`/`tvocPpb`
se posílají jako `0`. Duvod: Dan prioritizuje přesnou teplotu/vlhkost;
pokud by se ukázalo, že ohřívač skutečně přispívá k self-heatingu (i když
test naznačuje, že možná ne jako hlavní příčina), aktivní měření plynů by
tomu jen škodilo. Snadno se znovu zapne odkomentováním
`startStandardMeasure()` volání, pokud by CO2/TVOC data byla v budoucnu
zase potřeba.

Vedlejší efekt vypnutí: `SENSOR_WARMUP_MS` (1 min) byl původně odvozený
od doby zahřívání ENS160 MOX ohřívače – bez aktivního měření je teoreticky
zbytečně dlouhý (AHT21 je téměř okamžitý), coz by mohlo výrazně zlepšit
výdrž baterie. **Hodnota zatím NEZKRÁCENA** – vyžaduje přepočítat tabulku
výdrže baterie výše a promyslet zvlášť.

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
| VOUT+ | 3.3V napájecí větev (ESP32-C3 3V3, Emitor Q1) |
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

Proto: `src/node/main.cpp` zapíná senzor přes PNP tranzistor BC557
(GPIO10, aktivní LOW) jen na dobu zahřátí + čtení, pak ho vypne a teprve
potom odešle data přes ESP-NOW a jde spát (`esp_deep_sleep_start()` s
timer wakeup).
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

**PNP tranzistorový spínač Q1 = BC557 (finální volba, součástka
potvrzená – Dan má BC547/BC557 po ruce):** high-side spínač (spíná 3.3V
větev senzoru, ne GND):

| Vývod Q1 | Připojit na |
|---|---|
| Emitor (E) | trvale živá 3.3V (výstup XL63070, stejná větev jako ESP32 3V3) |
| Kolektor (C) | spínaná 3.3V větev → napájí 3V3/ADD/CS senzoru |
| Báze (B) | rezistor **R1 = 1kΩ** na GPIO10 **+** rezistor **R2 = 10kΩ** (pull-up) na Emitor/3.3V |

Fyzický pinout BC557 (TO-92, popsaná strana k uživateli, nožičky dolů):
typicky zleva doprava **E-B-C** (Fairchild/ON Semi konvence) – ověřit
podle konkrétního kusu, pinout se mezi výrobci občas liší.

Výpočet R1: při GPIO10=LOW (0V) a Vbe≈0.7V zbývá na R1 ~2.6V; při cíli
Ib≈2.6mA (forced beta ~11 pro Ic≈29mA) vychází R1 ≈ 1kΩ. GPIO10 tím
sink-uje ~2.6mA, hluboko pod limitem ESP32 GPIO (bezpečně do ~20mA).

**Potvrzené parametry BC557 z datasheetu** (ne jen odhad – viz
[BC557 datasheet](https://www.mouser.com/datasheet/2/149/fairchild%20semiconductor_bc559-320188.pdf)):
hFE 110–800 podle gradace (i nejhorší grade A: 110–220 – naší dimenzi
s forced beta ~11 dává obrovskou rezervu), Vce(sat) jen -90 až -300mV při
Ic=10mA/Ib=0.5mA (při našem silnějším buzení pravděpodobně blíž dolní
hranici), Ic max -100mA (3× naše potřeba), výkonová ztráta na tranzistoru
při Ic=29mA a Vce(sat)~0.15V ≈ 4mW – zanedbatelné, žádný tepelný problém.

Logika: `GPIO10 LOW = senzor zapnutý` (R2 jako pull-up zajišťuje výchozí
vypnutý stav, dokud firmware GPIO explicitně nenastaví na LOW). Odpovídá
`sensorPowerOn()`/`sensorPowerOff()` v `src/node/main.cpp`.

R1/R2 hodnoty samotné jsou stále jen návrh (NEOVĚŘENO na reálném
zapojení), i když součástka (BC557) a její datasheetové parametry už jsou
potvrzené.

## Google Sheets logování

Hub (`src/hub/main.cpp`) přeposílá přijatá data z node krabiček do Google
Sheets přes jednoduché HTTP rozhraní postavené na **Google Apps Script**.

**Postup nastavení (jednorázově, v Google účtu):**

1. Vytvořit nový Google Sheet ([sheets.google.com](https://sheets.google.com))
2. **Extensions → Apps Script**, vložit tenhle kód (smazat výchozí
   `function myFunction() {}`):

```javascript
// Radek 1-3: "aktualni hodnoty" souhrn (B/C/D = M1/M2/M3), vzdy na zacatku
// listu, prepisuje se pri kazdem prijatem paketu. Radek 5+: historicky log
// (jeden appendovany radek na kazdy prijaty paket) - kazdy node ma
// VLASTNI trojici sloupcu Cas/Teplota/Vlhkost (ne jeden sdileny cas), aby
// M1/M2/M3 slo v grafu porovnat i pres to, ze kazdy posila v jiny okamzik
// (vlastni nezavisly deep-sleep cyklus). eCO2/TVOC zamerne vynechano
// (senzor vypnuty, viz PROJECT_NOTES.md "Vypnout mereni CO2/TVOC").
var NODE_LABELS = ['M1', 'M2', 'M3'];
var SUMMARY_HEADER_ROW = 1;
var SUMMARY_TEMP_ROW = 2;
var SUMMARY_HUM_ROW = 3;
var LOG_HEADER_ROW = 5;

function doGet(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();

  if (sheet.getRange(SUMMARY_HEADER_ROW, 1).getValue() === '') {
    sheet.getRange(SUMMARY_HEADER_ROW, 1).setValue('Aktualni hodnoty:');
    sheet.getRange(SUMMARY_HEADER_ROW, 2, 1, 3).setValues([NODE_LABELS]);
    sheet.getRange(SUMMARY_TEMP_ROW, 1).setValue('Teplota (C)');
    sheet.getRange(SUMMARY_HUM_ROW, 1).setValue('Vlhkost (%)');
    // Kazdy node ma vlastni trojici sloupcu (Cas/Teplota/Vlhkost) - node
    // se probouzi/posila nezavisle na ostatnich (vlastni deep-sleep cyklus),
    // takze spolecny jeden "Cas mereni" sloupec by pro 2 ze 3 nodu v danem
    // radku neodpovidal realnemu casu jejich mereni.
    sheet.getRange(LOG_HEADER_ROW, 1, 1, 9).setValues([[
      'M1 Cas mereni', 'M1 Teplota (C)', 'M1 Vlhkost (%)',
      'M2 Cas mereni', 'M2 Teplota (C)', 'M2 Vlhkost (%)',
      'M3 Cas mereni', 'M3 Teplota (C)', 'M3 Vlhkost (%)'
    ]]);
    // hodnotove bunky (ne popisky v A) zarovnat doprava - Apps Script by
    // je jinak zapisoval jako text (leva zarovnani), cisla maji byt
    // zarovnana jako obvykle cisla, at pod sebou pekne sedi.
    sheet.getRange(SUMMARY_TEMP_ROW, 2, 1, 3).setHorizontalAlignment('right');
    sheet.getRange(SUMMARY_HUM_ROW, 2, 1, 3).setHorizontalAlignment('right');
  }

  var params = e.parameter;
  var ts = params.ts ? new Date(parseInt(params.ts, 10) * 1000) : new Date();
  var nodeId = parseInt(params.nodeId, 10);
  var temp = params.temp !== undefined ? parseFloat(params.temp) : '';
  var hum = params.hum !== undefined ? parseFloat(params.hum) : '';

  var row = ['', '', '', '', '', '', '', '', ''];
  if (nodeId >= 0 && nodeId <= 2) {
    var col = 2 + nodeId; // B=M1, C=M2, D=M3 v souhrnu
    // POTVRZENO 2026-08-09: bunka casem naformatovana jako Datum (nekdo
    // do ni drive rucne napsal neco ve tvaru "23.1", Sheets si to
    // automaticky preformatovalo na datum den.mesic) - setValue() cislo
    // pak zobrazuje jako datum ("23.1.1900..."). setNumberFormat() to
    // pri kazdem zapisu vynuti zpet na normalni cislo.
    sheet.getRange(SUMMARY_TEMP_ROW, col).setValue(temp).setNumberFormat('0.0');
    sheet.getRange(SUMMARY_HUM_ROW, col).setValue(hum).setNumberFormat('0.0');

    var logCol = nodeId * 3; // 0-indexovano do row[]: M1=0, M2=3, M3=6
    row[logCol] = ts;
    row[logCol + 1] = temp;
    row[logCol + 2] = hum;
  }
  sheet.appendRow(row);
  // appendRow() novy radek cely zarovnat doprava (cas i cisla).
  var newRow = sheet.getLastRow();
  sheet.getRange(newRow, 1, 1, 9).setHorizontalAlignment('right');
  // Stejna ochrana jako u souhrnu - vynutit normalni cislo (ne datum) na
  // hodnotovych sloupcich noveho radku (cas ve sloupcich A/D/G zustava Date).
  if (nodeId >= 0 && nodeId <= 2) {
    var logCol1 = nodeId * 3 + 2; // Teplota (1-indexovano do sloupcu)
    sheet.getRange(newRow, logCol1, 1, 2).setNumberFormat('0.0');
  }

  return ContentService.createTextOutput("OK").setMimeType(ContentService.MimeType.TEXT);
}
```

(Verze s `ts` parametrem - viz [Fronta a časové razítko](#fronta-a-časové-razítko-2026-08-08) níže pro důvod. `appendRow()` vždy přidá za poslední neprázdný řádek, takže log pod souhrnem funguje bez ručního sledování čísla řádku.)

**POZOR při přechodu ze starého layoutu (2026-08-09):** stará data mají sloupce
`Cas mereni, Node ID, Teplota, Vlhkost, eCO2, TVOC` v řádcích od 1 - nový
skript čeká souhrn v řádcích 1-3 a hlavičku logu v řádku 5. Buď staré řádky
přesunout na jiný list (archiv), nebo smazat, než skript poprvé zapíše
(jinak by se souhrn zapsal doprostřed starých dat).

3. Uložit, pak **Nasadit (Deploy) → Nové nasazení → Webová aplikace**
4. **Spustit jako:** Já. **Kdo má přístup:** Kdokoli (DŮLEŽITÉ – jinak ESP32
   dostane přesměrování na přihlašovací stránku Google místo odpovědi,
   viz níže)
5. Autorizovat (kliknout skrz varování "Google neověřil tuto aplikaci" –
   je to vlastní skript, to je v pořádku)
6. Zkopírovat URL webové aplikace (`.../exec`)

**POTVRZENO chybou (2026-08-08):** pokud přístup není nastavený na
"Kdokoli", ESP32 dostane při GET požadavku HTML přihlašovací stránku
Google místo odpovědi "OK" – vypadá to jako by skript nefungoval, ale ve
skutečnosti jde jen o špatné nastavení přístupu u nasazení. Oprava: **Nasadit
→ Spravovat nasazení → tužka (upravit) → změnit "Kdo má přístup" na
"Kdokoli"**.

**WiFi heslo a URL** se ukládají do `src/hub/secrets.h` (kopie
`secrets.h.example`, v `.gitignore` – nikdy se nekomituje):

```cpp
constexpr char WIFI_SSID[] = "...";
constexpr char WIFI_PASSWORD[] = "...";
constexpr char SHEETS_WEBHOOK_URL[] = "https://script.google.com/macros/s/XXXXX/exec";
```

### Problém s WiFi kanálem – POTVRZENO a VYŘEŠENO (2026-08-08)

ESP-NOW vyžaduje stejný WiFi kanál na obou stranách. Hub se po připojení k
routeru ocitl na jeho kanálu (u Dana potvrzeno kanál 3), zatímco node se k
žádné síti nepřipojuje a bez zásahu zůstává na kanálu 1 – pakety vůbec
nedocházely. **Oprava:** node explicitně nastaví kanál přes
`esp_wifi_set_channel()` (viz `HUB_WIFI_CHANNEL` konstanta v
`src/node_test/main.cpp` a `src/node/main.cpp`) – hodnotu je nutné ručně
sladit s tím, co hub po připojení vypíše ("WiFi kanal: X"). Pokud se
kanál routeru v budoucnu změní (auto-kanál na routeru), je nutné tuhle
konstantu aktualizovat, nebo routeru nastavit pevný kanál.

### Problém s WiFi power-save – POTVRZENO a VYŘEŠENO (2026-08-08)

I se správně sladěným kanálem pakety pořád nedocházely. Příčina: ESP32
defaultně zapíná úsporný režim WiFi rádia (modem-sleep/power-save), když
je STA připojená k reálné AP – rádio pak není vždy plně "vzhůru" a může
zmeškat ESP-NOW broadcast pakety mimo okna, kdy poslouchá beacony od
routeru. **Oprava:** `WiFi.setSleep(false);` na hubu hned po úspěšném
připojení k WiFi (v `connectWiFi()`). Po týhle opravě pakety chodí
spolehlivě.

### Fronta a časové razítko (2026-08-08)

HTTP požadavek na Google Sheets je blokující a pomalý (1-3+ vteřiny) –
bez fronty by se pakety přijaté MEZI jednotlivými HTTP voláními ztrácely
(přepisovaly by "posledni prijaty paket"). Hub teď řadí pakety do kruhové
fronty (`PENDING_QUEUE_SIZE = 20` v `src/hub/main.cpp`) a zpracovává je
postupně v `loop()`.

Časové razítko se zaznamenává **hned při přijetí** ESP-NOW paketu (v
`onDataRecv()`), ne až při odeslání do Sheets – jinak by fronta
způsobovala rostoucí zpoždění mezi "kdy bylo opravdu změřeno" a "jaký čas
se zapíše do tabulky". Vyžaduje NTP synchronizaci (`setupTime()`,
`configTzTime()` s POSIX TZ řetězcem pro Europe/Prague) – bez ní by
zaznamenaný čas byl jen nesmyslný počet vteřin od 1.1.1970.

### Past: víc nasazení (deployments) se stejným Apps Scriptem

Při úpravě existujícího nasazení je nutné použít **Nasadit → Spravovat
nasazení → tužka (upravit existující) → Nová verze**, NE "Nové nasazení"
(to vytvoří úplně nové, jiné URL). Dan omylem vytvořil několik různých
nasazení téhož skriptu s různými URL, což způsobilo zmatek (upravoval
kód, ale testovali jsme starou, jinou URL, která se nikdy needitovala).
Řešení: v "Spravovat nasazení" zkontrolovat, která URL je u AKTUÁLNÍ
(nejnovější) verze, a tu použít v `secrets.h`. Staré/duplicitní nasazení
lze archivovat (ikona s šipkou dolů v panelu Konfigurace).

## Softwarová architektura

### Struktura PlatformIO projektu

Jeden `platformio.ini`, environments:

- `env:node` – produkční firmware node (deep sleep cyklus)
- `env:node-test` – diagnostický test senzoru pro node (viz níže; navíc
  posílá hodnoty přes ESP-NOW broadcast)
- `env:hub` – produkční firmware hubu (ESP-NOW příjem → Google Sheets)
- `env:hub-test` – jednoduchý testovací příjem senzorových dat (jen
  Serial výpis, bez WiFi/Sheets – užitečné pro izolaci ESP-NOW problémů
  od WiFi/HTTP problémů)
- `env:wifi-test-node` / `env:wifi-test-hub` – čistý test dosahu ESP-NOW
  (počítadlo paketů + ztrátovost, bez senzoru)

Všechny kompilují i `src/common/`.

### Diagnostický test senzoru (`src/node_test/main.cpp`)

Samostatný sketch pro ověření zapojení senzoru na krabičce, oddělený od
produkčního `src/node/main.cpp` (aby se testováním nezasahovalo do
deep-sleep cyklu). Rozdíly oproti produkčnímu kódu:

- **Nejde spát** – běží v nekonečné smyčce, čte a vypisuje hodnoty každých
  500ms přes Serial, aby šlo sledovat průběh.
- **Navíc posílá hodnoty přes ESP-NOW na broadcast adresu** (na rozdíl od
  produkčního `src/node/main.cpp`, který posílá cíleně na MAC hubu) – pro
  test "senzor + WiFi dohromady" proti `env:hub-test`.
- Na startu udělá **I2C scan** (`Wire.beginTransmission`/`endTransmission`
  na adresách 1–126) a vypíše, co našel – očekává se `0x38` (AHT21, pevná
  adresa) a `0x53` (ENS160, protože ADD je na 3.3V/HIGH). Pokud scan
  nenajde nic, je problém v zapojení (SDA/SCL, napájení přes Q1, GND,
  chybějící R3/R4), ne v knihovnách – řešit v tomhle pořadí.

Spuštění: vybrat environment `node-test` v PlatformIO panelu (VS Code),
nebo `pio run -e node-test -t upload -t monitor`.

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

## Otevřené otázky / TODO

- **Board definice** – `esp32-c3-devkitm-1` a `esp32doit-devkit-v1` v
  `platformio.ini` jsou nejbližší generické desky, ne nutně přesná shoda s
  koupenými klony. Ověřit po prvním flashi.
- **Piny node I2C** – POTVRZENO kontinuitou (SDA=GPIO4, SCL=GPIO3), viz
  [Zjištění z prvního testu na reálném HW](#zjištění-z-prvního-testu-na-reálném-hw-2026-08-08).
- **Knihovna ENS160** (`sciosense/ScioSense_ENS16x` v2.0.5) – API
  OVĚŘENO reálnou kompilací + inspekcí zdrojů nainstalované knihovny
  (2026-08-08): třída se jmenuje `ENS160`, ne `ScioSense_ENS16x`;
  `begin(&Wire, adresa)`, `init()`, `startStandardMeasure()`; čtení až po
  `wait()`+`update()==RESULT_OK`+`hasNewData()`, pak `getEco2()`/`getTvoc()`
  (malá písmena). Použito v `src/node_test/main.cpp` i `src/node/main.cpp`.
- **I2C senzor VYŘEŠENO (2026-08-08)** – potvrzeno reálnými hodnotami ze
  Serial Monitoru (teplota/vlhkost/eCO2/TVOC, viz
  [Zjištění z prvního testu na reálném HW](#zjištění-z-prvního-testu-na-reálném-hw-2026-08-08)),
  ne jen LED indikátorem. Přesná příčina původního `Error -1` selhání
  zůstává nejistá (pravděpodobně vadný kontakt na breadboardu).
- **Chybějící Serial výstup VYŘEŠENO (2026-08-08)** – chyběl
  `ARDUINO_USB_CDC_ON_BOOT` build flag, viz
  [Chybějící Serial výstup](#chybějící-serial-výstup---arduino_usb_cdc_on_boot-2026-08-08).
- **AHT21 self-heating – ROZDIAGNOSTIKOVÁNO (2026-08-08)** – po
  vychladnutí stabilní offset ~2.5-2.9°C (ne rostoucí), pravděpodobně od
  ESP32-C3/desky, ne od ENS160. Pro Dana pořád moc – **rozhodnuto pořídit
  samostatný senzor AHT21/AHT20/AHT21B** mimo desku ESP32-C3. Viz
  [Mereni CO2/TVOC vypnuto](#mereni-co2tvoc-vypnuto---self-heating-vyšetřování-2026-08-08).
  **Dalsi krok:** az dorazi novy senzor, upravit zapojeni/kod aby cetl z
  neho misto z kombo modulu (kombo modul zustane nepouzity, nebo se
  pouzije pozdeji jen pro CO2/TVOC, pokud se mereni znovu zapne).
- **CO2/TVOC měření záměrně vypnuté** – `ens160.startStandardMeasure()`
  se nevolá (viz stejná sekce výše). Pokud/až bude potřeba, jde snadno
  zase zapnout.
- **`SENSOR_WARMUP_MS` (1 min) ke zvážení zkrácení** – od vypnutí
  CO2/TVOC měření je teoreticky zbytečně dlouhý (AHT21 je okamžitý).
  Nezkráceno zatím – vyžaduje přepočet tabulky výdrže baterie.
- **PNP tranzistorový spínač Q1 (BC557)** – součástka potvrzená (Dan má
  BC547/BC557 po ruce), datasheetové parametry (hFE, Vce(sat)) ověřené.
  Hodnoty R1(1kΩ)/R2(10kΩ) jsou ale pořád jen výpočet na papíře, ne
  změřené na reálném zapojení – ověřit po sestavení.
- **R3/R4 I2C pull-upy (4.7kΩ)** – POTVRZENO nutné: multimetr na koupeném
  modulu naměřil ~4MΩ mezi SDA/SCL a 3V3 (2026-08-07), což je jen zbytkový
  svod, ne funkční pull-up. Nutno zapojit R3/R4 na spínanou 3.3V větev
  (za Q1), ne na trvale živou.
- **Interval probouzení node** (`MEASURE_INTERVAL_US`, výchozí 15 min) a
  doba zahřívání senzoru (`SENSOR_WARMUP_MS`, výchozí 1 min) – zatím
  odhad z popisku modulu/datasheetu, doladit podle reálné výdrže/potřeby
  čerstvosti dat a podle toho, jestli jsou první hodnoty po probuzení
  stabilní.
- **IDLE režim ENS160** – čip má kromě STANDARD i IDLE/DEEP_SLEEP režimy s
  nižší spotřebou, možná rychlejším náběhem než studený start – zatím
  nevyužito (firmware čip vždy úplně vypíná/zapíná přes tranzistorový
  spínač). Přesné
  proudy pro IDLE se nepodařilo dohledat z veřejných zdrojů, chtělo by to
  změřit na reálném HW.
- **Napětí baterie** – `batteryVoltage` v paketu se zatím neplní (chybí
  ADC měření na node).
- **Hub → Google Sheets VYŘEŠENO a OVĚŘENO na reálném HW (2026-08-08)** –
  kompletní řetězec node → ESP-NOW → hub → WiFi → Sheets funguje
  spolehlivě. Cestou bylo nutné vyřešit sladění WiFi kanálu a WiFi
  power-save (viz [Google Sheets logování](#google-sheets-logování)).
- **`src/hub/secrets.h`** – WIFI_SSID/WIFI_PASSWORD vyplněné Danem přímo
  v souboru (nikdy neposílat v konverzaci). SHEETS_WEBHOOK_URL vyplněná a
  funkční (ověřeno na reálném HW, 2026-08-08) – pozor na past s více
  nasazeními, viz [Google Sheets logování](#google-sheets-logování).
