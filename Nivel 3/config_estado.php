<?php
require __DIR__ . '/lib.php';
$pdo = db();
$cols = "cmd_id, sensor, intervalo_ms, estado, motivo,
    CAST(UNIX_TIMESTAMP(creado_ts)*1000 AS UNSIGNED) AS creado,
    CAST(UNIX_TIMESTAMP(entregado_ts)*1000 AS UNSIGNED) AS entregado,
    CAST(UNIX_TIMESTAMP(ack_ts)*1000 AS UNSIGNED) AS ack_n2,
    CAST(UNIX_TIMESTAMP(ack_nube_ts)*1000 AS UNSIGNED) AS ack_nube";
$st = $pdo->prepare("SELECT $cols FROM comandos WHERE nodo=? ORDER BY creado_ts DESC LIMIT 8");
$st->execute([NODO_DEF]);
$cmds = $st->fetchAll();
$st = $pdo->prepare("SELECT $cols FROM comandos WHERE nodo=? AND estado='aplicado' ORDER BY ack_nube_ts DESC LIMIT 1");
$st->execute([NODO_DEF]);
$ult = $st->fetch() ?: null;
responder(['ultima' => $ult, 'comandos' => $cmds]);
