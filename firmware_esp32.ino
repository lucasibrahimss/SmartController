#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>

// ============ BOTOES ============
const int NUM_BUTTONS = 7;

const int BTN1_PIN = 23;
const int BTN2_PIN = 25;
const int BTN3_PIN = 26;
const int BTN4_PIN = 27;
const int BTN5_PIN = 32;
const int BTN6_PIN = 4;
const int BTN7_PIN = 33;

const int BUTTON_PINS[NUM_BUTTONS] = {
  BTN1_PIN, BTN2_PIN, BTN3_PIN, BTN4_PIN, BTN5_PIN, BTN6_PIN, BTN7_PIN
};

const char* BUTTON_NAMES[NUM_BUTTONS] = {
  "Botao 1", "Botao 2", "Botao 3", "Botao 4", "Botao 5", "Botao 6", "Botao 7"
};

const char* BUTTON_TOPICS[NUM_BUTTONS] = {
  "tcc/lucas/button1",
  "tcc/lucas/button2",
  "tcc/lucas/button3",
  "tcc/lucas/button4",
  "tcc/lucas/button5",
  "tcc/lucas/button6",
  "tcc/lucas/button7"
};

bool lastButtonState[NUM_BUTTONS];
bool pressActive = false;
bool longMsgShown = false;
int activeButtonIndex = -1;
unsigned long pressStartTime = 0;

// ============ LCD ============
LiquidCrystal_I2C lcd(0x27, 16, 2);

bool lcdBacklightOn = true;
unsigned long lastLcdActivity = 0;

const unsigned long LCD_IDLE_TIMEOUT_MS = 15000;
const unsigned long LCD_REMEDIO_BACKLIGHT_MS = 60000;

bool remedioMessageActive = false;
unsigned long lcdMsgExpireAt = 0;

// ============ RELÓGIO / NTP ============
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = -3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

bool timeInitialized = false;
unsigned long lastClockUpdateMs = 0;
char lastClockStr[9] = "";

// ============ CONFIG PADRAO ============
const unsigned long WIFI_TIMEOUT_MS = 15000;
const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASS = "";
const char* DEFAULT_MQTT_HOST = "192.168.3.30";
const uint16_t DEFAULT_MQTT_PORT = 1883;
const char* DEFAULT_MQTT_USER = "lucasibrahim";
const char* DEFAULT_MQTT_PASS = "1234";

// ============ MQTT ============
const char* MQTT_CLIENT_BASE = "esp32_tcc_remoto";
const char* MQTT_STATUS_TOPIC = "tcc/remoto/status";
const char* MQTT_DEBUG_TOPIC = "tcc/remoto/debug";

const char* MQTT_LCD_TOPIC = "tcc/lucas/lcd";
const char* MQTT_REMEDIOS_TOPIC = "tcc/lucas/remedios";

// ============ AUXILIARES REMEDIOS ============
const int MAX_REMEDIOS_LCD = 10;

struct RemedioLCD {
  String nome;
  String dose;
};

RemedioLCD remediosLCD[MAX_REMEDIOS_LCD];
int remediosLCDTotal = 0;
int remedioLCDIndex = 0;
unsigned long ultimoCicloRemedio = 0;
const unsigned long intervaloCicloRemedio = 3000;
String remedioHora = "";

// ============ CONFIGURACAO PERSISTENTE ============
Preferences prefs;
WebServer configServer(80);

String cfg_wifi_ssid;
String cfg_wifi_pass;
String cfg_mqtt_host;
uint16_t cfg_mqtt_port;
String cfg_mqtt_user;
String cfg_mqtt_pass;

bool configPortalActive = false;
const char* CONFIG_AP_SSID = "ESP32-Remedios-Setup";
const char* CONFIG_AP_PASS = "12345678";

// ============ CLIENTES ============
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============ FUNÇÕES AUXILIARES LCD ============

String jsonGetStringValue(const String& src, const String& key) {
  String token = "\"" + key + "\"";
  int keyPos = src.indexOf(token);
  if (keyPos < 0) return "";

  int colonPos = src.indexOf(':', keyPos);
  if (colonPos < 0) return "";

  int firstQuote = src.indexOf('"', colonPos + 1);
  if (firstQuote < 0) return "";

  int secondQuote = src.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";

  return src.substring(firstQuote + 1, secondQuote);
}

