/*
 * ============================================================
 *  AQUÁRIO INTELIGENTE - ESP32
 *  Componentes:
 *    - Bomba submersa Fish Prime FP-220
 *    - Sensor ultrassônico HC-SR04 (nível de água)
 *    - Display LCD 16x2 com módulo I2C
 *    - Servo Motor SG90 (alimentador)
 *    - Sensor de temperatura DS18B20
 *    - ESP32 30 pinos
 *
 *  Protocolo: MQTT (HiveMQ Cloud)
 *  IDE: Arduino IDE 2.x
 * ============================================================
 *
 *  BIBLIOTECAS NECESSÁRIAS (instalar pelo Library Manager):
 *    - PubSubClient         (MQTT)
 *    - LiquidCrystal_I2C   (Display LCD I2C)
 *    - OneWire              (DS18B20)
 *    - DallasTemperature    (DS18B20)
 *    - ESP32Servo           (Servo no ESP32)
 *    - ArduinoJson          (parse de JSON via MQTT)
 *    - WiFiClientSecure     (MQTT com TLS)
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
// CONFIGURAÇÕES - EDITE AQUI
// ============================================================

// Wi-Fi
const char* WIFI_SSID     = "SEU_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA";

// MQTT Broker (HiveMQ Cloud - cadastro gratuito em hivemq.com)
const char* MQTT_HOST     = "SEU_BROKER.s2.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;          // TLS
const char* MQTT_USER     = "SEU_USUARIO";
const char* MQTT_PASSWORD = "SUA_SENHA_MQTT";
const char* DEVICE_ID     = "aquario_01";  // ID único do dispositivo

// Tópicos MQTT
#define TOPIC_STATUS      "aquario/status"       // ESP32 → App/Painel
#define TOPIC_ALIMENTAR   "aquario/alimentar"    // App → ESP32
#define TOPIC_HORARIO     "aquario/horario"      // App → ESP32 (agenda)
#define TOPIC_BOMBA       "aquario/bomba"        // App → ESP32
#define TOPIC_CONFIG      "aquario/config"       // App → ESP32 (configs)
#define TOPIC_CMD         "aquario/cmd"          // App → ESP32 (comandos)

// Limites e alertas
#define NIVEL_CRITICO_CM  15   // cm do sensor até a água = nível BAIXO
#define NIVEL_OK_CM       5    // cm do sensor até a água = nível OK
#define TEMP_MAX          30.0 // °C - alerta de temperatura alta
#define TEMP_MIN          22.0 // °C - alerta de temperatura baixa

// ============================================================
// PINAGEM - ESP32 30 pinos
// ============================================================

// HC-SR04 (Ultrassônico)
#define PIN_TRIG    5
#define PIN_ECHO    18

// DS18B20 (Temperatura)
#define PIN_TEMP    19

// Servo Motor SG90 (Alimentador)
#define PIN_SERVO   21

// Relé Bomba (conecte o relé ao pino abaixo)
#define PIN_BOMBA   22

// LCD I2C (SDA e SCL padrão do ESP32)
// SDA → GPIO 21  (mas cuidado: compartilha com servo!)
// Use pinos alternativos de I2C:
#define I2C_SDA     23
#define I2C_SCL     25

// ============================================================
// OBJETOS
// ============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);  // Endereço I2C padrão 0x27

OneWire oneWire(PIN_TEMP);
DallasTemperature sensors(&oneWire);

Servo servoAlimentador;

WiFiClientSecure wifiSecure;
PubSubClient mqtt(wifiSecure);

// ============================================================
// VARIÁVEIS DE ESTADO
// ============================================================

float temperatura      = 0.0;
float nivelAgua_cm     = 0.0;  // distância sensor → superfície
bool  bombaLigada      = false;
bool  alertaNivel      = false;
bool  alertaTemp       = false;
int   anguloServoAberto = 90;   // ângulo para liberar alimento
int   anguloServoFechado = 0;   // ângulo fechado
int   tempoServoAberto  = 1000; // ms que o servo fica aberto

// Horários de alimentação (até 4 horários)
struct Horario {
  int hora;
  int minuto;
  bool ativo;
};
Horario horarios[4] = {
  {8,  0, false},
  {12, 0, false},
  {18, 0, false},
  {21, 0, false}
};

// Controle de tempo (sem delay bloqueante)
unsigned long ultimaLeitura   = 0;
unsigned long ultimoPublish   = 0;
unsigned long ultimoLCD       = 0;
unsigned long ultimoHorario   = 0;
unsigned long ultimoReconect  = 0;

const unsigned long INTERVALO_LEITURA  = 3000;   // 3s entre leituras
const unsigned long INTERVALO_PUBLISH  = 5000;   // 5s entre envios MQTT
const unsigned long INTERVALO_LCD      = 2000;   // 2s para alternar tela LCD
const unsigned long INTERVALO_HORARIO  = 30000;  // 30s checar horários

int telaLCD = 0;  // 0 = temp/nivel, 1 = bomba/alim, 2 = alertas

// ============================================================
// CERTIFICADO CA RAIZ (HiveMQ Cloud usa Let's Encrypt)
// Baixe em: https://letsencrypt.org/certs/isrgrootx1.pem
// Cole aqui apenas o conteúdo entre -----BEGIN e -----END
// ============================================================
const char* CA_CERT = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVUEGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoBggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwF
XmAJspcpj5/2ISFykF5hSF6Cd2mCBxHn8Dz5/xT7/lME3NlPK5uTg0KHEEHGEuJ3
ZXqfF2vVJHaP7Ah7sMbNWlk5pEJXjfHFGkXJHxM4p24gR3P5eJzOo/nNP5RM/1fL
U2UuHlGMqbBTiDU68r9I8TbFz6m3MCKj7bCJTQ5U0XJq+5m4/N21H8A/mPXwzF5Y
/ZOGAExMhOx3E2n+Sn3z16LVNV31/XBqBaXnnRuBfP4JMhfLiC5d9eCi
-----END CERTIFICATE-----
)EOF";

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== AQUÁRIO INTELIGENTE ===");

  // I2C em pinos alternativos
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Aquario Smart   ");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...    ");

  // Pinos
  pinMode(PIN_TRIG,  OUTPUT);
  pinMode(PIN_ECHO,  INPUT);
  pinMode(PIN_BOMBA, OUTPUT);
  digitalWrite(PIN_BOMBA, LOW);  // Bomba desligada no início

  // Servo
  servoAlimentador.attach(PIN_SERVO);
  servoAlimentador.write(anguloServoFechado);
  delay(500);

  // Sensor de temperatura
  sensors.begin();

  // Wi-Fi
  conectarWifi();

  // NTP para horário real
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Aguardando NTP");
  struct tm t;
  while (!getLocalTime(&t)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" OK");

  // MQTT
  wifiSecure.setCACert(CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
  conectarMQTT();

  lcd.setCursor(0, 0);
  lcd.print("Sistema OK!     ");
  lcd.setCursor(0, 1);
  lcd.print("                ");
  delay(1500);
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  // Manter conexão MQTT
  if (!mqtt.connected()) {
    unsigned long agora = millis();
    if (agora - ultimoReconect > 5000) {
      ultimoReconect = agora;
      conectarMQTT();
    }
  }
  mqtt.loop();

  unsigned long agora = millis();

  // Leituras dos sensores
  if (agora - ultimaLeitura > INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    lerTemperatura();
    lerNivelAgua();
    verificarAlertas();
  }

  // Publicar status via MQTT
  if (agora - ultimoPublish > INTERVALO_PUBLISH) {
    ultimoPublish = agora;
    publicarStatus();
  }

  // Atualizar LCD
  if (agora - ultimoLCD > INTERVALO_LCD) {
    ultimoLCD = agora;
    atualizarLCD();
  }

  // Verificar horários de alimentação
  if (agora - ultimoHorario > INTERVALO_HORARIO) {
    ultimoHorario = agora;
    verificarHorariosAlimentacao();
  }
}

// ============================================================
// SENSORES
// ============================================================

void lerTemperatura() {
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if (t != DEVICE_DISCONNECTED_C) {
    temperatura = t;
  }
  Serial.printf("[TEMP] %.1f °C\n", temperatura);
}

void lerNivelAgua() {
  // Pulso trigger
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duracao = pulseIn(PIN_ECHO, HIGH, 30000); // timeout 30ms
  if (duracao > 0) {
    nivelAgua_cm = (duracao * 0.0343) / 2.0;
  }
  Serial.printf("[NIVEL] %.1f cm do sensor\n", nivelAgua_cm);
}

void verificarAlertas() {
  // Nível baixo: sensor longe da água = nível baixo
  alertaNivel = (nivelAgua_cm >= NIVEL_CRITICO_CM);
  alertaTemp  = (temperatura > TEMP_MAX || temperatura < TEMP_MIN);

  if (alertaNivel) {
    Serial.println("[ALERTA] Nível de água BAIXO!");
    publicarAlerta("nivel_baixo", "Nível de água crítico!");
  }
  if (alertaTemp) {
    Serial.printf("[ALERTA] Temperatura fora do range: %.1f°C\n", temperatura);
    String msg = temperatura > TEMP_MAX ? "Temperatura ALTA!" : "Temperatura BAIXA!";
    publicarAlerta("temperatura", msg);
  }
}

// ============================================================
// ALIMENTAÇÃO
// ============================================================

void alimentar() {
  Serial.println("[ALIMENTAR] Liberando ração...");
  servoAlimentador.write(anguloServoAberto);
  delay(tempoServoAberto);
  servoAlimentador.write(anguloServoFechado);
  Serial.println("[ALIMENTAR] Concluído.");

  // Notificar via MQTT
  StaticJsonDocument<128> doc;
  doc["evento"]    = "alimentacao";
  doc["timestamp"] = obterTimestamp();
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATUS, buf);
}

void verificarHorariosAlimentacao() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;

  for (int i = 0; i < 4; i++) {
    if (horarios[i].ativo && horarios[i].hora == h && horarios[i].minuto == m) {
      Serial.printf("[HORÁRIO] Alimentação programada %02d:%02d\n", h, m);
      alimentar();
      delay(60000); // evitar alimentar duas vezes no mesmo minuto
    }
  }
}

// ============================================================
// MQTT
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.printf("[MQTT] Recebido em [%s]: %s\n", topic, msg.c_str());

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.println("[MQTT] Erro ao parsear JSON");
    return;
  }

  String topicStr = String(topic);

  // Comando de alimentação manual
  if (topicStr == TOPIC_ALIMENTAR) {
    alimentar();
  }

  // Configurar horários
  else if (topicStr == TOPIC_HORARIO) {
    /*
     * Formato esperado:
     * {
     *   "slot": 0,        (0-3)
     *   "hora": 8,
     *   "minuto": 30,
     *   "ativo": true
     * }
     */
    int slot = doc["slot"] | -1;
    if (slot >= 0 && slot < 4) {
      horarios[slot].hora   = doc["hora"]   | 0;
      horarios[slot].minuto = doc["minuto"] | 0;
      horarios[slot].ativo  = doc["ativo"]  | false;
      Serial.printf("[HORÁRIO] Slot %d: %02d:%02d ativo=%d\n",
        slot, horarios[slot].hora, horarios[slot].minuto, horarios[slot].ativo);
    }
  }

  // Controle da bomba
  else if (topicStr == TOPIC_BOMBA) {
    bool ligar = doc["ligar"] | false;
    bombaLigada = ligar;
    digitalWrite(PIN_BOMBA, ligar ? HIGH : LOW);
    Serial.printf("[BOMBA] %s\n", ligar ? "LIGADA" : "DESLIGADA");
  }

  // Configurações gerais
  else if (topicStr == TOPIC_CONFIG) {
    /*
     * {
     *   "servo_aberto": 90,
     *   "servo_fechado": 0,
     *   "tempo_servo": 1000
     * }
     */
    if (doc.containsKey("servo_aberto"))  anguloServoAberto  = doc["servo_aberto"];
    if (doc.containsKey("servo_fechado")) anguloServoFechado = doc["servo_fechado"];
    if (doc.containsKey("tempo_servo"))   tempoServoAberto   = doc["tempo_servo"];
    Serial.println("[CONFIG] Configurações atualizadas");
  }

  // Comando direto
  else if (topicStr == TOPIC_CMD) {
    String cmd = doc["cmd"] | "";
    if (cmd == "restart") ESP.restart();
    if (cmd == "status")  publicarStatus();
  }
}

