#include "max6675.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Modbusino.h>
#include <math.h>

// Mapeamento dos registradores Modbus: Arduino -> tag no SCADA
// O endereço configurado no SCADA corresponde ao índice do tab_reg + 1.

// tab_reg[0]  -> Temperatura_Atual       | Endereço 1  | Temperatura medida x 100; dividir por 100 para obter °C.
// tab_reg[1]  -> Estado_Resistencia      | Endereço 2  | Estado da resistência: 0 = desligada; 1 = ligada.
// tab_reg[2]  -> BT_Cancelar             | Endereço 3  | Comando do SCADA: 1 = cancelar a execução.
// tab_reg[3]  -> BT_Enter                | Endereço 4  | Comando do SCADA: 1 = iniciar com os parâmetros enviados.
// tab_reg[4]  -> Temperatura_Desejada     | Endereço 5  | Temperatura definida pelo SCADA, em °C.
// tab_reg[5]  -> Case                    | Endereço 6  | Estado do sistema; veja os valores abaixo.
// RETIRADO tab_reg[6]  -> Status_Tempo_Finalizado | Endereço 7  | 1 = tempo finalizado; zerado ao reiniciar o sistema.
// tab_reg[7]  -> Sem tag correspondente  | Endereço 8  | Reservado, sem utilização atual.
// tab_reg[8]  -> SetPoint                | Endereço 9  | Temperatura efetivamente utilizada no controle, em °C.
// tab_reg[9]  -> Tempo_Desejado_Min       | Endereço 10 | Duração configurada, em minutos.
// tab_reg[10] -> Tempo_Restante           | Endereço 11 | Tempo restante da execução, em segundos.
// tab_reg[11] -> Bloquear_Controles       | Endereço 12 | Controles do SCADA: 0 = liberados; 1 = bloqueados.
// tab_reg[12] -> Sem tag correspondente  | Endereço 13 | Sem utilização atual; falha sinalizada pelo tab_reg[5].

// Valores enviados em tab_reg[5] -> tag Case:
// 0 = Início, aguardando configuração.
// 1 = Escolha do modo: Arduino ou SCADA.
// 2 = Aguardando configuração pelo SCADA.
// 3 = Configuração da temperatura pelo Arduino.
// 4 = Configuração do tempo pelo Arduino.
// 5 = Execução em andamento.
// 6 = Execução cancelada.
// 7 = Tempo finalizado.
// 9 = Falha do sensor; resistência desligada e execução interrompida.


// Inicializa o escravo com o ID 1
ModbusinoSlave modbusino_slave(1);

//Alocar um mapeamento de 13 valores
uint16_t tab_reg[13] = { 0 };

// ---------- Pinos ----------
#define pinSO 5    // PINO DIGITAL (SO)
#define pinCS 4    // PINO DIGITAL (CS)
#define pinCLK 3   // PINO DIGITAL (CLK / SCK)
#define pinRele 7  // PINO DIGITAL PARA CONTROLE DO RELÉ


#define pinPot A0                 // potenciômetro
#define pinBot 9                  // botão de confirmação
#define AlavancaDeConfiguracao 8  // Alavanca para configuração de scada ou arduino
#define PinBotCancelar 6

// ---------- Variáveis de temperatura ----------
int setpoint = 30;
String situResist = "DESLIGADO";
int ValorResist;
int valor;
float temperatura;

unsigned int tempoDesejado = 0;
unsigned long tempoInicio = 0;
unsigned long ultimaLeitura = 0;
unsigned long tempoCorrido = 0;
unsigned long tempoTotal = 0;
unsigned long tempoRestante = 0;
int horasRestantes = 0;
int minutosRestantes = 0;
int segundosRestantes = 0;
int horasSelecionadas = 0;
int minutosSelecionados = 0;

#define LEDVERDE 10
#define LEDVERMELHO 11
#define LEDAMARELO 12

// ---------- LCD ----------
#define endereco 0x27  // Endereços comuns: 0x27, 0x3F
#define colunas 16
#define linhas 2
LiquidCrystal_I2C lcd(endereco, colunas, linhas);

// ---------- Sensor ----------
MAX6675 sensorTemp(pinCLK, pinCS, pinSO);

// ---------- Estados ----------
enum Estado { START,
              MODO_CONFIG,
              CONFIG_SCADA,
              CONFIG_ARDUINO,
              CONFIG_TIMER,
              RODANDO,
              TEMPO_FINALIZADO,
              CANCELAR,
              FALHA_SENSOR };

Estado estado = START;

