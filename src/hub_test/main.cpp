// MeasureQ - jednoduchy testovaci prijimac senzorovych dat pres ESP-NOW
// (ESP32 30pin, stejna deska jako produkcni src/hub). Na rozdil od
// produkcniho src/hub/main.cpp nepotrebuje zapojene displeje/tlacitka -
// jen vypisuje prijate hodnoty na Serial. Proti tomuhle bezi
// src/node_test (posila MeasurementPacket na broadcast adresu).

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "../common/MeasureQProtocol.h"

// Musi sedet s HUB_WIFI_CHANNEL v src/node_test/main.cpp - bez tohohle
// zustava tahle deska (nepripojena k zadne WiFi siti) na defaultnim
// kanalu 1, zatimco node_test si vynucuje kanal 3 (kvuli parite s
// produkcnim hubem, ktery je pripojeny ke skutecne WiFi). Bez sladeni
// kanalu ESP-NOW pakety vubec nedorazi (potvrzeno 2026-08-09 - hub_test
// nepřijal nic, i kdyz node_test aktivne vysilal).
constexpr uint8_t NODE_TEST_WIFI_CHANNEL = 3;

// Jak dlouho bez jedineho paketu, nez to oznacime jako "data ztracena"
// (node_test posila kazdych 2000ms - viz READ_INTERVAL_MS tam).
constexpr uint32_t DATA_LOST_TIMEOUT_MS = 6000;

volatile uint32_t lastPacketMs = 0;
volatile bool everReceived = false;

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
{
  if (len != sizeof(MeasurementPacket))
  {
    return;
  }
  MeasurementPacket packet;
  memcpy(&packet, data, sizeof(packet));

  lastPacketMs = millis();
  everReceived = true;

  Serial.printf("Node %u  #%-6lu  Teplota: %5.1f C   Vlhkost: %5.1f %%   eCO2: %5u ppm   TVOC: %5u ppb\n",
                packet.nodeId, (unsigned long)packet.seq, packet.temperatureC,
                packet.humidityPct, packet.co2Ppm, packet.tvocPpb);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== MeasureQ - test prijmu senzorovych dat pres ESP-NOW ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(NODE_TEST_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("MAC teto desky: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init selhal!");
    while (true)
    {
      delay(1000);
    }
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Cekam na pakety z src/node_test...");
  Serial.println();
}

void loop()
{
  delay(1000);

  static uint32_t lastWarningMs = 0;
  uint32_t now = millis();

  if (everReceived && (now - lastPacketMs) > DATA_LOST_TIMEOUT_MS)
  {
    if ((now - lastWarningMs) > 3000)
    {
      lastWarningMs = now;
      Serial.printf("(DATA ZTRACENA - posledni paket pred %lu ms - node je mimo dosah,\n"
                     " vypnuty, dosla mu baterie, nebo mu prestal odpovidat senzor)\n",
                     (unsigned long)(now - lastPacketMs));
    }
  }
}