void publicarStatus() {
  if (!mqtt.connected()) return;

  StaticJsonDocument<384> doc;
  doc["device"]      = DEVICE_ID;
  doc["temperatura"] = temperatura;
  doc["nivel_cm"]    = nivelAgua_cm;
  doc["bomba"]       = bombaLigada;
  doc["alerta_nivel"]= alertaNivel;
  doc["alerta_temp"] = alertaTemp;
  doc["timestamp"]   = obterTimestamp();
  doc["rssi"]        = WiFi.RSSI();

  // Horários configurados
  JsonArray slots = doc.createNestedArray("horarios");
  for (int i = 0; i < 4; i++) {
    JsonObject s = slots.createNestedObject();
    s["slot"]   = i;
    s["hora"]   = horarios[i].hora;
    s["minuto"] = horarios[i].minuto;
    s["ativo"]  = horarios[i].ativo;
  }

  char buf[384];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATUS, buf, true); // retained = true
  Serial.println("[MQTT] Status publicado");
}

void publicarAlerta(const char* tipo, const char* mensagem) {
  if (!mqtt.connected()) return;
  StaticJsonDocument<128> doc;
  doc["alerta"]     = tipo;
  doc["mensagem"]   = mensagem;
  doc["timestamp"]  = obterTimestamp();
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish("aquario/alertas", buf);
}

