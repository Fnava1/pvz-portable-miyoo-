# Plan de Optimización de Hordas de Zombies y Fix de Carga para Miyoo Mini Plus

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Este plan ataca directamente los dos problemas reportados por el usuario:
1. **El crasheo al iniciar el juego a media carga:** Se debió a la precarga anticipada (`PrecachePlantIdle`) ejecutándose concurrentemente en el hilo secundario de carga de recursos (`LoadingThreadProc`), generando contención en `mEffectSystem`. Se elimina de la rutina de arranque y se mantiene 100% diferida (*lazy*) bajo demanda en el hilo principal durante la partida.
2. **La caída de framerate al entrar la horda de zombies:** Cada zombie que entra en pantalla es una estructura articulada de 25 piezas que se rasterizan como 50 triángulos software en la CPU. Con 30–40 zombies, la CPU debe procesar entre 1,500 y 2,000 triángulos por fotograma. Se implementa **Sprite Caching de Marcha (`anim_walk`) de 12 fases** para los tipos más numerosos de la horda (`ZOMBIE_NORMAL`, `ZOMBIE_TRAFFIC_CONE`, `ZOMBIE_PAIL`, `ZOMBIE_DOOR`, `ZOMBIE_FLAG`). Los zombies que caminan hacia las defensas pasan de 50 triángulos software a **1 único blit rectangular directo** (reducción del 95% del costo de CPU por zombie).

---

## Archivos Involucrados

| Archivo | Responsabilidad en este plan |
| :--- | :--- |
| `src/Lawn/System/ReanimationLawn.h` | Declaración de `mZombieWalkFrames` (12 fases por tipo de zombie), `HasCachedZombieWalk`, `PrecacheZombieWalk`, `DrawCachedZombieWalk` y `MakeCachedZombieWalkFrame`. |
| `src/Lawn/System/ReanimationLawn.cpp` | Eliminar `PrecachePlantIdle` de `PrecacheAll()`. Implementar la generación y dibujado de las 12 fases de marcha para zombies normales, caraconos, cubiletes, puertas y banderas. Limpieza en `ReanimatorCacheDispose()`. |
| `src/Lawn/Zombie.cpp` | Conectar en `Zombie::DrawReanim()` la llamada a `DrawCachedZombieWalk()` cuando el zombie está en su marcha normal de aproximación (`anim_walk`, sin daño a armadura ni pérdida de extremidades). |
| `src/Lawn/Projectile.cpp` | Limpiar la comprobación redundante de sombras de proyectiles para aligerar la ejecución de ráfagas. |

---

## Tareas de Implementación

### Tarea 1: Corrección Definitiva del Crasheo de Arranque
- [ ] Modificar `ReanimatorCache::PrecacheAll()` en `src/Lawn/System/ReanimationLawn.cpp`:
  - Remover la llamada a `PrecachePlantIdle(aSeed)`.
  - Asegurar que `DrawCachedPlantIdle()` gestione la caché bajo demanda de forma 100% perezosa (*lazy*) en el hilo principal de renderizado cuando la planta entra en juego.
  - Verificar que el arranque y carga del juego sea 100% inmune a condiciones de carrera en `LoadingThreadProc`.

### Tarea 2: Estructura de Datos y API de Caché de Marcha de Zombies
- [ ] En `src/Lawn/System/ReanimationLawn.h`:
  - Declarar `static constexpr int NUM_CACHED_ZOMBIE_WALK_FRAMES = 12;`.
  - Declarar `std::array<std::vector<std::unique_ptr<MemoryImage>>, ZombieType::NUM_CACHED_ZOMBIE_TYPES> mZombieWalkFrames;`.
  - Declarar funciones:
    - `bool HasCachedZombieWalk(ZombieType theZombieType);`
    - `void PrecacheZombieWalk(ZombieType theZombieType);`
    - `std::unique_ptr<MemoryImage> MakeCachedZombieWalkFrame(ZombieType theZombieType, float theAnimTime);`
    - `void DrawCachedZombieWalk(Graphics* g, const ZombieDrawPosition& theDrawPos, ZombieType theZombieType, float theAnimTime);`