bool jsonGetNextItem(const String& src, int& searchPos, String& nome, String& dose) {
  int nomeKey = src.indexOf("\"nome\"", searchPos);
  if (nomeKey < 0) return false;

  int nomeColon = src.indexOf(':', nomeKey);
  if (nomeColon < 0) return false;

  int nomeQ1 = src.indexOf('"', nomeColon + 1);
  if (nomeQ1 < 0) return false;

  int nomeQ2 = src.indexOf('"', nomeQ1 + 1);
  if (nomeQ2 < 0) return false;

  nome = src.substring(nomeQ1 + 1, nomeQ2);

  int doseKey = src.indexOf("\"dose\"", nomeQ2);
  if (doseKey < 0) return false;

  int doseColon = src.indexOf(':', doseKey);
  if (doseColon < 0) return false;

  int doseQ1 = src.indexOf('"', doseColon + 1);
  if (doseQ1 < 0) return false;

  int doseQ2 = src.indexOf('"', doseQ1 + 1);
  if (doseQ2 < 0) return false;

  dose = src.substring(doseQ1 + 1, doseQ2);

  searchPos = doseQ2 + 1;
  return true;
}

void lcdWake() {
  if (!lcdBacklightOn) {
    lcd.backlight();
    lcdBacklightOn = true;
  }
  lastLcdActivity = millis();
}

void lcdShow(const char* line1, const char* line2 = "") {
  lcdWake();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  if (line2 && line2[0] != '\0') {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }
}

void lcdShowTransient(const String& msg) {
  remedioMessageActive = false;
  lcdMsgExpireAt = millis() + 2000;

  String line1 = msg.substring(0, 16);
  String line2 = "";
  if (msg.length() > 16) {
    line2 = msg.substring(16, 32);
  }
  lcdShow(line1.c_str(), line2.c_str());
}

void lcdShowRemedio(const String& msg) {
  remedioMessageActive = true;
  lcdMsgExpireAt = 0;
  remediosLCDTotal = 0;
  remedioLCDIndex = 0;
  ultimoCicloRemedio = millis();

  remedioHora = jsonGetStringValue(msg, "hora");

  int pos = 0;
  while (remediosLCDTotal < MAX_REMEDIOS_LCD) {
    String nome, dose;
    bool ok = jsonGetNextItem(msg, pos, nome, dose);
    if (!ok) break;

    remediosLCD[remediosLCDTotal].nome = nome;
    remediosLCD[remediosLCDTotal].dose = dose;
    remediosLCDTotal++;
  }

  if (remedioHora == "") {
    remedioHora = "--:--";
  }

  if (remediosLCDTotal == 0) {
    lcdShow("Sem remedios", "");
    remedioMessageActive = false;
    return;
  }

  String line1 = remedioHora;
  line1 += " ";
  line1 += String(remedioLCDIndex + 1);
  line1 += "/";
  line1 += String(remediosLCDTotal);

  String line2 = remediosLCD[remedioLCDIndex].nome;

  if (line1.length() > 16) line1 = line1.substring(0, 16);
  if (line2.length() > 16) line2 = line2.substring(0, 16);

  lcdShow(line1.c_str(), line2.c_str());
}

void updateRemedioLCD() {
  if (!remedioMessageActive) return;
  if (remediosLCDTotal <= 1) return;

  if (millis() - ultimoCicloRemedio >= intervaloCicloRemedio) {
    ultimoCicloRemedio = millis();
    remedioLCDIndex++;
    if (remedioLCDIndex >= remediosLCDTotal) {
      remedioLCDIndex = 0;
    }

    String line1 = remedioHora + " " + String(remedioLCDIndex + 1) + "/" + String(remediosLCDTotal);
    String line2 = remediosLCD[remedioLCDIndex].nome;

    if (line1.length() > 16) line1 = line1.substring(0, 16);
    if (line2.length() > 16) line2 = line2.substring(0, 16);

    lcdShow(line1.c_str(), line2.c_str());
  }
}

