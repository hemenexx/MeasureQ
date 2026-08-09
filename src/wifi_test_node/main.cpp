// MeasureQ - test dosahu ESP-NOW, vysilaci strana (ESP32-C3-SuperMini).
// Nezavisi na senzoru/I2C - izoluje test jen na WiFi/ESP-NOW, aby se dalo
// projit s krabickou po dome/venku a zjistit, kde prestanou pakety
// prochazet. Vysila na broadcast adresu (FF:FF:FF:FF:FF:FF), takze neni
// potreba znat predem MAC prijimace - proti nemu bezi src/wifi_test_hub.
//
// Pouziti: nahrat na node, nahrat src/wifi_test_hub na hub, sledovat
// Serial Monitor na hub strane (ten pocita ztracene pakety). Node strana
// vypisuje jen potvrzeni odeslani (ESP-NOW send callback rika jen "odeslano
// do etheru v poradku", ne "prijato hubem" - ztratovost se pozna az na
// prijimaci strane podle mezer v poradovych cislech).
//
// POZNAMKA k napajeni: tenhle test nejde spat (na rozdil od produkcniho
// src/node/main.cpp) a bezi porad - pro test na baterii/venku by bylo
// potreba pripojit powerbanku/baterii na 3.3V pin, ESP32-C3-SuperMini
// samotne bez USB kabelu napajeni nema.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "../common/WifiRangeTestProtocol.h"

// Vestavena LED (GPIO8, aktivni LOW) - bliknuti = paket byl odeslan (ne
// nutne prijat, jen predany WiFi driveru), aby slo sledovat, ze vysilac
// porad zije, i kdyz zrovna nekoukas na Serial Monitor.
constexpr int LED_PIN = 8;

constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint32_t SEND_INTERVAL_MS = 500;

uint32_t seq = 0;

void onDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.printf("  odeslano #%lu: %s\n", (unsigned long)(seq - 1),
                status == ESP_NOW_SEND_SUCCESS ? "OK" : "CHYBA");
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // zhasnuto

  Serial.println();
  Serial.println("=== MeasureQ - test dosahu ESP-NOW (vysilac) ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.print("MAC teto krabicky: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init selhal!");
    while (true)
    {
      delay(1000);
    }
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK)
  {
    Serial.println("esp_now_add_peer (broadcast) selhal!");
    while (true)
    {
      delay(1000);
    }
  }

  Serial.println("Vysilam kazdych 500ms na broadcast adresu...");
  Serial.println();
}

void loop()
{
  RangeTestPacket packet{};
  packet.seq = seq;
  packet.sentMs = millis();

  Serial.printf("Vysilam #%lu...\n", (unsigned long)seq);
  seq++;

  digitalWrite(LED_PIN, LOW);
  esp_now_send(BROADCAST_MAC, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
  delay(30);
  digitalWrite(LED_PIN, HIGH);

  delay(SEND_INTERVAL_MS);
}
