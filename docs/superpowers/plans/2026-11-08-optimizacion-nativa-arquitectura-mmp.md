# Optimización Nativa de Arquitectura y Rendimiento para Miyoo Mini Plus (PvZ-Portable)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminar de raíz la lentitud y pesadez del juego en escenas densas y con múltiples plantas en el jardín en la Miyoo Mini Plus (SoC SigmaStar SSD202D, ARM Cortex-A7 dual @ 1.2 GHz, 128 MB RAM), manteniendo 100% de fidelidad visual (animaciones continuas y fluidas a 60 FPS, sin saltos robóticos ni frame skipping).

**Architecture:** Canal directo de rasterizado en `SWTri` para eludir el doble recorte redundante de Sutherland-Hodgman y conversión intermedia de structs; unificación y persistencia de batches de triángulos entre plantas y sus accesorios (attachments); pre-cálculo e inversión analítica 2D para matrices de pose base en `Reanimator`; aceleración ARM NEON sin divisiones de 64 bits en `FastStretchBlt`; culling cinemático de huesos para zombies en spawn off-screen ($x > 840$); y acceso directo $O(1)$ a la cuadrícula de plantorchas para proyectiles.

**Tech Stack:** C++17, ARMv7-A NEON SIMD, VFPv4, Linux embebido (Miyoo Mini Plus / OnionOS), PopCap SexyAppFramework.

**Spec:** Análisis de código base y cuellos de botella reales de la Miyoo Mini Plus contrastado contra `.trail/learnings/` (noviembre 2026).

---

## Global Constraints

- **Cero Degradación Visual:** Las plantas y zombies deben animarse de forma 100% continua y suave a 60 FPS. Queda estrictamente prohibido aplicar discretización de fotogramas (AnimLoop escalonado) o saltos bruscos que produzcan aspecto pixel-art o robótico.
- **Sin Dependencias Externas:** Cero dependencias adicionales de terceros.
- **Preservación de Aprendizajes de TRAIL:** No repetir soluciones descartadas (hilo worker en Core 1 causó chasquidos de audio; recorte de deuda de ticks rompió menús; culling de partículas rompió indicadores de juego).
- **Terminación de Tareas:** Cualquier script en Python o comando en segundo plano debe ejecutarse de forma estrictamente síncrona o finalizarse inmediatamente sin dejar procesos huérfanos.

## Review Focus

1. **Recorte en bordes de pantalla:** Triángulos parcialmente fuera de pantalla deben recortarse exactamente con los mismos límites sin desbordar el framebuffer ni generar artefactos visuales.
2. **Accesorios animados (Attachments):** Cabezas de Lanzaguisantes, flores de Girasoles y objetos sostenidos deben seguir la matriz de su hueso padre con precisión matemática exacta.
3. **Escalado de sombras:** Sombras de setas (0.5x), nueces altas (1.3x) y calabazas (1.4x) deben mantener el mismo canal alpha y suavizado sin artefactos en sus bordes.
4. **Zombies cruzando la frontera de spawn ($x = 840$):** Al entrar en el campo de visión, el esqueleto del zombie debe estar perfectamente ensamblado sin mostrar poses en T o huesos flotantes.
5. **Colisión de guisantes con Plantorchas:** El truco clásico de "torchwood clip" y el encendido de guisantes a fuego deben funcionar de forma idéntica sin perder ningún impacto.

---

### Task 1: Canal Directo de Rasterizado en `SWTri` y `MemoryImage` (Bypass del Doble Sutherland-Hodgman)

**Files:**
- Modify: `src/SexyAppFramework/graphics/SWTri.h`
- Modify: `src/SexyAppFramework/graphics/SWTri.cpp:292-523`
- Modify: `src/SexyAppFramework/graphics/MemoryImage.h`
- Modify: `src/SexyAppFramework/graphics/MemoryImage.cpp:1820-1846`
- Modify: `src/PvzpLib/EffectSystem.cpp:392-425`