void lcdAppendDot() {
  lcdWake();
  static uint8_t pos = 0;
  lcd.setCursor(pos, 1);
  lcd.print(".");
  pos = (pos + 1) % 16;
}

void showButtonMessageAndClear(const char* buttonName, const char* line2) {
  remedioMessageActive = false;
  lcdMsgExpireAt = 0;

  lcdWake();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(buttonName);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  Serial.print("[BOTAO] ");
  Serial.print(buttonName);
  Serial.print(" -> ");
  Serial.println(line2);

  delay(2000);
  lcd.clear();
  lastLcdActivity = millis();
}

void handleLcdTimeout() {
  unsigned long now = millis();
  unsigned long timeout = remedioMessageActive ? LCD_REMEDIO_BACKLIGHT_MS : LCD_IDLE_TIMEOUT_MS;

  if (lcdBacklightOn && (now - lastLcdActivity > timeout)) {
    lcd.noBacklight();
    lcdBacklightOn = false;
  }

  if (lcdMsgExpireAt != 0 && now >= lcdMsgExpireAt) {
    lcd.clear();
    lcdMsgExpireAt = 0;
    lastLcdActivity = now;
  }
}

// ============ CONFIG ============
void loadConfig() {
  prefs.begin("appcfg", true);
  cfg_wifi_ssid = prefs.getString("wifi_ssid", DEFAULT_WIFI_SSID);
  cfg_wifi_pass = prefs.getString("wifi_pass", DEFAULT_WIFI_PASS);
  cfg_mqtt_host = prefs.getString("mqtt_host", DEFAULT_MQTT_HOST);
  cfg_mqtt_port = prefs.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
  cfg_mqtt_user = prefs.getString("mqtt_user", DEFAULT_MQTT_USER);
  cfg_mqtt_pass = prefs.getString("mqtt_pass", DEFAULT_MQTT_PASS);
  prefs.end();

  Serial.println("[CFG] Config carregada:");
  Serial.println("  wifi_ssid: " + cfg_wifi_ssid);
  Serial.println("  mqtt_host: " + cfg_mqtt_host);
  Serial.println("  mqtt_port: " + String(cfg_mqtt_port));
  Serial.println("  mqtt_user: " + cfg_mqtt_user);
}

void saveConfig(
  const String& wifi_ssid,
  const String& wifi_pass,
  const String& mqtt_host,
  uint16_t mqtt_port,
  const String& mqtt_user,
  const String& mqtt_pass
) {
  prefs.begin("appcfg", false);
  prefs.putString("wifi_ssid", wifi_ssid);
  prefs.putString("wifi_pass", wifi_pass);
  prefs.putString("mqtt_host", mqtt_host);
  prefs.putUShort("mqtt_port", mqtt_port);
  prefs.putString("mqtt_user", mqtt_user);
  prefs.putString("mqtt_pass", mqtt_pass);
  prefs.end();
}

void clearConfig() {
  prefs.begin("appcfg", false);
  prefs.clear();
  prefs.end();
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

// ============ PORTAL DE CONFIGURACAO ============
void handleConfigRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Config ESP32</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; max-width: 480px; margin: 20px auto; padding: 10px; }
    input { width: 100%; padding: 10px; margin: 6px 0 14px 0; box-sizing: border-box; }
    button { width: 100%; padding: 12px; font-size: 16px; }
    h2 { margin-bottom: 4px; }
    .box { border: 1px solid #ddd; border-radius: 10px; padding: 16px; }
  </style>
</head>
<body>
  <div class="box">
    <h2>Configuracao ESP32</h2>
    <p>Preencha os dados e clique em salvar.</p>
    <form action="/save" method="post">
      <label>Wi-Fi SSID</label>
      <input name="wifi_ssid" value=")rawliteral" + htmlEscape(cfg_wifi_ssid) + R"rawliteral(">

      <label>Wi-Fi Senha</label>
      <input name="wifi_pass" type="password" value=")rawliteral" + htmlEscape(cfg_wifi_pass) + R"rawliteral(">

      <label>MQTT Host / IP</label>
      <input name="mqtt_host" value=")rawliteral" + htmlEscape(cfg_mqtt_host) + R"rawliteral(">

      <label>MQTT Porta</label>
      <input name="mqtt_port" value=")rawliteral" + String(cfg_mqtt_port) + R"rawliteral(">

      <label>MQTT Usuario</label>
      <input name="mqtt_user" value=")rawliteral" + htmlEscape(cfg_mqtt_user) + R"rawliteral(">

      <label>MQTT Senha</label>
      <input name="mqtt_pass" type="password" value=")rawliteral" + htmlEscape(cfg_mqtt_pass) + R"rawliteral(">

      <button type="submit">Salvar e reiniciar</button>
    </form>
    <br>
    <form action="/reset" method="post">
      <button type="submit">Apagar configuracao salva</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

  configServer.send(200, "text/html", html);
}