int lerPotTemp(int minV, int maxV) {
  int raw = analogRead(pinPot);  // 0–1023
  return map(raw, 0, 1023, minV, maxV);
}

bool ler_Bot(int pin) {
  static bool last = HIGH;
  bool leitura = digitalRead(pin);
  bool edge = (last == HIGH && leitura == LOW);  // transição HIGH→LOW
  last = leitura;
  return edge;
}

void escreve_LCD(String L1, String L2) {
  lcd.setCursor(0, 0);
  lcd.print(L1);
  lcd.setCursor(0, 1);
  lcd.print(L2);
}

void controla_LED(bool a, bool b, bool c) {
  digitalWrite(LEDVERDE, a);
  digitalWrite(LEDVERMELHO, b);
  digitalWrite(LEDAMARELO, c);
}

void controle_de_temperatura(float t) {
  const float histerese = 1;
  if (t <= setpoint - histerese) {
    digitalWrite(pinRele, HIGH);
    situResist = "LIG";
    ValorResist = 1;
  } else if (t > setpoint) {
    digitalWrite(pinRele, LOW);
    situResist = "DES";
    ValorResist = 0;
  }
}

void resetSistema() {
  // Variáveis de controle
  setpoint = 0;
  tempoDesejado = 0;
  tempoInicio = 0;
  tempoCorrido = 0;
  tempoTotal = 0;
  tempoRestante = 0;
  minutosRestantes = 0;
  segundosRestantes = 0;
  ultimaLeitura = 0;
  ValorResist = 0;
  situResist = "DES";
  temperatura = 0;

  // Desliga o relé
  digitalWrite(pinRele, LOW);

  // Apaga os LEDs
  controla_LED(0, 0, 0);

  // Limpa registradores Modbus (SCADA)
  for (int i = 0; i < 13; i++) {
    tab_reg[i] = 0;
  }

  // Limpa LCD
  lcd.clear();
}

// ---------- Setup ----------
void setup() {
  pinMode(LEDVERDE, OUTPUT);
  pinMode(LEDVERMELHO, OUTPUT);
  pinMode(LEDAMARELO, OUTPUT);
  pinMode(AlavancaDeConfiguracao, INPUT_PULLUP);

  modbusino_slave.setup(9600);
  Serial.begin(9600);
  lcd.begin();
  lcd.backlight();
  lcd.clear();

  pinMode(PinBotCancelar, INPUT_PULLUP);
  pinMode(pinRele, OUTPUT);
  pinMode(pinBot, INPUT_PULLUP);
  digitalWrite(pinRele, LOW);  // Relé inicialmente desligado
}

