// MeasureQ - DIAGNOSTICKY test senzoru ENS160+AHT21 na mericí krabicce
// (ESP32-C3-SuperMini). Na rozdil od produkcniho src/node/main.cpp:
//   - NEJDE spat (zadny deep sleep) - behem cteni zustava zapnuty Serial,
//     aby se dalo sledovat, co se deje
//   - NEPOSILA nic pres ESP-NOW - izoluje test jen na I2C/senzor
//   - na zacatku udela I2C scan (najde adresy pripojenych zarizeni), aby
//     se dalo overit zapojeni jeste pred tim, nez selze konkretni knihovna
//
// Pouziti: `pio run -e node-test -t upload -t monitor` (nebo vyber
// prostredi "node-test" v PlatformIO panelu ve VS Code).
//
// Ocekavane I2C adresy (pokud je zapojeni podle PROJECT_NOTES.md):
//   0x38 - AHT21 (pevna adresa)
//   0x53 - ENS160 (protoze ADD pin je pripojeny na 3.3V/HIGH)
// Pokud scanner nenajde ANI JEDNU adresu, je problem uz na urovni
// zapojeni (SDA/SCL prohozene, spatny pin, senzor bez napajeni, chybejici
// pull-upy R3/R4, prerusene GND...) - nemá smysl resit knihovny, dokud
// tohle nefunguje.
//
// API ScioSense_ENS16x knihovny (v2.0.5) overeno podle prilozeneho
// prikladu examples/01_Basic/ENS160/ENS160.ino - trida se jmenuje ENS160
// (ne ScioSense_ENS16x, jak jsem puvodne predpokladal bez kompilace),
// begin() bere ukazatel na Wire + adresu, cteni je getEco2()/getTvoc()
// (mala pismena), az po zavolani wait()+update() a kontrole hasNewData().

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>

// POTVRZENO kontinuitou multimetrem (2026-08-08): SDA na pinu "4", SCL na
// pinu "3" - puvodni predpoklad byl spravne (drivejsi "oprava" na 3/4 byla
// zalozena na chybnem mereni - zameneny pin "3.3V" za pin "3"). Viz
// PROJECT_NOTES.md.
constexpr int PIN_SDA = 4;
constexpr int PIN_SCL = 3;

// PNP tranzistor BC557, aktivni LOW = senzor zapnuty (high-side spinac).
constexpr int SENSOR_POWER_PIN = 10;

// Vestavena LED na ESP32-C3-SuperMini, aktivni LOW (LOW = sviti). Slouzi
// jako vizualni indikace stavu i bez Serial Monitoru - bliká = oba senzory
// (AHT21 i ENS160) uspesne komunikuji, trvale zhasnuto = ne.
constexpr int LED_PIN = 8;

// ADD pin senzoru je pripojeny na 3.3V (HIGH) -> I2C adresa ENS160 0x53
// (LOW by dalo 0x52). Viz PROJECT_NOTES.md.
constexpr uint8_t ENS160_I2C_ADDRESS = 0x53;

// PROVIZORNI kalibracni offset teploty, dokud nedorazi samostatny AHT21/
// AHT20 senzor mimo desku ESP32-C3 (viz PROJECT_NOTES.md). Pricina zbyvajiciho
// posunu i s vypnutym ENS160 ohrivacem: ENS160 je porad napajeny/pripojeny
// po I2C (klidovy proud), a je fyzicky na stejne malicke desticce jako
// AHT21 - nejde oddelit bez samostatneho modulu. Zmereno 2026-08-08:
// syrova hodnota 28.9 C vs 26.4 C na stolnim teplomeru -> offset -2.5 C.
// Pri vymene za samostatny AHT21/AHT20 senzor tento offset SMAZAT/prepocitat.
constexpr float TEMP_CALIBRATION_OFFSET_C = -2.5f;

// PROVIZORNI kalibracni offset vlhkosti, stejny duvod jako u teploty. POZOR:
// vlhkost je fyzikalne svazana s teplotou (relativni vlhkost = mnozstvi
// vodni pary vuci tomu, kolik by vzduch pojmul PRI DANE teplote) - tenhle
// pevny offset je tedy jen hruby odhad platny poblíž aktualnich podminek,
// ne presna psychrometricka korekce. Zmereno 2026-08-08: syrova hodnota
// 39.4 % vs 44 % na referencnim vlhkomeru -> offset +4.6 %.
constexpr float HUMIDITY_CALIBRATION_OFFSET_PCT = 4.6f;

// Kratsi nez v produkcnim kodu - tady nejde o baterii, jen chceme rychle
// videt, jestli senzor vubec zije. Pro finalni cteni po zahrati pockej
// dele (viz PROJECT_NOTES.md - SENSOR_WARMUP_MS).
constexpr uint32_t WARMUP_MS = 5000;

constexpr uint32_t READ_INTERVAL_MS = 2000;

Adafruit_AHTX0 aht;
ENS160 ens160;

bool ahtOk = false;
bool ensOk = false;

bool ahtAddrFound = false;
bool ensAddrFound = false;

