# Plan de Ingeniería de Alto Impacto: Rendimiento Real en Supervivencia Infinita (Plants vs. Zombies - Miyoo Mini / OnionOS)

> **Para trabajadores agenticos:** SUB-HABILIDAD REQUERIDA: Usar `superpowers:executing-plans` para implementar este plan tarea por tarea. Los pasos usan la sintaxis de casillas (`- [ ]`) para seguimiento.

**Meta:** Eliminar la lentitud y caídas de cuadros en el modo Supervivencia Infinita cuando más del 70% del jardín está plantado con calabazas y atacan oleadas densas de zombies, atacando el 75% del consumo real de CPU sin introducir downgrades visuales artificiales.

**Arquitectura:**
1. Activar la función nativa del motor `ReanimatorCache::DrawCachedPlant` para plantas pasivas/estacionarias y calabazas intactas en estado `anim_idle`, reemplazando 96,000 transformaciones de huesos de triángulos por segundo por blits directos de sprites 2D en memoria.
2. Culling de actualización esquelética en `Zombie::UpdateReanim()` para zombies fuera de pantalla ($x > 840$), evitando calcular 30 articulaciones por zombie antes de su ingreso al césped.
3. Calibración del gobernador nativo de sobrecarga de partículas (`MAX_PARTICLES_SIZE` a 150) en `PvzpParticle`, activando la poda automática de micro-chispas secundarias en explosiones masivas de Mazorcas y Melones mediante `PARTICLE_DIE_IF_OVERLOADED`.

**Tech Stack:** C++17, PopCap SexyAppFramework, ARM NEON SIMD, SDL2 Linux Framebuffer (`/dev/fb0`), SigmaStar SSD202D.

