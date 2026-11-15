# Estabilización y Rediseño de Rendimiento en Supervivencia Densa

* **Author:** Fnava1
* **Date:** 2026-11-14

## Overview

Este documento establece el plan de rediseño técnico para erradicar las caídas severas de cuadros por segundo (FPS) y los micro-congelamientos durante partidas de alta densidad en el modo Supervivencia (Survival Endless) en la consola portátil Miyoo Mini Plus (SoC SigmaStar SSD202D, CPU Dual-Core ARM Cortex-A7 @ 1.2 GHz, 128 MB RAM, techo OOM kernel de 85 MB).

La solución ataca cuatro frentes críticos identificados en el motor:
1. Desactivación absoluta de la escritura síncrona a disco (`std::flush`) en el sistema de logging, eliminando la contención en el bus de la tarjeta microSD.
2. Rehidratación y precarga síncrona completa en la restauración de partidas guardadas (`LawnLoadGameV4` / `FixBoardAfterLoad`), garantizando en memoria RAM todas las definiciones `.reanim`, `.spc` y variantes calcinadas de zombies antes de reanudar el combate.
3. Despacho optimizado por `SeedType` para el estado de reposo de plantas en `Plant.cpp`, habilitando el uso inmediato de `SpriteCachePool` para la Tripitidora (`ThreePeater`) y el Lanzaguisantes Doble (`SplitPea`) sin búsquedas lineales de pistas de animación ni caídas al renderizador por software.
4. Muerte simplificada fuera del césped visible para entidades destruidas por explosiones (Petacereza) antes de cruzar la línea de juego (`GetZombieRect().mX >= 800`), previniendo picos de asignación de reanimaciones esqueléticas pesadas fuera de pantalla.

## Analysis

### Revisión Multi-Lente de Goldfishes (Hallazgos Críticos)

1. **Cuello de Botella de I/O en Almacenamiento (Lente Bajo Nivel & E/S):**
   * `Sexy::DispatchLogLn` en `Common.cpp` adquiere un mutex global y ejecuta `gLogFileSink << theText << '\n' << std::flush;`.
   * En la arquitectura de la consola, la tarjeta microSD opera bajo un bus SDIO compartido. Cada `std::flush` detiene la tubería de ejecución del Cortex-A7 esperando la sincronización física del controlador de bloques. Más de 700 líneas por segundo congelan el juego independientemente de la potencia de cálculo.
   * *Resolución:* Desactivar la escritura a archivo de texto en disco. Implementar un buffer circular estático en memoria RAM de 32 KB (`gMemLogRingBuffer`) que registre únicamente en memoria sin bloquear la CPU ni tocar la microSD durante el gameplay.

2. **Falla Estructural de Precarga en Partidas Guardadas (Lente Arquitectura & Estados):**
   * Al iniciar una partida fresca, `Board::StartLevel` invoca `PreloadSpriteCache()`. Sin embargo, al cargar una partida guardada (`LawnLoadGameV4`), el motor transiciona directamente a `GameScenes::SCENE_PLAYING` sin invocar `PreloadSpriteCache()` ni `PreloadResources()`.
   * Además, el `SpriteStreamer` lee secuencialmente de la tarjeta a razón de **1 solo fotograma por tick de juego** (24 ticks / 240 ms por entidad). Con 10 plantas y múltiples zombies, la cola demora más de 400 ticks (4 segundos) en transferir los sprites a la memoria, forzando la evaluación por CPU durante el combate.
   * *Resolución:* Ejecutar la precarga en bloque síncrona dentro de `FixBoardAfterLoad` mediante lecturas contiguas (`fread` de archivo completo al slot asignado), dejando todo listo antes del primer fotograma visible.

3. **Trampa de Desbordamiento de Slots en SpriteCachePool (Lente Integración & Riesgos):**
   * En `SpriteCacheDef.h`, la constante `SPC_MAX_PLANT_SLOTS` está fijada en **10**. No obstante, el arreglo de entidades `gPlantCacheEntities` contiene **11** plantas pre-horneadas, siendo la Tripitidora (`SEED_THREEPEATER`) la entidad #11.
   * Si en la partida conviven 10 plantas base, la asignación de la Tripitidora es rechazada silenciosamente en `AllocatePlantSlot`.
   * *Resolución:* Incrementar `SPC_MAX_PLANT_SLOTS` a **12** para garantizar espacio suficiente para todas las plantas cacheadas y sus variantes en partidas avanzadas.

4. **Escaneo de Plantas Existentes en el Tablero (Lente Casos Borde):**
   * `Board::PreloadSpriteCache()` inspeccionaba históricamente solo `mSeedBank->mSeedPackets`. En Supervivencia Infinita, el jugador suele llevar semillas utilitarias en el banco (Setas Congeladas, Calabazas, Granos de Café), mientras que en el césped ya están sembradas plantas mejoradas complejas (Mazorcañon, Pinchorroca, Tripitidora).
   * *Resolución:* `FixBoardAfterLoad` debe recorrer tanto `theBoard->mPlants` como `theBoard->mSeedBank->mSeedPackets` para capturar todas las plantas activas en el mapa.

