# Plan de Optimización de Rendimiento Bruto en Partidas Densas (Miyoo Mini Plus)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (native mode requested by user) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maximizar los FPS y eliminar los tirones en partidas de alta densidad (60–100 zombies y 40–45 plantas, como oleadas finales de Supervivencia Infinita) en la Miyoo Mini Plus reduciendo drásticamente el coste de CPU por entidad sin degradar la fluidez visual ni provocar desincronización de físicas.

**Architecture:** Se atacan los 5 cuellos de botella fundamentales del procesador SigmaStar SSD1955 (ARM Cortex-A7 mono-núcleo 1.2 GHz con 32KB L1/128KB L2):
1. **LOD Esquelético Multi-Rate (Checkerboard para Plantas):** División en tablero de ajedrez `(col + row + frame) % 2` para alternar la evaluación de huesos a 30 Hz manteniendo lógica a 60 Hz.
2. **LOD Esquelético de Horda para Zombies ($N > 25$):** Zombies tras la vanguardia interpolan su posición $X$ suavemente a 60 Hz mientras su ciclo esquelético interno camina a 30 Hz.
3. **Throttling y Tope de Partículas de Impacto (Hit Splats):** Capping de emisores concurrentes de `PARTICLE_PEA_SPLAT` por zombie (máx. 1-2 activos), eliminando la explosión cuadrática de partículas alpha-blended superpuestas.
4. **Particionamiento Espacial por Fila para Plantas (`mRowPlants` y `mRowHasPlantsMask`):** Eliminación del escaneo lineal de 45 plantas en cada tick de cada zombie en `FindPlantTarget` y `FindZombieTarget`.
5. **Culling de Sombras de Proyectiles en Celdas Saturadas:** Omisión de sombras pequeñas de proyectiles cuando sobrevuelan aglomeraciones densas de zombies donde la sombra es invisible.

**Tech Stack:** C++17, RetroArch/Linux ARMv7 (SigmaStar SSD1955, Cortex-A7), Docker ARM toolchain (`arm-linux-gnueabihf-g++`).

**Spec:** Análisis técnico de saturación de CPU y rasterizador de software en Miyoo Mini Plus derivado de reportes de partida densa.

## Global Constraints
- **Fluidez Visual 100% Garantizada:** El desplazamiento espacial ($X, Y$), la detección de colisiones, el daño y los proyectiles deben seguir ejecutándose a 60 Hz continuos; no debe haber congelamientos ni stuttering perceptual.
- **Cero Desincronización de Físicas:** El daño de proyectiles, la ingesta de zombies y las habilidades de plantas no pueden alterarse.
- **Compatibilidad con Guardado/Carga:** No se altera la estructura serializada de `SaveGame.cpp`.
- **Límite Estricto de Memoria:** El consumo de RAM debe permanecer estrictamente $\le 85\text{ MB}$.
- **Gestión de Procesos:** Ningún script de Python ni proceso de compilación quedará huérfano en segundo plano.

## Review Focus
1. **Zombies de vanguardia no deben perder fluidez:** El primer zombie que recibe disparos en una fila debe evaluarse siempre a 60 Hz completo para que su animación de impacto sea nítida.
2. **Plantas con animaciones reactivas (Squash, Chomper, Cob Cannon, Potato Mine):** Plantas ejecutando habilidades especiales activas deben ignorar el checkerboard y animar a 60 Hz de inmediato.
3. **Colisiones de zombies no deben ignorar plantas recién puestas:** Cuando se planta una flor o nuez en una celda, `mRowPlants` y `mRowHasPlantsMask` deben actualizarse de inmediato.
4. **Zombies controlados mentalmente (Hypnoshroom):** La búsqueda en fila no debe confundir zombies amigos con enemigos.
5. **Partículas de impacto:** Capping no debe impedir que se reproduzca el sonido de impacto ni el conteo de daño.

---

## Tareas de Implementación

### Task 1: Particionamiento Espacial por Fila para Plantas (`mRowPlants` y `mRowHasPlantsMask`)

**Files:**
- Modify: `src/Lawn/Board.h` (declaración de `mRowPlants` y `mRowHasPlantsMask`)
- Modify: `src/Lawn/Board.cpp` (mantenimiento en adición/eliminación de plantas)
- Modify: `src/Lawn/Zombie.cpp` (`FindPlantTarget` y `FindZombieTarget`)

**Interfaces:**
- Consumes: `mPlants` DataArray en `src/Lawn/Board.h`.
- Produces: `mRowPlants[MAX_GRID_SIZE_Y]` y `mRowHasPlantsMask` utilizados por `Zombie::FindPlantTarget()` y `Zombie::FindZombieTarget()`.

