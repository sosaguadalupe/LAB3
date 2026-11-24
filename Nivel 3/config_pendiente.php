<?php
require __DIR__ . '/lib.php';
exigir_clave();
$nodo = $_GET['nodo'] ?? NODO_DEF;
$pdo = db();
$st = $pdo->prepare("SELECT cmd_id, sensor, intervalo_ms FROM comandos WHERE nodo=? AND estado='pendiente' ORDER BY creado_ts");
$st->execute([$nodo]);
$filas = $st->fetchAll();
foreach ($filas as &$f) { $f['intervalo_ms'] = (int)$f['intervalo_ms']; }
unset($f);
$pdo->prepare("UPDATE comandos SET entregado_ts=? WHERE nodo=? AND estado='pendiente' AND entregado_ts IS NULL")
    ->execute([ahora_utc(), $nodo]);
responder(['comandos' => $filas]);
