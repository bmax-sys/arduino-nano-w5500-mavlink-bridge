#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <EEPROM.h>
#include <avr/wdt.h>

// =====================================================
// bmax_sys
// Arduino Nano + W5500 MAVLink Bridge
// Arduino Nano + W5500
// UART <-> UDP + EEPROM + WEB SETTINGS
// =====================================================

#define W5500_CS  10
#define W5500_RST 9

byte mac[] = {0x02, 0x4D, 0x41, 0x58, 0x58, 0x06};

const byte DEF_DEVICE_IP[4] = {192,168,88,50};
const byte DEF_MASK[4]      = {255,255,255,0};
const byte DEF_GATEWAY[4]   = {192,168,88,1};
const byte DEF_TARGET_IP[4] = {192,168,88,11};

const uint16_t DEF_UDP_PORT = 14550;
const uint32_t DEF_BAUD     = 115200;

const uint32_t SETTINGS_MAGIC = 0x424D5831UL; // "BMX1"

struct Settings {
  uint32_t magic;
  byte deviceIP[4];
  byte mask[4];
  byte gateway[4];
  byte targetIP[4];
  uint16_t udpPort;
  uint32_t baud;
};

Settings cfg;

IPAddress deviceIP;
IPAddress subnetMask;
IPAddress gatewayIP;
IPAddress targetIP;

uint16_t udpPort = DEF_UDP_PORT;
uint32_t uartBaud = DEF_BAUD;

EthernetUDP udp;
EthernetServer server(80);

// Small buffers to protect Nano SRAM
byte uartBuffer[64];
byte udpBuffer[64];
byte uartLen = 0;
unsigned long lastByteTime = 0;

// =====================================================
// SETTINGS
// =====================================================

void copy4(byte *dst, const byte *src) {
  for (byte i = 0; i < 4; i++) dst[i] = src[i];
}

void setDefaults() {
  cfg.magic = SETTINGS_MAGIC;

  copy4(cfg.deviceIP, DEF_DEVICE_IP);
  copy4(cfg.mask, DEF_MASK);
  copy4(cfg.gateway, DEF_GATEWAY);
  copy4(cfg.targetIP, DEF_TARGET_IP);

  cfg.udpPort = DEF_UDP_PORT;
  cfg.baud = DEF_BAUD;
}

void applySettings() {
  deviceIP = IPAddress(
    cfg.deviceIP[0], cfg.deviceIP[1],
    cfg.deviceIP[2], cfg.deviceIP[3]
  );

  subnetMask = IPAddress(
    cfg.mask[0], cfg.mask[1],
    cfg.mask[2], cfg.mask[3]
  );

  gatewayIP = IPAddress(
    cfg.gateway[0], cfg.gateway[1],
    cfg.gateway[2], cfg.gateway[3]
  );

  targetIP = IPAddress(
    cfg.targetIP[0], cfg.targetIP[1],
    cfg.targetIP[2], cfg.targetIP[3]
  );

  udpPort = cfg.udpPort;
  uartBaud = cfg.baud;
}

void loadSettings() {
  EEPROM.get(0, cfg);

  if (cfg.magic != SETTINGS_MAGIC) {
    setDefaults();
    EEPROM.put(0, cfg);
  }

  applySettings();
}

void saveSettings() {
  cfg.magic = SETTINGS_MAGIC;
  EEPROM.put(0, cfg);
}

// =====================================================
// HELPERS
// =====================================================

bool parseIP(const char *s, byte out[4]) {
  int a, b, c, d;

  if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }

  if (a < 0 || a > 255 ||
      b < 0 || b > 255 ||
      c < 0 || c > 255 ||
      d < 0 || d > 255) {
    return false;
  }

  out[0] = (byte)a;
  out[1] = (byte)b;
  out[2] = (byte)c;
  out[3] = (byte)d;

  return true;
}

void printIP(EthernetClient &client, const byte ip[4]) {
  client.print(ip[0]); client.print('.');
  client.print(ip[1]); client.print('.');
  client.print(ip[2]); client.print('.');
  client.print(ip[3]);
}