- [ ] **Step 1: Declarar estructuras en `Board.h`**
  Agregar en `Board`:
  ```cpp
  std::vector<Plant*> mRowPlants[MAX_GRID_SIZE_Y];
  uint32_t mRowHasPlantsMask = 0;
  void RebuildRowPlants();
  ```

- [ ] **Step 2: Implementar mantenimiento en `Board.cpp`**
  En `Board::RebuildRowPlants()`:
  - Limpiar vectores de cada fila y `mRowHasPlantsMask = 0`.
  - Recorrer `mPlants` y rellenar `mRowPlants[aPlant->mRow].push_back(aPlant)` activando `mRowHasPlantsMask |= (1U << aPlant->mRow)`.
  - Llamar a `RebuildRowPlants()` en `Board::NewPlant()`, `Board::RemovePlant()`, y cuando cambie el estado de juego.

- [ ] **Step 3: Optimizar `Zombie::FindPlantTarget` en `Zombie.cpp`**
  En `src/Lawn/Zombie.cpp:L6455`:
  - Comprobar si `(mBoard->mRowHasPlantsMask & (1U << mRow)) == 0`. De ser así, retornar `nullptr` de inmediato (0 iteraciones).
  - En lugar de iterar `for (Plant* aPlant : mBoard->mPlants)`, iterar únicamente `for (Plant* aPlant : mBoard->mRowPlants[mRow])`.

- [ ] **Step 4: Optimizar `Zombie::FindZombieTarget` en `Zombie.cpp`**
  En `src/Lawn/Zombie.cpp:L6476`:
  - Iterar únicamente `mBoard->mRowZombies[mRow]` en lugar del array global `mBoard->mZombies`.

- [ ] **Step 5: Verificación y Compilación**
  Ejecutar compilación en contenedor Docker ARM y confirmar 0 errores.
  ```bash
  docker run --rm -v "c:\DEV\PVZ:/work" -w /work ghcr.io/mitchcurtis/retroarch-cross-compile-armhf:latest make -j4
  ```

---

### Task 2: Tope y Agrupación de Emisores de Partículas de Impacto (Hit Splat Capping)

**Files:**
- Modify: `src/Lawn/Zombie.h`
- Modify: `src/Lawn/Zombie.cpp`
- Modify: `src/Lawn/Projectile.cpp`

**Interfaces:**
- Consumes: `Zombie::AddAttachedParticle` y `Projectile::DoImpact`.
- Produces: `mActiveSplatParticlesCount` y limitador de saturación en zombies.

- [ ] **Step 1: Agregar contador de partículas de impacto en `Zombie.h`**
  En `src/Lawn/Zombie.h`:
  ```cpp
  int mLastSplatTick = -1;
  int mSplatCountInTick = 0;
  bool CanAttachHitSplat();
  ```

- [ ] **Step 2: Implementar lógica de throttling en `Zombie.cpp`**
  En `src/Lawn/Zombie.cpp`:
  - Si el tick actual coincide con `mLastSplatTick`, incrementar `mSplatCountInTick`. Si `mSplatCountInTick >= 2`, omitir la creación de un nuevo emisor de partículas repetitivo.
  - Si es un nuevo tick, reiniciar `mSplatCountInTick = 1` y actualizar `mLastSplatTick`.

- [ ] **Step 3: Conectar en `Projectile::DoImpact` en `Projectile.cpp`**
  En `src/Lawn/Projectile.cpp:L933-L959`:
  - Antes de invocar `theZombie->AddAttachedParticle(aPosX, aPosY, aEffect)`, verificar `theZombie->CanAttachHitSplat()`.
  - El daño a la salud del zombie y el destello de disparo (`mJustGotShotCounter`) se aplican al 100% de los impactos sin omitir ninguno.

- [ ] **Step 4: Verificación y Compilación**
  Compilar binario ARM en Docker y comprobar ausencia de regresiones sintácticas.

---

### Task 3: Desacoplamiento Esquelético Multi-Rate para Plantas (Checkerboard Idle 30 Hz)

**Files:**
- Modify: `src/Lawn/Plant.h`
- Modify: `src/Lawn/Plant.cpp`

**Interfaces:**
- Consumes: `Plant::UpdateReanim()` y ciclo `aBodyReanim->Update()`.
- Produces: Evaluación a 30 Hz de huesos en reposo con lógica completa a 60 Hz.

- [ ] **Step 1: Refinar la condición de omisión de esqueleto en `Plant::UpdateReanim`**
  En `src/Lawn/Plant.cpp:L2814-L2826`:
  - Cuando la partida supera 15 plantas o 15 zombies, las plantas en estado idle (`anim_idle`) que no estén sufriendo acciones prioritarias (masticadas, despertando o ejecutando especiales) alternan la actualización de su esqueleto:
    `bool updateBones = ((mPlantCol + mRow + mBoard->mBoardUpdateCounter) & 1) == 0;`
  - Si `!updateBones`:
    Avanzar únicamente el tiempo del loop (`aBodyReanim->UpdateAnimationTimeOnly()`) para no acumular desfasaje, pero omitir el cálculo completo de matrices de pistas internas (`aBodyReanim->Update()` de pistas complejas).

