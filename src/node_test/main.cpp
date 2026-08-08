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

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>

// Stejne piny jako v produkcnim src/node/main.cpp - viz PROJECT_NOTES.md
constexpr int PIN_SDA = 4;
constexpr int PIN_SCL = 3;

// PNP tranzistor BC557, aktivni LOW = senzor zapnuty (high-side spinac).
constexpr int SENSOR_POWER_PIN = 10;

// Kratsi nez v produkcnim kodu - tady nejde o baterii, jen chceme rychle
// videt, jestli senzor vubec zije. Pro finalni cteni po zahrati pockej
// dele (viz PROJECT_NOTES.md - SENSOR_WARMUP_MS).
constexpr uint32_t WARMUP_MS = 5000;

constexpr uint32_t READ_INTERVAL_MS = 2000;

Adafruit_AHTX0 aht;
ScioSense_ENS16x ens160;

bool ahtOk = false;
bool ensOk = false;

void scanI2C()
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
    }
  }
  if (found == 0)
  {
    Serial.println("  NIC NENALEZENO - zkontroluj zapojeni (SDA/SCL, napajeni");
    Serial.println("  senzoru pres Q1, GND, pull-up rezistory R3/R4) drive,");
    Serial.println("  nez resis knihovny.");
  }
  Serial.println("--- konec scanu ---");
}

void setup()
{
  Serial.begin(115200);
  delay(1500); // cas na otevreni Serial Monitoru po USB enumeraci

  Serial.println();
  Serial.println("=== MeasureQ - test senzoru ENS160+AHT21 ===");

  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW); // sepnout Q1 -> zapnout senzor
  Serial.println("Senzor zapnut (GPIO10 LOW). Cekam na zahrati...");
  delay(WARMUP_MS);

  Wire.begin(PIN_SDA, PIN_SCL);
  scanI2C();

  ahtOk = aht.begin();
  Serial.println(ahtOk ? "AHT21: OK (nalezen)" : "AHT21: CHYBA - nenalezen!");

  ensOk = ens160.begin();
  Serial.println(ensOk ? "ENS160: OK (nalezen)" : "ENS160: CHYBA - nenalezen!");
  if (ensOk)
  {
    ens160.setOperationMode(ENS16X_OPMODE_STD);
  }

  Serial.println("=== Zacinam prubezne cteni (kazdych 2s) ===");
  Serial.println();
}

void loop()
{
  if (ahtOk)
  {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    Serial.printf("Teplota: %5.1f C   Vlhkost: %5.1f %%   ",
                   temp.temperature, humidity.relative_humidity);
  }
  else
  {
    Serial.print("AHT21 nedostupny.   ");
  }

  if (ensOk)
  {
    Serial.printf("eCO2: %5u ppm   TVOC: %5u ppb\n",
                   ens160.getECO2(), ens160.getTVOC());
  }
  else
  {
    Serial.println("ENS160 nedostupny.");
  }

  delay(READ_INTERVAL_MS);
}