### Tarea 3: Implementación del Sprite Caching de Marcha en `ReanimationLawn.cpp`
- [ ] En `ReanimatorCache::ReanimatorCacheDispose()`:
  - Limpiar `mZombieWalkFrames[i].clear()` para liberar los fotogramas al salir del juego o reiniciar.
- [ ] En `HasCachedZombieWalk()`:
  - Retornar `true` para `ZOMBIE_NORMAL`, `ZOMBIE_FLAG`, `ZOMBIE_TRAFFIC_CONE`, `ZOMBIE_PAIL` y `ZOMBIE_DOOR`.
- [ ] En `MakeCachedZombieWalkFrame()`:
  - Crear un `MemoryImage` de 160x190 píxeles.
  - Instanciar la reanimación `REANIM_ZOMBIE` en `(50.0f, 35.0f)` con `anim_walk`.
  - Configurar las capas de sombrero/puerta/bandera con `Zombie::SetupReanimLayers(&aReanim, theZombieType)`.
  - Si es zombie de puerta (`ZOMBIE_DOOR`), habilitar la pista `anim_screendoor`.
  - Ajustar `mAnimTime = theAnimTime` y `mAnimRate = 0.0f`.
  - Llamar a `aReanim.Update()` y dibujar sobre el búfer con `aReanim.Draw(&aMemoryGraphics)`.
  - Destruir la reanimación y llamar a `ProcessDeleteQueue()`.
- [ ] En `DrawCachedZombieWalk()`:
  - Calcular el índice de fotograma entre 0 y 11 según `theAnimTime`.
  - Ejecutar un `DrawImage` directo con offset relativo `(-50.0f, -35.0f + theDrawPos.mBodyY)`.

### Tarea 4: Conexión en `Zombie::DrawReanim()` en `Zombie.cpp`
- [ ] En `Zombie::DrawReanim()`:
  - Evaluar las condiciones para usar la caché de marcha:
    1. `!mApp->Is3DAccelerated() && mApp->mReanimatorCache && mApp->mReanimatorCache->HasCachedZombieWalk(mZombieType)`
    2. `aBodyReanim->IsAnimPlaying("anim_walk") && !mIsEating && mZombiePhase == ZombiePhase::PHASE_ZOMBIE_NORMAL`
    3. `mBodyHealth >= mBodyMaxHealth` (sin pérdida de brazos ni cabeza)
    4. Si tiene cono/cubeta/puerta: `mHelmHealth >= mHelmMaxHealth`
    5. Sin efectos de estado: `mChilledCounter == 0 && mIceTrapCounter == 0 && mButteredCounter == 0 && mZombieFade == -1 && mJustGotShotCounter <= 0`
    6. Sin color alterado: `aColorOverride == Color::White && !aEnableExtraAdditiveDraw`
    7. Sin recorte especial: `theDrawPos.mClipHeight <= CLIP_HEIGHT_LIMIT`
  - Si se cumplen las condiciones:
    - Ejecutar `mApp->mReanimatorCache->DrawCachedZombieWalk(g, theDrawPos, mZombieType, aBodyReanim->mAnimTime);`
    - Omitir el pase esquelético software de 50 triángulos.
  - Si no se cumplen (ej. el zombie recibe daño, empieza a comer o se congela):
    - Caer con total normalidad a la llamada existente `aBodyReanim->DrawRenderGroup(g, theBaseRenderGroup);`.

### Tarea 5: Compilación ARM, Stripping, Empaquetado y Verificación
- [ ] Compilar en Docker con la toolchain nativa `aemiii91/miyoomini-toolchain:latest`.
- [ ] Aplicar strip con `/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip build/PvZ`.
- [ ] Desplegar a:
  - `bin/PvZ`
  - `ports/Games/Plants vs. Zombies/PvZ`
  - `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
  - `dist/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
- [ ] Actualizar el zip de distribución `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`.
- [ ] Auditar uso de RAM ($\le 85\text{ MB}$).
- [ ] Reconstruir grafo Trail con `trail build`.
- [ ] Verificar que no queden procesos huérfanos con `manage_task(Action='list')`.