bool getQueryValue(const char *request,
                   const char *key,
                   char *out,
                   byte outSize) {
  char token[18];
  snprintf(token, sizeof(token), "%s=", key);

  const char *p = strstr(request, token);
  if (!p) return false;

  p += strlen(token);

  byte i = 0;

  while (*p &&
         *p != '&' &&
         *p != ' ' &&
         i < outSize - 1) {
    out[i++] = *p++;
  }

  out[i] = 0;
  return true;
}

void rebootNano() {
  wdt_enable(WDTO_15MS);
  while (1) {}
}

// =====================================================
// UART -> UDP
// =====================================================

void uartToUDP() {
  while (Serial.available() && uartLen < sizeof(uartBuffer)) {
    uartBuffer[uartLen++] = Serial.read();
    lastByteTime = micros();
  }

  if (uartLen > 0 &&
      ((micros() - lastByteTime) > 2000 ||
       uartLen >= sizeof(uartBuffer))) {

    udp.beginPacket(targetIP, udpPort);
    udp.write(uartBuffer, uartLen);
    udp.endPacket();

    uartLen = 0;
  }
}

// =====================================================
// UDP -> UART
// =====================================================

void udpToUART() {
  int packetSize = udp.parsePacket();

  if (packetSize <= 0) return;

  while (packetSize > 0) {
    int chunk = packetSize;

    if (chunk > (int)sizeof(udpBuffer)) {
      chunk = sizeof(udpBuffer);
    }

    int len = udp.read(udpBuffer, chunk);

    if (len <= 0) break;

    Serial.write(udpBuffer, len);
    packetSize -= len;
  }
}

// =====================================================
// WEB
// =====================================================

void sendHttpHeader(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=UTF-8"));
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();
}

