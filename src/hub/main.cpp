// MeasureQ - firmware centralni jednotky (ESP32 30pin)
// Prijima MeasurementPacket pres ESP-NOW od mericich krabicek (viz
// src/node/main.cpp) a preposila je do Google Sheets pres HTTP (Google
// Apps Script web app jako jednoduchy prijimac).
//
// ZMENA NAVRHU (2026-08-08): puvodni verze mela 3x TM1637 displej +
// tlacitka pro fyzicke zobrazeni primo na hubu. Misto toho se teď vsechno
// zaznamenava do Google Sheets - zadne fyzicke zobrazeni, sledovani přes
// telefon/pocitac. TM1637Display.* jiz nejsou potreba.
//
// VYZADUJE src/hub/secrets.h (WIFI_SSID, WIFI_PASSWORD,
// SHEETS_WEBHOOK_URL) - zkopiruj a vypln z secrets.h.example, viz
// PROJECT_NOTES.md pro navod na Google Apps Script.
//
// Po prvnim nahrani vypise Serial vlastni MAC adresu - tu je nutne
// zkopirovat do hubMac[] v src/node/main.cpp na vsech mericich krabickach.
//
// FRONTA + CASOVE RAZITKO (2026-08-08): HTTP pozadavek na Google Sheets je
// blokujici a pomaly (1-3+ vterin kvuli Google serveru), takze kdyby se
// ukladal jen "posledni prijaty paket", vsechny pakety prijate MEZI
// jednotlivymi HTTP volanimi by se ztratily (prepsaly by se). Misto toho
// se pakety radi do fronty (viz PENDING_QUEUE_SIZE) a odesilaji postupne.
// Casove razitko se navic zaznamenava HNED PRI PRIJETI (v onDataRecv()),
// ne az pri odeslani do Sheets - jinak by fronta zpusobovala rostouci
// zpozdeni mezi "kdy bylo opravdu zmereno" a "jaky cas se zapise do
// tabulky". Vyzaduje NTP synchronizaci (viz setupTime()) - bez ni by
// zaznamenany cas byl jen "pocet vterin od 1.1.1970" bez smysluplneho
// data.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <time.h>

#include "../common/MeasureQProtocol.h"
#include "secrets.h"

// Europe/Prague (CET/CEST s automatickym prechodem na letni cas) - POSIX
// TZ retezec, aby configTzTime() spravne pocital i DST bez rucniho
// prepinani.
constexpr char TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char NTP_SERVER[] = "pool.ntp.org";

struct QueuedMeasurement
{
  MeasurementPacket packet;
  time_t timestamp; // Unix epoch (sekundy) - zachyceno pri prijeti, ne pri odeslani
};

constexpr size_t PENDING_QUEUE_SIZE = 20;
QueuedMeasurement pendingQueue[PENDING_QUEUE_SIZE];
volatile size_t queueHead = 0;  // dalsi volny slot pro zapis (onDataRecv)
volatile size_t queueTail = 0;  // dalsi polozka ke zpracovani (loop)
volatile size_t queueCount = 0;

// ESP-NOW callback bezi na jinem tasku nez loop() - neni bezpecne v nem
// delat blokujici sitove volani (HTTP pozadavek), proto se paket jen
// zaradi do fronty a odeslani do Sheets se deje v loop().
//
// POTVRZENO kompilaci (2026-08-08): nainstalovana verze jadra pouziva
// starsi tvar esp_now_recv_cb_t (jen MAC, ne esp_now_recv_info_t*) - ten
// novejsi tvar s RSSI informaci tahle verze API nema.
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
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

  if (queueCount >= PENDING_QUEUE_SIZE)
  {
    Serial.println("VAROVANI: fronta plna, nejstarsi cekajici paket se zahazuje!");
    // Uvolnit misto zahozenim nejstarsiho cekajiciho paketu, aby fronta
    // nezustala trvale ucpana jednim starym zaseknutym zaznamem.
    queueTail = (queueTail + 1) % PENDING_QUEUE_SIZE;
    queueCount--;
  }

  pendingQueue[queueHead].packet = packet;
  pendingQueue[queueHead].timestamp = time(nullptr); // cas PRIJETI, ne odeslani
  queueHead = (queueHead + 1) % PENDING_QUEUE_SIZE;
  queueCount++;
}

