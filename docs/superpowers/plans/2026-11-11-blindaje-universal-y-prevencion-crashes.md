# Blindaje Universal y Prevención de Crashes en PvZ (Miyoo Mini Plus) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implementar un blindaje exhaustivo y universal contra punteros nulos, faltantes de recursos en guardado/carga (SaveGame) y fugas de memoria en todos los modos de juego restantes (Minijuegos, Plantas multicabezas, Zombotany, Cinemática Game Over, AwardScreen y CreditScreen) en Miyoo Mini Plus.

**Architecture:** Se extienden las salvaguardas probadas en Dr. Zomboss y Jardín Zen al resto de sistemas vulnerables: precarga automática de plantas y paquetes de semillas en `Board::LoadGame`, reemplazo de accesos directos desprotegidos `mApp->ReanimationGet(...)` por `mApp->ReanimationTryToGet(...)` con guardas condicionales en Minijuegos, GridItems y plantas complejas (Threepeater, Split Pea, Pumpkin), protección de la cinemática de Game Over, y liberación explícita de texturas (`CleanSharedImages()`) en pantallas de diálogo/premios y créditos.

**Tech Stack:** C++17, PopCap SexyAppFramework modificado, Linux ARMv7 (SigmaStar SSD202D), CMake, Docker Toolchain (`aemiii91/miyoomini-toolchain`).

**Spec:** Vulnerability Analysis and Stability Specification (sesión 2026-11-11, b19ebc19-5ffd-4a83-a6bc-bfa349636ccb).

---

## Global Constraints

- **Platform Target:** Miyoo Mini Plus / OnionOS (ARMv7 Cortex-A7, 128 MB RAM, software framebuffer `/dev/fb0`).
- **Memory Ceiling:** Límite estricto de 85 MB de RAM antes de la activación del kernel OOM-killer.
- **Null Safety Contract:** Tras la actualización de `DataArrayGet` delegando en `DataArrayTryToGet`, cualquier ID inválido o 0 retorna `nullptr`; todo acceso a reanimaciones debe comprobar puntero válido antes de dereferenciar.
- **Process Rule:** Ningún script, proceso o contenedor Docker debe quedar huérfano en segundo plano. Toda tarea debe ejecutarse de forma síncrona o finalizarse de inmediato.
- **Preservation of Core Mechanics:** Ningún cambio debe alterar el timing, la lógica de juego o las animaciones originales cuando los recursos son válidos.

---

## Review Focus

1. **Restauración de partida guardada (SaveGame) con plantas complejas en el campo o en el semillero:** Al cargar niveles con Repetidora triple (Threepeater), Bipetidora (Split Pea), Mazorcañón o plantas nocturnas, los recursos de reanimación y proyectiles deben estar precargados sin provocar un fallo de segmentación.
2. **Derrota del jugador con memoria saturada (Game Over Cutscene):** Al activarse la cinemática "THE ZOMBIES ATE YOUR BRAINS!", si la memoria es escasa y la reanimación falla, debe omitir el efecto de sacudida sin crashear.
3. **Ciclado y salida de minijuegos con fondo especial (Zombiquarium vs Zen Garden):** Al salir del minijuego Zombiquarium (`GAMEMODE_CHALLENGE_ZOMBIQUARIUM`), `Board::DisposeBoard()` no debe invocar la lógica de recolección de herramientas y monedas del Jardín Zen (`LeaveGarden()`).
4. **Acumulación de memoria en pantallas de premios y créditos:** Entrar y salir repetidamente de `AwardScreen` o `CreditScreen` debe liberar las texturas de fondo y notas de zombie mediante `ReleaseTrackedResources` y `CleanSharedImages()` para evitar OOM.
5. **Comportamiento ante reanimaciones nulas en Minijuegos (Slot Machine, Martillo de Whack-A-Zombie, Portales):** Durante transiciones de minijuegos o estados nulos de IDs, las llamadas a `Draw` o `PlayReanim` no deben dereferenciar punteros nulos.

---

## Tasks

### Task 1: Preload Universal de Plantas y Semillas en `Board::LoadGame`