// ============================================================
// LCD
// ============================================================

void atualizarLCD() {
  lcd.clear();

  switch (telaLCD) {
    case 0:
      // Temperatura e Nível
      lcd.setCursor(0, 0);
      lcd.printf("Temp: %.1f C    ", temperatura);
      lcd.setCursor(0, 1);
      if (alertaNivel) {
        lcd.print("NIVEL BAIXO!    ");
      } else {
        lcd.printf("Nivel: %.1fcm   ", nivelAgua_cm);
      }
      break;

    case 1:
      // Bomba e status Wi-Fi
      lcd.setCursor(0, 0);
      lcd.printf("Bomba: %s       ", bombaLigada ? "ON " : "OFF");
      lcd.setCursor(0, 1);
      lcd.printf("WiFi: %s        ", WiFi.isConnected() ? "OK " : "ERR");
      break;

    case 2:
      // Alertas
      lcd.setCursor(0, 0);
      lcd.print(alertaTemp  ? "!TEMP ANOMALA!  " : "Temp: OK        ");
      lcd.setCursor(0, 1);
      lcd.print(alertaNivel ? "!NIVEL BAIXO!   " : "Nivel: OK       ");
      break;
  }

  telaLCD = (telaLCD + 1) % 3;
}

// ============================================================
// WI-FI E MQTT
// ============================================================

void conectarWifi() {
  Serial.printf("Conectando ao Wi-Fi: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  if (WiFi.isConnected()) {
    Serial.printf("\nWi-Fi OK! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nFalha no Wi-Fi!");
  }
}

void conectarMQTT() {
  if (!WiFi.isConnected()) conectarWifi();

  Serial.print("Conectando ao MQTT...");
  String clientId = String(DEVICE_ID) + "_" + String(random(0xffff), HEX);

  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.println(" OK!");
    // Subscrever nos tópicos de controle
    mqtt.subscribe(TOPIC_ALIMENTAR);
    mqtt.subscribe(TOPIC_HORARIO);
    mqtt.subscribe(TOPIC_BOMBA);
    mqtt.subscribe(TOPIC_CONFIG);
    mqtt.subscribe(TOPIC_CMD);
    // Anunciar que está online
    publicarStatus();
  } else {
    Serial.printf(" Falha! Código: %d\n", mqtt.state());
  }
}

// ============================================================
// UTILITÁRIOS
// ============================================================

String obterTimestamp() {
  struct tm t;
  if (!getLocalTime(&t)) return "N/A";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t);
  return String(buf);
}
