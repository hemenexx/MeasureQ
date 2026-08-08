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
- **PNP tranzistor BC557** – high-side spínač 3.3V větve senzoru řízený
  GPIO (senzor je bez napájení, dokud ESP32 spí – viz níže)
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

**Výsledek testu: teplota se prakticky NEZMĚNILA** (32.5-32.6°C i s
vypnutým ohřívačem) – hypotéza č.1 tedy **buď neplatí, nebo není jediná
příčina**. Pravděpodobnější vysvětlení: nashromážděné teplo z dlouhé
testovací session (desítky minut téměř nepřetržitého napájení přes mnoho
re-flashů), kdy okolí senzoru nemělo čas vychladnout zpátky na pokojovou
teplotu mezi jednotlivými testy. **Probíhá test:** odpojit USB, nechat
vychladnout ~5-10 min, pak zkontrolovat úplně první čtení po startu –
výsledek zatím NEZAZNAMENÁN, čeká se na dokončení.

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

## Piny (hub, ESP32 30pin) – NEOVĚŘENO na HW

| Signál | Pin |
|---|---|
| Display 0 (node 0) CLK / DIO | 25 / 26 |
| Display 1 (node 1) CLK / DIO | 27 / 14 |
| Display 2 (node 2) CLK / DIO | 12 / 13 |
| Tlačítko 0 / 1 / 2 | 32 / 33 / 4 |

## Softwarová architektura

### Struktura PlatformIO projektu

Jeden `platformio.ini`, tři environments:

- `env:node` – produkční firmware node (build_src_filter vylučuje
  `src/hub/` a `src/node_test/`)
- `env:node-test` – diagnostický test senzoru pro node (viz níže;
  vylučuje `src/hub/` a `src/node/`)
- `env:hub` – firmware hubu (vylučuje `src/node/` a `src/node_test/`)

Všechny kompilují i `src/common/`.

### Diagnostický test senzoru (`src/node_test/main.cpp`)

Samostatný sketch pro ověření zapojení senzoru na krabičce, oddělený od
produkčního `src/node/main.cpp` (aby se testováním nezasahovalo do
deep-sleep cyklu). Rozdíly oproti produkčnímu kódu:

- **Nejde spát** – běží v nekonečné smyčce, čte a vypisuje hodnoty každé
  2s přes Serial, aby šlo sledovat průběh.
- **Neposílá nic přes ESP-NOW** – izoluje test jen na I2C/senzor.
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
- **Piny node I2C** – POTVRZENO kontinuitou (SDA=GPIO4, SCL=GPIO3), viz
  [Zjištění z prvního testu na reálném HW](#zjištění-z-prvního-testu-na-reálném-hw-2026-08-08).
  **Piny hub** (displeje/tlačítka) – stále čistě předpoklad, hub zatím
  fyzicky nezapojen.
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
- **AHT21 self-heating – NEDOŘEŠENO, čeká se na test po vychladnutí** –
  viz [Mereni CO2/TVOC vypnuto](#mereni-co2tvoc-vypnuto---self-heating-vyšetřování-2026-08-08).
  Vypnutí ENS160 ohřívače teplotu nezlepšilo, takže příčina je
  pravděpodobně nashromážděné teplo z dlouhé testovací session, ne (jen)
  MOX ohřívač. Čeká se na výsledek testu po ~5-10 min vychladnutí.
- **CO2/TVOC měření záměrně vypnuté** – `ens160.startStandardMeasure()`
  se nevolá (viz stejná sekce výše). Pokud/až bude potřeba, jde snadno
  zase zapnout.
- **`SENSOR_WARMUP_MS` (1 min) ke zvážení zkrácení** – od vypnutí
  CO2/TVOC měření je teoreticky zbytečně dlouhý (AHT21 je okamžitý).
  Nezkráceno zatím – vyžaduje přepočet tabulky výdrže baterie.
- **Napájecí tlačítko na hubu** – zatím firmware s ním vůbec nepočítá
  (předpoklad: čistě fyzický spínač v napájecí větvi). Pokud má jít o
  "soft power" řízené firmwarem, je potřeba přidat GPIO a logiku.
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
