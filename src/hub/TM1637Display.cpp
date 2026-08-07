#include "TM1637Display.h"

namespace
{
  const uint8_t TM_CMD_DATA = 0x40;           // auto-increment adresy
  const uint8_t TM_CMD_ADDR = 0xC0;           // start na adrese 0
  const uint8_t TM_CMD_DISPLAY_ON = 0x88 | 7; // zapnuto, max jas
  const uint8_t TM_CMD_DISPLAY_OFF = 0x80;

  // bit0..6 = a..g, standardni univerzalni 7-segmentove kodovani.
  const uint8_t DIGIT_FONT[10] = {
      0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

  // Pro pripad, ze by byl displej namontovany vzhuru nohama (jako u MIDIQ):
  // a<->d, b<->e, c<->f (g i dp/dvojtecka zustavaji, lezi na ose otaceni).
  uint8_t flipUpsideDown(uint8_t pattern)
  {
    return (uint8_t)(((pattern & 0x07) << 3) | ((pattern & 0x38) >> 3) | (pattern & 0xC0));
  }

  // TM1637Display::BLANK (mimo 0-9) -> zadne segmenty.
  uint8_t digitPattern(uint8_t d)
  {
    return (d <= 9) ? DIGIT_FONT[d] : 0x00;
  }
}

TM1637Display::TM1637Display(uint8_t clkPin, uint8_t dioPin, bool invertedMount)
    : _clkPin(clkPin), _dioPin(dioPin), _invertedMount(invertedMount)
{
}

void TM1637Display::setClk(bool level)
{
  digitalWrite(_clkPin, level ? HIGH : LOW);
}

void TM1637Display::setDio(bool level)
{
  digitalWrite(_dioPin, level ? HIGH : LOW);
}

void TM1637Display::setDioDir(bool asInput)
{
  pinMode(_dioPin, asInput ? INPUT : OUTPUT);
}

void TM1637Display::begin()
{
  pinMode(_clkPin, OUTPUT);
  pinMode(_dioPin, OUTPUT);
  setClk(true);
  setDio(true);
}

void TM1637Display::tmStart()
{
  setDio(true);
  setClk(true);
  setDio(false); // DATA high->low pri CLK high = START
}

void TM1637Display::tmStop()
{
  setClk(false);
  setDio(false);
  setClk(true);
  setDio(true); // DATA low->high pri CLK high = STOP
}

// LSB prvni. Po bajtu uvolni DATA jako vstup pro ACK bit od TM1637 (hodnotu
// nekontrolujeme, jde jen o to nekolidovat s aktivnim stazenim od displeje).
void TM1637Display::tmWriteByte(uint8_t value)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    setClk(false);
    setDio((value >> i) & 0x01);
    setClk(true);
  }
  setClk(false);
  setDioDir(true);
  setClk(true);
  setClk(false);
  setDioDir(false);
}

void TM1637Display::tmSendCommand(uint8_t command)
{
  tmStart();
  tmWriteByte(command);
  tmStop();
}

void TM1637Display::sendPatterns(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
  tmSendCommand(TM_CMD_DATA);

  tmStart();
  tmWriteByte(TM_CMD_ADDR);
  if (_invertedMount)
  {
    tmWriteByte(flipUpsideDown(p3));
    tmWriteByte(flipUpsideDown(p2));
    tmWriteByte(flipUpsideDown(p1));
    tmWriteByte(flipUpsideDown(p0));
  }
  else
  {
    tmWriteByte(p0);
    tmWriteByte(p1);
    tmWriteByte(p2);
    tmWriteByte(p3);
  }
  tmStop();

  tmSendCommand(TM_CMD_DISPLAY_ON);
}

void TM1637Display::showDigits(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
  sendPatterns(digitPattern(d0), digitPattern(d1), digitPattern(d2), digitPattern(d3));
}

// Vodici nuly (krome posledni cislice) se nahradi prazdnym vzorem.
void TM1637Display::showNumber(uint16_t value)
{
  if (value > 9999)
    value = 9999;
  uint8_t d[4] = {
      (uint8_t)((value / 1000) % 10),
      (uint8_t)((value / 100) % 10),
      (uint8_t)((value / 10) % 10),
      (uint8_t)(value % 10),
  };

  uint8_t p[4];
  bool leading = true;
  for (uint8_t i = 0; i < 4; i++)
  {
    bool isLast = (i == 3);
    if (leading && d[i] == 0 && !isLast)
    {
      p[i] = 0x00;
    }
    else
    {
      p[i] = DIGIT_FONT[d[i]];
      leading = false;
    }
  }

  sendPatterns(p[0], p[1], p[2], p[3]);
}

void TM1637Display::showAllSegments()
{
  tmSendCommand(TM_CMD_DATA);

  tmStart();
  tmWriteByte(TM_CMD_ADDR);
  for (uint8_t i = 0; i < 4; i++)
    tmWriteByte(0xFF);
  tmStop();

  tmSendCommand(TM_CMD_DISPLAY_ON);
}

void TM1637Display::off()
{
  tmSendCommand(TM_CMD_DISPLAY_OFF);
}