**Files:**
- Modify: `src/Lawn/Board.cpp:424-442`
- Verify: Compilación estática y simulación de deserialización SaveGame

**Interfaces:**
- Consumes: `Plant::PreloadPlantResources(SeedType theSeedType)`, `mPlants`, `mSeedBank->mSeedPackets`
- Produces: Precarga garantizada de todas las plantas vivas e imitater, así como de todas las semillas en el semillero al cargar partidas guardadas.

- [ ] **Step 1: Verificar el código actual en `Board::LoadGame`**
  Inspeccionar líneas 424-442 de `src/Lawn/Board.cpp` confirmando que solo itera `mZombies` pero omite `mPlants` y `mSeedBank`.

- [ ] **Step 2: Implementar la precarga universal de plantas y semillas en `Board::LoadGame`**
  En `src/Lawn/Board.cpp`, dentro de `bool Board::LoadGame(...)`, tras el bucle de `mZombies`, añadir:
  ```cpp
  for (Plant* aPlant : mPlants)
  {
      if (!aPlant->mDead)
      {
          Plant::PreloadPlantResources(aPlant->mSeedType);
          if (aPlant->mImitaterType != SeedType::SEED_NONE)
          {
              Plant::PreloadPlantResources(aPlant->mImitaterType);
          }
      }
  }

  if (mSeedBank)
  {
      for (int i = 0; i < mSeedBank->mNumPackets; ++i)
      {
          SeedPacket* aPacket = &mSeedBank->mSeedPackets[i];
          if (aPacket->mPacketType != SeedType::SEED_NONE)
          {
              Plant::PreloadPlantResources(aPacket->mPacketType);
              if (aPacket->mImitaterType != SeedType::SEED_NONE)
              {
                  Plant::PreloadPlantResources(aPacket->mImitaterType);
              }
          }
      }
  }
  ```

- [ ] **Step 3: Verificar compilación sintáctica**
  Ejecutar compilación en Docker toolchain para comprobar que no hay errores de sintaxis ni de tipos.

- [ ] **Step 4: Commit**
  ```bash
  git add src/Lawn/Board.cpp
  git commit -m "fix(savegame): preload plant and seedbank resources on load game"
  ```

---

### Task 2: Blindaje de Reanimaciones y Punteros en Minijuegos y Entidades Críticas

**Files:**
- Modify: `src/Lawn/Challenge.cpp:2132-2148` (Slot Machine)
- Modify: `src/Lawn/CursorObject.cpp:206-210` (Whack-a-zombie hammer)
- Modify: `src/Lawn/GridItem.cpp:477-490` (Portal Combat)
- Modify: `src/Lawn/Zombie.cpp:3555-3560` (Zombotany decapitation)

**Interfaces:**
- Consumes: `mApp->ReanimationTryToGet(ReanimationID)`, `mApp->RemoveReanimation(ReanimationID)`
- Produces: Funciones protegidas contra punteros nulos durante renderizado y muerte de entidades en minijuegos.

- [ ] **Step 1: Identificar las líneas vulnerables sin chequeo nulo**
  - `src/Lawn/Challenge.cpp:2146`: `mApp->ReanimationGet(mReanimChallenge)->Draw(&gBoardParent);`
  - `src/Lawn/CursorObject.cpp:208`: `mApp->ReanimationGet(mReanimCursorID)->Draw(g);`
  - `src/Lawn/GridItem.cpp:479-482`: `Reanimation* aPortalReanim = mApp->ReanimationGet(mGridItemReanimID);` seguido de `aPortalReanim->mLoopCount` sin verificación.
  - `src/Lawn/Zombie.cpp:3557`: `mApp->ReanimationGet(mSpecialHeadReanimID)->ReanimationDie();`

- [ ] **Step 2: Aplicar guardas en `Challenge.cpp` y `CursorObject.cpp`**
  En `Challenge::DrawSlotMachine(Graphics* g)`:
  ```cpp
  if (Reanimation* aChallengeReanim = mApp->ReanimationTryToGet(mReanimChallenge))
  {
      aChallengeReanim->Draw(&gBoardParent);
  }
  ```
  En `CursorObject::Draw(Graphics* g)`:
  ```cpp
  case CursorType::CURSOR_TYPE_HAMMER:
      if (Reanimation* aHammerReanim = mApp->ReanimationTryToGet(mReanimCursorID))
      {
          aHammerReanim->Draw(g);
      }
      break;
  ```

