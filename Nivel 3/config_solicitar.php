<?php
// Lo llama el dashboard. Protegido con PANEL_CLAVE (no con API_KEY, que no debe viajar al navegador).
require __DIR__ . '/lib.php';
$p = leer_json();
if (!isset($p['clave']) || !is_string($p['clave']) || !hash_equals(PANEL_CLAVE, $p['clave'])) {
    usleep(400000);
    responder(['error' => 'clave del panel incorrecta'], 403);
}
$cambios = $p['cambios'] ?? null;
if (!is_array($cambios) || !$cambios) responder(['error' => 'sin cambios'], 400);

$nuevos = [];                                   // un solo valor por sensor (el ultimo)
foreach ($cambios as $c) {
    $s = $c['sensor'] ?? '';
    $ms = $c['intervalo_ms'] ?? null;
    if (!in_array($s, SENSORES, true) || !is_numeric($ms) || (int)$ms != $ms || $ms < MIN_MS[$s] || $ms > 65535) {
        responder(['error' => "$s: entero entre " . (MIN_MS[$s] ?? '?') . ' y 65535 ms'], 400);
    }
    $nuevos[$s] = (int)$ms;
}
$pdo = db();
$ahora = ahora_utc();
$creados = [];
foreach ($nuevos as $s => $ms) {
    $pdo->prepare("UPDATE comandos SET estado='reemplazado' WHERE nodo=? AND sensor=? AND estado='pendiente'")
        ->execute([NODO_DEF, $s]);
    $id = 'c' . (int)(microtime(true) * 1000) . $s[0];      // 15 caracteres (el ESP8266 admite 23)
    $pdo->prepare("INSERT INTO comandos (cmd_id,nodo,sensor,intervalo_ms,estado,creado_ts) VALUES (?,?,?,?, 'pendiente', ?)")
        ->execute([$id, NODO_DEF, $s, $ms, $ahora]);
    $creados[] = ['cmd_id' => $id, 'sensor' => $s, 'intervalo_ms' => $ms];
}
responder(['ok' => true, 'comandos' => $creados]);
