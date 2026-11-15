# Plan de Acción Integral: Reciclaje de Memoria en Supervivencia y Expansión de Caché de Sprites

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminar definitivamente los parones por contienda de E/S y el congelamiento severo de 10 segundos en las transiciones de ronda en Supervivencia, e integrar la aceleración por caché de sprites pre-renderizados en zombies no típicos de alta carga (Football, Newspaper, Polevaulter) y plantas de fondo continuas (Girasol y Birasol).

**Architecture:** Implementar una arquitectura de *demand-paging* con recolección forzada de memoria en frontera de ronda (`ClearLevelTransientCaches`) y retorno de páginas al kernel Linux (`malloc_trim`). Complementar con la ampliación del sistema de caché de sprites lazy de 1 solo blit NEON para zombies de carrera rápida y plantas oscilantes, garantizando cero fallos visuales mediante un contrato de fallback inmediato a renderizado esquelético tradicional.

**Tech Stack:** C++20, SDL2, Linux Kernel 4.9 (ARM Cortex-A7 / SigmaStar SSD202D), Miyoo Mini Plus Toolchain (GCC cross-compiler), Docker, glibc `<malloc.h>`.

**Spec:** Análisis forense de memoria y evaluación de viabilidad de caché de sprites documentados en la sesión de optimización para Miyoo Mini Plus.

---

## Global Constraints

- **Presupuesto de RAM Estricto:** La memoria física total es de 128 MB. El consumo del juego base no debe superar los 85 MB bajo ninguna circunstancia.
- **Zero-Glitch Contract:** El sprite pre-renderizado solo se dibuja si la entidad está 100% sana, en su animación estándar y sin efectos de estado (hielo, fuego, masticando, flash de impacto).
- **Terminación de Tareas y Procesos:** Cualquier ejecución de Python o scripts auxiliares debe ser estrictamente síncrona, sin dejar procesos huérfanos en segundo plano.
- **Rutas de Distribución:** El binario compilado debe ser distribuido a las 4 rutas requeridas y empaquetado en el zip final de distribución.

---

## Review Focus

1. **Evicción de memoria en transición de ronda:** Al terminar una ronda de Supervivencia y llamar a `InitSurvivalStage()`, ¿se liberan los ~8–12 MB de fotogramas de la ronda previa antes de que se instancie el selector de semillas y se precargue la nueva oleada?
2. **Prevención de congelamiento de 10 segundos:** ¿Se invoca `malloc_trim(0)` en Linux para devolver páginas al kernel y evitar que `kswapd` / `pdflush` bloquee el proceso en estado `D`?
3. **Alineación espacial de zombies no típicos:** ¿Los fotogramas cacheados del Football Zombie y Polevaulter coinciden al píxel con sus posiciones esqueléticas para evitar sacudidas o *popping* al recibir disparos?
4. **Respeto a estados de armadura y fases:** ¿El Newspaper Zombie cae inmediatamente a renderizado dinámico al romperse el periódico (`PHASE_NEWSPAPER_MAD`), y el Polevaulter al ejecutar el salto?
5. **Estabilidad en segundo plano y concurrencia:** ¿Toda generación de imágenes MemoryImage permanece lazy en el hilo principal sin introducir carreras con `mEffectSystem`?

---

### Task 1: Recolección y Desalojo de Memoria en Frontera de Ronda (`ClearLevelTransientCaches`)

**Files:**
- Modify: `src/Lawn/System/ReanimationLawn.h`
- Modify: `src/Lawn/System/ReanimationLawn.cpp`
- Modify: `src/Lawn/Board.cpp:1275-1310` y `src/Lawn/Board.cpp:268-295`

- [x] **Step 1.1:** Declarar el método `void ClearLevelTransientCaches();` en la clase `ReanimatorCache` dentro de [`ReanimationLawn.h`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.h).
- [x] **Step 1.2:** Implementar `ClearLevelTransientCaches()` en [`ReanimationLawn.cpp`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp):
  - Recorrer `mZombieWalkFrames` y vaciar cada vector de fotogramas (`mZombieWalkFrames[i].clear()`).
  - Resetear `mZombieImages[i].reset()`.
  - En plataformas Linux (`#if defined(__linux__) && !defined(__ANDROID__)`), incluir `<malloc.h>` y ejecutar `malloc_trim(0)` para contraer las arenas del heap y devolver las páginas físicas al sistema operativo.