**Interfaces:**
- Consumes: `TriVertex mVertArray[MAX_TRIANGLES][3]` ya recortados contra `theClipRect` en `PvzpTriangleGroup::AddTriangle`.
- Produces: `void MemoryImage::BltTrianglesTexDirect(Image *theTexture, const TriVertex theVertices[][3], int theNumTriangles, const Rect &theClipRect, const Color &theColor, int theDrawMode, void *theSurface, int theBytePitch, int thePixelFormat, float tx, float ty, bool blend)` que realiza conversión directa `TriVertex` -> `SWVertex` y llama directamente a `SWDrawTriangle`, saltando `SWDrawShape`, `clipShape` y la estructura intermedia `XYZStruct`.

- [ ] **Step 1: Declarar `BltTrianglesTexDirect` en `MemoryImage.h` y `SWDrawTriangleDirect` en `SWTri.h`**
- [ ] **Step 2: Implementar `SWDrawTriangleDirect` en `SWTri.cpp`**
  - Configura `SWTextureInfo` una sola vez por lote en lugar de una vez por cada triángulo.
  - Convierte las coordenadas flotantes a punto fijo $16.16$ de forma directa.
  - Invoca directamente `SWDrawTriangle`.
- [ ] **Step 3: Implementar `BltTrianglesTexDirect` en `MemoryImage.cpp`**
  - Mapea el formato de píxeles y superficie de destino directamente sin pasar por `SWDrawShape`.
- [ ] **Step 4: Conectar `PvzpTriangleGroup::DrawGroup` en `EffectSystem.cpp`**
  - Para renderizado por software (`!gSexyAppBase->Is3DAccelerated()`), llamar a `g->mDestImage->BltTrianglesTexDirect(...)` en lugar del blit genérico.
- [ ] **Step 5: Compilar y verificar**
  - Comando: `make -j4`
  - Esperado: Compilación limpia sin advertencias.
- [ ] **Step 6: Commit**
  - `git add src/SexyAppFramework/graphics/SWTri.h src/SexyAppFramework/graphics/SWTri.cpp src/SexyAppFramework/graphics/MemoryImage.h src/SexyAppFramework/graphics/MemoryImage.cpp src/PvzpLib/EffectSystem.cpp`
  - `git commit -m "perf: bypass redundant 2nd Sutherland-Hodgman clipping and struct repackaging in SWTri"`

---

### Task 2: Pre-cálculo e Inversión Analítica 2D para Matrices de Pose Base en `Reanimator`

**Files:**
- Modify: `src/PvzpLib/Reanimator.h`
- Modify: `src/PvzpLib/Reanimator.cpp:1117-1130`
- Modify: `src/PvzpLib/PvzpCommon.h`
- Modify: `src/PvzpLib/PvzpCommon.cpp:978-1001`

**Interfaces:**
- Consumes: `ReanimatorTrack` y `ReanimatorDefinition` cargados en `ReanimatorLoadDefinition`.
- Produces: `mBasePoseMatrixInv` precalculado en `ReanimatorTrack`, y función analítica 2D `SexyMatrix3InverseAffine2D` (6 multiplicaciones, 1 división) para matrices afines 2D.

- [ ] **Step 1: Implementar `SexyMatrix3InverseAffine2D` en `PvzpCommon.cpp`**
  - Aprovecha que en 2D afín la tercera fila es estrictamente `[0, 0, 1]` para resolver la inversa analíticamente sin cofactores $3\times3$ genéricos.
- [ ] **Step 2: Añadir campos `mBasePoseMatrixInv` y `mHasBasePoseMatrixInv` a `ReanimatorTrack` en `Reanimator.h`**
- [ ] **Step 3: Pre-calcular la matriz inversa en `ReanimatorLoadDefinition` o al primer acceso**
- [ ] **Step 4: Modificar `Reanimation::GetAttachmentOverlayMatrix` en `Reanimator.cpp`**
  - Leer directamente `aTrack->mBasePoseMatrixInv` en lugar de llamar a `GetTrackBasePoseMatrix` y `SexyMatrix3Inverse` dinámicamente en cada frame.