// ---------- Loop ----------
void loop() {

  modbusino_slave.loop(tab_reg, 13);

  switch (estado) {

    case START:
      //Ações (Saídas)
      tab_reg[5] = 0;   // SCADA: Case (início, aguardando clique para configurar)
      tab_reg[11] = 1;  // SCADA: Bloquear_Controles (0=libera, 1=bloqueia)
      escreve_LCD("Clique para", "configurar");
      controla_LED(1, 0, 0);

      // Transições entre estados (Entradas)
      if (ler_Bot(pinBot)) {
        estado = MODO_CONFIG;
        lcd.clear();
      }
      break;

    case MODO_CONFIG:

      //Ações (Saídas)
      tab_reg[5] = 1;  // SCADA: Case (escolha do modo Arduino ou SCADA)
      if (digitalRead(AlavancaDeConfiguracao) == LOW) {
        escreve_LCD("Escolha: ARDUINO", " ");
        controla_LED(0, 1, 0);
      }
      if (digitalRead(AlavancaDeConfiguracao) == HIGH) {
        escreve_LCD("Escolha: SCADA  ", " ");
        controla_LED(0, 0, 1);
      }

      // Transições entre estados (Entradas)
      if (ler_Bot(pinBot)) {
        if (digitalRead(AlavancaDeConfiguracao) == LOW) {
          estado = CONFIG_ARDUINO;
          lcd.clear();
        }
        if (digitalRead(AlavancaDeConfiguracao) == HIGH) {
          estado = CONFIG_SCADA;
          lcd.clear();
        }
      }
      break;

    case CONFIG_SCADA:
      tab_reg[5] = 2;  // SCADA: Case (aguardando configuração pelo SCADA)
      escreve_LCD("Aguardando", "Config: SCADA");
      controla_LED(0, 0, 1);

      tab_reg[11] = 0;      // SCADA: Bloquear_Controles (0=libera, 1=bloqueia)
      if (tab_reg[3] == 1)  // SCADA: BT_Enter (1=inicia)
      {

        setpoint = constrain(tab_reg[4], 0, 240);  // SCADA: Temperatura_Desejada (°C) //Setpoint recebe a temperatura
        tempoDesejado = tab_reg[9];  // SCADA: Tempo_Desejado_Min (min)

        tab_reg[3] = 0;              // SCADA: BT_Enter (1=inicia)
        tab_reg[2] = 0;   // BT_Cancelar: descarta clique feito antes de iniciar
        tempoInicio = millis();
        estado = RODANDO;
        //lcd.print(setpoint);
      }
      break;

    case CONFIG_ARDUINO:
      tab_reg[5] = 3;  // SCADA: Case (configuração da temperatura pelo Arduino)
      controla_LED(1, 0, 0);

      valor = lerPotTemp(0, 240);  // limite arbitrário
      lcd.setCursor(0, 0);
      lcd.print("Defina TEMP");
      lcd.setCursor(0, 1);
      lcd.print("Valor:    ");
      lcd.print(valor);
      lcd.print((char)223);
      lcd.print("C  ");  // símbolo de grau
      setpoint = valor;

      if (ler_Bot(pinBot)) {
        estado = CONFIG_TIMER;
        lcd.clear();
      }
      break;

    case CONFIG_TIMER:
      tab_reg[5] = 4;  // SCADA: Case (configuração do tempo pelo Arduino)
      tempoDesejado = lerPotTemp(1, 240);
      horasSelecionadas = tempoDesejado / 60;
      minutosSelecionados = tempoDesejado % 60;

      lcd.setCursor(0, 0);
      lcd.print("Defina TEMPO    ");

      lcd.setCursor(0, 1);
      lcd.print("Valor: ");

      if (horasSelecionadas == 0) {
        // Até 59 minutos
        lcd.print(minutosSelecionados);
        lcd.print("min     ");
      } else {
        // A partir de 1 hora
        lcd.print(horasSelecionadas);  //essa parte funciona para o display mostrar 1h e não 60segundos
        lcd.print("h:");

        // Zero à esquerda
        if (minutosSelecionados < 10) {
          lcd.print("0");
        }

        lcd.print(minutosSelecionados);
        lcd.print("min ");
      }

      if (ler_Bot(pinBot)) {
        tempoInicio = millis();
        estado = RODANDO;
        lcd.clear();
      }
      break;

    case RODANDO:
      tab_reg[5] = 5;  // SCADA: Case (execução em andamento)
      controla_LED(1, 1, 1);
      tab_reg[11] = 1;  // SCADA: Bloquear_Controles (0=libera, 1=bloqueia)
      if (millis() - ultimaLeitura >= 500) {
        ultimaLeitura = millis();


        // Primeiro verifica a leitura, sem substituir a última válida.
        float leitura = sensorTemp.readCelsius();

        if (isnan(leitura) || isinf(leitura)) {

          // Desliga imediatamente a resistência.
          digitalWrite(pinRele, LOW);
          ValorResist = 0;
          situResist = "DES";

          // Atualiza os sinais enviados ao supervisório.
          tab_reg[1] = 0;  // SCADA: Estado_Resistencia (0=OFF, 1=ON)
          //tab_reg[6] = 0;  // SCADA: Status_Tempo_Finalizado (1=finalizado)
          tab_reg[5] = 9;  // SCADA: Case (falha do sensor)

          estado = FALHA_SENSOR;
          lcd.clear();

          // Sai do switch antes de controlar a temperatura
          // ou converter a leitura inválida para Modbus.
          break;
        }

        temperatura = leitura;
        controle_de_temperatura(temperatura);
      }

      // Verifica se o tempo acabou
      tempoCorrido = millis() - tempoInicio;
      tempoTotal = (unsigned long)tempoDesejado * 60 * 1000;


      tempoRestante = 0;
      if (tempoTotal > tempoCorrido) {
        tempoRestante = tempoTotal - tempoCorrido;
      }

      horasRestantes = tempoRestante / 3600000UL;
      minutosRestantes = (tempoRestante % 3600000UL) / 60000UL;
      segundosRestantes = (tempoRestante % 60000UL) / 1000UL;

      // Mostra no LCD
      lcd.setCursor(0, 0);
      lcd.print("TMP ");
      lcd.print(temperatura);
      lcd.print((char)223);
      lcd.print("C ");
      lcd.print(situResist);
      lcd.setCursor(0, 1);

      // Mostra SetPoint + tempo restante
      lcd.setCursor(0, 1);

      lcd.print("SP:");
      lcd.print(setpoint);
      lcd.print(" ");
      lcd.print("T");

      // HORAS
      if (horasRestantes < 10) {
        lcd.print("0");
      }
      lcd.print(horasRestantes);
      lcd.print(":");

      // MINUTOS
      if (minutosRestantes < 10) {
        lcd.print("0");
      }
      lcd.print(minutosRestantes);
      lcd.print(":");

      // SEGUNDOS
      if (segundosRestantes < 10) {
        lcd.print("0");
      }
      lcd.print(segundosRestantes);

      // Envia o tempo restante total em segundos para o SCADA
      tab_reg[10] = tempoRestante / 1000UL;  // SCADA: Tempo_Restante (s)
      //delay(250);  // precisa do delay para funcionar

      // 2. Atualiza registradores Modbus
      tab_reg[0] = temperatura * 100;  // SCADA: Temperatura_Atual (°C x 100)
      tab_reg[1] = ValorResist;        // SCADA: Estado_Resistencia (0=OFF, 1=ON)
      tab_reg[8] = setpoint;           // SCADA: SetPoint (°C)
      tab_reg[9] = tempoDesejado;      // SCADA: Tempo_Desejado_Min (min)

      if (tempoCorrido >= tempoTotal) {
        controla_LED(1, 0, 0);
        tempoInicio = 0;    // reseta para não ficar entrando aqui
        tempoDesejado = 0;  // reseta o tempo
        estado = TEMPO_FINALIZADO;
        lcd.clear();
      }

      if (tab_reg[2] == 1) {  // SCADA: BT_Cancelar (1=cancela)
        estado = CANCELAR;
        lcd.clear();
      }

      if (ler_Bot(PinBotCancelar)) {
        estado = CANCELAR;
        lcd.clear();
      }
      break;

    case FALHA_SENSOR:

      // Mantém a resistência desligada.
      digitalWrite(pinRele, LOW);
      ValorResist = 0;
      situResist = "DES";

      tab_reg[1] = 0;   // SCADA: Estado_Resistencia (0=OFF, 1=ON)
      tab_reg[5] = 9;   // SCADA: Case (falha do sensor)
      tab_reg[11] = 1;  // SCADA: Bloquear_Controles (0=libera, 1=bloqueia)


      // Alterna o LED vermelho a cada 500 ms.
      if (millis() - ultimaLeitura >= 500UL) {
        ultimaLeitura = millis();

        controla_LED(
          0,
          digitalRead(LEDVERMELHO) == LOW,
          0);
      }

      escreve_LCD(
        "FALHA NO SENSOR ",
        "Clique p voltar ");

      // Retorna ao início pelo botão físico de confirmação.
      if (ler_Bot(pinBot)) {
        resetSistema();
        estado = START;
      }

      break;

    case CANCELAR:
      tab_reg[5] = 6;  // SCADA: Case (execução cancelada)
      digitalWrite(pinRele, LOW);
      tab_reg[1] = 0;
      escreve_LCD("CANCELADO", "Clique p voltar");
      //tab_reg[7] = 0;  // Manda parar o cronômetro no scada
      if (millis() - ultimaLeitura >= 500) {
        ultimaLeitura = millis();

        if (digitalRead(LEDVERMELHO) == LOW) {
          controla_LED(0, 1, 0);  // acende vermelho
        } else {
          controla_LED(0, 0, 0);  // apaga tudo
        }
      }

      if (ler_Bot(pinBot)) {
        resetSistema();
        estado = START;
      }
      break;


    case TEMPO_FINALIZADO:
      tab_reg[5] = 7;  // SCADA: Case (tempo finalizado)
      digitalWrite(pinRele, LOW);
      tab_reg[1] = 0;
      escreve_LCD("TEMPO FINALIZADO", "Clique p voltar");
      //tab_reg[6] = 1;  // SCADA: Status_Tempo_Finalizado (1=finalizado)
      //tab_reg[7] = 0;
      if (millis() - ultimaLeitura >= 500) {
        ultimaLeitura = millis();
        if (digitalRead(LEDAMARELO) == LOW) {
          controla_LED(0, 0, 1);  // acende amarelo
        } else {
          controla_LED(0, 0, 0);  // apaga tudo
        }
      }

      if (ler_Bot(pinBot)) {
        controla_LED(0, 0, 0);  // apaga tudo ao sair
        resetSistema();
        estado = START;
      }
      break;
  }
}
