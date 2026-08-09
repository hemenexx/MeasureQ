// MeasureQ - test dosahu ESP-NOW, prijimaci strana (ESP32 30pin, stejna
// deska jako produkcni hub). Pocita mezery v poradovych cislech paketu od
// src/wifi_test_node, aby dal konkretni cislo ztratovosti podle
// vzdalenosti/prekazek.
//
// POZNAMKA k RSSI: nainstalovana verze ESP-IDF/arduino-esp32 jadra ma jen
// starsi tvar esp_now_recv_cb_t (mac, data, len) - BEZ pristupu k RSSI
// prijateho paketu (ten by vyzadoval esp_now_recv_info_t z novejsiho API,
// ktere tahle verze nema - viz oprava v src/hub/main.cpp, 2026-08-08).
// Test proto meri dosah cisteji pres ztratovost paketu (kolik jich vubec
// dorazi), ne pres silu signalu.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "../common/WifiRangeTestProtocol.h"

// Ztratovost se pocita jen za posledni WINDOW_MS (klouzave/periodicke
// okno), NE kumulativne od startu - jinak by stare ztraty (napr. z doby,
// kdy byl vysilac daleko nebo mu dochazela baterie) navzdy zkreslovaly
// aktualni cislo, i kdyz uz je signal mezitim perfektni.
constexpr uint32_t WINDOW_MS = 10000;

volatile int32_t lastSeq = -1;
volatile uint32_t lifetimeReceived = 0; // jen informativni pocitadlo, nejde do %
volatile uint32_t windowReceived = 0;
volatile uint32_t windowLost = 0;
volatile uint32_t windowStartMs = 0;
volatile uint32_t lastPacketMs = 0;

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
{
  if (len != sizeof(RangeTestPacket))
  {
    return;
  }
  RangeTestPacket packet;
  memcpy(&packet, data, sizeof(packet));

  lifetimeReceived++;
  lastPacketMs = millis();
  if (lastSeq >= 0)
  {
    int32_t gap = (int32_t)packet.seq - lastSeq - 1;
    if (gap > 0)
    {
      windowLost += (uint32_t)gap;
    }
  }
  lastSeq = (int32_t)packet.seq;
  windowReceived++;

  uint32_t windowExpected = windowReceived + windowLost;
  float lossPct = windowExpected > 0 ? (100.0f * windowLost / windowExpected) : 0.0f;

  Serial.printf("Prijato #%-6lu  ztratovost (posl. %lus): %5.1f%%  (prijato celkem od startu: %lu)\n",
                (unsigned long)packet.seq, (unsigned long)(WINDOW_MS / 1000),
                lossPct, (unsigned long)lifetimeReceived);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== MeasureQ - test dosahu ESP-NOW (prijimac/hub) ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
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

  Serial.println("Cekam na pakety z src/wifi_test_node...");
  Serial.println();
}

// Jak dlouho bez jedineho paketu, nez to oznacime jako "signal ztracen"
// (vysilac posila kazdych 500ms, takze tohle je ~6 zmeskanych v rade -
// dost na to, aby to nebyla jen normalni obcasna ztrata).
constexpr uint32_t SIGNAL_LOST_TIMEOUT_MS = 3000;

void loop()
{
  // Vsechna prace se deje v onDataRecv() callbacku.
  delay(1000);

  static uint32_t lastReminderMs = 0;
  uint32_t now = millis();

  if (lastSeq < 0)
  {
    // Jeste vubec nic neprislo od startu.
    if ((now - lastReminderMs) > 5000)
    {
      lastReminderMs = now;
      Serial.println("(zatim zadny paket neprisel...)");
    }
  }
  else if ((now - lastPacketMs) > SIGNAL_LOST_TIMEOUT_MS)
  {
    // Uz jsme neco prijali, ale ted uz dlouho nic - vysilac je mimo dosah,
    // vypnuty, nebo mu dosla baterie.
    if ((now - lastReminderMs) > 3000)
    {
      lastReminderMs = now;
      Serial.printf("(SIGNAL ZTRACEN - posledni paket pred %lu ms)\n",
                     (unsigned long)(now - lastPacketMs));
    }
  }

  // Periodicky reset klouzaveho okna pro ztratovost (viz komentar u
  // WINDOW_MS vyse).
  if ((now - windowStartMs) > WINDOW_MS)
  {
    windowStartMs = now;
    windowReceived = 0;
    windowLost = 0;
  }
}
