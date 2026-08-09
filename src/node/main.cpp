// MeasureQ - firmware mericí krabicky (ESP32-C3-SuperMini)
// HW: ENS160+AHT21 (I2C) spinane pres PNP tranzistor (BC557) jako high-side
// spinac 3.3V vetve senzoru (aktivni LOW), XL63070 (TPS63070) buck-boost
// 2x AAA -> 3.3V, ESP-NOW vysilani do hubu.
//
// Napajeci cyklus (kvuli baterii - ESP32 vetsinu casu spi v deep sleep).
// Deep sleep = reset cipu, RAM (krome RTC_DATA_ATTR) se neuchova, takze po
// kazdem probuzeni bezi cely setup() znovu od zacatku:
//   1. zapnout napajeni senzoru
//   2. pockat SENSOR_WARMUP_MS (viz definice nize a PROJECT_NOTES.md)
//   3. inicializovat I2C, precist senzor
//   4. vypnout napajeni senzoru - uz neni potreba, setri baterii pred WiFi
//   5. inicializovat WiFi/ESP-NOW, odeslat paket, kratce pockat na potvrzeni
//   6. esp_deep_sleep_start() na MEASURE_INTERVAL_US
//
// PIN_SDA/PIN_SCL a API knihovny ScioSense_ENS16x jsou uz overene na realnem
// HW (viz src/node_test/main.cpp a PROJECT_NOTES.md). Pred prvnim flashem
// KONKRETNI krabicky jeste zkontrolovat/upravit:
//  - NODE_ID (zmenit pro kazdou ze 3 krabicek: 0, 1, 2)
//  - hubMac[] (MAC adresa hubu - vypsana hubem na Serial po prvnim flashi,
//    viz src/hub/main.cpp)
//  - PNP tranzistor (BC557) - zapojeni viz PROJECT_NOTES.md

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>

#include "../common/MeasureQProtocol.h"

constexpr uint8_t NODE_ID = 0; // ZMENIT per krabicka: 0, 1, 2 - tohle je M1 (2026-08-09)
uint8_t hubMac[6] = {0xB4, 0xBF, 0xE9, 0xC8, 0x57, 0x40}; // MAC hubu, vypsano hubem 2026-08-09

// DULEZITE: ESP-NOW vyzaduje stejny WiFi kanal na obou stranach. Node se
// (na rozdil od hubu, ktery se pripojuje ke skutecne WiFi kvuli Google
// Sheets) k zadne WiFi siti nepripojuje, takze bez explicitniho nastaveni
// zustane na kanalu 1 - jenze hub bezi na kanalu sve realne WiFi site
// (u Dana potvrzeno kanal 3, 2026-08-08). Musi se rucne sladit s tim, co
// hub po pripojeni vypise ("WiFi kanal: X") - pokud se v budoucnu zmeni
// kanal routeru, je nutne zmenit i tohle cislo (nebo nastavit routeru
// pevny kanal).
constexpr uint8_t HUB_WIFI_CHANNEL = 3;

// Jak casto se krabicka probouzi a posila data. Delsi interval = vyrazne
// lepsi vydrz baterie, protoze 3min zahrivani senzoru tvori mensi podil
// cyklu - viz tabulka vydrze v PROJECT_NOTES.md.
// DOCASNE ZKRACENO na 30s kvuli testovani (2026-08-09) - pri realnem
// nasazeni na baterii VRATIT ZPET na 15 minut (900), jinak vydrz z tabulky
// v PROJECT_NOTES.md neplati (SENSOR_WARMUP_MS 1 min pak tvori vetsinu
// kazdeho cyklu).
constexpr uint64_t MEASURE_INTERVAL_US = 30ULL * 1000000; // 30 sekund (testovani)

