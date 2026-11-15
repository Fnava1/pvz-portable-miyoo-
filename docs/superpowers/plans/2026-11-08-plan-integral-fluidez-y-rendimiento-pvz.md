# Plan Integral de Fluidez, Pacing y Rendimiento Máximo — PvZ Miyoo Mini Plus

> **Para desarrolladores:** Plan integral de optimización de frametime, sincronización VSync y rendimiento de rasterización. Diseñado para eliminar al 100% el comportamiento "entrecortado" (*judder/microstuttering*) y las caídas de fotogramas tanto en escenarios vacíos (Zomboss Revenge con 1 zombie) como en escenarios densos (jardín lleno de Tripitidoras).

**Meta:** Lograr 60 FPS estables, con frametime plano de 16.6 ms, cero tirones de pacing de ticks y cero temblores en cabezas de plantas/zombies en la Miyoo Mini Plus.

**Arquitectura:**
1. Desactivación de la invalidación accidental de fondo estático a 100 Hz en `Board.cpp` (ahorro de 192 MB/s de ancho de banda DRAM).
2. Pacing de VSync de hardware a 60 Hz en `SexyAppBase.cpp` para alinear la simulación con el refresco de pantalla y eliminar la alternancia de 1-tick / 2-ticks.
3. Restauración íntegra de `Update()` esquelético para garantizar sincronización perfecta de matrices en partes anexadas (cabezas de Tripitidoras, sombreros y brazos de zombies).
4. Caché de sprites animados multi-frame (16 fases) en `ReanimatorCache` para plantas multicabezas (`SEED_THREEPEATER`, `SEED_SPLITPEA`, `SEED_REPEATER`, `SEED_GATLINGPEA`), reduciendo el renderizado de 2,550 triángulos a 25 blits rápidos (aceleración de 125×).

**Pila Tecnológica:** C++20, SDL2, ARM NEON, Docker toolchain (`arm-linux-gnueabihf`), Miyoo Mini Plus (Linux 4.9, Cortex-A7 @ 1.2 GHz, 128 MB DRAM).

---

## Restricciones Globales
- **Límite de Memoria RAM:** Consumo máximo $\le 85\text{ MB}$ (actualmente en 78 MB, holgura de $\sim 7\text{ MB}$).
- **Fidelidad Visual:** Cero regresiones gráficas, balanceo de plantas 100% natural, animaciones de disparo y retroceso intactas.
- **Procesos Huérfanos:** Cero procesos de Python o tareas en segundo plano abandonadas.
- **Compatibilidad:** Miyoo Mini / Miyoo Mini Plus (OnionOS / retroarch ports).

---

## Fases de Implementación

### Tarea 1: Eliminar el Bucle de Invalidación de Fondo a 100 Hz en `Board.cpp`

**Archivos:**
- Modificar: `src/Lawn/Board.cpp`

**Contexto:**
En `Board::UpdateRowPlantMask()`, la asignación `mStaticBackdropValid = false;` se ejecutaba en cada tick de juego (100 Hz), provocando que `UpdateStaticBackdrop()` copiara por software la imagen completa del fondo (800x600, 1.92 MB) **100 veces por segundo**. Esto consumía 192 MB/s y entre 5 y 8 ms de CPU por tick, asfixiando incluso a Zomboss Revenge con 1 solo zombie.

**Pasos:**
- [ ] Eliminar `mStaticBackdropValid = false;` de `Board::UpdateRowPlantMask()`.
- [ ] Asegurar que `mStaticBackdropValid = false;` solo se invoque cuando una planta sea plantada (`Board::AddPlant`), destruida (`Plant::Die`), o removida con pala.
- [ ] En `Board::UpdateGame()`, verificar que `UpdateStaticBackdrop()` se ejecute una sola vez al entrar al nivel.

---

### Tarea 2: Sincronización de Hardware VSync (60 Hz) y Eliminación de Judder en `SexyAppBase.cpp`

**Archivos:**
- Modificar: `src/SexyAppFramework/SexyAppBase.cpp`

**Contexto:**
Al estar `mVSyncUpdates = false`, `mWaitForVSync = false` y `mIsPhysWindowed = true`, el motor creía que corría en modo ventana sin VSync a 100 Hz. Mientras tanto, SDL2 bloqueaba en hardware en los 60 Hz de la pantalla (16.6 ms). La deuda de tiempo acumulada provocaba que el motor alternara entre 1 tick y 2 ticks de simulación en cada cuadro visual, haciendo que los zombies, plantas y la cinta transportadora avanzaran a saltos ("entrecortado").

**Pasos:**
- [ ] En el constructor de `SexyAppBase`:
  - `mVSyncUpdates = true;`
  - `mWaitForVSync = true;`
  - `mIsPhysWindowed = false;` (la Miyoo corre en framebuffer nativo a pantalla completa).
- [ ] En `SexyAppBase::Init()`:
  - Confirmar que `mSyncRefreshRate = 60;` se inicialice con el valor de `mGLInterface->mRefreshRate`.