void handleConfigSave() {
  String wifi_ssid = configServer.arg("wifi_ssid");
  String wifi_pass = configServer.arg("wifi_pass");
  String mqtt_host = configServer.arg("mqtt_host");
  String mqtt_port_str = configServer.arg("mqtt_port");
  String mqtt_user = configServer.arg("mqtt_user");
  String mqtt_pass = configServer.arg("mqtt_pass");

  uint16_t mqtt_port = mqtt_port_str.toInt();
  if (mqtt_port == 0) mqtt_port = DEFAULT_MQTT_PORT;

  saveConfig(wifi_ssid, wifi_pass, mqtt_host, mqtt_port, mqtt_user, mqtt_pass);

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="font-family:Arial;max-width:480px;margin:20px auto;">
<h3>Configuracao salva.</h3>
<p>O ESP32 vai reiniciar em alguns segundos.</p>
</body>
</html>
)rawliteral";

  configServer.send(200, "text/html", html);
  delay(1500);
  ESP.restart();
}

void handleConfigReset() {
  clearConfig();

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="font-family:Arial;max-width:480px;margin:20px auto;">
<h3>Configuracao apagada.</h3>
<p>O ESP32 vai reiniciar em alguns segundos.</p>
</body>
</html>
)rawliteral";

  configServer.send(200, "text/html", html);
  delay(1500);
  ESP.restart();
}

void startConfigPortal() {
  configPortalActive = true;

  WiFi.disconnect(true, true);
  delay(300);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.println("[CFG] Portal iniciado");
  Serial.print("[CFG] AP IP: ");
  Serial.println(ip);

  lcdShow("Modo Config", ip.toString().c_str());

  configServer.on("/", HTTP_GET, handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.on("/reset", HTTP_POST, handleConfigReset);
  configServer.begin();
}

void handleConfigPortal() {
  if (configPortalActive) {
    configServer.handleClient();
  }
}

// ============ RELÓGIO / NTP ============
void initTimeFromNtp() {
  Serial.println("[TIME] Configurando NTP...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10000)) {
    timeInitialized = true;
    Serial.println("[TIME] Hora sincronizada via NTP.");
  } else {
    timeInitialized = false;
    Serial.println("[TIME] Falha ao obter hora via NTP.");
  }
}

void updateClockDisplayIfIdle() {
  if (!timeInitialized) return;
  if (remedioMessageActive || lcdMsgExpireAt != 0 || pressActive || configPortalActive) return;

  unsigned long nowMs = millis();
  if (nowMs - lastClockUpdateMs < 1000) return;
  lastClockUpdateMs = nowMs;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);

  if (strcmp(buf, lastClockStr) == 0) return;
  strcpy(lastClockStr, buf);

  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  char linha[32];
  sprintf(linha, "....%s....", buf);
  lcd.print(linha);
}

