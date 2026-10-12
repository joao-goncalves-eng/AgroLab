/*
  ESP-A — NÓ DE SENSORES + ACIONAMENTO DA BOMBA
  ----------------------------------------------------
  Lê: DS18B20, SHT31, BH1750, sensor de fluxo (pulso),
      boia (nível discreto), TDS Meter v1.0, entrada
      reservada para pH (futuro).
  Aciona: bomba d'água via módulo relé de 3,3V.
  Envia tudo como JSON, uma linha por leitura, via
  Serial2 (UART2) para a ESP-B.
  Recebe comandos de bomba da ESP-B pela mesma Serial2.

  Bibliotecas necessárias (Gerenciador de Bibliotecas):
    - OneWire
    - DallasTemperature
    - Adafruit SHT31 Library
    - BH1750 (by Christopher Laws)
    - ArduinoJson (v6.x)

  ---- PINAGEM ----
    DS18B20 (OneWire) ......... GPIO14 (terminal J3)
    SHT31 + BH1750 (I2C) ...... SDA=GPIO21 / SCL=GPIO22
    Sensor de fluxo (pulso) ... GPIO19 (J4, com interrupção)
    Boia / nível discreto ..... GPIO18 (J4, pull-up)
    TDS Meter v1.0 (ADC1) ..... GPIO34  (pino somente de entrada)
    pH (futuro, ADC1) ......... GPIO33 (ainda sem sensor)
    RELÉ DA BOMBA (3,3V) ...... GPIO25
    Link dedicado p/ ESP-B .... RX2=GPIO26 / TX2=GPIO27
    (USB / Serial fica livre para debug e regravação)

  ---- PROTOCOLO ----
    ESP-A -> ESP-B : {"tempAgua":..., "bomba":true, "bombaBloqueada":false, ...}
    ESP-B -> ESP-A : CMD:{"pump":true}   /   CMD:{"pump":false}

  ATENÇÃO ELÉTRICA:
    - DS18B20 precisa de resistor de pull-up 4.7kΩ entre
      DATA e 3V3, senão a leitura falha (-127.00).
    - Sensor de fluxo tipo YF-S201/ZJ-S201 costuma ser
      alimentado em 5V. Se a saída de pulso também for 5V,
      use divisor resistivo (10k/20k) antes do GPIO19 — o
      ESP32 não é tolerante a 5V.
    - TDS e pH ficam em GPIO34/33 (ADC1), porque o ADC2
      (GPIO25/26/27) não funciona com Wi-Fi ativo.
    - GPIO34 é somente entrada: serve para leitura analógica,
      mas não aceita pinMode OUTPUT nem pull-up interno.

  ---- SEGURANÇA DA BOMBA (leia antes de ligar na tomada) ----
  1) PROTEÇÃO CONTRA FUNCIONAMENTO A SECO
     Se a boia indicar nível baixo, a bomba é desligada e
     NENHUM comando consegue ligá-la. Bomba girando sem água
     queima em minutos. Esta trava é intencionalmente
     impossível de burlar pelo dashboard.

  2) TEMPO MÁXIMO DE ACIONAMENTO (MAX_RUN_MS)
     A bomba desliga sozinha após o tempo limite, mesmo sem
     receber comando de desligar. Isso cobre o caso do link
     serial cair ou da ESP-B travar com a bomba ligada.

  3) ELÉTRICA DO RELÉ (módulo de 3,3V)
     - VCC no 3V3 (J8), GND comum, IN no GPIO25.
     - A bobina puxa 70-90 mA. Se a placa resetar no momento
       em que a bomba liga, é queda de tensão: ponha um
       capacitor de 470-1000uF no 3V3 ou alimente o relé por
       fonte 3,3V separada, mantendo o GND comum.
     - Bomba é carga indutiva. Módulos com optoacoplador já
       trazem diodo de roda-livre; se o seu não tiver, add um.
*/

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BH1750.h>
#include <ArduinoJson.h>

// ---------- Pinagem dos sensores ----------
#define ONE_WIRE_BUS   14
#define I2C_SDA        21
#define I2C_SCL        22
#define FLOW_PIN       19
#define FLOAT_PIN      18
#define TDS_PIN        34
#define PH_PIN         33

// ---------- Relé da bomba ----------
#define PUMP_PIN            25
#define RELAY_ACTIVE_LOW    false     // confirmado no hardware: este módulo aciona em nível ALTO
#define MAX_RUN_MS          300000UL  // 5 min — tempo máximo de bomba ligada sem novo comando

// ---------- Link serial dedicado com a ESP-B ----------
#define RX2_PIN   26
#define TX2_PIN   27
#define LINK_BAUD 115200

// ---------- Objetos dos sensores ----------
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
BH1750 lightMeter;

volatile uint32_t pulseCount = 0;
void IRAM_ATTR onFlowPulse() { pulseCount++; }

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 2000; // ms entre envios

// Ajuste conforme calibração real do seu sensor de fluxo
const float FLOW_CAL_FACTOR = 7.5; // pulsos por segundo / fator = L/min (típico YF-S201)

// ---------- Estado da bomba ----------
bool pumpRequested = false;   // o que o usuário pediu pelo dashboard
bool pumpOn        = false;   // o que está de fato acionado
bool pumpBlocked   = false;   // trava de segurança ativa
unsigned long pumpStartedAt = 0;
String linkBuffer = "";

