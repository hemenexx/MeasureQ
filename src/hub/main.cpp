// MeasureQ - firmware centralni jednotky (ESP32 30pin)
// HW: 3x TM1637 (primo na GPIO), 3x tlacitko pod displejem (prepina
// zobrazovanou velicinu daneho display/node), napajeci tlacitko, USB-C napajeni.
// Prijima MeasurementPacket pres ESP-NOW od 3 mericich krabicek (viz src/node/).
//
// NEOVERENO NA HW - pred prvnim flashem zkontrolovat/upravit:
//  - DISPLAY_CLK_PIN/DISPLAY_DIO_PIN a BUTTON_PIN nize (placeholder piny)
//  - napajeci tlacitko: predpoklad je, ze jde o fyzicky spinac primo v
//    napajeci vetvi (neni pripojeny na GPIO) - pokud ma byt "soft power"
//    rizeny firmwarem, je potreba pridat GPIO a logiku zvlast
//
// Po prvnim nahrani vypise Serial vlastni MAC adresu - tu je nutne zkopirovat
// do hubMac[] v src/node/main.cpp na vsech 3 mericich krabickach.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "../common/MeasureQProtocol.h"
#include "TM1637Display.h"

// --- Piny displeju - NEOVERENO na HW ---
constexpr uint8_t DISPLAY_CLK_PIN[MEASUREQ_NODE_COUNT] = {25, 27, 12};
constexpr uint8_t DISPLAY_DIO_PIN[MEASUREQ_NODE_COUNT] = {26, 14, 13};

// --- Piny tlacitek (1 na display, cykluje zobrazovanou velicinu) - NEOVERENO ---
constexpr uint8_t BUTTON_PIN[MEASUREQ_NODE_COUNT] = {32, 33, 4};
constexpr uint32_t BUTTON_DEBOUNCE_MS = 200;

enum class DisplayedVariable : uint8_t
{
  Temperature = 0,
  Humidity = 1,
  CO2 = 2,
  TVOC = 3,
  Count = 4,
};

TM1637Display displays[MEASUREQ_NODE_COUNT] = {
    TM1637Display(DISPLAY_CLK_PIN[0], DISPLAY_DIO_PIN[0]),
    TM1637Display(DISPLAY_CLK_PIN[1], DISPLAY_DIO_PIN[1]),
    TM1637Display(DISPLAY_CLK_PIN[2], DISPLAY_DIO_PIN[2]),
};

MeasurementPacket latestPacket[MEASUREQ_NODE_COUNT] = {};
uint32_t lastPacketMs[MEASUREQ_NODE_COUNT] = {0, 0, 0};
DisplayedVariable shownVariable[MEASUREQ_NODE_COUNT] = {
    DisplayedVariable::Temperature,
    DisplayedVariable::Temperature,
    DisplayedVariable::Temperature,
};
uint32_t lastButtonMs[MEASUREQ_NODE_COUNT] = {0, 0, 0};

// Novejsi jadro arduino-esp32 (IDF 5.x) pouziva esp_now_recv_info_t*.
// Pokud build selze na starsi verzi platformy, nahradit prvni parametr
// za "const uint8_t *mac".
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (len != sizeof(MeasurementPacket))
  {
    return;
  }
  MeasurementPacket packet;
  memcpy(&packet, data, sizeof(packet));

  if (packet.nodeId >= MEASUREQ_NODE_COUNT)
  {
    return;
  }
  latestPacket[packet.nodeId] = packet;
  lastPacketMs[packet.nodeId] = millis();
}

void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.print("MAC hubu (zkopirovat do hubMac[] na vsech nodech): ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init selhal!");
  }
  esp_now_register_recv_cb(onDataRecv);

  for (uint8_t i = 0; i < MEASUREQ_NODE_COUNT; i++)
  {
    displays[i].begin();
    displays[i].showDigits(TM1637Display::BLANK, TM1637Display::BLANK,
                            TM1637Display::BLANK, TM1637Display::BLANK);
    pinMode(BUTTON_PIN[i], INPUT_PULLUP);
  }
}

// Vraci hodnotu vybrane veliciny pro dany node, zaokrouhlenou na cele
// jednotky pro zobrazeni na 4-cislicovem displeji.
// TODO: teplota/vlhkost se tak zobrazi bez desetinne carky (napr. 236 misto
// 23.6) - trida TM1637Display zatim nema API pro desetinnou tecku.
uint16_t valueForDisplay(const MeasurementPacket &packet, DisplayedVariable variable)
{
  switch (variable)
  {
  case DisplayedVariable::Temperature:
    return (uint16_t)round(packet.temperatureC * 10.0f);
  case DisplayedVariable::Humidity:
    return (uint16_t)round(packet.humidityPct * 10.0f);
  case DisplayedVariable::CO2:
    return packet.co2Ppm;
  case DisplayedVariable::TVOC:
    return packet.tvocPpb;
  default:
    return 0;
  }
}

void loop()
{
  uint32_t now = millis();

  for (uint8_t i = 0; i < MEASUREQ_NODE_COUNT; i++)
  {
    if (digitalRead(BUTTON_PIN[i]) == LOW && (now - lastButtonMs[i]) > BUTTON_DEBOUNCE_MS)
    {
      lastButtonMs[i] = now;
      uint8_t next = (uint8_t)shownVariable[i] + 1;
      if (next >= (uint8_t)DisplayedVariable::Count)
      {
        next = 0;
      }
      shownVariable[i] = (DisplayedVariable)next;
    }

    bool haveData = lastPacketMs[i] != 0;
    if (haveData)
    {
      displays[i].showNumber(valueForDisplay(latestPacket[i], shownVariable[i]));
    }
  }

  delay(20);
}
