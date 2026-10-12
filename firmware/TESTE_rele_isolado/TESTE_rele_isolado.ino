/*
  TESTE ISOLADO DO RELÉ — grave na ESP-A
  ----------------------------------------------------
  Este sketch NÃO depende de nada: sem sensores, sem ESP-B,
  sem link serial, sem dashboard. Ele só liga e desliga o
  GPIO25 a cada 2 segundos.

  O objetivo é responder uma única pergunta:
  o conjunto ESP32 + relé funciona, sim ou não?

  COMO INTERPRETAR O RESULTADO:

  A) Você ouve o "clique" do relé a cada 2 s
     -> O hardware está bom. O problema está na cadeia de
        comando (ESP-B não atualizada, link GPIO26/27, ou a
        boia bloqueando). Volte para o firmware completo e
        investigue por lá.

  B) O LED do módulo de relé acende/apaga, mas NÃO clica
     -> O sinal chega, mas a bobina não energiza. Quase
        sempre é alimentação: o módulo precisa de mais
        corrente do que o 3V3 do ESP32 entrega, ou o jumper
        VCC/JD-VCC está na posição errada. Alimente o relé
        por fonte separada, com GND comum.

  C) Nada acontece, nem LED nem clique
     -> Ou o fio IN não está no GPIO25, ou o módulo está sem
        VCC/GND, ou o módulo aciona em nível ALTO. Troque
        ACTIVE_LOW abaixo para false e grave de novo.

  D) O relé fica permanentemente ligado, sem alternar
     -> Polaridade invertida: troque ACTIVE_LOW.

  Observe também o Serial Monitor a 115200: ele imprime o
  estado a cada troca. Se as mensagens aparecem mas o relé
  não reage, o código está rodando e o problema é elétrico.
*/

#define PUMP_PIN     25
#define ACTIVE_LOW   false  // confirmado no hardware: este módulo aciona em nível ALTO

bool estado = false;

void writeRelay(bool on) {
  digitalWrite(PUMP_PIN, ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PUMP_PIN, OUTPUT);
  writeRelay(false);

  Serial.println();
  Serial.println("=== TESTE ISOLADO DO RELE ===");
  Serial.print("Pino: GPIO");
  Serial.println(PUMP_PIN);
  Serial.print("Acionamento em nivel: ");
  Serial.println(ACTIVE_LOW ? "BAIXO (LOW)" : "ALTO (HIGH)");
  Serial.println("Alternando a cada 2 segundos. Ouca o clique.");
  Serial.println();
}

void loop() {
  estado = !estado;
  writeRelay(estado);

  Serial.print(millis() / 1000);
  Serial.print("s -> rele ");
  Serial.print(estado ? "LIGADO " : "DESLIGADO ");
  Serial.print("(GPIO25 em ");
  Serial.print(digitalRead(PUMP_PIN) ? "HIGH" : "LOW");
  Serial.println(")");

  delay(2000);
}