- [ ] **Step 3: Aplicar guardas en `GridItem.cpp` y `Zombie.cpp`**
  En `GridItem::UpdatePortal()`:
  ```cpp
  Reanimation* aPortalReanim = mApp->ReanimationTryToGet(mGridItemReanimID);
  if (!aPortalReanim)
      return;
  ```
  En `Zombie::DieNoLoot()`:
  ```cpp
  if (Zombie::IsZombotany(mZombieType))
  {
      mApp->RemoveReanimation(mSpecialHeadReanimID);
      mSpecialHeadReanimID = ReanimationID::REANIMATIONID_NULL;
      return;
  }
  ```

- [ ] **Step 4: Verificar compilación**
  Ejecutar compilación en Docker toolchain y validar que todos los archivos compilen limpiamente.

- [ ] **Step 5: Commit**
  ```bash
  git add src/Lawn/Challenge.cpp src/Lawn/CursorObject.cpp src/Lawn/GridItem.cpp src/Lawn/Zombie.cpp
  git commit -m "fix(minigames): null-guard reanimation pointers in slot machine, hammer, portals and zombotany"
  ```

---

### Task 3: Blindaje de Plantas Multi-Cabezas (Threepeater, Split Pea) y Calabazas (Pumpkin)

**Files:**
- Modify: `src/Lawn/Plant.cpp:755-763`, `src/Lawn/Plant.cpp:840-856`, `src/Lawn/Plant.cpp:3285-3318`, `src/Lawn/Plant.cpp:3350-3395`, `src/Lawn/Plant.cpp:4061-4069`

**Interfaces:**
- Consumes: `mApp->ReanimationTryToGet(ReanimationID)`
- Produces: Lógica de plantas con múltiples cabezas protegida contra fallos de inicialización o desincronización de reanimaciones.

- [ ] **Step 1: Identificar las dereferencias desprotegidas en `Plant.cpp`**
  - Línea 757: `Reanimation* aHeadReanim2 = mApp->ReanimationGet(mHeadReanimID2);` en Split Pea secundario.
  - Líneas 842-844: `aHeadReanim1`, `aHeadReanim2`, `aHeadReanim3` en Threepeater al detectar zombies en filas adyacentes.
  - Líneas 3289-3291 y 3308-3310: comprobación de disparo `mShootingCounter == 1` en Threepeater y Split Pea.
  - Líneas 3353-3354 y 3387: regreso a animación idle tras disparo en Threepeater y Split Pea.
  - Líneas 4063-4067: renderizado de la parte trasera de la calabaza (`aDrawPumpkinBack`).

- [ ] **Step 2: Aplicar protecciones en disparo y transiciones de Threepeater y Split Pea**
  En `Plant::PlayShootingAnim`:
  ```cpp
  if (mSeedType == SeedType::SEED_SPLITPEA && thePlantWeapon == PlantWeapon::WEAPON_SECONDARY)
  {
      if (Reanimation* aHeadReanim2 = mApp->ReanimationTryToGet(mHeadReanimID2))
      {
          aHeadReanim2->StartBlend(20);
          aHeadReanim2->mLoopType = ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD;
          aHeadReanim2->mAnimRate = 35.0f;
          aHeadReanim2->SetFramesForLayer("anim_splitpea_shooting");
      }
      mShootingCounter = 26;
  }
  ```
  En detección de zombies y cambio a shooting (`FindTargetZombie`):
  Usar `ReanimationTryToGet` para `aHeadReanim1`, `aHeadReanim2`, `aHeadReanim3` y verificar que no sean nulos antes de llamar `StartBlend` y `SetFramesForLayer`.
  En disparo (`mShootingCounter == 1`):
  Comprobar `aHeadReanim && aHeadReanim->mLoopType == ...` antes de invocar `Fire(...)`.
  En vuelta a idle:
  Comprobar que todas las instancias de reanimación existen antes de acceder a sus miembros (`mLoopCount`, `StartBlend`, etc.).

