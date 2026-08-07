// MeasureQ - firmware mericí krabicky (ESP32-C3-SuperMini)
// HW: ENS160+AHT21 (I2C), XL6007 boost 2x AAA -> 3.3V, ESP-NOW vysilani do hubu.
//
// NEOVERENO NA HW - pred prvnim flashem zkontrolovat/upravit:
//  - PIN_SDA/PIN_SCL nize (vychozi piny konkretniho ESP32-C3-SuperMini klonu
//    se lisi kus od kusu)
//  - NODE_ID (zmenit pro kazdou ze 3 krabicek: 0, 1, 2)
//  - hubMac[] (MAC adresa hubu - vypsana hubem na Serial po prvnim flashi,
//    viz src/hub/main.cpp)
//  - API knihovny ScioSense_ENS16x - overit podle skutecne nainstalovane
//    verze (metody/konstanty nize jsou podle aktualni dokumentace k srpnu
//    2026, ale knihovna se meni)

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>

#include "../common/MeasureQProtocol.h"

constexpr uint8_t NODE_ID = 0; // ZMENIT per krabicka: 0, 1, 2
uint8_t hubMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // TODO: MAC adresa hubu

constexpr uint32_t MEASURE_INTERVAL_MS = 10000;

// I2C piny - predpoklad pro ESP32-C3-SuperMini, NEOVERENO na HW.
constexpr int PIN_SDA = 8;
constexpr int PIN_SCL = 9;

Adafruit_AHTX0 aht;
ScioSense_ENS16x ens160;

uint32_t packetSeq = 0;
bool peerReady = false;

void onDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.printf("ESP-NOW send: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);

  if (!aht.begin())
  {
    Serial.println("AHT21 nenalezen - zkontrolovat zapojeni!");
  }

  if (!ens160.begin())
  {
    Serial.println("ENS160 nenalezen - zkontrolovat zapojeni!");
  }
  else
  {
    ens160.setOperationMode(ENS16X_OPMODE_STD);
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.print("Vlastni MAC (pro info): ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init selhal!");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, hubMac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peerReady = (esp_now_add_peer(&peer) == ESP_OK);
  if (!peerReady)
  {
    Serial.println("esp_now_add_peer selhal - zkontrolovat hubMac[]!");
  }
}

void loop()
{
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  MeasurementPacket packet{};
  packet.nodeId = NODE_ID;
  packet.seq = packetSeq++;
  packet.temperatureC = temp.temperature;
  packet.humidityPct = humidity.relative_humidity;
  packet.co2Ppm = ens160.getECO2();
  packet.tvocPpb = ens160.getTVOC();
  packet.batteryVoltage = 0.0f; // TODO: zmerit pres ADC (delic napeti)

  if (peerReady)
  {
    esp_now_send(hubMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
  }

  delay(MEASURE_INTERVAL_MS);
  // TODO: nahradit delay() za esp_deep_sleep - kvuli 2x AAA napajeni je
  // aktivni cekani zbytecne plytvani baterii.
}