- [ ] En `SexyAppBase::Process()`:
  - Cuando `isVSynched` esté activo, asegurar que `aFrameFTime = 1000.0 / 60.0 = 16.666 ms`.
  - Acotar el acumulador `mPendingUpdatesAcc`: si supera 1.8, clampear a 1.8 para evitar que transiciones o picos de carga acumulen más de 1 tick de recuperación en un solo cuadro.
  - Asegurar que `DrawDirtyStuff()` se ejecute puntualmente en cada refresco vertical (60 FPS continuos).

---

### Tarea 3: Revertir Skips Esqueléticos que Rompen los Attachments en `Plant.cpp` y `Zombie.cpp`

**Archivos:**
- Modificar: `src/Lawn/Plant.cpp`
- Modificar: `src/Lawn/Zombie.cpp`

**Contexto:**
Saltar `Update()` en favor de `UpdateAnimationTimeOnly()` en frames alternos impedía que `AttachmentUpdateAndSetMatrix` calculara la posición de las cabezas de las Tripitidoras (3 cabezas anexas) y los sombreros/brazos de los zombies, congelándolos en frames alternos y haciéndolos temblar a 30 Hz.

**Pasos:**
- [ ] En `Plant::UpdateReanim()`: Revertir la bifurcación de `skipPlantBoneUpdate`. Llamar siempre a `aBodyReanim->Update()`.
- [ ] En `Zombie::UpdateReanim()`: Revertir la bifurcación de `skipHordeBoneUpdate`. Llamar siempre a `aBodyReanim->Update(); aBodyReanim->PropogateColorToAttachments();`.
- [ ] Verificar que `Reanimation::Update()` ejecuta únicamente lógica ligera de matrices (coste total < 0.02 ms para 30 entidades).

---

### Tarea 4: Caché de Sprites Animados Multi-Frame (16 fases) para Plantas Multicabezas

**Archivos:**
- Modificar: `src/Lawn/System/ReanimationLawn.h`
- Modificar: `src/Lawn/System/ReanimationLawn.cpp`
- Modificar: `src/Lawn/Plant.cpp`

**Contexto:**
25 Tripitidoras representan 100 animaciones esqueléticas, 1,275 pistas y 2,550 software triangles por cuadro (25-30 ms de CPU). Para plantas en reposo (`anim_idle`), pre-renderizamos 16 fases del ciclo de balanceo en memoria RAM.

**Pasos:**
- [ ] En `ReanimatorCache`:
  - Declarar `constexpr int NUM_CACHED_IDLE_FRAMES = 16;`.
  - Añadir estructura `std::array<std::vector<std::unique_ptr<MemoryImage>>, SeedType::NUM_SEED_TYPES> mPlantIdleFrames;`.
  - Implementar `MakeCachedPlantIdleFrame(SeedType theSeedType, float theAnimTime)`: ensambla el cuerpo con todas sus cabezas anexadas en la fase `theAnimTime` y lo dibuja en un `MemoryImage` de 120x120.
  - Implementar `DrawCachedPlantIdle(Graphics* g, float thePosX, float thePosY, SeedType theSeedType, float theAnimTime)`.
  - Pre-hornear las 16 fases de las plantas más pesadas: `SEED_THREEPEATER`, `SEED_SPLITPEA`, `SEED_REPEATER`, `SEED_GATLINGPEA`. Coste en RAM: $\sim 3.6\text{ MB}$.
- [ ] En `Plant::Draw()`:
  - Evaluar si la planta está en balanceo normal:
    `mShootingCounter == 0 && mState != PlantState::STATE_DOINGSPECIAL && mRecentlyEatenCountdown == 0 && mEatenFlashCountdown == 0 && !mSquished && aBodyReanim->IsAnimPlaying("anim_idle")`.
  - Si cumple y la planta tiene caché idle (`mApp->mReanimatorCache->HasCachedIdle(mSeedType)`):
    Dibujar mediante `DrawCachedPlantIdle()` (1 solo blit directo por NEON, 0.008 ms).
  - Si la planta está disparando (`mShootingCounter > 0`), recibiendo daño o realizando una acción especial:
    Conmutar al instante al renderizado dinámico `aBodyReanim->Draw(g)` con fidelidad 100%.

---

### Tarea 5: Compilación ARM, Empaquetado, Auditoría de Memoria y Trail Graph

**Pasos:**
- [ ] Compilar binario nativo ARM usando el contenedor Docker `aemiii91/miyoomini-toolchain:latest`.
- [ ] Despojar símbolos de depuración (`arm-linux-gnueabihf-strip`) logrando binario de $\sim 5.4\text{ MB}$.
- [ ] Copiar binario a las 4 rutas de distribución:
  - `bin/PvZ`
  - `ports/Games/Plants vs. Zombies/PvZ`
  - `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
  - `dist/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
- [ ] Generar release zip actualizado `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`.
- [ ] Auditar consumo de memoria RAM: confirmar que el uso total sea $\le 85\text{ MB}$ (con el caché de 3.6 MB añadido).
- [ ] Reconstruir grafo de contexto `trail build`.
