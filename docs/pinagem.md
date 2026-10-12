# Pinagem Final do Projeto — ESP-A (Sensores) + ESP-B (Dashboard + NeoPixel)

Consolidado a partir da conversa anterior "Pinagem ESP32 para múltiplos sensores" + decisão do link serial dedicado.

---

## ESP-A — Nó de Sensores (placa DOIT ESP32)

| Função | GPIO | Terminal | Tipo | Observação |
|---|---|---|---|---|
| DS18B20 (temperatura da água, OneWire) | GPIO14 | J3 | Digital | Precisa de pull-up 4.7kΩ entre DATA e 3V3 |
| SHT31 (temp/umidade do ar, I²C) | SDA=GPIO23 / SCL=GPIO22 | J5/J6 | I²C | `Wire.begin(23, 22)` |
| BH1750 (luminosidade, I²C) | SDA=GPIO23 / SCL=GPIO22 | J5/J6 | I²C | Mesmo barramento do SHT31 (endereços diferentes, sem conflito) |
| Sensor de fluxo (YF-S201/ZJ-S201) | GPIO19 | J4 | Digital + interrupção | Confirmar nível lógico do pulso (5V) — pode precisar de divisor resistivo |
| Boia / nível discreto | GPIO18 | J4 | Digital (pull-up interno) | Ajustar lógica conforme tipo de boia (NA/NF) |
| TDS Meter v1.0 (condutividade) | GPIO32 | J5/J6/J7 | Analógico (ADC1) | ADC1 obrigatório — ADC2 não funciona junto com Wi-Fi |
| pH (a adicionar no futuro) | GPIO33 | *cablear até terminal livre* | Analógico (ADC1) | Placeholder no código, sem sensor físico ainda |
| **Relé da bomba d'água** | GPIO25 | *cablear até terminal livre* | Digital (saída) | Módulo **3,3V** com optoacoplador, acionamento em LOW |
| **Link serial dedicado → ESP-B** | **RX2=GPIO26 / TX2=GPIO27** | J6/J7 | UART2 | Não conflita com nenhum sensor acima |
| Serial (USB) | GPIO1/GPIO3 (RX0/TX0) | — | UART0 | Fica **livre** para debug/gravação — não usar para o link entre as ESPs |

**Pinos ainda livres na ESP-A:** GPIO12, GPIO13, GPIO15
(úteis para relé de bomba, botão, buzzer, LED de status, etc. — evite GPIO12/GPIO15 para qualquer coisa com pull-up permanente, são *strapping pins* que afetam o boot)

**Alimentação:** VIN/EN (J1) para sensores 5V (ex: fluxo, se for o caso); 3V3 (J8) para sensores 3.3V (DS18B20, SHT31, BH1750, TDS). GND comum obrigatório entre todos.

---

## ESP-B — Dashboard Web + Controle da Fita NeoPixel (placa DOIT ESP32)

| Função | GPIO | Tipo | Observação |
|---|---|---|---|
| **Link serial dedicado ← ESP-A** | **RX2=GPIO26 / TX2=GPIO27** | UART2 | Recebe o JSON de sensores em tempo real |
| Dado da fita NeoPixel (DIN) | GPIO4 | Digital (protocolo próprio WS2812) | Resistor ~330-470Ω em série recomendado |
| Wi-Fi (dashboard) | interno (não usa GPIO físico) | — | Caminho 1: servidor web na própria placa |
| **Ponte USB com o PC** | GPIO1/GPIO3 (RX0/TX0) | UART0 | Caminho 2: envia `DATA:{json}` e recebe `CMD:{json}` |

### Redundância de canal (dois caminhos simultâneos)

| | Caminho 1 — Wi-Fi | Caminho 2 — USB |
|---|---|---|
| Interface | Página servida pela própria ESP-B | `dashboard_local_usb.html` aberto no PC |
| Transporte | HTTP na rede local / AP de emergência | Web Serial API pelo cabo |
| Depende de rede? | Sim | Não |
| Visualiza sensores? | Sim | Sim |
| Controla a fita? | Sim | Sim |

Os dois caminhos chamam a **mesma função** `applyLedState()` no firmware, então o estado da fita nunca dessincroniza — mudou por um, o outro reflete na atualização seguinte.

**Protocolo da ponte USB (115200 baud):**
```
ESP-B -> PC :  DATA:{"tempAgua":25.3,...,"ledMode":"pulse","ledR":0,...}
PC -> ESP-B :  CMD:{"mode":"pulse","r":0,"g":255,"b":136}
```
Linhas com prefixo `LOG:`, `ACK:` ou `ERR:` são mensagens de debug — o dashboard do PC as ignora na renderização e mostra apenas no painel de log.