void connectWiFi()
{
  Serial.printf("Pripojuji se k WiFi '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 15000)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("Pripojeno. IP: %s   WiFi kanal: %d\n",
                  WiFi.localIP().toString().c_str(), WiFi.channel());

    // DULEZITE: kdyz je STA pripojena k realne AP, ESP32 defaultne zapina
    // uspornej rezim radia (modem-sleep/power-save) - to muze zpusobit
    // zmeskani ESP-NOW broadcast paketu od node, protoze radio nemusi byt
    // vzdy plne "vzhuru" mimo obdobi, kdy ceka beacony od AP. Vypnuti
    // uspory drzi radio porad naslouchajici.
    WiFi.setSleep(false);
  }
  else
  {
    Serial.println("WiFi pripojeni selhalo - zkontroluj SSID/heslo v secrets.h!");
  }
}

// Synchronizuje realny cas pres NTP - bez tohoto by time(nullptr) vracelo
// jen pocet vterin od bootu (pripadne od 1.1.1970), ne skutecne datum.
void setupTime()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi nepripojeno - NTP synchronizace se preskakuje.");
    return;
  }

  Serial.print("Synchronizuji cas pres NTP...");
  configTzTime(TIMEZONE, NTP_SERVER);

  struct tm timeinfo;
  uint32_t startMs = millis();
  while (!getLocalTime(&timeinfo, 1000) && (millis() - startMs) < 15000)
  {
    Serial.print(".");
  }
  Serial.println();

  if (timeinfo.tm_year > 0)
  {
    Serial.printf("Cas synchronizovan: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
  else
  {
    Serial.println("NTP synchronizace selhala - casova razitka nebudou spravna!");
  }
}

void sendToSheets(const QueuedMeasurement &item)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi nepripojeno - paket se neposila do Sheets.");
    return;
  }

  const MeasurementPacket &packet = item.packet;
  String url = String(SHEETS_WEBHOOK_URL) +
               "?nodeId=" + String(packet.nodeId) +
               "&temp=" + String(packet.temperatureC, 1) +
               "&hum=" + String(packet.humidityPct, 1) +
               "&co2=" + String(packet.co2Ppm) +
               "&tvoc=" + String(packet.tvocPpb) +
               "&ts=" + String((uint32_t)item.timestamp);

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  http.end();

  Serial.printf("Node %u  #%-6lu  Teplota: %5.1f C   Vlhkost: %5.1f %%  ->  Sheets HTTP %d  (fronta: %u)\n",
                packet.nodeId, (unsigned long)packet.seq, packet.temperatureC,
                packet.humidityPct, httpCode, (unsigned)queueCount);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== MeasureQ hub - ESP-NOW -> Google Sheets ===");

  connectWiFi();
  setupTime();

  Serial.print("MAC hubu (zkopirovat do hubMac[] na vsech nodech): ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init selhal!");
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.printf("DIAGNOSTIKA: WiFi kanal tesne pred loop(): %d\n", WiFi.channel());
  Serial.println("Cekam na pakety...");
  Serial.println();
}

void loop()
{
  if (queueCount > 0)
  {
    QueuedMeasurement item = pendingQueue[queueTail];
    queueTail = (queueTail + 1) % PENDING_QUEUE_SIZE;
    queueCount--;

    sendToSheets(item);
  }

  // WiFi muze obcas vypadnout - zkousime znovu pripojit, dokud to nevyjde.
  static uint32_t lastReconnectAttemptMs = 0;
  if (WiFi.status() != WL_CONNECTED && (millis() - lastReconnectAttemptMs) > 10000)
  {
    lastReconnectAttemptMs = millis();
    connectWiFi();
    setupTime();
  }

  delay(10);
}