// Vraci pocet nalezenych zarizeni. Krome vypisu si taky poznamena, jestli
// zivi ocekavane adresy AHT21/ENS160 - diky tomu muzeme nize NEVOLAT
// aht.begin()/ens160.begin(), kdyz na sbernici vubec nic neni. To je
// dulezite, protoze Adafruit_AHTX0::begin() ma uvnitr "while (getStatus() &
// BUSY) delay(10);" BEZ timeoutu, a kdyz cteni trvale selhava, getStatus()
// vraci 0xFF (BUSY bit vzdy nastaveny) -> nekonecna smycka, cely program
// navzdy zamrzne uz v setup() a Serial uz nic dalsiho nikdy nevypise.
uint8_t scanI2C()
{
  Serial.println("--- I2C scan ---");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0)
    {
      Serial.printf("  nalezeno zarizeni na adrese 0x%02X\n", addr);
      found++;
      if (addr == 0x38) ahtAddrFound = true;
      if (addr == ENS160_I2C_ADDRESS) ensAddrFound = true;
    }
  }
  if (found == 0)
  {
    Serial.println("  NIC NENALEZENO - zkontroluj zapojeni (SDA/SCL, napajeni");
    Serial.println("  senzoru pres Q1, GND, pull-up rezistory R3/R4) drive,");
    Serial.println("  nez resis knihovny.");
  }
  Serial.println("--- konec scanu ---");
  return found;
}

void setup()
{
  Serial.begin(115200);
  delay(1500); // cas na otevreni Serial Monitoru po USB enumeraci

  Serial.println();
  Serial.println("=== MeasureQ - test senzoru ENS160+AHT21 ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // LED zpocatku sviti (stav pred overenim = "problem")

  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW); // sepnout Q1 -> zapnout senzor
  Serial.println("Senzor zapnut (GPIO10 LOW). Cekam na zahrati...");
  delay(WARMUP_MS);

  Wire.begin(PIN_SDA, PIN_SCL);
  scanI2C();

  // aht.begin()/ens160.init() volame jen kdyz scan skutecne nasel prislusnou
  // adresu - jinak by aht.begin() zamrzl navzdy (viz komentar u scanI2C()).
  if (ahtAddrFound)
  {
    ahtOk = aht.begin();
    Serial.println(ahtOk ? "AHT21: OK (nalezen)" : "AHT21: CHYBA - odpovida na adrese, ale begin() selhal!");
  }
  else
  {
    Serial.println("AHT21: CHYBA - adresa 0x38 na sbernici neodpovida, preskakuji begin() (zamrzlo by).");
  }

  if (ensAddrFound)
  {
    ens160.begin(&Wire, ENS160_I2C_ADDRESS);
    ensOk = ens160.init();
    Serial.println(ensOk ? "ENS160: OK (nalezen)" : "ENS160: CHYBA - odpovida na adrese, ale init() selhal!");
    // ZAMERNE nevolame ens160.startStandardMeasure() - to by spustilo MOX
    // ohrivac (200-300 C), ktery i pres izolacni drazku v PCB dost ovlivni
    // sousedici AHT21 (namereno +6-7 C posun teploty, viz PROJECT_NOTES.md).
    // Kdyz zustane cip v IDLE (stav po init()), ohrivac NEbezi - CO2/TVOC
    // nedostaneme, ale AHT21 zmeri cistou teplotu/vlhkost bez zahrivani od
    // souseda.
  }
  else
  {
    Serial.printf("ENS160: CHYBA - adresa 0x%02X na sbernici neodpovida, preskakuji init().\n", ENS160_I2C_ADDRESS);
  }

  Serial.println("=== Zacinam prubezne cteni ===");
  Serial.println();
}

// Bliká, kdyz je AHT21 dostupny (ENS160 se zamerne NEZAPOJUJE do teto
// podminky - bez startStandardMeasure() zustava v IDLE, coz je ocekavany
// stav, ne chyba). Trvale sviti, kdyz AHT21 nekomunikuje.
void blinkStatus()
{
  if (ahtOk)
  {
    digitalWrite(LED_PIN, LOW); // rozsvitit (aktivni LOW)
    delay(150);
    digitalWrite(LED_PIN, HIGH); // zhasnout
    delay(150);
  }
  else
  {
    digitalWrite(LED_PIN, LOW); // trvale sviti = problem
  }
}

void loop()
{
  blinkStatus();

  if (ahtOk)
  {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    float calibratedTemp = temp.temperature + TEMP_CALIBRATION_OFFSET_C;
    float calibratedHumidity = humidity.relative_humidity + HUMIDITY_CALIBRATION_OFFSET_PCT;
    Serial.printf("Teplota (syrova): %5.1f C  (kompenzovana: %5.1f C)   Vlhkost (syrova): %5.1f %%  (kompenzovana: %5.1f %%)\n",
                   temp.temperature, calibratedTemp, humidity.relative_humidity, calibratedHumidity);
  }
  else
  {
    Serial.println("AHT21 nedostupny.");
  }

  delay(READ_INTERVAL_MS);
}
