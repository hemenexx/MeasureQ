// MeasureQ - sdileny paket pro test dosahu ESP-NOW (src/wifi_test_node,
// src/wifi_test_hub). Oddeleny od MeasureQProtocol.h (skutecny mereni
// protokol) - tohle je jen docasny diagnosticky nastroj.
#pragma once

#include <cstdint>

struct __attribute__((packed)) RangeTestPacket
{
  uint32_t seq;
  uint32_t sentMs;
};