// ============ Wi-Fi ============
bool connectWiFi() {
  remedioMessageActive = false;
  lcdMsgExpireAt = 0;

  if (cfg_wifi_ssid.isEmpty()) {
    Serial.println("[WiFi] Sem SSID salvo.");
    return false;
  }

  lcdShow("WiFi:", "Conectando...");
  Serial.println("[WiFi] Iniciando conexao...");
  Serial.println("[WiFi] SSID: " + cfg_wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg_wifi_ssid.c_str(), cfg_wifi_pass.c_str());

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         (millis() - startAttemptTime) < WIFI_TIMEOUT_MS) {
    delay(500);
    lcdAppendDot();
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    lcdShow("WiFi conectado", ip.c_str());
    Serial.print("[WiFi] Conectado! IP: ");
    Serial.println(ip);

    initTimeFromNtp();

    delay(2000);
    lcd.clear();
    lastLcdActivity = millis();
    return true;
  } else {
    lcdShow("Falha no WiFi", "Abrindo config");
    Serial.println("[WiFi] Falha ao conectar.");
    delay(1500);
    lcd.clear();
    lastLcdActivity = millis();
    return false;
  }
}

void checkWiFiConnection() {
  static unsigned long lastCheck = 0;
  const unsigned long CHECK_INTERVAL_MS = 5000;

  if (configPortalActive) return;

  if (millis() - lastCheck >= CHECK_INTERVAL_MS) {
    lastCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Desconectado. Tentando reconectar...");
      remedioMessageActive = false;
      lcdMsgExpireAt = 0;
      lcdShow("WiFi caiu", "Reconectando...");

      if (!connectWiFi()) {
        startConfigPortal();
      }
    }
  }
}

// ============ MQTT ============
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Msg em ");
  Serial.print(topic);
  Serial.print(": ");

  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);

  if (strcmp(topic, MQTT_LCD_TOPIC) == 0) {
    lcdShowTransient(msg);
  } else if (strcmp(topic, MQTT_REMEDIOS_TOPIC) == 0) {
    lcdShowRemedio(msg);
  }
}

bool connectMQTT() {
  mqttClient.setServer(cfg_mqtt_host.c_str(), cfg_mqtt_port);
  mqttClient.setCallback(mqttCallback);

  remedioMessageActive = false;
  lcdMsgExpireAt = 0;

  lcdShow("MQTT:", "Conectando...");
  Serial.println("[MQTT] Iniciando conexao...");
  Serial.println("[MQTT] Host: " + cfg_mqtt_host + ":" + String(cfg_mqtt_port));

  unsigned long startAttemptTime = millis();
  const unsigned long MQTT_TIMEOUT_MS = 10000;

  while (!mqttClient.connected() &&
         (millis() - startAttemptTime) < MQTT_TIMEOUT_MS) {

    String clientId = String(MQTT_CLIENT_BASE) + "_" + String(random(0xFFFF), HEX);
    Serial.print("[MQTT] Tentando como: ");
    Serial.println(clientId);

    bool ok;
    if (cfg_mqtt_user.length() > 0) {
      ok = mqttClient.connect(
        clientId.c_str(),
        cfg_mqtt_user.c_str(),
        cfg_mqtt_pass.c_str(),
        MQTT_STATUS_TOPIC,
        1,
        true,
        "offline"
      );
    } else {
      ok = mqttClient.connect(
        clientId.c_str(),
        MQTT_STATUS_TOPIC,
        1,
        true,
        "offline"
      );
    }

    if (ok) {
      Serial.println("[MQTT] Conectado!");
      lcdShow("MQTT conectado", "");
      mqttClient.publish(MQTT_STATUS_TOPIC, "online", true);
      mqttClient.publish(MQTT_DEBUG_TOPIC, "ESP32 iniciou", false);

      mqttClient.subscribe(MQTT_LCD_TOPIC);
      mqttClient.subscribe(MQTT_REMEDIOS_TOPIC);

      Serial.print("[MQTT] Assinado: "); Serial.println(MQTT_LCD_TOPIC);
      Serial.print("[MQTT] Assinado: "); Serial.println(MQTT_REMEDIOS_TOPIC);

      delay(1000);
      lcd.clear();
      lastLcdActivity = millis();
      return true;
    } else {
      Serial.print("[MQTT] Falha, rc=");
      Serial.println(mqttClient.state());
      lcdAppendDot();
      delay(500);
    }
  }

  if (!mqttClient.connected()) {
    lcdShow("Falha MQTT", "Verif. broker");
    Serial.println("[MQTT] Nao foi possivel conectar.");
    delay(1500);
    lcd.clear();
    lastLcdActivity = millis();
    return false;
  }

  return true;
}

