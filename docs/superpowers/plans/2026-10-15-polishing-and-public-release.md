# Plan de Trabajo: Pulido, Compatibilidad Universal y Release Público en GitHub

> **Objetivo:** Preparar la versión definitiva y pública de *Plants vs. Zombies: GOTY Edition* para la familia de consolas **Miyoo Mini** (Miyoo Mini v1/v2/v3/v4 y Miyoo Mini Plus en OnionOS), incorporando soporte dinámico de hardware/overclock, navegación ergonómica en Jardín Zen, optimizaciones probadas del ecosistema de ports y documentación integral para el release en GitHub.

---

## 1. Alcance de los Requerimientos

1. **Compatibilidad Universal Miyoo Mini (v1/v2/v3/v4 y Plus) & Overclock Seguro:**
   - Detección en tiempo de ejecución del modelo de consola sin depender de herramientas externas.
   - Asignación de frecuencias seguras validadas: **1400 MHz** para Miyoo Mini estándar (evitando congelamientos por bajo voltaje del PMIC) y **1700 MHz** para Miyoo Mini Plus.
   - Gobernanza estricta mediante el binario oficial de OnionOS `/mnt/SDCARD/.tmp_update/bin/cpuclock`, con restauración garantizada a **1200 MHz ondemand** en trampas de salida (`EXIT`, `INT`, `TERM`, `HUP`) junto con `sync`.
2. **Navegación Ergonómica en Jardín Zen (L/R, A, B):**
   - Permitir navegar entre las herramientas disponibles en la barra superior (Regadera, Fertilizante, Insecticida, Fonógrafo, Chocolate, Guante, Carretilla, Símbolo de Moneda / Tienda) utilizando los gatillos **L** y **R**, similar al ciclado de paquetes de semillas en combate.
   - Botón **A**: Equipa la herramienta seleccionada o la aplica sobre la planta/maceta en la posición actual del cursor.
   - Botón **B**: Cancela / deselecciona la herramienta en mano (`ClearCursor()`), devolviendo el cursor a su estado estándar.
3. **Propuestas de Optimización Seguras (Basadas en Ports Recientes para Miyoo Mini):**
   - **Link-Time Optimization (`-flto` & `-Wl,--as-needed`):** Optimización intermodular que reduce el tamaño del ejecutable y maximiza el inlining en la reducida caché L1 (32 KB) del Cortex-A7.
   - **Ajuste de Latencia de Audio (1024 muestras vs 2048):** Reduce la latencia de respuesta sonora de 93 ms a 46 ms a 22.05 kHz sin penalización de CPU.
   - **Tuning de VSync en Driver SDL2 Miyoo:** `SDL_MMIYOO_VSYNC_MODE=adaptive` o `off` para evitar caídas bruscas a 30 FPS ante micro-picos de renderizado.
   - **Buffer de Lectura I/O Stream (64 KB):** Minimiza la latencia de lectura flash de la tarjeta microSD al leer chunks de audio y animación.
4. **Actualización Completa de Documentación (`README.md`):**
   - Cobertura exhaustiva de ambas consolas (MM y MM+), esquema de controles unificado (Lawn grid + Zen Garden L/R), guía de instalación de 2 vías (PC GUI Builder y conversor autónomo `setup_miyoo`), arquitectura técnica y créditos.

---

## 2. Desglose de Tareas

### Tarea 1: Detección Dinámica de Modelo y Overclocking Seguro (Miyoo Mini & Plus)
* **Archivos a modificar:**
  * `ports/Games/Plants vs. Zombies/launch.sh`
  * `packaging/Roms/PORTS/Games/Plants vs. Zombies/launch.sh`
  * `tools/build_release.py`
  * `scripts/package_release.py`
  * `tests/test_launch_script.sh`
* **Diseño Técnico:**
  * Implementar la detección validada en Zuma (`c:\DEV\regitro_conocimiento_zuma.md`):
    ```sh
    IS_PLUS=0
    if [ -d /sys/class/net/wlan0 ] || grep -qi "plus" /proc/cmdline 2>/dev/null; then
        IS_PLUS=1
    fi

    if [ "$IS_PLUS" -eq 1 ]; then
        CPU_FREQ=1700
    else
        # Miyoo Mini (v1/v2/v3/v4 non-Plus): Techo seguro sin pánico del PMIC
        CPU_FREQ=1400
    fi

    if [ -f "/mnt/SDCARD/.tmp_update/bin/cpuclock" ]; then
        /mnt/SDCARD/.tmp_update/bin/cpuclock $CPU_FREQ 2>/dev/null
    elif command -v cpuclock >/dev/null 2>&1; then
        cpuclock $CPU_FREQ 2>/dev/null
    fi
    ```
  * En la función `cleanup()`, asegurar que se devuelva el reloj a 1200 MHz y el gobernador a `ondemand`.
  * Sincronizar scripts de release y actualizar las pruebas de integración en `test_launch_script.sh`.

