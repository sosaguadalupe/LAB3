<?php
require __DIR__ . '/lib.php';
exigir_clave();
$p = leer_json();
$nodo = $p['nodo'] ?? null;
$ms = $p['muestras'] ?? null;
if (!is_string($nodo) || $nodo === '' || !is_array($ms)) responder(['error' => 'cuerpo invalido'], 400);

$pdo = db();
try {
    $st = $pdo->prepare('INSERT IGNORE INTO muestras (nodo,sensor,ts,valor,ok,intervalo_ms,seq,recibido_ts) VALUES (?,?,?,?,?,?,?,?)');
    $rec = ahora_utc();
    $n = 0;
    $pdo->beginTransaction();
    foreach ($ms as $m) {
        if (!is_array($m)) continue;
        $s = $m['sensor'] ?? '';
        $ts = isset($m['ts']) ? iso_a_utc((string)$m['ts']) : null;
        if (!in_array($s, SENSORES, true) || $ts === null) continue;
        $valor = (isset($m['valor']) && is_numeric($m['valor'])) ? (float)$m['valor'] : null;
        $ok = (array_key_exists('ok', $m) && !$m['ok']) ? 0 : 1;
        $iv = isset($m['intervalo_ms']) ? (int)$m['intervalo_ms'] : null;
        $sq = isset($m['seq']) ? (int)$m['seq'] : null;
        $st->execute([$nodo, $s, $ts, $valor, $ok, $iv, $sq, $rec]);   // INSERT IGNORE: un reenvio no duplica
        $n++;
    }
    $pdo->commit();
} catch (Throwable $e) {
    if ($pdo->inTransaction()) $pdo->rollBack();
    responder(['error' => 'bd'], 500);       // nivel2.py reintenta en el proximo ciclo
}
responder(['recibidas' => $n]);
