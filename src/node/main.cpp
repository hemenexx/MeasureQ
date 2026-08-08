// MeasureQ - firmware mericí krabicky (ESP32-C3-SuperMini)
// HW: ENS160+AHT21 (I2C) spinane pres PNP tranzistor (BC557) jako high-side
// spinac 3.3V vetve senzoru (aktivni LOW), XL63070 (TPS63070) buck-boost
// 2x AAA -> 3.3V, ESP-NOW vysilani do hubu.
//
// Napajeci cyklus (kvuli baterii - ESP32 vetsinu casu spi v deep sleep).
// Deep sleep = reset cipu, RAM (krome RTC_DATA_ATTR) se neuchova, takze po
// kazdem probuzeni bezi cely setup() znovu od zacatku:
//   1. zapnout napajeni senzoru
//   2. pockat SENSOR_WARMUP_MS (viz definice nize a PROJECT_NOTES.md)
//   3. inicializovat I2C, precist senzor
//   4. vypnout napajeni senzoru - uz neni potreba, setri baterii pred WiFi
//   5. inicializovat WiFi/ESP-NOW, odeslat paket, kratce pockat na potvrzeni
//   6. esp_deep_sleep_start() na MEASURE_INTERVAL_US
//
// PIN_SDA/PIN_SCL a API knihovny ScioSense_ENS16x jsou uz overene na realnem
// HW (viz src/node_test/main.cpp a PROJECT_NOTES.md). Pred prvnim flashem
// KONKRETNI krabicky jeste zkontrolovat/upravit:
//  - NODE_ID (zmenit pro kazdou ze 3 krabicek: 0, 1, 2)
//  - hubMac[] (MAC adresa hubu - vypsana hubem na Serial po prvnim flashi,
//    viz src/hub/main.cpp)
//  - PNP tranzistor (BC557) - zapojeni viz PROJECT_NOTES.md

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>

#include "../common/MeasureQProtocol.h"

constexpr uint8_t NODE_ID = 0; // ZMENIT per krabicka: 0, 1, 2
uint8_t hubMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // TODO: MAC adresa hubu

// Jak casto se krabicka probouzi a posila data. Delsi interval = vyrazne
// lepsi vydrz baterie, protoze 3min zahrivani senzoru tvori mensi podil
// cyklu - viz tabulka vydrze v PROJECT_NOTES.md.
constexpr uint64_t MEASURE_INTERVAL_US = 15ULL * 60 * 1000000; // 15 minut

// Puvodne odvozeno z doby zahrati ENS160 MOX ohrivace (~1-3 min, zdroje se
// rozchazeji). Od te doby se ale ENS160 mereni zamerne VYPNULO (viz
// startStandardMeasure() nize) kvuli ovlivnovani AHT21 teploty - AHT21 sam
// o sobe je prakticky okamzity. Tato hodnota je tedy teoreticky zbytecne
// dlouha (jen brzdi baterii) - NEZKRACENO zatim, protoze by to zmenilo
// tabulku vydrze baterie v PROJECT_NOTES.md a chce to promyslet zvlast.
constexpr uint32_t SENSOR_WARMUP_MS = 1UL * 60 * 1000;

// Jak dlouho cekat na potvrzeni odeslani ESP-NOW pred uspanim.
constexpr uint32_t SEND_CONFIRM_TIMEOUT_MS = 2000;

// I2C piny - GPIO4/GPIO3 zvoleny zamerne mimo strapping piny (2, 8, 9).
// POTVRZENO kontinuitou multimetrem (2026-08-08): SDA na pinu "4", SCL na
// pinu "3" - puvodni predpoklad byl spravne (viz PROJECT_NOTES.md).
constexpr int PIN_SDA = 4;
constexpr int PIN_SCL = 3;

// Rizeni PNP tranzistoru BC557 (aktivni LOW = sepnuto) pres baznovy
// rezistor R1 - spina 3.3V vetev senzoru (high-side spinac), GND senzoru
// zustava trvale spolecna s ESP32. Zbyva v "bezpecne" pinove skupine
// (0, 1, 10) spolu s GPIO0/1, ktere zustavaji volne pro budouci ADC
// mereni napeti baterie. Viz PROJECT_NOTES.md.
constexpr int SENSOR_POWER_PIN = 10;

// ADD pin senzoru je pripojeny na 3.3V (HIGH) -> I2C adresa ENS160 0x53
// (LOW by dalo 0x52). Viz PROJECT_NOTES.md.
constexpr uint8_t ENS160_I2C_ADDRESS = 0x53;

Adafruit_AHTX0 aht;
ENS160 ens160;

// RTC_DATA_ATTR prezije deep sleep (na rozdil od normalnich globalnich
// promennych) - poradove cislo tak ma smysl i pres spanek.
RTC_DATA_ATTR uint32_t packetSeq = 0;

volatile bool sendConfirmed = false;

void onDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
  sendConfirmed = true;
  Serial.printf("ESP-NOW send: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void sensorPowerOn()
{
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
}

void sensorPowerOff()
{
  digitalWrite(SENSOR_POWER_PIN, HIGH);
}

void goToSleep()
{
  esp_sleep_enable_timer_wakeup(MEASURE_INTERVAL_US);
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);

  sensorPowerOn();
  delay(SENSOR_WARMUP_MS);

  Wire.begin(PIN_SDA, PIN_SCL);

  bool ahtOk = aht.begin();
  if (!ahtOk)
  {
    Serial.println("AHT21 nenalezen - zkontrolovat zapojeni!");
  }

  ens160.begin(&Wire, ENS160_I2C_ADDRESS);
  bool ensOk = ens160.init();
  if (!ensOk)
  {
    Serial.println("ENS160 nenalezen - zkontrolovat zapojeni!");
  }
  // ZAMERNE nevolame ens160.startStandardMeasure() - to by spustilo MOX
  // ohrivac (200-300 C), ktery i pres izolacni drazku v PCB dost ovlivni
  // sousedici AHT21 (namereno +6-7 C posun teploty, viz PROJECT_NOTES.md).
  // Dan prioritizuje presnou teplotu/vlhkost pred CO2/TVOC - viz TODO
  // "Mereni CO2/TVOC vypnuto" v PROJECT_NOTES.md pro moznost znovuzapnuti.

  MeasurementPacket packet{};
  packet.nodeId = NODE_ID;
  packet.seq = packetSeq++;

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  packet.temperatureC = temp.temperature;
  packet.humidityPct = humidity.relative_humidity;

  packet.co2Ppm = 0; // ENS160 mereni zamerne vypnuto, viz vyse
  packet.tvocPpb = 0;
  packet.batteryVoltage = 0.0f; // TODO: zmerit pres ADC (delic napeti)

  sensorPowerOff();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() == ESP_OK)
  {
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, hubMac, 6);
    peer.channel = 0;
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) == ESP_OK)
    {
      esp_now_send(hubMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));

      uint32_t waitStart = millis();
      while (!sendConfirmed && (millis() - waitStart) < SEND_CONFIRM_TIMEOUT_MS)
      {
        delay(10);
      }
    }
    else
    {
      Serial.println("esp_now_add_peer selhal - zkontrolovat hubMac[]!");
    }
  }
  else
  {
    Serial.println("ESP-NOW init selhal!");
  }

  goToSleep();
}

void loop()
{
  // Nedosazitelne - setup() konci esp_deep_sleep_start(), ktery cip resetuje.
}