- [ ] **Step 3: Aplicar protección en renderizado de calabazas (`SEED_PUMPKIN`)**
  En `Plant::Draw`:
  ```cpp
  if (aDrawPumpkinBack)
  {
      if (Reanimation* aPumpkinReanim = mApp->ReanimationTryToGet(aPumpkin->mBodyReanimID))
      {
          Graphics aPumpkinGraphics(*g);
          aPumpkinGraphics.mTransX += aPumpkin->mX - mX;
          aPumpkinGraphics.mTransY += aPumpkin->mY - mY;
          aPumpkinReanim->DrawRenderGroup(&aPumpkinGraphics, 1);
      }
  }
  ```

- [ ] **Step 4: Verificar compilación**
  Compilar en Docker toolchain y validar que no haya errores de compilación ni advertencias de punteros.

- [ ] **Step 5: Commit**
  ```bash
  git add src/Lawn/Plant.cpp
  git commit -m "fix(plant): guard multi-head plant animations and pumpkin rendering against null reanimations"
  ```

---

### Task 4: Blindaje de Cinemática Game Over y Desconexión Limpia de Zombiquarium

**Files:**
- Modify: `src/Lawn/CutScene.cpp:1595-1607`
- Modify: `src/Lawn/Board.cpp:280-299`

**Interfaces:**
- Consumes: `mApp->ReanimationTryToGet(mZombiesWonReanimID)`, `mApp->mGameMode`
- Produces: Cinemática de fin de partida a prueba de fallos de memoria; separación clara entre el minijuego Zombiquarium y el acuario del Jardín Zen.

- [ ] **Step 1: Proteger `CutScene::UpdateZombiesWon()` contra punteros nulos**
  En `src/Lawn/CutScene.cpp`:
  ```cpp
  if (mCutsceneTime == LostTimeBrainGraphicShake)
  {
      if (Reanimation* aWonReanim = mApp->ReanimationTryToGet(mZombiesWonReanimID))
          aWonReanim->SetShakeOverride("ZombiesWon", 1.0f);
  }
  if (mCutsceneTime == LostTimeBrainGraphicCancelShake)
  {
      if (Reanimation* aWonReanim = mApp->ReanimationTryToGet(mZombiesWonReanimID))
          aWonReanim->SetShakeOverride("ZombiesWon", 0.0f);
  }
  if (mCutsceneTime == LostTimeBrainGraphicEnd)
  {
      if (Reanimation* aWonReanim = mApp->ReanimationTryToGet(mZombiesWonReanimID))
          aWonReanim->SetFramesForLayer("anim_screen");
  }
  ```

- [ ] **Step 2: Restringir `ZenGarden::LeaveGarden()` en `Board::DisposeBoard()` al modo Zen Garden real**
  En `src/Lawn/Board.cpp`:
  Modificar la condición para que solo llame a `LeaveGarden()` y libere reanimaciones exclusivas del Jardín Zen si `mApp->mGameMode == GAMEMODE_CHALLENGE_ZEN_GARDEN`:
  ```cpp
  if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN)
  {
      mApp->mZenGarden->LeaveGarden();
  #ifdef LOW_MEMORY
      ReanimationFreeDefinition(&gReanimatorDefArray[ReanimationType::REANIM_ZENGARDEN_WATERINGCAN]);
      ReanimationFreeDefinition(&gReanimatorDefArray[ReanimationType::REANIM_ZENGARDEN_FERTILIZER]);
      ReanimationFreeDefinition(&gReanimatorDefArray[ReanimationType::REANIM_ZENGARDEN_BUGSPRAY]);
      ReanimationFreeDefinition(&gReanimatorDefArray[ReanimationType::REANIM_ZENGARDEN_PHONOGRAPH]);
      ReanimationFreeDefinition(&gReanimatorDefArray[ReanimationType::REANIM_STINKY]);
      mApp->mResourceManager->ReleaseTrackedResources(mApp->mZenGarden->mLoadedResourceNames);
      mApp->mZenGarden->mLoadedResourceNames.clear();
      mApp->mResourceManager->DeleteResources("DelayLoad_GreenHouseGarden");
      mApp->mResourceManager->DeleteResources("DelayLoad_GreenHouseOverlay");
      mApp->mResourceManager->DeleteResources("DelayLoad_MushroomGarden");
      mApp->mResourceManager->DeleteResources("DelayLoad_Zombiquarium");
      mApp->CleanSharedImages();
  #endif
  }
  ```