- [ ] **Step 5: Compilar y verificar**
  - Comando: `make -j4`
  - Esperado: Compilación exitosa.
- [ ] **Step 6: Commit**
  - `git add src/PvzpLib/Reanimator.h src/PvzpLib/Reanimator.cpp src/PvzpLib/PvzpCommon.h src/PvzpLib/PvzpCommon.cpp`
  - `git commit -m "perf: cache static base pose matrix inverse and implement analytical 2D affine inversion"`

---

### Task 3: Persistencia y Unificación de Batches de Triángulos para Attachments y Plantas

**Files:**
- Modify: `src/PvzpLib/Reanimator.h`
- Modify: `src/PvzpLib/Reanimator.cpp:1023-1047`
- Modify: `src/PvzpLib/Attachment.h`
- Modify: `src/PvzpLib/Attachment.cpp:871-882`

**Interfaces:**
- Consumes: `PvzpTriangleGroup`
- Produces: `DrawRenderGroup` y `AttachmentDraw` aceptando `PvzpTriangleGroup* theParentGroup = nullptr`.

- [ ] **Step 1: Actualizar la firma de `Reanimation::DrawRenderGroup` y `AttachmentDraw`**
  - Permitir recibir opcionalmente un puntero a un grupo de triángulos existente.
- [ ] **Step 2: Conectar el grupo en `Reanimation::DrawRenderGroup`**
  - Si se proporciona un grupo padre, acumular en él en lugar de instanciar un buffer de 21 KB en el stack.
  - Al procesar un attachment, pasar el grupo a `AttachmentDraw` para que las piezas adjuntas se acumulen en el mismo lote sin vaciar el rasterizador.
- [ ] **Step 3: Compilar y verificar**
  - Comando: `make -j4`
  - Esperado: Compilación exitosa sin regresiones en el dibujado de accesorios.
- [ ] **Step 4: Commit**
  - `git add src/PvzpLib/Reanimator.h src/PvzpLib/Reanimator.cpp src/PvzpLib/Attachment.h src/PvzpLib/Attachment.cpp`
  - `git commit -m "perf: unify triangle batches across reanimation attachments to eliminate micro-flushes"`

---

### Task 4: Vectorización ARM NEON y Eliminación de Divisiones Dobles en `FastStretchBlt`

**Files:**
- Modify: `src/SexyAppFramework/graphics/MemoryImage.cpp:1532-1747`

**Interfaces:**
- Consumes: `MemoryImage::FastStretchBlt(Image* theImage, const Rect& theDestRect, const FRect& theSrcRect, const Color& theColor, int theDrawMode)`
- Produces: Escalador horizontal acelerado con NEON SIMD para píxeles contiguos y cálculo VFPv4 flotante sin divisiones de 64 bits (`double`).

- [ ] **Step 1: Reemplazar divisiones `double` por punto flotante simple VFPv4**
  - Sustituir `(theSrcRect.mWidth / (double)theDestRect.mWidth) * 65536.0` por `(theSrcRect.mWidth / static_cast<float>(theDestRect.mWidth)) * 65536.0f`.
- [ ] **Step 2: Pre-calcular límites de recorte horizontal válidos por fila**
  - Separar el tramo de píxeles interiores que no requieren comprobaciones condicionales `sx < 0` o `sx > maxSrcX`.
- [ ] **Step 3: Implementar mezclado alpha vectorizado con ARM NEON para tramos continuos**
  - Usar `vld1q_u8` para cargar 4 píxeles fuente y 4 de destino, y calcular la mezcla con instrucciones `vmull_u8` y `vrshrn_u16`.
- [ ] **Step 4: Compilar y verificar**
  - Comando: `make -j4`
  - Esperado: Compilación exitosa y sombras renderizadas idénticamente.
- [ ] **Step 5: Commit**
  - `git add src/SexyAppFramework/graphics/MemoryImage.cpp`
  - `git commit -m "perf: vectorize FastStretchBlt with ARM NEON and eliminate 64-bit double divisions"`

---