**Pinos livres na ESP-B:** praticamente todos os demais (GPIO5, 12, 13, 14, 15, 18, 19, 21, 22, 23, 25, 32, 33...) — como essa placa não tem sensores, sobra bastante margem para expansão futura (botão físico, buzzer, display, etc.)

**Alimentação da fita NeoPixel:**
- Até ~8-10 LEDs: pode tentar alimentar pelo 5V/VIN do próprio ESP32, com cautela.
- Mais que isso: **fonte 5V externa dedicada**, com GND interligado ao GND do ESP32.
- Capacitor eletrolítico ~1000µF entre 5V e GND, o mais próximo possível do primeiro LED da fita.

---

## Ligação física entre as duas ESP32

```
ESP-A GPIO27 (TX2)  ------->  ESP-B GPIO26 (RX2)
ESP-A GPIO26 (RX2)  <-------  ESP-B GPIO27 (TX2)
ESP-A GND           ---------- ESP-B GND      (obrigatório!)
```

**Por que não RX0/TX0:** esses pinos (GPIO1/GPIO3) são a mesma UART0 usada pelo cabo USB — tanto para gravar o firmware quanto para o Serial Monitor. Ligar permanentemente a outra ESP32 neles causaria conflito de barramento (dois "mestres" na mesma linha) e tiraria seu acesso de debug via USB bem na hora que mais precisa (montagem de última hora). Por isso o link usa UART2 remapeada para GPIO26/27 via crossbar do ESP32 — mantém RX0/TX0 e o Serial Monitor 100% livres nas duas placas.

## Confirmação de que não há conflito de pinos

Nenhum GPIO é reutilizado entre função de sensor e função de link/LED em nenhuma das duas placas. Os barramentos (OneWire, I²C, UART2, ADC1, saída digital do NeoPixel) são fisicamente independentes entre si — não há compartilhamento de linha, portanto não há risco de interferência entre a leitura dos sensores e a comunicação/controle do LED.


---

## Tensões de operação dos sensores

| Sensor | Alimentação | Sinal de saída | Ligar em |
|---|---|---|---|
| DS18B20 | 3,0 – 5,5V | Digital no nível do VCC | 3V3 (J8) |
| SHT31 (módulo) | 3,3 – 5V* | I²C no nível do VCC | **3V3 obrigatório** |
| BH1750 (GY-302) | 3,3 – 5V* | I²C no nível do VCC | **3V3 obrigatório** |
| Fluxo YF-S201/ZJ-S201 | 5 – 18V | Pulso ~5V | VIN + divisor resistivo |
| Boia (nível) | passiva | Contato seco | 3V3 via pull-up interno |
| TDS Meter v1.0 | 3,3 – 5,5V | Analógico 0 – 2,3V | VIN (5V), sinal direto no ADC |
| pH (futuro) | 5V ou 3,3V | 0–5V (PH4502C) ou 0–3V (Gravity V2) | ver modelo |
| Fita NeoPixel | 5V | Dado espera ~5V | Fonte 5V externa |
| Módulo relé | **3,3V** | — | 3V3 (J8) — ver nota de corrente abaixo |

\* Os módulos aceitam 5V pelo regulador interno, **mas não use**: os pull-ups do I²C ficam ligados ao VCC do módulo, então alimentar em 5V coloca 5V nas linhas SDA/SCL e danifica o ESP32. Como os dois compartilham o barramento, ambos precisam estar em 3V3.

### Divisor resistivo para o sensor de fluxo

```
Pulso do sensor (5V) ----[ 10k ]----+---- GPIO19 (3,3V)
                                    |
                                  [ 20k ]
                                    |
                                   GND
```
Saída = 5V × 20k/(10k+20k) ≈ 3,33V.

---

## Controle da bomba d'água

| Item | Definição |
|---|---|
| Pino do relé | GPIO25 na **ESP-A** |
| Quem decide | ESP-A (tem a boia e o relé) |
| Quem pede | Dashboards, via ESP-B → `CMD:{"pump":true}` |
| Trava 1 | Nível baixo na boia → bomba desligada e comando recusado |
| Trava 2 | Tempo máximo de 5 min ligada, desliga sozinha |

A ESP-B apenas encaminha o pedido; toda a lógica de segurança vive na ESP-A. Se o link serial cair com a bomba ligada, a ESP-A continua protegida por conta própria — essa separação é intencional.

**Cuidados elétricos:** o relé de 3,3V pode sair do barramento 3V3, mas a bobina puxa 70-90 mA — somado ao pico do Wi-Fi e aos sensores, pode derrubar a tensão e resetar a placa. Se isso acontecer ao acionar a bomba, use fonte 3,3V separada (GND comum) ou um capacitor de 470-1000 µF no 3V3. Bomba é carga indutiva e precisa de diodo de roda-livre, geralmente já presente em módulos com optoacoplador; se a bomba for 127/220V, o lado de alta tensão deve ficar isolado e inacessível.
