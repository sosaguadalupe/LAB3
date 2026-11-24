<?php
require __DIR__ . '/config.php';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

function responder($d, int $code = 200): void {
    http_response_code($code);
    echo json_encode($d, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function esquema(PDO $pdo): void {
    $pdo->exec("CREATE TABLE IF NOT EXISTS muestras (
        nodo VARCHAR(20) NOT NULL, sensor VARCHAR(8) NOT NULL, ts DATETIME(3) NOT NULL,
        valor DOUBLE NULL, ok TINYINT NOT NULL, intervalo_ms INT NULL, seq INT NULL,
        recibido_ts DATETIME(3) NOT NULL,
        PRIMARY KEY (nodo, sensor, ts)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    $pdo->exec("CREATE TABLE IF NOT EXISTS comandos (
        cmd_id VARCHAR(32) NOT NULL PRIMARY KEY, nodo VARCHAR(20) NOT NULL, sensor VARCHAR(8) NOT NULL,
        intervalo_ms INT NOT NULL, estado VARCHAR(12) NOT NULL,
        creado_ts DATETIME(3) NOT NULL, entregado_ts DATETIME(3) NULL, ack_ts DATETIME(3) NULL,
        ack_nube_ts DATETIME(3) NULL, motivo VARCHAR(64) NULL,
        KEY ix_c (nodo, estado, creado_ts)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

function db(): PDO {
    static $pdo = null;
    if ($pdo) return $pdo;
    try {
        $pdo = new PDO('mysql:host=' . DB_HOST . ';dbname=' . DB_NAME . ';charset=utf8mb4', DB_USER, DB_PASS,
            [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
        $pdo->exec("SET time_zone = '+00:00'");   // todo se guarda y lee en UTC
        esquema($pdo);
    } catch (Throwable $e) {
        responder(['error' => 'bd'], 500);
    }
    return $pdo;
}

function exigir_clave(): void {
    $k = $_SERVER['HTTP_X_API_KEY'] ?? '';
    if ($k === '' && function_exists('getallheaders')) {
        foreach (getallheaders() as $n => $v) { if (strtolower($n) === 'x-api-key') $k = $v; }
    }
    if ($k === '' || !hash_equals(API_KEY, $k)) responder(['error' => 'api key invalida'], 401);
}

function leer_json(): array {
    $p = json_decode(file_get_contents('php://input'), true);
    if (!is_array($p)) responder(['error' => 'cuerpo invalido'], 400);
    return $p;
}

// ISO 8601 (con zona) -> 'Y-m-d H:i:s.v' en UTC; null si no se puede interpretar
function iso_a_utc(string $s): ?string {
    try {
        return (new DateTimeImmutable($s))->setTimezone(new DateTimeZone('UTC'))->format('Y-m-d H:i:s.v');
    } catch (Throwable $e) {
        return null;
    }
}

function ahora_utc(): string {
    return (new DateTimeImmutable('now', new DateTimeZone('UTC')))->format('Y-m-d H:i:s.v');
}

// Valor de la grafica y incertidumbre expandida U (k = 2), desde datos crudos
function banda(string $s, float $v, $iv, $T): array {
    if ($s === 'temp') return [$v, 2 * U_T];
    if ($s === 'ldr') {            // R = Rf (1024/N - 1), propagacion en cuadratura (ec. 9)
        $n = max($v, 1.0);
        $r = RF * (1024 / $n - 1);
        $uc = sqrt(pow(RF * 1024 / ($n * $n) * U_N, 2) + pow((1024 / $n - 1) * U_RF, 2));
        return [$r / 1000, 2 * $uc / 1000];
    }
    if ($s === 'dist') {           // d = v t / 2, v = 331,3 + 0,606 T
        if ($T !== null) { $vs = 331.3 + 0.606 * $T; $uv = 0.606 * U_T; }
        else             { $vs = 344.4;              $uv = U_V_FIJA; }
          $uc = sqrt(pow(U_RES_D, 2) + pow(U_DAT_D, 2) + pow($v * $uv / $vs, 2));
        return [$v, 2 * $uc];
    }
    $dr = 60000.0 / (PPR * max((float)$iv, 1.0));          // resolucion segun el intervalo
    $uc = sqrt(pow($dr / sqrt(3), 2) + pow($v * U_T_MILLIS, 2));
    return [$v, 2 * $uc];
}