// Puvodne odvozeno z doby zahrati ENS160 MOX ohrivace (~1-3 min, zdroje se
// rozchazeji). Od te doby se ale ENS160 mereni zamerne VYPNULO (viz
// startStandardMeasure() nize) kvuli ovlivnovani AHT21 teploty - AHT21 sam
// o sobe je prakticky okamzity. ZKRACENO 2026-08-09 (Dan: "jake zahrivani,
// ten CO2 senzor je vypnuty") na 100ms - jen zbyva cas na ustaleni napajeni
// senzoru po sepnuti tranzistoru pred prvnim I2C pristupem. Pri zapnuti
// ENS160 mereni zpet (viz PROJECT_NOTES.md) je nutne tuhle hodnotu vratit
// na ~1-3 min kvuli MOX ohrivaci.
constexpr uint32_t SENSOR_WARMUP_MS = 100;

// Jak dlouho cekat na potvrzeni odeslani ESP-NOW pred uspanim.
constexpr uint32_t SEND_CONFIRM_TIMEOUT_MS = 2000;

// TESTOVACI REZIM (2026-08-09): senzor se zapne jen jednou pri startu (ne
// pri kazdem paketu) a dal se bez deep sleep posila kazde TEST_SEND_INTERVAL_MS
// - pro rychlou zpetnou vazbu pri ladeni na stole. VYPNOUT (false) pred
// realnym nasazenim na baterii, jinak zadny deep sleep = vybita baterie
// behem hodin, ne tydnu/mesicu.
constexpr bool TEST_MODE = false;
constexpr uint32_t TEST_SEND_INTERVAL_MS = 2000;

// PROVIZORNI kalibracni offset teploty, dokud nedorazi samostatny AHT21/
// AHT20 senzor mimo desku ESP32-C3 (viz PROJECT_NOTES.md). Pricina zbyvajiciho
// posunu i s vypnutym ENS160 ohrivacem: ENS160 je porad napajeny/pripojeny
// po I2C (klidovy proud), a je fyzicky na stejne malicke desticce jako
// AHT21 - nejde oddelit bez samostatneho modulu. Zmereno 2026-08-08:
// syrova hodnota 28.9 C vs 26.4 C na stolnim teplomeru -> offset -2.5 C.
// Pri vymene za samostatny AHT21/AHT20 senzor tento offset SMAZAT/prepocitat.
constexpr float TEMP_CALIBRATION_OFFSET_C = -2.5f;

// PROVIZORNI kalibracni offset vlhkosti, stejny duvod jako u teploty. POZOR:
// vlhkost je fyzikalne svazana s teplotou (relativni vlhkost = mnozstvi
// vodni pary vuci tomu, kolik by vzduch pojmul PRI DANE teplote) - tenhle
// pevny offset je tedy jen hruby odhad platny poblíž aktualnich podminek,
// ne presna psychrometricka korekce. Zmereno 2026-08-08: syrova hodnota
// 39.4 % vs 44 % na referencnim vlhkomeru -> offset +4.6 %.
constexpr float HUMIDITY_CALIBRATION_OFFSET_PCT = 4.6f;

// I2C piny - GPIO4/GPIO3 zvoleny zamerne mimo strapping piny (2, 8, 9).
// POTVRZENO kontinuitou multimetrem (2026-08-08): SDA na pinu "4", SCL na
// pinu "3" - puvodni predpoklad byl spravne (viz PROJECT_NOTES.md).
constexpr int PIN_SDA = 4;
constexpr int PIN_SCL = 3;

// Rizeni PNP tranzistoru BC557 (aktivni LOW = sepnuto) pres baznovy
// rezistor R1 - spina 3.3V vetev senzoru (high-side spinac), GND senzoru
// zustava trvale spolecna s ESP32. Zbyva v "bezpecne" pinove skupine
// (0, 1, 10) spolu s GPIO0/1, ktere zustavaji volne pro budouci ADC
// mereni napeti baterie. Viz PROJECT_NOTES.md.
constexpr int SENSOR_POWER_PIN = 10;

// ADD pin senzoru je pripojeny na 3.3V (HIGH) -> I2C adresa ENS160 0x53
// (LOW by dalo 0x52). Viz PROJECT_NOTES.md.
constexpr uint8_t ENS160_I2C_ADDRESS = 0x53;

Adafruit_AHTX0 aht;
ENS160 ens160;

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

bool peerReady = false;

