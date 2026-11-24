<?php
define('DB_HOST', '127.0.0.1');
define('DB_NAME', 'u874918252_dbg04');
define('DB_USER', 'u874918252_dbg04');
define('DB_PASS', 'PONER_...');
define('API_KEY', 'PONER_...');
define('PANEL_CLAVE', 'PONER_...');
define('NODO_DEF', 'nodo1');


const SENSORES = ['ldr', 'dist', 'temp', 'rpm'];
const MIN_MS   = ['ldr' => 50, 'dist' => 60, 'temp' => 750, 'rpm' => 500];   // igual que el Mega

// constantes metrologicas (informe, Anexos B y C)
const PPR = 1;
const RF  = 270.0;
const U_N = 1.310;                                  // cuentas ADC
define('U_DAT_D', 3 / sqrt(3));
define('U_RF', 2.7 / sqrt(3));                      // tolerancia de Rf (ohm)
define('U_T', sqrt(0.0625 ** 2 + 0.5 ** 2) / sqrt(3));   // DS18B20: 0,291 C
define('U_RES_D', 0.344 / sqrt(3));                 // micros(), mm
const U_V_FIJA = 1.75;                              // m/s si no hay temperatura (T +-5 C)
define('U_T_MILLIS', 0.0078 / sqrt(3));             // base de tiempo millis()