5. **Corte Visual en Muerte Fuera de Pantalla (Lente Experiencia & Pulido):**
   * Vehículos como el Zomboni tienen dimensiones anchas (153x140 px). Evaluar ciegamente `mPosX >= 800.0f` provocaría que un Zomboni cuya parte delantera ya asoma en el pixel 740 desaparezca abruptamente si su origen está en 810.
   * *Resolución:* La condición de simplificación debe validar `GetZombieRect().mX >= BOARD_WIDTH` (800) o `mPosX >= 835.0f` para garantizar que la entidad esté 100% fuera del campo de visión antes de aplicar la eliminación ligera con partícula de humo.

## Goals vs. Non-Goals

### In-Scope Goals
* **G1:** Desactivar la escritura a archivo en disco en `DispatchLogLn`, reemplazándola por un buffer circular estático en RAM de 32 KB con volcado exclusivo ante fallos críticos (`SIGSEGV`, `SIGABRT`).
* **G2:** Rehidratar de forma síncrona `PreloadResources()` y `PreloadSpriteCache()` en `FixBoardAfterLoad`, asegurando la carga contigua en memoria de zombies permitidos, sus animaciones de calcinado (`Zombie_charred_*`) y todas las plantas del mapa.
* **G3:** Elevar `SPC_MAX_PLANT_SLOTS` de 10 a 12 en `SpriteCacheDef.h`.
* **G4:** Refactorizar `Plant::UpdateReanim` y `Plant::Draw` centralizando la condición de caché en el método unificado `bool Plant::CanUseSpriteCache() const`, despachando por `mSeedType` sin comparaciones de cadenas lineales.
* **G5:** Implementar muerte simplificada en `Zombie::ApplyBurn` cuando `GetZombieRect().mX >= BOARD_WIDTH`, emitiendo `PARTICLE_POWDER_SMOKE` y llamando `DieWithLoot()` sin alocar reanimaciones calcinadas pesadas ni alterar `mActiveCharredCount`.

### Out-of-Scope Non-Goals
* **NG1:** No se integrarán vehículos (`Zomboni`, `Catapult`) en `SpriteCachePool` como `.spc`: su baja densidad de aparición (1 a 3 a la vez) y sus estados de daño y animación de conducción no justifican comprometer los 8 MB del pool de zombies.
* **NG2:** No se alterará el límite global de memoria virtual de 14 MB ni el sistema de descarte de páginas con `madvise(MADV_DONTNEED)`, que ya opera correctamente al vaciar oleadas.
* **NG3:** No se crearán hilos secundarios de I/O en Core 1 para evitar contención con el mezclador de audio de libopenmpt.

## Alternatives Weighed

* **Buffer asíncrono con hilo worker de logging:** Rechazado. El Core 1 del SoC SSD202D está asignado al hilo de audio SDL/libopenmpt. La creación de un hilo worker introduce contención de mutex/futex y arriesga chasquidos en el sonido. Se adoptó el buffer circular en RAM sin hilos.
* **Hornear Zomboni en SpriteCache (.spc):** Rechazado. Un frame de vehículo de 180x150 px ocupa ~1.3 MB en el pool de 8 MB. Dada su baja frecuencia y múltiples capas de daño en chasis, la relación costo-beneficio en memoria es negativa. La precarga en RAM de su `.reanim` es suficiente para garantizar 60 FPS sin tirones.
* **Comprobación de `mPosX >= 800.0f` simple para muerte off-screen:** Rechazado. Provoca desaparición visual abrupta de vehículos anchos que ya muestran el frontal en pantalla. Se adoptó `GetZombieRect().mX >= BOARD_WIDTH`.
* **Streaming dinámico perezoso en partidas cargadas:** Rechazado. Distribuir la lectura a 1 frame por tick durante el combate mantiene a las entidades en fallback de software por más de 2 segundos. Se adoptó la carga masiva en bloque durante el paso de restauración.

## Key Decisions & Staging for Next Steps

1. **Decisión de Arquitectura de Memoria:** El incremento de `SPC_MAX_PLANT_SLOTS` a 12 añade únicamente 2 estructuras `SpriteCacheSlot` (~80 bytes de metadata), manteniéndose perfectamente dentro del presupuesto de 4 MB de la zona de plantas.
2. **Decisión de Mapeo de Plantas:** En `FixBoardAfterLoad`, se iterará un `std::set<SeedType>` que consolidará tanto las plantas vivas en el césped como las semillas del banco de cartas para ejecutar una precarga completa y sin duplicados.
3. **Decisión de Desacople de Animación:** En `Reanimation::IsAnimPlaying`, se verificará `TrackExists` antes de calcular el índice para evitar que consultas booleanas generen trazas de error.
4. **Paso Siguiente:** Someter este brief al proceso formal de diseño con `/egm-draft` o aprobación directa para ejecución.
