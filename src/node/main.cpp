// MeasureQ - firmware mericí krabicky (ESP32-C3-SuperMini)
// HW: ENS160+AHT21 (I2C) spinane pres P-MOSFET load switch (aktivni LOW),
// XL6007 boost 2x AAA -> 3.3V, ESP-NOW vysilani do hubu.
//
// Napajeci cyklus (kvuli baterii - ESP32 vetsinu casu spi v deep sleep).
// Deep sleep = reset cipu, RAM (krome RTC_DATA_ATTR) se neuchova, takze po
// kazdem probuzeni bezi cely setup() znovu od zacatku:
//   1. zapnout napajeni senzoru
//   2. pockat SENSOR_WARMUP_MS (ENS160 potrebuje az 3 min na zahrati -
//      podle datasheetu, viz PROJECT_NOTES.md)
//   3. inicializovat I2C, precist senzor
//   4. vypnout napajeni senzoru - uz neni potreba, setri baterii pred WiFi
//   5. inicializovat WiFi/ESP-NOW, odeslat paket, kratce pockat na potvrzeni
//   6. esp_deep_sleep_start() na MEASURE_INTERVAL_US
//
// NEOVERENO NA HW - pred prvnim flashem zkontrolovat/upravit:
//  - PIN_SDA/PIN_SCL/SENSOR_POWER_PIN nize (vychozi piny konkretniho
//    ESP32-C3-SuperMini klonu se lisi kus od kusu)
//  - NODE_ID (zmenit pro kazdou ze 3 krabicek: 0, 1, 2)
//  - hubMac[] (MAC adresa hubu - vypsana hubem na Serial po prvnim flashi,
//    viz src/hub/main.cpp)
//  - API knihovny ScioSense_ENS16x
//  - P-MOSFET load switch zapojeni - viz PROJECT_NOTES.md

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

// ENS160 potrebuje az 3 min na zahrati po zapnuti napajeni, nez da platne
// hodnoty (podle datasheetu). AHT21 je prakticky okamzity.
constexpr uint32_t SENSOR_WARMUP_MS = 3UL * 60 * 1000;

// Jak dlouho cekat na potvrzeni odeslani ESP-NOW pred uspanim.
constexpr uint32_t SEND_CONFIRM_TIMEOUT_MS = 2000;

// I2C piny - GPIO4/GPIO3 zvoleny zamerne mimo strapping piny (2, 8, 9) a
// JTAG piny (5, 6, 7): GPIO8 je navic sdileny se zabudovanou LED a GPIO9
// s tlacitkem BOOT. Fyzicke zapojeni na desce (silkscreen "4"/"3") zatim
// NEOVERENO kontinuitou/multimetrem.
constexpr int PIN_SDA = 4;
constexpr int PIN_SCL = 3;

// Spina P-MOSFET (aktivni LOW = sepnuto) na 3.3V vetvi senzoru. Zbyva v
// "bezpecne" pinove skupine (0, 1, 10) spolu s GPIO0/1, ktere zustavaji
// volne pro budouci ADC mereni napeti baterie. Viz PROJECT_NOTES.md.
constexpr int SENSOR_POWER_PIN = 10;

Adafruit_AHTX0 aht;
ScioSense_ENS16x ens160;

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

  bool ensOk = ens160.begin();
  if (!ensOk)
  {
    Serial.println("ENS160 nenalezen - zkontrolovat zapojeni!");
  }
  else
  {
    ens160.setOperationMode(ENS16X_OPMODE_STD);
  }

  MeasurementPacket packet{};
  packet.nodeId = NODE_ID;
  packet.seq = packetSeq++;

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  packet.temperatureC = temp.temperature;
  packet.humidityPct = humidity.relative_humidity;
  packet.co2Ppm = ens160.getECO2();
  packet.tvocPpb = ens160.getTVOC();
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