- [x] **Step 1.3:** Conectar la recolección en [`Board::InitSurvivalStage()`](file:///c:/DEV/PVZ/src/Lawn/Board.cpp#L1275):
  - Invocar `mApp->mReanimatorCache->ClearLevelTransientCaches()` al inicio de `InitSurvivalStage()`, justo antes de mostrar el `SeedChooserScreen` e iniciar la precarga de zombies de la siguiente ronda.
- [x] **Step 1.4:** Conectar la recolección en [`Board::DisposeBoard()`](file:///c:/DEV/PVZ/src/Lawn/Board.cpp#L268):
  - Invocar `mApp->mReanimatorCache->ClearLevelTransientCaches()` al destruir el tablero para asegurar que al salir al menú principal o reiniciar partida no quede memoria residual.

---

### Task 2: Optimización de E/S en Guardado (`TryToSaveGame`)

**Files:**
- Modify: `src/Lawn/Board.cpp:1720-1735`

- [x] **Step 2.1:** Revisar el momento exacto en que se ejecuta `TryToSaveGame()` en `UpdateLevelEndSequence()`:
  - Asegurar que el guardado en la tarjeta MicroSD ocurra antes de que comience el paneo de cámara y la precarga de la cutscene, evitando que la contienda de E/S con la lectura de texturas sature el bus MMC de la consola.
- [x] **Step 2.2:** Añadir `mApp->ClearUpdateBacklog(true)` después del guardado para absorber cualquier latencia física de la MicroSD sin acumular retraso en el bucle de actualización.

---

### Task 3: Caché de Sprites para Zombies No Típicos de Alto Impacto (Football, Newspaper, Polevaulter)

**Files:**
- Modify: `src/Lawn/System/ReanimationLawn.h`
- Modify: `src/Lawn/System/ReanimationLawn.cpp:654-767`
- Modify: `src/Lawn/Zombie.cpp:5795-5825`

- [x] **Step 3.1:** Actualizar `ReanimatorCache::HasCachedZombieWalk` en [`ReanimationLawn.cpp`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp):
  - Añadir soporte para `ZombieType::ZOMBIE_FOOTBALL`, `ZombieType::ZOMBIE_NEWSPAPER` y `ZombieType::ZOMBIE_POLEVAULTER`.
- [x] **Step 3.2:** Extender `ReanimatorCache::MakeCachedZombieWalkFrame` en [`ReanimationLawn.cpp`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp):
  - Para `ZOMBIE_FOOTBALL`: asegurar dimensiones adecuadas ($200\times 210$), inicializar con `REANIM_ZOMBIE_FOOTBALL`, pista `anim_walk`, casco asignado.
  - Para `ZOMBIE_NEWSPAPER`: inicializar con `REANIM_ZOMBIE_NEWSPAPER`, pista `anim_walk`, mostrar periódico intacto.
  - Para `ZOMBIE_POLEVAULTER`: inicializar con `REANIM_ZOMBIE_POLEVAULTER`, pista `anim_run`, pértiga adjunta.
- [x] **Step 3.3:** Calibrar los desfases espaciales en `ReanimatorCache::DrawCachedZombieWalk`:
  - Aplicar los offsets relativos de cada especie (ej. Football Y offset -16px) para que coincidan con exactitud de subpíxel con el esqueleto.
- [x] **Step 3.4:** Actualizar la guarda de renderizado en `Zombie::DrawReanim` en [`Zombie.cpp`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L5795):
  - Football: validar `mHelmType == HelmType::HELMTYPE_FOOTBALL && mHelmHealth >= mHelmMaxHealth`.
  - Newspaper: validar `mShieldType == ShieldType::SHIELDTYPE_NEWSPAPER && mShieldHealth >= mShieldMaxHealth && mZombiePhase != ZombiePhase::PHASE_NEWSPAPER_MAD`.
  - Polevaulter: validar `mZombiePhase == ZombiePhase::PHASE_POLEVAULTER_PRE_VAULT && aBodyReanim->IsAnimPlaying("anim_run")`.
  - Caída automática al renderizado dinámico en cuanto se sufra daño, se pierda el objeto, se salte o se cambie de estado.

---

### Task 4: Caché de Sprites para Plantas de Fondo Clave (Girasol y Birasol)

**Files:**
- Modify: `src/Lawn/System/ReanimationLawn.cpp:475-620`
- Modify: `src/Lawn/Plant.cpp:4080-4125`

- [x] **Step 4.1:** Ampliar `ReanimatorCache::HasCachedIdle` en [`ReanimationLawn.cpp`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp):
  - Añadir `SeedType::SEED_SUNFLOWER` y `SeedType::SEED_TWINSUNFLOWER`.
- [x] **Step 4.2:** Extender `ReanimatorCache::MakeCachedPlantIdleFrame`:
  - Configurar las pistas de `anim_idle` para Girasol (cuerpo y cabeza) y Birasol (dos cabezas gemelas `anim_head_idle1` y `anim_head_idle2`).
- [x] **Step 4.3:** En `Plant::DrawReanim` en [`Plant.cpp`](file:///c:/DEV/PVZ/src/Lawn/Plant.cpp#L4080):
  - Validar que al encontrarse en oscilación pasiva (`anim_idle`) sin producir sol activo ni ser devorada, se dibuje el fotograma cacheado mediante el blit NEON.

---

### Task 5: Compilación Cruzada ARM, Empaquetado y Verificación

**Files:**
- Output: `build/miyoo/PvZ` (binario ARM strip)
- Deployment:
  - `dist/PvZ`
  - `dist/miyoo/app/PvZ`
  - `dist/miyoo/app/PvZ.bin`
  - `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`

- [x] **Step 5.1:** Compilar en contenedor Docker `aemiii91/miyoomini-toolchain:latest` mediante `cmake --build build -j 4` / `make -j4`.
- [x] **Step 5.2:** Ejecutar `/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip` sobre el binario generado, verificando que el tamaño se mantenga en ~5.5 MB (`5,677,692` bytes).
- [x] **Step 5.3:** Desplegar el ejecutable a las 4 rutas de release y actualizar el archivo ZIP de distribución `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`.
- [x] **Step 5.4:** Regenerar el grafo de Trail con `trail build` para mantener sincronizados los símbolos del repositorio.