void readAndSendPacket()
{
  MeasurementPacket packet{};
  packet.nodeId = NODE_ID;
  packet.seq = packetSeq++;

  // DIAGNOSTIKA (2026-08-09): Adafruit_AHTX0::getEvent() ma uvnitr
  // "while (busy) delay(10)" BEZ timeoutu - podezreni, ze kdyz je senzor
  // pripojeny na delsich dratech (slabsi I2C signal), tahle smycka se
  // obcas natahne o desitky vterin navic (nepravidelne delsi cykly, viz
  // PROJECT_NOTES.md). Merime, jak dlouho getEvent() fakt trval.
  uint32_t ahtStartMs = millis();
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  Serial.printf("aht.getEvent() trval %lu ms\n", (unsigned long)(millis() - ahtStartMs));
  packet.temperatureC = temp.temperature;
  packet.humidityPct = humidity.relative_humidity + HUMIDITY_CALIBRATION_OFFSET_PCT;

  packet.co2Ppm = 0; // ENS160 mereni zamerne vypnuto, viz komentar v setup()
  packet.tvocPpb = 0;
  packet.batteryVoltage = 0.0f; // TODO: zmerit pres ADC (delic napeti)

  Serial.printf("Node %u  #%-6lu  Teplota: %5.1f C   Vlhkost: %5.1f %%\n",
                packet.nodeId, (unsigned long)packet.seq, packet.temperatureC, packet.humidityPct);

  if (!peerReady)
  {
    return;
  }

  sendConfirmed = false;
  esp_now_send(hubMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));

  uint32_t waitStart = millis();
  while (!sendConfirmed && (millis() - waitStart) < SEND_CONFIRM_TIMEOUT_MS)
  {
    delay(10);
  }
}

void setup()
{
  Serial.begin(115200);
  if (TEST_MODE)
  {
    delay(1500); // cas na pripojeni Serial Monitoru po naflashovani
    Serial.println("=== MeasureQ node - TEST_MODE (bez deep sleep) ===");
  }

  sensorPowerOn();
  delay(SENSOR_WARMUP_MS);

  Wire.begin(PIN_SDA, PIN_SCL);

  uint32_t ahtBeginStartMs = millis();
  bool ahtOk = aht.begin();
  Serial.printf("aht.begin() trval %lu ms\n", (unsigned long)(millis() - ahtBeginStartMs));
  if (!ahtOk)
  {
    Serial.println("AHT21 nenalezen - zkontrolovat zapojeni!");
  }

  ens160.begin(&Wire, ENS160_I2C_ADDRESS);
  bool ensOk = ens160.init();
  if (!ensOk)
  {
    Serial.println("ENS160 nenalezen - zkontrolovat zapojeni!");
  }
  // ZAMERNE nevolame ens160.startStandardMeasure() - to by spustilo MOX
  // ohrivac (200-300 C), ktery i pres izolacni drazku v PCB dost ovlivni
  // sousedici AHT21 (namereno +6-7 C posun teploty, viz PROJECT_NOTES.md).
  // Dan prioritizuje presnou teplotu/vlhkost pred CO2/TVOC - viz TODO
  // "Mereni CO2/TVOC vypnuto" v PROJECT_NOTES.md pro moznost znovuzapnuti.

  // V produkcnim rezimu senzor po prvnim cteni v readAndSendPacket() hned
  // vypneme (setri baterii pred WiFi). V TEST_MODE zustava zapnuty, protoze
  // se cte opakovane v loop().

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(HUB_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() == ESP_OK)
  {
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, hubMac, 6);
    peer.channel = HUB_WIFI_CHANNEL;
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) == ESP_OK)
    {
      peerReady = true;
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

  if (TEST_MODE)
  {
    return; // dal pokracuje loop()
  }

  readAndSendPacket();
  sensorPowerOff();
  goToSleep();
}

void loop()
{
  if (!TEST_MODE)
  {
    return; // nedosazitelne - setup() konci esp_deep_sleep_start(), ktery cip resetuje
  }

  static uint32_t lastSendMs = 0;
  uint32_t now = millis();
  if (now - lastSendMs >= TEST_SEND_INTERVAL_MS)
  {
    lastSendMs = now;
    readAndSendPacket();
  }
}
