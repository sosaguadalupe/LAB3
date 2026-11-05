#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// pines y direccion I2C 
#define I2C_ADDR   0x08   // direccion del Mega como esclavo
#define PIN_TRIG   22     // SRF04: disparo
#define PIN_ECHO   24     // SRF04: eco (sin interrupcion en el Mega, se lee por polling)
#define PIN_HALL   18     // A3144 en INT3
#define PIN_DS     26     // DS18B20, bus 1-Wire con pull-up de 4.7k
#define PIN_LDR    A0     // divisor resistivo del LDR

// constantes 
#define N_SENS     4
#define PPR        1                    // pulsos por vuelta (1 iman)
#define INVALIDA   ((int16_t)-32768)    // centinela de temperatura invalida
#define ECHO_TOUT  30000UL              // timeout del eco en us (30 ms, ~5 m, mas que el rango del sensor)

// indices de cada sensor en los arreglos de intervalo / tUlt / MIN_MS
enum { S_LDR, S_SRF, S_DS, S_HALL };

// estados de la maquina del SRF04 (el TRIG de 10 us se hace bloqueante, no hace falta estado)
enum EstadoSrf { REPOSO, ESPERA_ECHO, MIDE_ECHO };

// intervalo minimo aceptado por sensor (ms). Si el ESP manda menos, se ignora
const uint16_t MIN_MS[N_SENS] = {50, 60, 750, 500};

// intervalos actuales (ms). volatile porque los cambia recibirComando() desde la ISR de I2C
volatile uint16_t intervalo[N_SENS] = {1000, 500, 1000, 1000};

// ultima vez que se atendio cada sensor (millis)
unsigned long tUlt[N_SENS];

//  A3144 
volatile unsigned long pulsos = 0;   // lo incrementa la ISR, lo lee y borra el loop

//  LDR 
uint16_t bufLdr[8];                  // buffer circular para el promedio movil de 8
uint8_t idxLdr = 0;                  // donde se escribe la proxima muestra
uint8_t nLdr = 0;                    // cuantas muestras validas hay (evita promediar ceros al arrancar)

//  valores publicados (lo que ve el ESP8266) 
uint16_t ldr = 0;        // cuentas ADC promedio
uint16_t dist = 0;       // mm, 0 = sin eco
uint16_t rpm = 0;        // RPM enteras
int16_t temp = INVALIDA; // grados C x 100, INVALIDA si no hay lectura buena

// ultima temperatura valida, se usa para compensar la velocidad del sonido
float tActual = 0;
bool tValida = false;

//  SRF04 
EstadoSrf estado = REPOSO;
unsigned long tIni = 0;      // micros() cuando el eco subio a HIGH
unsigned long tEstado = 0;   // micros() de referencia para el timeout

// copia estable de los datos para onRequest: 4 valores + 4 intervalos = 8 x uint16 = 16 bytes
uint16_t bufferTx[8];

OneWire oneWire(PIN_DS);
DallasTemperature sensors(&oneWire);
DeviceAddress dsAddr;

// cuenta un pulso por cada flanco descendente del A3144
void isrHall() {
  pulsos++;
}

// copia los valores al buffer de envio con interrupciones apagadas
// el AVR es de 8 bits: sin esto, onRequest podria leer un uint16 a medio escribir
void publicar() {
  noInterrupts();
  bufferTx[0] = ldr;
  bufferTx[1] = dist;
  bufferTx[2] = (uint16_t)temp;
  bufferTx[3] = rpm;
  for (uint8_t i = 0; i < N_SENS; i++) bufferTx[4 + i] = intervalo[i];
  interrupts();
}

// ESP8266 -> Mega. Formato de 4 bytes: [0x01][indice sensor][intervalo LSB][intervalo MSB]
void recibirComando(int n) {
  if (n != 4) {
    while (Wire.available()) Wire.read();  // largo incorrecto, se vacia el buffer
    return;
  }
  uint8_t cmd = Wire.read();
  uint8_t idx = Wire.read();
  uint8_t lo  = Wire.read();
  uint8_t hi  = Wire.read();
  uint16_t v = lo | ((uint16_t)hi << 8);

  // solo se aplica si el comando, el indice y el minimo son validos
  if (cmd == 0x01 && idx < N_SENS && v >= MIN_MS[idx]) {
    intervalo[idx] = v;  // rige desde la proxima vuelta del loop
  }
}

// Mega -> ESP8266. Manda los 16 bytes de bufferTx
void enviarDatos() {
  // AVR es little endian, sale primero el byte menos significativo
  Wire.write((uint8_t*)bufferTx, sizeof(bufferTx));
}

