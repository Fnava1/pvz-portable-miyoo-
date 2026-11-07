# Plan de Implementación: Optimización de Rendimiento y Velocidad Alterna en Supervivencia Infinita (Miyoo Mini / OnionOS)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implementar el reescalado zonal adaptativo (preservando nitidez perfecta en barras de UI), la selección binaria de velocidad rápida alterna (1X <-> 1.5X/2.0X configurable y persistente) y la deduplicación de canales de audio para maximizar la fluidez en partidas pesadas de Supervivencia Infinita sin inestabilidad ni caídas de cuadros.

**Architecture:**
1. En `GLInterface::Flush`, dividir la pantalla verticalmente en 3 zonas (Banda Superior de UI, Césped de Combate y Banda Inferior de UI). La UI superior e inferior siempre se reescala con suavizado bilineal NEON, mientras que el césped activa el reescalado 5:4 rápido Nearest-Neighbor solo durante combate pesado en `SCENE_PLAYING`.
2. En `LawnApp`, implementar la velocidad alterna configurable (1.5X ó 2.0X) con persistencia en el registro y alternancia binaria limpia al presionar SELECT (1.0X <-> Preferida). La velocidad 1.5X se implementa mediante cadencia fija alternante de 1 y 2 ticks por fotograma (sin acumulación de deuda).
3. En `NewOptionsDialog`, reemplazar el control inoperante de aceleración 3D por un botón interactivo de ajuste de velocidad rápida (`[ TURBO: 1.5X ]` / `[ TURBO: 2.0X ]`).
4. En `SDLSoundManager`, reducir la mezcla de canales de 32 a 16 e implementar un gobernador de polifonía máxima (máximo 2 voces simultáneas para sonidos repetitivos como humosetas y congelación).

**Tech Stack:** C++17, SDL2 Framebuffer (`/dev/fb0`), ARM NEON SIMD, PopCap SexyAppFramework, Miyoo Mini Plus / OnionOS.