- [ ] **Step 3: Verificar compilación**
  Compilar en Docker toolchain.

- [ ] **Step 4: Commit**
  ```bash
  git add src/Lawn/CutScene.cpp src/Lawn/Board.cpp
  git commit -m "fix(lifecycle): safe game over cutscene and isolate zombiquarium minigame from zen garden leave"
  ```

---

### Task 5: Ciclo de Vida Limpio y Liberación de Memoria en Pantallas de Premios y Créditos

**Files:**
- Modify: `src/Lawn/Widget/AwardScreen.cpp:256`
- Modify: `src/Lawn/Widget/CreditScreen.cpp:375-379`

**Interfaces:**
- Consumes: `mApp->CleanSharedImages()`, `mApp->mResourceManager->ReleaseTrackedResources(...)`
- Produces: Liberación de texturas masivas al cerrar la pantalla de premios (AwardScreen) y la pantalla de créditos musicales (CreditScreen).

- [ ] **Step 1: Implementar destructor activo en `AwardScreen`**
  En `src/Lawn/Widget/AwardScreen.cpp`:
  Reemplazar `AwardScreen::~AwardScreen() = default;` por:
  ```cpp
  AwardScreen::~AwardScreen()
  {
      mApp->CleanSharedImages();
  }
  ```

- [ ] **Step 2: Añadir purga de texturas en `CreditScreen::~CreditScreen()`**
  En `src/Lawn/Widget/CreditScreen.cpp`:
  En el destructor `CreditScreen::~CreditScreen()`:
  ```cpp
  CreditScreen::~CreditScreen()
  {
      mApp->SetMusicVolume(mOriginalMusicVolume);
      mApp->CleanSharedImages();
  }
  ```

- [ ] **Step 3: Verificar compilación**
  Compilar en Docker toolchain y comprobar que no hay errores de enlace.

- [ ] **Step 4: Commit**
  ```bash
  git add src/Lawn/Widget/AwardScreen.cpp src/Lawn/Widget/CreditScreen.cpp
  git commit -m "fix(memory): purge shared images on award and credit screens destruction"
  ```

---

### Task 6: Compilación Nativa ARM, Despliegue de Binarios y Empaquetado Release

**Files:**
- Build output: `build/PvZ` (ELF ARMv7)
- Deploy target: `bin/PvZ`, `ports/Games/Plants vs. Zombies/PvZ`
- Release archive: `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`

**Interfaces:**
- Consumes: Código fuente blindado en `src/`
- Produces: Paquete final actualizado y desplegable para Miyoo Mini Plus / OnionOS.

- [ ] **Step 1: Ejecutar compilación completa en contenedor Docker**
  Ejecutar síncronamente:
  `docker run --rm -v "c:\DEV\PVZ:/work" -w /work aemiii91/miyoomini-toolchain:latest /bin/bash -c "cmake --build build -j4"`
  Verificar que termine con código 0 y genere `build/PvZ` de 9+ MB.

- [ ] **Step 2: Desplegar el ejecutable a las carpetas binarias y de ports**
  Copiar `build/PvZ` a `bin/PvZ` y a `ports/Games/Plants vs. Zombies/PvZ`.

- [ ] **Step 3: Generar paquete ZIP de release**
  Ejecutar el script o comando de empaquetado para refrescar `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`.

- [ ] **Step 4: Verificar terminación de procesos y contenedores**
  Comprobar que no queda ningún proceso Python ni contenedor de Docker en ejecución.

- [ ] **Step 5: Commit final y resumen de entrega**
  ```bash
  git add bin/ ports/ dist/
  git commit -m "chore(release): package and deploy v1.1.0 with universal crash shielding"
  ```