### Tarea 2: Ergonomía y Controles de Jardín Zen con Botones (L/R, A, B)
* **Archivos a modificar:**
  * `src/Lawn/Board.h` & `src/Lawn/Board.cpp`
  * `src/input/HandheldInput.hpp` & `src/input/HandheldInput.cpp`
  * `src/SexyAppFramework/platform/default/Input.cpp`
  * `tests/test_input_mapping.cpp`
* **Diseño Técnico:**
  * Crear `Board::CycleZenTool(int theDelta)`:
    * Cuando `mGameMode == GAMEMODE_CHALLENGE_ZEN_GARDEN`:
    * Crear una lista dinámica de herramientas desbloqueadas y utilizables (`CanUseGameObject(aTool)`).
    * Al pulsar **L** (`theDelta = -1`) o **R** (`theDelta = 1`), avanzar o retroceder en la lista de herramientas disponibles.
    * Equipa inmediatamente la herramienta en `mCursorObject->mCursorType` (o posiciona el cursor sobre la herramienta y reproduce el feedback sonoro `FOLEY_DROP`).
  * En `Board::KeyDown` / `HandheldInput`:
    * Botón **A**: Si hay una herramienta en mano y se pulsa sobre una planta / maceta, ejecuta `MouseDownWithFeedingTool` o la acción correspondiente en esa celda.
    * Botón **B**: Si hay una herramienta en mano (`mCursorObject->mCursorType != CURSOR_TYPE_NORMAL`), invoca `mBoard->ClearCursor()` para soltar la herramienta limpiamente.
  * Verificar en tests de mapeo de controles que no interfiera con el ciclado normal de paquetes de semillas en los niveles de aventura / minijuegos.

### Tarea 3: Optimizaciones Seguras de Compilador y Runtime
* **Archivos a modificar:**
  * `cmake/miyoomini.toolchain.cmake`
  * `CMakeLists.txt`
  * `ports/Games/Plants vs. Zombies/launch.sh`
* **Diseño Técnico:**
  * Evaluar e incorporar Link-Time Optimization (`-flto -Wl,--as-needed`) en el toolchain ARM (`cmake/miyoomini.toolchain.cmake`), validando que la compilación en Docker termine limpia y sin símbolos indefinidos.
  * Ajustar el entorno de VSync en `launch.sh`: `export SDL_MMIYOO_VSYNC_MODE=adaptive` para prevenir bloqueos de 30 FPS si un cuadro complejo roza los 16.7 ms.
  * Evaluar el tamaño del buffer de audio streaming en `OpenMptMusicInterface`: optimizar el chunk de render a 1024 muestras (a 22050 Hz) para obtener latencia sonora inmediata (~46 ms) al plantar y recolectar soles.

### Tarea 4: Actualización Exhaustiva del `README.md` & Preparación de Release
* **Archivos a modificar:**
  * `README.md`
* **Contenido a documentar:**
  1. **Compatibilidad:** Confirmación de soporte probado en Miyoo Mini (v1/v2/v3/v4) y Miyoo Mini Plus bajo OnionOS 4.x.
  2. **Controles Físicos:**
     * Aventura / Minijuegos: D-Pad (snapping inteligente 9×5), L/R (selección rápida de plantas), A (plantar/confirmar), B (deseleccionar/pala), X (auto-colectar sol), Y/Start (pausa/menú), Select (alternar modo cursor libre).
     * Jardín Zen: L/R (ciclar herramientas superiores), A (aplicar herramienta en planta actual), B (soltar herramienta).
  3. **Rendimiento:** 60 FPS estables, aceleración NEON SIMD, escalado limpio a 640×480, consumo de RAM $\le 85$ MB.
  4. **Instalación:**
     * Método 1: PC Release Builder (`Launcher.py` con interfaz gráfica interactiva).
     * Método 2: Instalación autónoma en consola (`setup_miyoo` con progreso directo en `/dev/fb0` y auto-limpieza).
  5. **Licencias y Reconocimientos:** Reconocimiento a PopCap Games, proyectos de descompilación base y comunidad OnionOS.

---

## 3. Plan de Verificación y Criterios de Aceptación

1. **Compilación ARM:** El binario debe compilar limpiamente en el contenedor Docker oficial `aemiii91/miyoomini-toolchain`.
2. **Sincronización:** Los 4 binarios (`build/PvZ`, `bin/PvZ`, `ports/.../PvZ`, `packaging/.../PvZ`) deben compartir idéntico hash SHA256.
3. **Pruebas Automatizadas:** La suite de tests en Python (`tests/test_launch_script.sh`, `tests/test_pak_lib.py`, `tests/test_release_package.py`) debe pasar al 100%.
4. **Reglas Limpias:** Cero archivos `.exe` o assets propietarios rastreados en Git.
5. **Control de Git:** Commits locales en `main` sin ejecutar `git push`.
