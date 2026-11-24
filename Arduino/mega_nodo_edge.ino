#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// pines y direccion I2C
#define I2C_ADDR   0x08
#define PIN_TRIG   22
#define PIN_ECHO   24
#define PIN_HALL   18
#define PIN_DS     26
#define PIN_LDR    A0

// constantes
#define N_SENS     4
#define PPR        1                    // pulsos por vuelta (1 iman)
#define INVALIDA   ((int16_t)-32768)    // centinela de temperatura invalida
#define ECHO_TOUT  30000UL              // timeout del eco (us)

// indice de cada sensor (mismo orden en intervalo[], tUlt[], MIN_MS[], seq[] y en el bus I2C)
enum { S_LDR, S_SRF, S_DS, S_HALL };
enum EstadoSrf { REPOSO, ESPERA_ECHO, MIDE_ECHO };

// intervalo minimo aceptado por sensor (ms)
const uint16_t MIN_MS[N_SENS] = {50, 60, 750, 500};

// intervalos actuales (ms); los modifica recibirComando() desde la ISR de I2C
volatile uint16_t intervalo[N_SENS] = {1000, 500, 1000, 1000};

unsigned long tUlt[N_SENS];

// contador de muestras nuevas por sensor (uint16, con vuelta). Permite al gateway
// publicar cada muestra una sola vez aunque el sensor sea mas lento que su consulta
uint16_t seq[N_SENS] = {0, 0, 0, 0};

// A3144
volatile unsigned long pulsos = 0;

// LDR: promedio movil de 8
uint16_t bufLdr[8];
uint8_t idxLdr = 0;
uint8_t nLdr = 0;

// valores publicados
uint16_t ldr = 0;         // cuentas ADC promedio
uint16_t dist = 0;        // mm, 0 = sin eco
int16_t  temp = INVALIDA; // grados C x 100
uint16_t rpm = 0;         // RPM enteras

float tActual = 0;        // ultima temperatura valida (compensacion de v del sonido)
bool  tValida = false;

// SRF04
EstadoSrf estado = REPOSO;
unsigned long tIni = 0;
unsigned long tEstado = 0;

// trama Mega -> ESP: 12 x uint16 = 24 bytes little-endian
//  [0..3]  valores:    ldr, dist, temp(x100, int16), rpm
//  [4..7]  intervalos: ldr, srf, ds, hall (ms)
//  [8..11] seq:        ldr, srf, ds, hall
uint16_t bufferTx[12];

OneWire oneWire(PIN_DS);
DallasTemperature sensors(&oneWire);
DeviceAddress dsAddr;

void isrHall() {
  pulsos++;
}

// copia a bufferTx con interrupciones apagadas (AVR de 8 bits)
void publicar() {
  noInterrupts();
  bufferTx[0] = ldr;
  bufferTx[1] = dist;
  bufferTx[2] = (uint16_t)temp;
  bufferTx[3] = rpm;
  for (uint8_t i = 0; i < N_SENS; i++) {
    bufferTx[4 + i] = intervalo[i];
    bufferTx[8 + i] = seq[i];
  }
  interrupts();
}

// ESP8266 -> Mega: [0x01][indice sensor][intervalo LSB][intervalo MSB]
void recibirComando(int n) {
  if (n != 4) {
    while (Wire.available()) Wire.read();
    return;
  }
  uint8_t cmd = Wire.read();
  uint8_t idx = Wire.read();
  uint8_t lo  = Wire.read();
  uint8_t hi  = Wire.read();
  uint16_t v = lo | ((uint16_t)hi << 8);
  if (cmd == 0x01 && idx < N_SENS && v >= MIN_MS[idx]) {
    intervalo[idx] = v;
  }
}

// Mega -> ESP8266: 24 bytes
void enviarDatos() {
  Wire.write((uint8_t*)bufferTx, sizeof(bufferTx));
}