**Spec:** [`docs/superpowers/specs/2026-10-11-pvz-miyoo-mini-plus-design.md`](file:///c:/DEV/PVZ/docs/superpowers/specs/2026-10-11-pvz-miyoo-mini-plus-design.md) y [`c:\DEV\WIKI\registro_conocimiento_pvz_miyoo.md`](file:///c:/DEV/WIKI/registro_conocimiento_pvz_miyoo.md).

## Global Constraints

- **Cero Hilos en Core 1 para Presentación:** Respetar la decisión de Trail (`2026-10-24` y `8a4da26`); el Core 1 se reserva para el mezclador de audio de SDL sin contención de bloqueos.
- **Cero Bucles Reactivos de Catch-Up:** No implementar bucles `while (mUpdateFTimeAcc >= aFrameFTime * 2.0)` para evitar la espiral de la muerte (`349387e`).
- **Preservar `MAX_PARTICLES_SIZE = 900`:** No recortar arrays de partículas para evitar punteros nulos y fallos SIGSEGV.
- **Integridad Visual de UI:** El texto de soles, semillas, recargas y barra de progreso debe mantenerse 100% nítido mediante interpolación bilineal NEON.
- **Memoria RAM Rigurosa:** Presupuesto físico $\le 78\text{ MB}$.
- **Gestión de Procesos:** No dejar procesos en segundo plano ni tareas huérfanas de Python.

## Review Focus

1. **Nitidez de Texto y Números:** Verificar que al activarse el reescalado rápido en el césped, los dígitos de soles y el botón Menú en la parte superior no muestren distorsión ni dientes de sierra.
2. **Alternancia Binaria de SELECT:** Verificar que pulsar SELECT solo alterne entre 1.0X y la velocidad configurada (1.5X o 2.0X), sin ciclar por tres estados.
3. **Persistencia de Ajustes:** Confirmar que al cambiar la velocidad rápida en Opciones y reiniciar el juego, la preferencia se conserva intacta.
4. **Cadencia Estable a 1.5X:** Comprobar que en 1.5X el avance de cuadros sea fluido (alternancia 1-2 ticks) sin micro-tirones ni desincronización de audio.
5. **Calidad de Audio bajo Hordas:** Comprobar que con 8 humosetas disparando a la vez, el limitador de 2 voces impida la saturación y elimine chasquidos sin enmudecer otros efectos.

---

## Tareas de Implementación

### Tarea 1: Reescalado Zonal Adaptativo en `GLInterface::Flush` (Césped Rápido, UI Suave)

**Archivos a Modificar:**
- [`src/SexyAppFramework/graphics/GLInterface.cpp:1675-1730`](file:///c:/DEV/PVZ/src/SexyAppFramework/graphics/GLInterface.cpp#L1675-L1730) (`GLInterface::Flush`)
- [`src/SexyAppFramework/graphics/GLInterface.h`](file:///c:/DEV/PVZ/src/SexyAppFramework/graphics/GLInterface.h)

**Interfaces y Geometría:**
- Coordenadas de origen 800×600 divididas en 120 bloques de 5 filas cada uno:
  - Bloques 0 a 15 ($Y = 0\dots 79$): Banda superior (Sol, Semillas, Pala, Menú).
  - Bloques 16 a 115 ($Y = 80\dots 579$): Tablero de césped jugable (`LAWN_YMIN = 80`).
  - Bloques 116 a 119 ($Y = 580\dots 599$): Banda inferior (Barra de progreso de oleada).
- Condición de activación: `mApp && mApp->mGameScene == SCENE_PLAYING && mApp->mBoard && (mApp->mBoard->mPlants.mSize > 20 || mApp->mBoard->mZombies.mSize > 15)`.

- [ ] **Paso 1: Definir la lógica de partición zonal en `GLInterface::Flush`:**
  En el bucle de bloques de `GLInterface::Flush`:
  ```cpp
  bool useFastLawnScaler = false;
  if (mApp && mApp->mGameScene == GameScenes::SCENE_PLAYING && mApp->mBoard)
  {
      if (mApp->mBoard->mPlants.mSize > 20 || mApp->mBoard->mZombies.mSize > 15)
      {
          useFastLawnScaler = true;
      }
  }

  for (int block = 0; block < 120; ++block)
  {
      int sy = block * 5;
      bool isLawnRow = (block >= 16 && block < 116);

      if (useFastLawnScaler && isLawnRow)
      {
          // 4 filas de césped mediante Nearest-Neighbor 5:4 ultra rápido
          for (int r = 0; r < 4; ++r)
          {
              int srcY = sy + (r * 5) / 4;
              const uint32_t* srcRow = srcBits + (srcY * 800);
              uint32_t* dst = (uint32_t*)(dstRow + r * texPitch);
              for (int dx = 0; dx < 640; dx += 4)
              {
                  dst[dx + 0] = srcRow[sScaleX[dx + 0]];
                  dst[dx + 1] = srcRow[sScaleX[dx + 1]];
                  dst[dx + 2] = srcRow[sScaleX[dx + 2]];
                  dst[dx + 3] = srcRow[sScaleX[dx + 3]];
              }
          }
      }
      else
      {
          // UI Superior, UI Inferior o fuera de gameplay: Fused Smooth Bilinear NEON
          DownscaleRow800To640(srcBits + (sy + 0) * 800, (uint32_t*)(dstRow + 0 * texPitch));
          DownscaleRow800To640(srcBits + (sy + 1) * 800, (uint32_t*)(dstRow + 1 * texPitch));
          DownscaleRow800To640(srcBits + (sy + 2) * 800, (uint32_t*)(dstRow + 2 * texPitch));
          AverageAndDownscaleRow800To640(srcBits + (sy + 3) * 800, srcBits + (sy + 4) * 800, (uint32_t*)(dstRow + 3 * texPitch));
      }
      dstRow += 4 * texPitch;
  }
  ```
- [ ] **Paso 2: Inicializar la tabla estática de mapeo `sScaleX`:**
  Asegurar que `sScaleX` esté precalculado de forma estática una sola vez para evitar recómputos por cuadro.

---

### Tarea 2: Selector Binario de Velocidad Rápida Alterna (1.0X <-> 1.5X ó 2.0X)

**Archivos a Modificar:**
- [`src/LawnApp.h:52-56`](file:///c:/DEV/PVZ/src/LawnApp.h#L52-L56) (`GameSpeedMode`)
- [`src/LawnApp.cpp:1655-1678`](file:///c:/DEV/PVZ/src/LawnApp.cpp#L1655-L1678) (`LawnApp::UpdateFrames`)
- [`src/LawnApp.cpp:1791-1805`](file:///c:/DEV/PVZ/src/LawnApp.cpp#L1791-L1805) (`LawnApp::CycleGameSpeed`)
- [`src/Lawn/Board.cpp:7721-7733`](file:///c:/DEV/PVZ/src/Lawn/Board.cpp#L7721-L7733) (`Board::DrawUIBottom`)
- [`src/SexyAppFramework/SexyAppBase.cpp:1158-1170`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.cpp#L1158-L1170) (`WriteToRegistry` / `ReadFromRegistry`)

**Interfaces:**
- `GameSpeedMode`: `SPEED_1X = 1`, `SPEED_1_5X = 2`, `SPEED_2X = 3`.
- `mFastSpeedPreference`: `SPEED_1_5X` ó `SPEED_2X` (por defecto `SPEED_2X`).
- Guardado en registro bajo la clave `"FastSpeedPreference"`.

- [ ] **Paso 1: Actualizar `GameSpeedMode` y variables en `LawnApp`:**
  ```cpp
  enum class GameSpeedMode : int
  {
      SPEED_1X = 1,
      SPEED_1_5X = 2,
      SPEED_2X = 3
  };
  ```
  Agregar en `LawnApp`:
  - `GameSpeedMode mFastSpeedPreference = GameSpeedMode::SPEED_2X;`
  - `uint32_t mSpeedUpdateCounter = 0;`

- [ ] **Paso 2: Implementar la cadencia de actualización en `LawnApp::UpdateFrames()`:**
  ```cpp
  int aUpdateCount = 1;
  if (mGameScene == GameScenes::SCENE_PLAYING && mBoard && !mBoard->mPaused && GetDialogCount() == 0)
  {
      if (mGameSpeed == GameSpeedMode::SPEED_2X)
      {
          aUpdateCount = 2;
      }
      else if (mGameSpeed == GameSpeedMode::SPEED_1_5X)
      {
          // Cadencia determinista 1.5x: alterna 1 tick y 2 ticks por fotograma (3 ticks cada 2 fotogramas)
          aUpdateCount = ((++mSpeedUpdateCounter) & 1) ? 2 : 1;
      }
  }
  ```

- [ ] **Paso 3: Implementar la alternancia binaria en `LawnApp::CycleGameSpeed()`:**
  ```cpp
  void LawnApp::CycleGameSpeed()
  {
      if (mGameSpeed == GameSpeedMode::SPEED_1X)
      {
          mGameSpeed = mFastSpeedPreference;
      }
      else
      {
          mGameSpeed = GameSpeedMode::SPEED_1X;
      }
      PlaySample(Sexy::SOUND_BUTTONCLICK);
  }
  ```

- [ ] **Paso 4: Actualizar el indicador en pantalla en `Board::DrawUIBottom()`:**
  Dibujar el cartel acorde a la velocidad activa:
  - Si `mGameSpeed == GameSpeedMode::SPEED_1_5X`: mostrar `">> 1.5X"`.
  - Si `mGameSpeed == GameSpeedMode::SPEED_2X`: mostrar `">> 2X"`.

- [ ] **Paso 5: Persistir la preferencia en el registro del juego:**
  En `SexyAppBase::WriteToRegistry`:
  `RegistryWriteInteger("FastSpeedPreference", static_cast<int>(gLawnApp->mFastSpeedPreference));`
  En `SexyAppBase::ReadFromRegistry`:
  Cargar el valor y asignar a `mFastSpeedPreference`.

---

### Tarea 3: Integración del Selector de Velocidad en el Menú de Opciones

**Archivos a Modificar:**
- [`src/Lawn/Widget/NewOptionsDialog.h:44-60`](file:///c:/DEV/PVZ/src/Lawn/Widget/NewOptionsDialog.h#L44-L60)
- [`src/Lawn/Widget/NewOptionsDialog.cpp:78-80`](file:///c:/DEV/PVZ/src/Lawn/Widget/NewOptionsDialog.cpp#L78-L80)
- [`src/Lawn/Widget/NewOptionsDialog.cpp:208-210`](file:///c:/DEV/PVZ/src/Lawn/Widget/NewOptionsDialog.cpp#L208-L210)
- [`src/Lawn/Widget/NewOptionsDialog.cpp:316-360`](file:///c:/DEV/PVZ/src/Lawn/Widget/NewOptionsDialog.cpp#L316-L360) (`ButtonDepress`)

**Interfaces:**
- Botón `mFastSpeedButton`: Reemplaza el espacio del checkbox inoperante de Aceleración 3D.
- Muestra dinámicamente `TURBO: 1.5X` o `TURBO: 2.0X`.

- [ ] **Paso 1: Agregar el botón `mFastSpeedButton` en `NewOptionsDialog`:**
  En el constructor de `NewOptionsDialog`:
  Crear el botón `mFastSpeedButton` en la posición donde estaba el checkbox inactivo de 3D Acceleration.
  Establecer la etiqueta según `mApp->mFastSpeedPreference`:
  - `TURBO: 1.5X` o `TURBO: 2.0X`.

- [ ] **Paso 2: Manejar el clic en `ButtonDepress`:**
  Al pulsar `NewOptionsDialog_FastSpeed`:
  Alternar `mApp->mFastSpeedPreference`:
  - Si es `SPEED_1_5X` $\rightarrow$ pasa a `SPEED_2X`.
  - Si es `SPEED_2X` $\rightarrow$ pasa a `SPEED_1_5X`.
  Actualizar el texto del botón y reproducir `SOUND_BUTTONCLICK`.
  Si el juego estaba actualmente en velocidad rápida, actualizar `mApp->mGameSpeed` a la nueva preferencia de inmediato.

- [ ] **Paso 3: Actualizar el dibujo de etiquetas en `NewOptionsDialog::Draw()`:**
  En lugar de dibujar "3D Acceleration" en el fondo, dibujar "Modo Turbo" o dejar que el botón muestre claramente la etiqueta con su estado actual.

---

### Tarea 4: Deduplicación y Límite de Polifonía de Audio en `SDLSoundManager`

**Archivos a Modificar:**
- [`src/SexyAppFramework/sound/SoundManager.h:36`](file:///c:/DEV/PVZ/src/SexyAppFramework/sound/SoundManager.h#L36) (`MAX_CHANNELS`)
- [`src/SexyAppFramework/sound/SDLSoundManager.cpp:65-75`](file:///c:/DEV/PVZ/src/SexyAppFramework/sound/SDLSoundManager.cpp#L65-L75) (`Mix_AllocateChannels`)
- [`src/SexyAppFramework/sound/SDLSoundManager.cpp:350-410`](file:///c:/DEV/PVZ/src/SexyAppFramework/sound/SDLSoundManager.cpp#L350-L410) (`GetFreeChannel` / `Play`)

**Interfaces:**
- Reducción de canales de mezcla: `MAX_CHANNELS = 16`.
- Límite por efecto: Máximo 2 canales concurrentes del mismo `theSoundNum` cuando `theSoundNum` pertenece a sonidos repetitivos continuos (`SOUND_FUME`, `SOUND_FROZEN`, `SOUND_CHOMP`).

- [ ] **Paso 1: Reducir canales de mezcla a 16:**
  En `src/SexyAppFramework/sound/SoundManager.h`:
  `constexpr const int MAX_CHANNELS = 16;`
  En `SDLSoundManager`: asignar 16 canales en lugar de 32, reduciendo a la mitad la carga del hilo mezclador de SDL en la CPU.

- [ ] **Paso 2: Implementar la guarda de polifonía por sonido:**
  En `SDLSoundManager::GetFreeChannel(int theSoundNum)`:
  Contar cuántos canales activos están reproduciendo actualmente `theSoundNum`.
  Si el sonido es repetitivo de ráfaga y ya cuenta con 2 canales activos:
  Retornar canal nulo (`-1`), descartando silenciosamente la voz redundante sin error.

---

### Tarea 5: Compilación Cruzada ARMv7, Verificación y Empaquetado

**Archivos a Generar y Verificar:**
- `build/PvZ` (compilación ARMv7 con Docker)
- `bin/PvZ`, `packaging/.../PvZ`, `ports/.../PvZ`
- `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`

- [ ] **Paso 1: Compilar limpiamente con el contenedor Docker de Miyoo Mini:**
  `docker run --rm -v "${PWD}:/root/workspace" -w /root/workspace aemiii91/miyoomini-toolchain make -C build -j4`
- [ ] **Paso 2: Despojar símbolos con strip:**
  `docker run --rm -v "${PWD}:/root/workspace" -w /root/workspace aemiii91/miyoomini-toolchain /opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip build/PvZ`
- [ ] **Paso 3: Desplegar binarios en los 3 árboles del repositorio:**
  Copiar `build/PvZ` a `bin/PvZ`, `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ` y `ports/Games/Plants vs. Zombies/PvZ`.
- [ ] **Paso 4: Ejecutar el conjunto de pruebas de ciclo de vida e integración:**
  `docker run --rm -v "${PWD}:/root/workspace" -w /root/workspace aemiii91/miyoomini-toolchain bash tests/test_launch_script.sh` (verificar 71/71 pass).
- [ ] **Paso 5: Ejecutar la auditoría y empaquetador de lanzamiento:**
  `python scripts/package_release.py` de forma síncrona.
- [ ] **Paso 6: Actualizar el grafo de Trail:**
  `trail build` (garantizando $0 de API key y 0 nodos huérfanos).
- [ ] **Paso 7: Realizar commit en Git y entregar resumen al usuario.**
