#pragma once

#include <cstdint>

// Sdileny datovy paket posilany pres ESP-NOW z mericí krabicky (node) do
// centralni jednotky (hub). Struktura MUSI byt bit-identicka na obou
// stranach (stejne poradi/typy polí, stejny #pragma pack chovani).

constexpr uint8_t MEASUREQ_NODE_COUNT = 3;

struct __attribute__((packed)) MeasurementPacket
{
  uint8_t nodeId;          // 0..MEASUREQ_NODE_COUNT-1, identifikuje krabicku
  uint32_t seq;             // poradove cislo - detekce ztraty/duplicity paketu
  float temperatureC;
  float humidityPct;
  uint16_t co2Ppm;
  uint16_t tvocPpb;
  float batteryVoltage;     // napeti na 2x AAA - TODO: zatim neimplementovano na node
};