void checkMQTTConnection() {
  static unsigned long lastCheck = 0;
  const unsigned long CHECK_INTERVAL_MS = 5000;

  if (configPortalActive) return;

  if (millis() - lastCheck >= CHECK_INTERVAL_MS) {
    lastCheck = millis();

    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
      Serial.println("[MQTT] Desconectado. Tentando reconectar...");
      remedioMessageActive = false;
      lcdMsgExpireAt = 0;
      lcdShow("MQTT caiu", "Reconectando...");
      connectMQTT();
    }
  }
}

// ============ PUBLICAÇÃO MQTT DOS BOTÕES ============
void publishButtonEvent(int index, const char* event) {
  if (!mqttClient.connected()) {
    Serial.print("[MQTT][BOTAO] Nao conectado, nao foi possivel publicar: ");
    Serial.print(BUTTON_TOPICS[index]);
    Serial.print(" -> ");
    Serial.println(event);
    return;
  }

  bool ok = mqttClient.publish(BUTTON_TOPICS[index], event, false);
  Serial.print("[MQTT][BOTAO] Pub ");
  Serial.print(BUTTON_TOPICS[index]);
  Serial.print(" = ");
  Serial.print(event);
  Serial.print(" (ok=");
  Serial.print(ok ? "true" : "false");
  Serial.println(")");
}

// ============ BOTÕES ============
void handleButtons() {
  unsigned long now = millis();

  if (!pressActive) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
      bool currentState = digitalRead(BUTTON_PINS[i]);

      if (currentState == LOW && lastButtonState[i] == HIGH) {
        pressActive = true;
        longMsgShown = false;
        activeButtonIndex = i;
        pressStartTime = now;

        remedioMessageActive = false;
        lcdMsgExpireAt = 0;

        break;
      }

      lastButtonState[i] = currentState;
    }
  } else {
    int i = activeButtonIndex;
    if (i < 0 || i >= NUM_BUTTONS) return;

    bool currentState = digitalRead(BUTTON_PINS[i]);
    unsigned long pressDuration = now - pressStartTime;

    if (!longMsgShown && pressDuration >= 3000) {
      publishButtonEvent(i, "5sec");

      if (i == 6) {
        remedioMessageActive = false;
        lcdMsgExpireAt = 0;
        lcd.clear();
        lastLcdActivity = millis();
        Serial.println("[BOTAO 7] Remedio apagado da tela.");
      } else {
        showButtonMessageAndClear(BUTTON_NAMES[i], "por 3 sec");
      }

      longMsgShown = true;
      pressActive = false;
      activeButtonIndex = -1;
    }

    if (currentState == HIGH && lastButtonState[i] == LOW && !longMsgShown) {
      publishButtonEvent(i, "on");
      showButtonMessageAndClear(BUTTON_NAMES[i], "apertado");
      pressActive = false;
      activeButtonIndex = -1;
    }

    lastButtonState[i] = currentState;
  }
}

// ============ SETUP & LOOP ============
void setup() {
  Serial.begin(115200);
  delay(100);

  randomSeed(micros());
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcdBacklightOn = true;
  lcdShow("Iniciando...", "ESP32 + MQTT");
  Serial.println("Inicializando sistema...");
  lastLcdActivity = millis();

  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    lastButtonState[i] = HIGH;
  }

  delay(1000);

  loadConfig();

  bool wifiOk = connectWiFi();
  if (!wifiOk) {
    startConfigPortal();
    return;
  }

  connectMQTT();
}

void loop() {
  if (configPortalActive) {
    handleConfigPortal();
    delay(5);
    return;
  }

  checkWiFiConnection();
  checkMQTTConnection();

  if (mqttClient.connected()) {
    mqttClient.loop();
    updateRemedioLCD();
  }

  handleButtons();
  updateClockDisplayIfIdle();
  handleLcdTimeout();

  delay(5);
}