void sendSettingsPage(EthernetClient &client) {
  sendHttpHeader(client);

  client.println(F("<!DOCTYPE html><html><head>"));
  client.println(F("<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  client.println(F("<title>bmax_sys | Nano W5500 MAVLink Bridge</title>"));

  client.println(F(
    "<style>"
    ":root{--y:#ffcc00;--g:rgba(255,204,0,.10);--f:#d0d0d0}"
    "*{box-sizing:border-box}"
    "html,body{margin:0;padding:0;width:100%;min-height:100%;background:#000;color:var(--y);font-family:'Courier New',Consolas,monospace}"
    "body{min-height:100vh;background-image:linear-gradient(var(--g) 1px,transparent 1px),linear-gradient(90deg,var(--g) 1px,transparent 1px);background-size:32px 32px}"
    ".frame{min-height:calc(100vh - 24px);margin:12px;border:2px solid var(--y);position:relative;background:#000;overflow:hidden}"
    ".frame:before,.frame:after{content:'';position:absolute;width:110px;height:6px;background:var(--y);top:-2px}"
    ".frame:before{left:24px}.frame:after{right:24px}"
    ".brand{position:absolute;top:22px;left:32px;z-index:10}.brand-main{font-family:'Lucida Console','Courier New',monospace;font-size:28px;font-weight:900;letter-spacing:3px;text-shadow:2px 0 0 #665200}"
    ".brand-sub{margin-top:4px;font-size:12px;letter-spacing:1px;color:#aaa}"
    ".top-right{position:absolute;top:26px;right:34px;font-weight:900;letter-spacing:3px;font-size:18px}"
  ));

  client.println(F(
    ".settings-wrapper{width:100%;padding:125px 50px 45px}"
    ".settings-panel{position:relative;width:100%;border:2px solid var(--y);padding:42px 42px 38px;background:#000;box-shadow:6px 6px 0 rgba(255,204,0,.08)}"
    ".panel-title{position:absolute;top:-20px;left:50%;transform:translateX(-50%);padding:5px 24px;background:#000;border:2px solid var(--y);font-weight:900;font-size:22px;letter-spacing:3px;white-space:nowrap}"
    ".settings-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:26px 42px}"
    ".field label{display:block;margin-bottom:9px;font-size:15px;font-weight:900;letter-spacing:1px}"
    "input,select{width:100%;height:48px;padding:0 14px;color:var(--f);background:#050505;border:2px solid var(--y);outline:none;border-radius:0;font-family:'Courier New',Consolas,monospace;font-size:18px;font-weight:700}"
    ".button-wrapper{display:flex;justify-content:center;margin-top:34px}"
    ".save-button{min-width:320px;height:52px;border:2px solid var(--y);background:var(--y);color:#000;font-family:'Courier New',Consolas,monospace;font-size:17px;font-weight:900;letter-spacing:2px;cursor:pointer;box-shadow:5px 5px 0 #8f7300}"
    ".foot{text-align:center;margin-top:18px;color:#777;font-size:11px;letter-spacing:1px}"
    "@media(max-width:900px){.settings-grid{grid-template-columns:1fr}.settings-wrapper{padding:135px 20px 30px}.panel-title{font-size:17px}.save-button{min-width:100%}}"
    "</style></head><body><div class='frame'>"
  ));

  client.println(F("<div class='brand'><div class='brand-main'>bmax_sys</div><div class='brand-sub'>UART / ETHERNET MAVLink BRIDGE</div></div>"));
  client.println(F("<div class='top-right'>bmax_sys</div>"));
  client.println(F("<div class='settings-wrapper'><form method='GET' action='/save'><div class='settings-panel'>"));
  client.println(F("<div class='panel-title'>NETWORK SETTINGS</div><div class='settings-grid'>"));

  client.print(F("<div class='field'><label>DEVICE IP</label><input name='deviceIP' value='"));
  printIP(client, cfg.deviceIP);
  client.println(F("' required></div>"));

  client.print(F("<div class='field'><label>TARGET IP</label><input name='targetIP' value='"));
  printIP(client, cfg.targetIP);
  client.println(F("' required></div>"));

  client.println(F("<div class='field'><label>UART BAUDRATE</label><select name='baud'>"));
  const uint32_t rates[] = {9600UL,19200UL,38400UL,57600UL,115200UL,230400UL};
  for (byte i=0;i<6;i++) {
    client.print(F("<option value='")); client.print(rates[i]); client.print('\'');
    if (cfg.baud == rates[i]) client.print(F(" selected"));
    client.print('>'); client.print(rates[i]); client.println(F("</option>"));
  }
  client.println(F("</select></div>"));

  client.print(F("<div class='field'><label>SUBNET MASK</label><input name='mask' value='"));
  printIP(client, cfg.mask);
  client.println(F("' required></div>"));

  client.print(F("<div class='field'><label>GATEWAY</label><input name='gateway' value='"));
  printIP(client, cfg.gateway);
  client.println(F("' required></div>"));

  client.print(F("<div class='field'><label>UDP PORT</label><input type='number' name='port' value='"));
  client.print(cfg.udpPort);
  client.println(F("' required></div>"));

  client.println(F(
    "</div><div class='button-wrapper'><button type='submit' class='save-button'>SAVE & REBOOT</button></div>"
    "<div class='foot'>ARDUINO NANO + W5500 / bmax_sys v1.0</div>"
    "</div></form></div></div></body></html>"
  ));
}
void sendSavedPage(EthernetClient &client) {
  sendHttpHeader(client);
  client.println(F(
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Settings Saved</title><style>"
    "body{margin:0;background:#000;color:#ffcc00;font-family:'Courier New',Consolas,monospace}"
    ".frame{min-height:calc(100vh - 24px);margin:12px;border:2px solid #ffcc00;display:flex;align-items:center;justify-content:center;text-align:center}"
    ".box{padding:34px;border:2px solid #ffcc00;box-shadow:6px 6px 0 rgba(255,204,0,.10)}"
    ".check{width:70px;height:70px;margin:0 auto 22px;border:3px solid #ffcc00;display:flex;align-items:center;justify-content:center;font-size:38px;font-weight:900}"
    "h1{margin:0 0 10px;font-size:28px;letter-spacing:2px}.line{height:2px;background:#ffcc00;margin:24px 0}.ip{font-size:18px;font-weight:900}"
    "</style></head><body><div class='frame'><div class='box'>"
    "<div class='check'>&#10003;</div><h1>SETTINGS SAVED</h1><div>Device is rebooting...</div>"
    "<div class='line'></div><div>Reconnect to:</div><div class='ip'>http://"
  ));
  printIP(client, cfg.deviceIP);
  client.println(F("</div></div></div></body></html>"));
}
void sendError(EthernetClient &client, const __FlashStringHelper *msg) {
  client.println(F("HTTP/1.1 400 Bad Request"));
  client.println(F("Content-Type: text/plain"));
  client.println(F("Connection: close"));
  client.println();
  client.println(msg);
}

void handleSave(EthernetClient &client, const char *request) {
  char value[24];
  byte tempIP[4];

  if (!getQueryValue(request, "deviceIP", value, sizeof(value)) ||
      !parseIP(value, tempIP)) {
    sendError(client, F("Invalid Device IP"));
    return;
  }
  copy4(cfg.deviceIP, tempIP);

  if (!getQueryValue(request, "targetIP", value, sizeof(value)) ||
      !parseIP(value, tempIP)) {
    sendError(client, F("Invalid Target IP"));
    return;
  }
  copy4(cfg.targetIP, tempIP);

  if (!getQueryValue(request, "mask", value, sizeof(value)) ||
      !parseIP(value, tempIP)) {
    sendError(client, F("Invalid Subnet Mask"));
    return;
  }
  copy4(cfg.mask, tempIP);

  if (!getQueryValue(request, "gateway", value, sizeof(value)) ||
      !parseIP(value, tempIP)) {
    sendError(client, F("Invalid Gateway"));
    return;
  }
  copy4(cfg.gateway, tempIP);

  if (!getQueryValue(request, "port", value, sizeof(value))) {
    sendError(client, F("Missing UDP Port"));
    return;
  }

  long newPort = atol(value);

  if (newPort < 1 || newPort > 65535) {
    sendError(client, F("Invalid UDP Port"));
    return;
  }

  cfg.udpPort = (uint16_t)newPort;

  if (!getQueryValue(request, "baud", value, sizeof(value))) {
    sendError(client, F("Missing Baud"));
    return;
  }

  long newBaud = atol(value);

  if (newBaud < 1200 || newBaud > 230400) {
    sendError(client, F("Invalid Baud"));
    return;
  }

  cfg.baud = (uint32_t)newBaud;

  saveSettings();
  sendSavedPage(client);

  delay(150);
  client.stop();
  delay(500);

  rebootNano();
}

void handleWeb() {
  EthernetClient client = server.available();

  if (!client) return;

  char requestLine[220];
  byte pos = 0;

  unsigned long timeout = millis();

  // Read only first HTTP line.
  // This is enough because the web UI uses GET for saving.
  while (client.connected() &&
         millis() - timeout < 1000 &&
         pos < sizeof(requestLine) - 1) {

    if (client.available()) {
      char c = client.read();

      if (c == '\n') {
        break;
      }

      if (c != '\r') {
        requestLine[pos++] = c;
      }
    }
  }

  requestLine[pos] = 0;

  // Drain remaining headers quickly
  bool blankLine = false;
  byte lineLen = 0;
  timeout = millis();

  while (client.connected() &&
         millis() - timeout < 500 &&
         !blankLine) {

    if (client.available()) {
      char c = client.read();

      if (c == '\n') {
        if (lineLen == 0) {
          blankLine = true;
        }
        lineLen = 0;
      }
      else if (c != '\r') {
        lineLen++;
      }
    }
  }

  if (strncmp(requestLine, "GET /save?", 10) == 0) {
    handleSave(client, requestLine);
    return;
  }

  sendSettingsPage(client);

  delay(2);
  client.stop();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  loadSettings();

  Serial.begin(uartBaud);

  pinMode(W5500_RST, OUTPUT);

  digitalWrite(W5500_RST, LOW);
  delay(200);

  digitalWrite(W5500_RST, HIGH);
  delay(1000);

  Ethernet.init(W5500_CS);

  Ethernet.begin(
    mac,
    deviceIP,
    gatewayIP,  // DNS
    gatewayIP,  // Gateway
    subnetMask
  );

  delay(1000);

  udp.begin(udpPort);
  server.begin();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  uartToUDP();
  udpToUART();
  handleWeb();
}
