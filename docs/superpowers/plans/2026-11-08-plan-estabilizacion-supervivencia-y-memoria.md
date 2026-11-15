# Plan de Estabilización de Supervivencia: Corrección de Congelamiento y Presupuesto Estricto de Memoria

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminar el congelamiento total en las letras rojas ("Ready, Set, Plant") al regresar al jardín en Modo Supervivencia, suprimir los tirones en el barrido de pantalla y selección de semillas, y fijar un presupuesto estricto de memoria ($\le 75\text{ MB}$ RSS) para erradicar el *direct page thrashing* en la consola Miyoo Mini Plus.

**Architecture:** 
1. Reemplazar la asignación insegura en `mEffectSystem` y la llamada a `ProcessDeleteQueue()` dentro de [`MakeCachedPlantIdleFrame`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp#L531) por instancias locales en pila (`Reanimation aReanim`), evitando cualquier corrupción de memoria durante el ciclo de renderizado.
2. Podar los cachés de sprites secundarios (Fútbol, Periódico, Garrocha) para reservar la caché exclusivamente a los zombies masivos de la horda (Normal, Cono, Cubeta, Bandera) y Girasol/Birasol, reduciendo el consumo de sprites de 27.1 MB a $\le 8.5\text{ MB}$.
3. Bloquear la generación de caché para zombies de utilería en la cinemática (`mFromWave == -2`) y optimizar la precarga síncrona en [`CutScene::PreloadResources`](file:///c:/DEV/PVZ/src/Lawn/CutScene.cpp#L335) durante la transición de rondas.

**Tech Stack:** C++17, SDL2, glibc ARM (Cortex-A7), Linux 4.9.84 (Miyoo Mini Plus / OnionOS).

---

## Global Constraints

- **Presupuesto Máximo de Memoria:** Heap total $\le 78.0\text{ MB}$ en todo momento. Prohibido superar los $85.0\text{ MB}$ (límite crítico del hardware con `swapoff -a`).
- **Integridad del Hilo de Renderizado:** Queda terminantemente prohibido llamar a `ProcessDeleteQueue()`, `AddReanimation()` o mutar `DataArray<Reanimation>` dentro de cualquier función de dibujado (`Draw()`).
- **Compatibilidad Visual:** Cero regresiones gráficas; fidelidad 100% en las transiciones de daño, congelamiento y fases especiales.
- **Procesos en Segundo Plano:** Toda ejecución de Python o scripts de compilación debe completarse de manera síncrona sin procesos huérfanos.

---

## Review Focus

1. **UAF / Punteros en `aRenderList`:** Verificar que `Board::DrawGameObjects()` dibuje `REANIM_READYSETPLANT` y todas las plantas del jardín sin fallos de segmentación ni interrupciones del hilo de SDL.
2. **Re-generación en Cinemática:** Verificar que `PlaceStreetZombies()` en la cinemática no dispare `PrecacheZombieWalk()` en la calle.
3. **Latencia de I/O en MicroSD:** Confirmar que `PreloadResources()` en rondas 2+ no re-lea archivos ya existentes en memoria ni congele el refresco tras el barrido a la derecha.
4. **Consumo de RAM tras 3+ Rondas:** Comprobar que tras múltiples rondas de Supervivencia el consumo no sobrepase los 78 MB.
5. **Cadencia del Game Loop:** Confirmar que el bucle de actualización mantenga sincronía nativa a 60 FPS sin tirones del acumulador.

---

### Task 1: Sanear `MakeCachedPlantIdleFrame` y Desacoplar de `EffectSystem`

**Files:**
- Modify: [`src/Lawn/System/ReanimationLawn.cpp:531-638`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp#L531-L638)

**Interfaces:**
- Consumes: [`Reanimation::ReanimationInitializeType`](file:///c:/DEV/PVZ/src/PvzpLib/Reanimator.cpp#L353)
- Produces: `MakeCachedPlantIdleFrame(SeedType theSeedType, float theAnimTime) -> std::unique_ptr<MemoryImage>` completamente aislado de `mEffectSystem`.

- [x] **Step 1: Inspeccionar la implementación actual de `MakeCachedPlantIdleFrame`**
  Confirmar que llama a `mApp->AddReanimation` y a `mApp->mEffectSystem->ProcessDeleteQueue()`.

- [x] **Step 2: Reescribir `MakeCachedPlantIdleFrame` usando reanimaciones locales en pila**
  - Instanciar `Reanimation aBodyReanim;` en la pila local.
  - Llamar a `aBodyReanim.ReanimationInitializeType(-aOffsetX, -aOffsetY, aPlantDef.mReanimationType);`.
  - Configurar capa `"anim_idle"`, `mAnimRate = 0.0f` y `mAnimTime = theAnimTime`.
  - Para plantas con cabeza separada (Girasol y Birasol no la necesitan, pero para Lanzaguisantes), usar `Reanimation aHeadReanim;` en la pila o mantener vector local de objetos por valor.
  - Actualizar y dibujar en `aMemoryGraphics`: `aBodyReanim.Update(); aBodyReanim.Draw(&aMemoryGraphics);`.
  - **Eliminar por completo** `aBodyReanim->ReanimationDie()` y `mApp->mEffectSystem->ProcessDeleteQueue()`.

- [x] **Step 3: Compilar y verificar ausencia de advertencias**
  Verificar que compile limpiamente en Docker.

---

### Task 2: Ajustar el Presupuesto de Caché de Sprites a $\le 8.5\text{ MB}$

**Files:**
- Modify: [`src/Lawn/System/ReanimationLawn.cpp:672-688`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp#L672-L688)
- Modify: [`src/Lawn/Zombie.cpp:5794-5845`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L5794-L5845)

**Interfaces:**
- Consumes: [`ReanimatorCache::HasCachedZombieWalk`](file:///c:/DEV/PVZ/src/Lawn/System/ReanimationLawn.cpp#L672)
- Produces: Caché de caminata restringido a los 4 tipos masivos de la horda:
  - `ZOMBIE_NORMAL`
  - `ZOMBIE_FLAG`
  - `ZOMBIE_TRAFFIC_CONE`
  - `ZOMBIE_PAIL`
  (Liberando 6.5 MB de RAM al excluir Fútbol, Periódico y Garrocha, los cuales son escasos en pantalla y corren perfectamente en esquelético).

- [x] **Step 1: Ajustar `HasCachedZombieWalk` en `ReanimationLawn.cpp`**
  Remover `ZOMBIE_FOOTBALL`, `ZOMBIE_NEWSPAPER` y `ZOMBIE_POLEVAULTER` de `HasCachedZombieWalk`.

- [x] **Step 2: Simplificar `Zombie::DrawReanim` en `src/Lawn/Zombie.cpp`**
  Alinear la condición en `Zombie::DrawReanim` para que solo evalúe `PHASE_ZOMBIE_NORMAL` sobre los zombies masivos permitidos, eliminando las ramas especiales de garrocha y periódico.

- [x] **Step 3: Verificar reducción de huella de memoria**
  Confirmar que el tamaño en memoria de `mZombieWalkFrames` no supere los 6.8 MB para toda la horda.

---

### Task 3: Evitar que los Zombies de Cinemática Regeneren la Caché

**Files:**
- Modify: [`src/Lawn/Zombie.cpp:5794-5840`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L5794-L5840)

**Interfaces:**
- Consumes: `mFromWave` de [`Zombie`](file:///c:/DEV/PVZ/src/Lawn/Zombie.h)
- Produces: Prevención de llamadas a `PrecacheZombieWalk` durante la cinemática de la calle.

- [x] **Step 1: Agregar guardia en `Zombie::DrawReanim`**
  Comprobar `mFromWave != -2` (los zombies de la calle usan `-2` en `mFromWave`).
  Si `mFromWave == -2` y el juego está en `SCENE_LEVEL_INTRO`, forzar `canUseWalkCache = false;` para que los zombies de muestra usen renderizado esquelético estándar sin instanciar buffers en memoria.

- [x] **Step 2: Verificar que durante el paneo a la derecha no se aloque ningún frame de caminata**
  Confirmar que la memoria no aumente durante el barrido a la calle.

---

### Task 4: Optimizar `CutScene::PreloadResources` en Transiciones de Supervivencia

**Files:**
- Modify: [`src/Lawn/CutScene.cpp:335-420`](file:///c:/DEV/PVZ/src/Lawn/CutScene.cpp#L335-L420)

**Interfaces:**
- Consumes: [`CutScene::IsSurvivalRepick()`](file:///c:/DEV/PVZ/src/Lawn/Cutscene.h)
- Produces: Salto de precarga redundante en etapas 2+ de Supervivencia.

- [x] **Step 1: Analizar la precarga redundante en `CutScene::PreloadResources`**
  Comprobar que en rondas posteriores a la primera, las 15 reanimaciones universales (`REANIM_BUNGEE`, `REANIM_IMP`, etc.) y las plantas ya están cargadas en el gestor de recursos.

- [x] **Step 2: Aplicar guardia de repick en `PreloadResources`**
  En `CutScene::PreloadResources()`, si `IsSurvivalRepick()` es verdadero:
  - Cargar únicamente los recursos específicos de los zombies de la nueva oleada (`Zombie::PreloadZombieResources`).
  - Omitir el bucle redundante de plantas que ya están en memoria.
  - Esto elimina el freeze de 300-800 ms de lectura de MicroSD al iniciar la cinemática.

---

### Task 5: Restaurar Temporización Estable del Game Loop, Compilar ARM, Empaquetar y Verificar

**Files:**
- Modify: [`src/SexyAppFramework/SexyAppBase.cpp`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.cpp)
- Build: Docker container `aemiii91/miyoomini-toolchain:latest`
- Package: [`scripts/package_release.py`](file:///c:/DEV/PVZ/scripts/package_release.py)

- [x] **Step 1: Restaurar configuración de sincronización de `SexyAppBase.cpp`**
  Verificar que `mSyncRefreshRate = 100`, `mVSyncUpdates = false`, `mWaitForVSync = false` y `mSoftVSyncWait = true` estén en sus valores estables originales.
  Mantener la absorción de I/O en [`Board::TryToSaveGame`](file:///c:/DEV/PVZ/src/Lawn/Board.cpp#L351).

- [x] **Step 2: Ejecutar compilación cruzada en Docker**
  Ejecutar el comando de compilación dentro del contenedor:
  `make -j4` en `build/`.

- [x] **Step 3: Stripping y verificación del binario ARM**
  Aplicar `/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip` a `bin/PvZ` para dejarlo en ~5.5 MB.

- [x] **Step 4: Empaquetar y actualizar `/dist`**
  Ejecutar `python scripts/package_release.py` asegurando que termine síncronamente.
  Verificar que `dist/Roms/.../PvZ`, `dist/PvZ_MiyooMiniPlus.zip` y `dist/PvZ_MiyooMiniPlus_v1.1.0.zip` queden actualizados con SHA256 idéntico.

- [x] **Step 5: Actualizar el grafo de Trail**
  Ejecutar `trail build` para sincronizar las definiciones.
