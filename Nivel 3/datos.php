<?php
// GET datos.php?sensor=temp&desde=<ISO UTC>&hasta=<ISO UTC>[&max=1500]
// Devuelve {t:[epoch ms], y:[valor], u:[U k=2]}; diezma uniformemente si hay mas de `max` puntos.
require __DIR__ . '/lib.php';
$s = $_GET['sensor'] ?? '';
if (!in_array($s, SENSORES, true)) responder(['error' => 'sensor invalido'], 400);
$desde = iso_a_utc($_GET['desde'] ?? '');
$hasta = iso_a_utc($_GET['hasta'] ?? '');
if ($desde === null || $hasta === null) responder(['error' => 'rango invalido'], 400);
$max = min(5000, max(100, (int)($_GET['max'] ?? 1500)));

$pdo = db();
$cond = "nodo=? AND sensor=? AND ok=1 AND valor IS NOT NULL AND ts>=? AND ts<?";
$st = $pdo->prepare("SELECT COUNT(*) FROM muestras WHERE $cond");
$st->execute([NODO_DEF, $s, $desde, $hasta]);
$n = (int)$st->fetchColumn();
$paso = max(1, (int)ceil($n / $max));

// el SRF04 usa la temperatura mas cercana (<= 120 s) para estimar v
$Tt = []; $Tv = [];
$pdo->setAttribute(PDO::MYSQL_ATTR_USE_BUFFERED_QUERY, false);   // recorre sin cargar todo en memoria
if ($s === 'dist') {
    $q = $pdo->prepare("SELECT CAST(UNIX_TIMESTAMP(ts)*1000 AS UNSIGNED) AS t, valor FROM muestras WHERE $cond ORDER BY ts");
    $q->execute([NODO_DEF, 'temp', $desde, $hasta]);
    while ($r = $q->fetch()) { $Tt[] = (int)$r['t']; $Tv[] = (float)$r['valor']; }
    $q->closeCursor();
}
function t_cercana(int $t, array $Tt, array $Tv): ?float {
    $n = count($Tt);
    if (!$n) return null;
    $lo = 0; $hi = $n - 1;
    while ($lo < $hi) { $mid = intdiv($lo + $hi, 2); if ($Tt[$mid] < $t) $lo = $mid + 1; else $hi = $mid; }
    $b = $lo;
    if ($lo > 0 && abs($Tt[$lo - 1] - $t) <= abs($Tt[$lo] - $t)) $b = $lo - 1;
    return abs($Tt[$b] - $t) <= 120000 ? $Tv[$b] : null;
}

$q = $pdo->prepare("SELECT CAST(UNIX_TIMESTAMP(ts)*1000 AS UNSIGNED) AS t, valor, intervalo_ms FROM muestras WHERE $cond ORDER BY ts");
$q->execute([NODO_DEF, $s, $desde, $hasta]);
$T = []; $Y = []; $U = [];
$i = 0;
while ($r = $q->fetch()) {
    if (($i++ % $paso) !== 0) continue;
    $t = (int)$r['t'];
    [$y, $u] = banda($s, (float)$r['valor'], $r['intervalo_ms'], $s === 'dist' ? t_cercana($t, $Tt, $Tv) : null);
    $T[] = $t; $Y[] = round($y, 3); $U[] = round($u, 3);
}
$q->closeCursor();
responder(['sensor' => $s, 'n' => $n, 'paso' => $paso, 't' => $T, 'y' => $Y, 'u' => $U]);
