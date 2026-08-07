#pragma once

#include <Arduino.h>

// Ovlada jeden TM1637 4-cislicovy displej pripojeny PRIMO na GPIO ESP32
// (na rozdil od MIDIQ, kde stejny protokol jde pres MCP23S17 SPI expander
// kvuli nedostatku pinu - MeasureQ hub ma dost volnych GPIO, expander tu
// neni potreba). Nizkourovnovy TM1637 protokol/casovani a vysokourovnove
// API (showDigits/showNumber/showAllSegments/off) je prevzate z MIDIQ,
// jen fyzicka vrstva je bit-bang pres pinMode/digitalWrite misto zapisu do
// registru expanderu.
class TM1637Display
{
public:
  // invertedMount: true, pokud je displej fyzicky namontovany vzhuru
  // nohama (jako u MIDIQ). Pro MeasureQ zatim NEOVERENO na HW - vychozi
  // false (normalni orientace).
  TM1637Display(uint8_t clkPin, uint8_t dioPin, bool invertedMount = false);

  void begin();

  static const uint8_t BLANK = 0xFF;

  // d0 = nejlevejsi cislice. Kazda pozice 0-9, nebo BLANK pro prazdno.
  void showDigits(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);

  // 0-9999, bez vodicich nul (posledni cislice se zobrazi vzdy, i kdyz je 0).
  void showNumber(uint16_t value);

  void showAllSegments();
  void off();

private:
  uint8_t _clkPin;
  uint8_t _dioPin;
  bool _invertedMount;

  void setClk(bool level);
  void setDio(bool level);
  void setDioDir(bool asInput);

  void tmStart();
  void tmStop();
  void tmWriteByte(uint8_t value);
  void tmSendCommand(uint8_t command);
  void sendPatterns(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3);
};
