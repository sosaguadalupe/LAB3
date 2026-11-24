<?php
require __DIR__ . '/lib.php';
exigir_clave();
$p = leer_json();
$cmd = $p['cmd_id'] ?? null;
$nodo = $p['nodo'] ?? null;
if (!is_string($cmd) || $cmd === '' || !is_string($nodo) || $nodo === '') responder(['error' => 'cuerpo invalido'], 400);
$ackTs = isset($p['ts']) ? iso_a_utc((string)$p['ts']) : null;
$motivo = isset($p['motivo']) ? substr((string)$p['motivo'], 0, 64) : null;
db()->prepare('UPDATE comandos SET estado=?, ack_ts=?, ack_nube_ts=?, motivo=? WHERE cmd_id=? AND nodo=?')
    ->execute([!empty($p['aplicado']) ? 'aplicado' : 'rechazado', $ackTs, ahora_utc(), $motivo, $cmd, $nodo]);
responder(['ok' => true]);