void writeRelay(bool on) {
  digitalWrite(PUMP_PIN, RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

/*
  Ponto único de decisão da bomba.
  Chamado a cada volta do loop, não só quando chega comando —
  assim a boia consegue desligar a bomba sozinha, sem depender
  do dashboard nem do link serial estar vivo.
*/
void updatePump(bool nivelBaixo) {
  // Trava 1: sem água, não liga de jeito nenhum
  if (nivelBaixo) {
    pumpBlocked = true;
    if (pumpOn) {
      pumpOn = false;
      writeRelay(false);
      Serial.println("SEGURANCA: nivel baixo -> bomba desligada");
    }
    return;
  }
  pumpBlocked = false;

  // Trava 2: tempo máximo de acionamento
  if (pumpOn && (millis() - pumpStartedAt > MAX_RUN_MS)) {
    pumpOn = false;
    pumpRequested = false;
    writeRelay(false);
    Serial.println("SEGURANCA: tempo maximo atingido -> bomba desligada");
    return;
  }

  // Aplica o pedido do usuário
  if (pumpRequested && !pumpOn) {
    pumpOn = true;
    pumpStartedAt = millis();
    writeRelay(true);
    Serial.println("BOMBA: ligada");
  } else if (!pumpRequested && pumpOn) {
    pumpOn = false;
    writeRelay(false);
    Serial.println("BOMBA: desligada");
  }
}

void readCommandsFromESP_B() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n' || c == '\r') {
      if (linkBuffer.length() > 0) {
        if (linkBuffer.startsWith("CMD:")) {
          StaticJsonDocument<128> doc;
          if (!deserializeJson(doc, linkBuffer.substring(4))) {
            if (doc.containsKey("pump")) {
              pumpRequested = doc["pump"].as<bool>();
              Serial.println(pumpRequested ? "CMD: pedido LIGAR bomba" : "CMD: pedido DESLIGAR bomba");
            }
          }
        }
        linkBuffer = "";
      }
    } else {
      linkBuffer += c;
      if (linkBuffer.length() > 200) linkBuffer = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(LINK_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // Relé PRIMEIRO, e desligado — evita que a bomba dê um pulso
  // durante o boot, enquanto os GPIOs ainda estão indefinidos.
  pinMode(PUMP_PIN, OUTPUT);
  writeRelay(false);

  ds18b20.begin();

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!sht31.begin(0x44)) {
    Serial.println("[AVISO] SHT31 nao respondeu no endereco 0x44.");
  }
  if (!lightMeter.begin()) {
    Serial.println("[AVISO] BH1750 nao respondeu.");
  }

  pinMode(FLOAT_PIN, INPUT_PULLUP);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onFlowPulse, FALLING);

  analogReadResolution(12); // 0-4095

  Serial.println("ESP-A pronta (sensores + bomba). Link em GPIO26/27, rele em GPIO25.");
}

float readTDS(int pin) {
  const int N = 30;
  uint32_t soma = 0;
  for (int i = 0; i < N; i++) {
    soma += analogRead(pin);
    delay(2);
  }
  float media = soma / (float)N;
  float voltage = media * (3.3 / 4095.0);
  // Fórmula genérica de datasheet — CALIBRE com solução TDS conhecida (ex: 342 ppm) antes de confiar no valor.
  float tds = (133.42f * voltage * voltage * voltage
             - 255.86f * voltage * voltage
             + 857.39f * voltage) * 0.5f;
  return tds; // ppm
}

void loop() {
  readCommandsFromESP_B();

  // A boia é lida a cada volta, não só na hora de enviar —
  // a proteção precisa reagir na hora, não daqui a 2 segundos.
  // Ajuste a lógica (LOW/HIGH) conforme o tipo de boia (NA ou NF).
  bool nivelBaixo = (digitalRead(FLOAT_PIN) == LOW);
  updatePump(nivelBaixo);

  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();

    ds18b20.requestTemperatures();
    float tempAgua = ds18b20.getTempCByIndex(0);

    float tempAr  = sht31.readTemperature();
    float umidade = sht31.readHumidity();
    float lux     = lightMeter.readLightLevel();

    noInterrupts();
    uint32_t pulsos = pulseCount;
    pulseCount = 0;
    interrupts();
    float vazao = (pulsos / (SEND_INTERVAL / 1000.0f)) / FLOW_CAL_FACTOR; // L/min

    float tds    = readTDS(TDS_PIN);
    int   phRaw  = analogRead(PH_PIN); // sem sensor ainda — placeholder

    StaticJsonDocument<320> doc;
    doc["tempAgua"]       = isnan(tempAgua) ? -127.0 : tempAgua;
    doc["tempAr"]         = isnan(tempAr)   ? -1     : tempAr;
    doc["umidade"]        = isnan(umidade)  ? -1     : umidade;
    doc["lux"]            = lux;
    doc["vazao"]          = vazao;
    doc["nivelBaixo"]     = nivelBaixo;
    doc["tds"]            = tds;
    doc["phRaw"]          = phRaw;
    doc["bomba"]          = pumpOn;
    doc["bombaBloqueada"] = pumpBlocked;
    doc["bombaSegundos"]  = pumpOn ? (int)((millis() - pumpStartedAt) / 1000) : 0;

    serializeJson(doc, Serial2);
    Serial2.println(); // delimitador de linha — a ESP-B lê até o '\n'

    // Espelha no USB só para você debugar em bancada
    serializeJson(doc, Serial);
    Serial.println();
  }
}