- [ ] **Step 2: Proteger plantas activas y ataques especiales**
  Asegurar que plantas en animación de disparo activo (como Gatling Pea ráfaga, Squash cayendo, Doom-shroom explotando, Chomper tragando) omitan el checkerboard y ejecuten a 60 Hz completo.

- [ ] **Step 3: Verificación y Compilación**
  Compilar en Docker ARM y verificar integridad del ejecutable.

---

### Task 4: LOD Esquelético de Horda para Zombies ($N > 25$)

**Files:**
- Modify: `src/Lawn/Zombie.h`
- Modify: `src/Lawn/Zombie.cpp`
- Modify: `src/Lawn/Board.cpp`

**Interfaces:**
- Consumes: `mBoard->mRowZombies[r]` y posición $X$ de cada zombie.
- Produces: Identificación de zombies de retaguardia para reducción a 30 Hz de matriz ósea.

- [ ] **Step 1: Marcar líderes de vanguardia por fila en `Board.cpp`**
  En `Board::UpdateGame()`:
  - En cada fila `r`: ordenar o marcar los 2 zombies con menor coordenada $X$ (los líderes de vanguardia que avanzan hacia las plantas o reciben proyectiles).
  - Establecer `aZombie->mIsHordeVanguard = true`. Los zombies restantes en la fila con $N_{fila} > 4$ y $N_{total} > 25$ reciben `mIsHordeVanguard = false`.

- [ ] **Step 2: Sub-stepping a 30 Hz de animación en zombies de retaguardia**
  En `src/Lawn/Zombie.cpp:L5283-L5397` (`Zombie::UpdateReanim`):
  - Si `!mIsHordeVanguard` y el zombie está simplemente caminando:
    - En ticks impares de su ID (`(mBoard->mBoardUpdateCounter + mDataArrayId) & 1`): avanzar `aBodyReanim->UpdateAnimationTimeOnly()` y reutilizar la matriz previa desplazada por su movimiento $X$.
    - En ticks pares: realizar `aBodyReanim->Update()` completo.
  - La posición física en el césped (`mPosX`, `mX`) avanza a **60 Hz** cada frame sin interrupción.

- [ ] **Step 3: Excepciones de seguridad**
  Zombies especiales (Gargantuar atacando, Bungee descendiendo, Zombis en trampolín/pogo, Zombis muriendo o congelados) siempre ejecutan actualización completa a 60 Hz.

- [ ] **Step 4: Verificación y Compilación**
  Compilar y verificar que no haya problemas de sincronización de extremidades o clipping.

---

### Task 5: Culling de Sombras de Proyectiles en Celdas Saturadas

**Files:**
- Modify: `src/Lawn/Projectile.cpp`

**Interfaces:**
- Consumes: Conteo de zombies por celda en `mBoard->mRowZombies[mRow]`.
- Produces: Supresión de dibujo de sombra individual cuando cae sobre aglomeraciones densas.

- [ ] **Step 1: Comprobar saturación en `Projectile::DrawShadow`**
  En `Projectile::DrawShadow`:
  - Si en la fila `mRow` y columna actual hay 3 o más zombies solapados, la sombra del proyectil (guisante, col, estrella) no se dibuja.
  - Ahorro directo en fillrate y operaciones de lectura-modificación-escritura en framebuffer.

- [ ] **Step 2: Verificación y Compilación**
  Compilar en Docker ARM y probar.

---

### Task 6: Empaquetado, Reducción de Binario y Verificación Final de Memoria

**Files:**
- Modify: `build/PvZ` (binario compilado)
- Update: `bin/PvZ`, `ports/PvZ/PvZ`, `packaging/PvZ/PvZ`
- Generate: `dist/PvZ-MiyooMiniPlus.zip` y `dist/PvZ-RetroArch-ARM.zip`

- [ ] **Step 1: Compilación limpia en Docker ARM**
  Ejecutar `make -j4` en el contenedor `retroarch-cross-compile-armhf`.

- [ ] **Step 2: Strip de símbolos y verificación de tamaño**
  Ejecutar `arm-linux-gnueabihf-strip build/PvZ` asegurando que el tamaño del binario sea $\sim 5.4\text{ MB}$.

- [ ] **Step 3: Staging y generación de zips**
  Copiar a directorios de distribución y generar paquetes zip en `dist/`.

- [ ] **Step 4: Auditoría de memoria RAM**
  Verificar que el binario y los pools de memoria continúen en $\le 85\text{ MB}$.

- [ ] **Step 5: Reconstrucción del grafo de contexto Trail**
  Ejecutar `trail build` deterministicamente.