void setup() {
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_HALL, INPUT_PULLUP);

  Wire.begin(I2C_ADDR);
  Wire.onReceive(recibirComando);
  Wire.onRequest(enviarDatos);
  // Wire.begin() activa pull-ups internos a 5 V; se apagan (el ESP8266 es de 3,3 V).
  // Pull-ups externos de 1,5 a 4,7 kOhm a 3,3 V en SDA y en SCL.
  digitalWrite(SDA, LOW);
  digitalWrite(SCL, LOW);

  memset(dsAddr, 0, sizeof(dsAddr));
  sensors.begin();
  if (sensors.getAddress(dsAddr, 0)) {
    sensors.setResolution(dsAddr, 12);
  }
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  attachInterrupt(digitalPinToInterrupt(PIN_HALL), isrHall, FALLING);

  publicar();
  pulsos = 0;
  unsigned long ahora = millis();
  for (uint8_t i = 0; i < N_SENS; i++) tUlt[i] = ahora;
}

void loop() {
  unsigned long ahora = millis();

  uint16_t iv[N_SENS];
  noInterrupts();
  for (uint8_t i = 0; i < N_SENS; i++) iv[i] = intervalo[i];
  interrupts();

  // analogRead y OneWire solo corren con el sonar en reposo (no arruinan el timing del eco)
  if (estado == REPOSO) {

    // LDR
    if (ahora - tUlt[S_LDR] >= iv[S_LDR]) {
      tUlt[S_LDR] = ahora;
      bufLdr[idxLdr] = analogRead(PIN_LDR);
      idxLdr = (idxLdr + 1) & 7;
      if (nLdr < 8) nLdr++;
      uint32_t suma = 0;
      for (uint8_t i = 0; i < nLdr; i++) suma += bufLdr[i];
      ldr = suma / nLdr;
      seq[S_LDR]++;
    }

    // DS18B20: lee la conversion pedida la vez anterior y pide la siguiente
    if (ahora - tUlt[S_DS] >= iv[S_DS]) {
      tUlt[S_DS] = ahora;
      float t = sensors.getTempC(dsAddr);
      sensors.requestTemperatures();
      if (t > -126.0f && fabs(t - 85.0f) > 0.01f && t >= -55.0f && t <= 125.0f) {
        tActual = t;
        tValida = true;
        temp = (int16_t)lroundf(t * 100.0f);
      } else {
        tValida = false;
        temp = INVALIDA;
      }
      seq[S_DS]++;
    }
  }

  // A3144: RPM = 60000 * N / (PPR * dt), con dt real
  if (ahora - tUlt[S_HALL] >= iv[S_HALL]) {
    unsigned long dt = ahora - tUlt[S_HALL];
    tUlt[S_HALL] = ahora;
    noInterrupts();
    unsigned long n = pulsos;
    pulsos = 0;
    interrupts();
    unsigned long r = 60000UL * n / ((unsigned long)PPR * dt);
    rpm = (r > 65535UL) ? 65535 : (uint16_t)r;
    seq[S_HALL]++;
  }

  // SRF04: maquina de estados, sin pulseIn()
  switch (estado) {
    case REPOSO:
      if (ahora - tUlt[S_SRF] >= iv[S_SRF]) {
        tUlt[S_SRF] = ahora;
        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG, LOW);
        tEstado = micros();
        estado = ESPERA_ECHO;
      }
      break;

    case ESPERA_ECHO:
      if (digitalRead(PIN_ECHO) == HIGH) {
        tIni = micros();
        estado = MIDE_ECHO;
      } else if (micros() - tEstado >= ECHO_TOUT) {
        dist = 0;
        seq[S_SRF]++;
        estado = REPOSO;
      }
      break;

    case MIDE_ECHO:
      if (digitalRead(PIN_ECHO) == LOW) {
        unsigned long dur = micros() - tIni;
        float vs = tValida ? (331.3f + 0.606f * tActual) : 344.4f;
        float d = dur * vs / 2000.0f;
        dist = (d > 65535.0f) ? 65535 : (uint16_t)(d + 0.5f);
        seq[S_SRF]++;
        estado = REPOSO;
      } else if (micros() - tIni >= ECHO_TOUT) {
        dist = 0;
        seq[S_SRF]++;
        estado = REPOSO;
      }
      break;
  }

  publicar();
}