**Especificación:** [`docs/superpowers/specs/2026-10-11-pvz-miyoo-mini-plus-design.md`](file:///c:/DEV/PVZ/docs/superpowers/specs/2026-10-11-pvz-miyoo-mini-plus-design.md) y [`c:\DEV\WIKI\registro_conocimiento_pvz_miyoo.md`](file:///c:/DEV/WIKI/registro_conocimiento_pvz_miyoo.md).

---

## Restricciones Globales

- **Cero Downgrades Inútiles:** No retirar sombras a proyectiles ni degradar el audio arbitrariamente; preservar la integridad estética original.
- **Espacio Lógico Intacto:** Mantener la lógica interna en 800×600 nativo de PopCap con el downscaler NEON 5:4 hacia 640×480 en el hilo principal.
- **Memoria RAM Rigurosa:** Presupuesto físico $\le 78\text{ MB}$ para garantizar operación en consolas de 128 MB RAM.
- **Terminación de Procesos:** No dejar procesos en segundo plano ni tareas huérfanas de Python.

---

## Tareas de Implementación

### Tarea 1: Caché Estática en Reposo para Plantas y Calabazas (`DrawCachedPlant`)

**Archivos a Modificar:**
- [`src/Lawn/Plant.cpp:4060-4100`](file:///c:/DEV/PVZ/src/Lawn/Plant.cpp#L4060-L4100) (`Plant::Draw`)
- [`src/Lawn/Plant.cpp:2805-2830`](file:///c:/DEV/PVZ/src/Lawn/Plant.cpp#L2805-L2830) (`Plant::UpdateReanim`)

**Interfaces:**
- Consume: `mApp->mReanimatorCache->DrawCachedPlant(Graphics* g, float thePosX, float thePosY, SeedType theSeedType, DrawVariation theDrawVariation)`
- Produce: Sustitución de dibujo esquelético por software por blit directo 2D cuando la planta está en reposo sin ser atacada.

- [ ] **Paso 1: Identificar las condiciones seguras de reposo para sustitución por caché:**
  Una planta puede usar `DrawCachedPlant` si:
  1. Está en el tablero (`IsOnBoard()`).
  2. Su animación activa es reposo (`aBodyReanim->IsAnimPlaying("anim_idle")`).
  3. No está sufriendo daño crítico o comiéndose a un zombie (salud completa o estado normal, `mState == STATE_NORMAL`).
  4. Pertenece a las especies estacionarias o de cobertura:
     - `SEED_PUMPKINSHELL` (Calabaza intacta: ¡ahorra dibujo doble de 40 huesos!)
     - `SEED_SUNFLOWER`, `SEED_TWINSUNFLOWER`
     - `SEED_WALLNUT`, `SEED_TALLNUT` (sin grietas)
     - `SEED_SPIKEROCK`, `SEED_SPIKEWEED`
     - `SEED_LILYPAD`, `SEED_FLOWERPOT`
     - `SEED_COFFEEBEAN`

- [ ] **Paso 2: Modificar `Plant::Draw` en `src/Lawn/Plant.cpp`:**
  En la sección de dibujado de la calabaza trasera (`aDrawPumpkinBack`) y del cuerpo principal de la planta (`mBodyReanimID`):
  Si se cumplen las condiciones de reposo y la escena es densa (`mBoard->mPlants.mSize > 20 || mBoard->mZombies.mSize > 15`), delegar a `mApp->mReanimatorCache->DrawCachedPlant(g, aOffsetX, aOffsetY, mSeedType, DrawVariation::VARIATION_NORMAL)`.

- [ ] **Paso 3: Optimizar `Plant::UpdateReanim`:**
  Si la planta está dibujándose mediante `DrawCachedPlant`, suspender `aBodyReanim->Update()` hasta que reciba un impacto o cambie de estado, eliminando el cómputo de sus matrices esqueléticas.

---

### Tarea 2: Culling de Animación Esquelética en Zombies Fuera de Pantalla

**Archivos a Modificar:**
- [`src/Lawn/Zombie.cpp:5380-5395`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L5380-L5395) (`Zombie::UpdateReanim`)

**Interfaces:**
- Consume: `mPosX`, `mBoard->mX`, `BOARD_WIDTH`
- Produce: Omisión de actualización de 30 huesos en zombies que aún no cruzan el margen derecho visible.

- [ ] **Paso 1: Agregar guarda de posición en `Zombie::UpdateReanim()`:**
  ```cpp
  if (mBoard && mZombieType != ZombieType::ZOMBIE_BOSS)
  {
      float aScreenX = mPosX + mBoard->mX;
      if (aScreenX > BOARD_WIDTH + 80.0f) // > 880.0f
      {
          // El zombie está en la zona de spawn fuera de la pantalla:
          // omitir el cálculo de sus 30 huesos hasta que ingrese al área visible.
          return;
      }
  }
  ```

- [ ] **Paso 2: Verificar sincronización al ingresar:**
  Comprobar que cuando $x \le 880$, `aBodyReanim->Update()` se reanuda de inmediato sin saltos de postura ni miembros desalineados.

---

### Tarea 3: Calibración del Gobernador Nativo de Partículas para Hardware Portátil

**Archivos a Modificar:**
- [`src/PvzpLib/PvzpParticle.h:38`](file:///c:/DEV/PVZ/src/PvzpLib/PvzpParticle.h#L38) (`MAX_PARTICLES_SIZE`)
- [`src/PvzpLib/PvzpParticle.cpp:1254-1258`](file:///c:/DEV/PVZ/src/PvzpLib/PvzpParticle.cpp#L1254-L1258) (`PvzpParticleHolder::IsOverLoaded`)

**Interfaces:**
- Consume: Sistema de partículas PopCap `PARTICLE_DIE_IF_OVERLOADED`
- Produce: Descarte selectivo de micro-chispas y humo redundante cuando se alcanzan picos de 150 partículas activas.

- [ ] **Paso 1: Redefinir `MAX_PARTICLES_SIZE` para consolas portátiles:**
  En `src/PvzpLib/PvzpParticle.h`:
  Reducir el umbral de sobrecarga de 900 a **150** partículas en plataformas embebidas (`#ifndef _WIN32`).

- [ ] **Paso 2: Validar en `PvzpParticle.cpp`:**
  Asegurar que las partículas vitales (el círculo de congelación del Melón de invierno, la onda de choque de la Mazorca o la explosión de la Petacereza) no se omiten porque no tienen la bandera `PARTICLE_DIE_IF_OVERLOADED`, mientras que los cientos de fragmentos y nubes repetitivas sí se podan de forma segura durante las saturaciones.

---

### Tarea 4: Compilación, Pruebas y Empaquetado

- [ ] **Paso 1: Compilar binario nativo con la toolchain Docker:**
  `docker run --rm -v "${PWD}:/root/workspace" -w /root/workspace aemiii91/miyoomini-toolchain make -C build -j4`
- [ ] **Paso 2: Despojar símbolos:**
  `/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip build/PvZ`
- [ ] **Paso 3: Propagar a `bin/`, `ports/` y `packaging/`:**
- [ ] **Paso 4: Ejecutar suite de pruebas de integración:**
  `bash tests/test_launch_script.sh` (71 pruebas)
- [ ] **Paso 5: Generar paquete release final:**
  `python scripts/package_release.py` -> `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`
- [ ] **Paso 6: Actualizar grafo Trail y verificar cero tareas en segundo plano.**