void setup() {
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_HALL, INPUT_PULLUP);  // salida de colector abierto del A3144

  Wire.begin(I2C_ADDR);
  Wire.onReceive(recibirComando);
  Wire.onRequest(enviarDatos);
  // Wire.begin() activa los pull-ups internos y el ESP8266 es de 3.3 V,
  // asi que se apagan (tiene que ser despues del begin)
  digitalWrite(SDA, LOW);
  digitalWrite(SCL, LOW);

  memset(dsAddr, 0, sizeof(dsAddr));
  sensors.begin();
  if (sensors.getAddress(dsAddr, 0)) {
    sensors.setResolution(dsAddr, 12);  // 0.0625 C, conversion de hasta 750 ms
  }
  sensors.setWaitForConversion(false);  // no bloquea esperando la conversion
  sensors.requestTemperatures();        // asi la primera lectura no es basura

  attachInterrupt(digitalPinToInterrupt(PIN_HALL), isrHall, FALLING);

  publicar();   // buffer inicial: ldr=0, dist=0, temp=INVALIDA, rpm=0
  pulsos = 0;   // descarta pulsos espurios durante el setup
  unsigned long ahora = millis();
  for (uint8_t i = 0; i < N_SENS; i++) tUlt[i] = ahora;
}

void loop() {
  unsigned long ahora = millis();

  // copia local de los intervalos: la ISR de I2C los puede cambiar en medio del loop
  uint16_t iv[N_SENS];
  noInterrupts();
  for (uint8_t i = 0; i < N_SENS; i++) iv[i] = intervalo[i];
  interrupts();

  // analogRead y OneWire tardan >100 us y arruinan el timing del eco,
  // por eso solo corren con el sonar en reposo
  if (estado == REPOSO) {

    // LDR: una muestra por intervalo, promedio movil de hasta 8
    if (ahora - tUlt[S_LDR] >= iv[S_LDR]) {
      tUlt[S_LDR] = ahora;
      bufLdr[idxLdr] = analogRead(PIN_LDR);
      idxLdr = (idxLdr + 1) & 7;     // equivale a % 8
      if (nLdr < 8) nLdr++;
      uint32_t suma = 0;
      for (uint8_t i = 0; i < nLdr; i++) suma += bufLdr[i];
      ldr = suma / nLdr;
    }

    // DS18B20: lee la conversion pedida la vez anterior y pide la siguiente
    if (ahora - tUlt[S_DS] >= iv[S_DS]) {
      tUlt[S_DS] = ahora;
      float t = sensors.getTempC(dsAddr);
      sensors.requestTemperatures();
      // -127 = sensor desconectado, 85 = valor de reset al encender
      if (t > -126.0f && fabs(t - 85.0f) > 0.01f && t >= -55.0f && t <= 125.0f) {
        tActual = t;
        tValida = true;
        temp = (int16_t)lroundf(t * 100.0f);
      } else {
        tValida = false;   // el SRF04 vuelve a usar 344.4 m/s
        temp = INVALIDA;
      }
    }
  }

  // A3144: RPM = 60000 * N / (PPR * dt), con dt real en ms (no el intervalo nominal)
  if (ahora - tUlt[S_HALL] >= iv[S_HALL]) {
    unsigned long dt = ahora - tUlt[S_HALL];
    tUlt[S_HALL] = ahora;
    noInterrupts();            // leer y borrar pulsos tiene que ser atomico
    unsigned long n = pulsos;
    pulsos = 0;
    interrupts();
    unsigned long r = 60000UL * n / ((unsigned long)PPR * dt);  // UL para que no desborde
    rpm = (r > 65535UL) ? 65535 : (uint16_t)r;                  // satura en 16 bits
  }

  // SRF04: maquina de estados, no usa pulseIn() para no bloquear el loop
  switch (estado) {
    case REPOSO:
      // llego el momento de medir: pulso de 10 us en TRIG
      if (ahora - tUlt[S_SRF] >= iv[S_SRF]) {
        tUlt[S_SRF] = ahora;   // sin esto dispara en cada vuelta
        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG, LOW);
        tEstado = micros();
        estado = ESPERA_ECHO;
      }
      break;

    case ESPERA_ECHO:
      // espera que ECHO suba; si no sube en 30 ms, no hubo eco
      if (digitalRead(PIN_ECHO) == HIGH) {
        tIni = micros();
        estado = MIDE_ECHO;
      } else if (micros() - tEstado >= ECHO_TOUT) {
        dist = 0;  // sin eco
        estado = REPOSO;
      }
      break;

    case MIDE_ECHO:
      // el ancho del pulso de ECHO es el tiempo de ida y vuelta
      if (digitalRead(PIN_ECHO) == LOW) {
        unsigned long dur = micros() - tIni;
        // v = 331.3 + 0.606*T con el DS18B20, o 344.4 fijo si no hay temperatura valida
        float vs = tValida ? (331.3f + 0.606f * tActual) : 344.4f;
        float d = dur * vs / 2000.0f;  // us * m/s / 2000 = mm (ya incluye el /2 de ida y vuelta)
        dist = (d > 65535.0f) ? 65535 : (uint16_t)(d + 0.5f);
        estado = REPOSO;
      } else if (micros() - tIni >= ECHO_TOUT) {
        dist = 0;  // ECHO se quedo en HIGH
        estado = REPOSO;
      }
      break;
  }

  publicar();  // deja bufferTx al dia para el proximo onRequest
}