### Task 5: Culling Cinemático de Esqueleto para Zombies en Área de Spawn ($x > 840$)

**Files:**
- Modify: `src/Lawn/Zombie.cpp:4389-4395, 5283-5390`

**Interfaces:**
- Consumes: `mPosX`, `mBoard->mX`, `aBodyReanim`
- Produces: Actualización posicional y de fase normal, pero omisión de la cinemática de huesos y transformación de vértices mientras el zombie esté completamente fuera del viewport a la derecha ($x > 840$).

- [ ] **Step 1: Identificar la condición de fuera de pantalla en `Zombie::UpdateReanim`**
  - Comprobar si `mPosX + mBoard->mX > 850.0f` y `mZombieType != ZombieType::ZOMBIE_BOSS`.
- [ ] **Step 2: En modo fuera de pantalla, avanzar únicamente `mAnimTime` sin evaluar cinemática de huesos**
  - Mantener la sincronización temporal para que al entrar a la pantalla no haya discontinuidad.
- [ ] **Step 3: Reanudar cinemática completa en cuanto $x \le 850.0f$**
- [ ] **Step 4: Compilar y verificar**
  - Comando: `make -j4`
  - Esperado: Compilación exitosa, zombies entran caminando con fluidez idéntica.
- [ ] **Step 5: Commit**
  - `git add src/Lawn/Zombie.cpp`
  - `git commit -m "perf: cull skeletal animation bone evaluation for zombies outside visible spawn zone"`

---

### Task 6: Comprobación $O(1)$ de Cuadrícula en `PeaAboutToHitTorchwood`

**Files:**
- Modify: `src/Lawn/Board.h`
- Modify: `src/Lawn/Board.cpp:5406-5440`
- Modify: `src/Lawn/Projectile.cpp:200-225`

**Interfaces:**
- Consumes: `mTorchwoodGrid[row]` o `mTorchwoodRowMask`
- Produces: Detección instantánea $O(1)$ sin iterar por todas las plantas del jardín en cada frame para cada proyectil.

- [ ] **Step 1: Registrar en `Board::UpdateRowPlantMask()` la columna o puntero de la plantorcha por fila**
- [ ] **Step 2: Reemplazar el bucle lineal `for (Plant* aPlant : mBoard->mPlants)` en `PeaAboutToHitTorchwood` por consulta directa a la cuadrícula de la fila correspondiente**
- [ ] **Step 3: Compilar y verificar**
  - Comando: `make -j4`
  - Esperado: Compilación limpia, las plantorchas encienden los guisantes exactamente igual.
- [ ] **Step 4: Commit**
  - `git add src/Lawn/Board.h src/Lawn/Board.cpp src/Lawn/Projectile.cpp`
  - `git commit -m "perf: replace O(N) linear plant search with O(1) grid lookup in PeaAboutToHitTorchwood"`

---

### Task 7: Verificación Integral de Ciclo de Vida, Memoria y Empaquetado Release

**Files:**
- Test: `tests/test_launch_script.sh`
- Package: `scripts/package_release.py`
- Dist: `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`

**Interfaces:**
- Consumes: Binario optimizado compilado.
- Produces: Suite de pruebas 100% aprobada, auditoría de memoria $\le 78$ MB, paquete release generado y grafo de TRAIL sincronizado.

- [ ] **Step 1: Ejecutar suite de pruebas de ciclo de vida**
  - Comando: `bash tests/test_launch_script.sh`
  - Esperado: 71/71 tests PASSED.
- [ ] **Step 2: Ejecutar empaquetado release de forma síncrona**
  - Comando: `python scripts/package_release.py`
  - Esperado: Archivo `dist/PvZ_MiyooMiniPlus_v1.1.0.zip` generado correctamente sin procesos huérfanos.
- [ ] **Step 3: Actualizar grafo de TRAIL**
  - Comando: `trail build`
  - Esperado: Grafo sincronizado al 100%.
- [ ] **Step 4: Commit final**
  - `git add .`
  - `git commit -m "chore: release package and trail graph synchronization for native performance optimizations"`
