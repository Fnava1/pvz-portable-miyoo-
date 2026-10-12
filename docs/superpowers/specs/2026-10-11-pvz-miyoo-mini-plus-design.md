# Especificación de Diseño: Port de Plants vs. Zombies GOTY a Miyoo Mini Plus

- **Fecha:** 2026-10-11
- **Estado:** Especificación Aprobada para Planificación
- **Plataforma Objetivo:** Miyoo Mini Plus (SoC SigmaStar SSD202D, OnionOS)
- **Base de Código:** [`wszqkzqk/PvZ-Portable`](https://github.com/wszqkzqk/PvZ-Portable) (C++20, SDL2, SDL-Mixer-X)
- **Autor/Equipo:** Pair programming (Usuario & Antigravity)

---

## 1. Contexto e Intención del Proyecto

El objetivo es lograr un port nativo, fluido y de calidad comercial de *Plants vs. Zombies: Game of the Year Edition* para la consola portátil **Miyoo Mini Plus (MMP)** ejecutando **OnionOS**.

### Por qué en este momento
PvZ es uno de los títulos clásicos de PC más reconocidos de PopCap. Aunque se puede emular mediante Nintendo DS (DraStic) o la versión móvil Java (J2ME), no existe un port nativo del motor de PC en 640×480 a pantalla completa. Lograrlo representa un hito técnico equivalente a los puertos de *Zuma Deluxe*, *Mario Kart 64 Decomp* o *Stardew Valley*.

### Filosofía Legal
El proyecto opera bajo el modelo **Clean-Room / Bring Your Own Data (BYOD)**:
- El repositorio contendrá **0% de activos propietarios** (sin texturas, música, sonidos, scripts ni `.pak`).
- El repositorio contendrá **0 ejecutables `.exe` de Windows** en Git para evitar falsos positivos de antivirus y bloqueos de Microsoft SmartScreen (lección aprendida de Zuma Deluxe; utilidades auxiliares como `unmo3` se descargan bajo demanda si se requieren).
- El usuario provee sus propios archivos adquiridos legítimamente desde su copia de Steam / EA App.

---

## 2. Objetivos, No-Objetivos y Anti-Objetivos

### Objetivos Principales (Goals)
1. **Rendimiento Sólido:** Alcanzar 30–60 FPS estables durante gameplay normal en los 50 niveles del modo Aventura.
2. **Compatibilidad Estricta de Memoria:** Operar dentro del límite de memoria usable de la consola (**~80–95 MB de RAM**) sin que el asesino OOM del kernel de Linux termine el proceso.
3. **Instalación Flexible con Detección Inteligente:** Método primario por asistente en PC (3 segundos), con conversor autónomo en consola para quienes prefieran copiar archivos brutos. Si se detecta conversión previa de PC, el conversor en consola se remueve para ahorrar espacio.
4. **Ergonomía Nativa para Handheld:** Control adaptado al D-pad: *grid-snapping* en el jardín con auto-recolección de soles, navegación directa en la tienda y cursor virtual contextual suave para modos libres (Jardín Zen, minijuegos).
5. **Ciclo de Vida Resiliente:** Salida limpia sin cuelgues ni pantallas negras, protección contra corrupción de MicroSD/temas y manejo ordenado de señales.

### No-Objetivos (Non-Goals)
1. **Soporte inicial para Miyoo Mini v1/v2/v3 (no-Plus):** El modelo original cuenta con un procesador monocore / PMIC restringido (máx 1300–1400 MHz). La compatibilidad con el modelo estándar queda postergada hasta que la versión Plus sea 100% funcional y estable.
2. **Resolución Widescreen (16:9):** La pantalla de la Miyoo Mini Plus es nativa 4:3 (640×480). El juego conservará su relación de aspecto original 4:3.
3. **Modos multijugador en red:** Aunque el dispositivo tiene Wi-Fi, el foco exclusivo es el juego monojugador local con paridad completa de partidas guardadas.

### Anti-Objetivos (Qué arruinaría el port)
1. **Pérdida de audio/música:** Un port sin banda sonora ni efectos sonoros icónicos (Laura Shigihara) se considera inaceptable.
2. **Latencia o imprecisión en los controles:** Un cursor flotante libre difícil de apuntar con la cruceta arruinaría la experiencia en oleadas rápidas.
3. **Corrupción de sistema al salir:** Un juego que congele la consola o requiera extraer la batería al cerrarse (destruyendo configuraciones de OnionOS) no es admisible.

---

## 3. Restricciones del Hardware y la Plataforma

| Componente | Especificación MMP | Desafío Técnico | Solución de Ingeniería |
|---|---|---|---|
| **SoC / CPU** | SigmaStar SSD202D (Dual ARM Cortex-A7) | CPU in-order limitada; sin división hardware en ARMv7 base | Overclock a **1700 MHz** vía `cpuclock`; compilación con `-mcpu=cortex-a7` (`sdiv`/`udiv`), vectorización NEON (`-mfpu=neon-vfpv4`) |
| **GPU** | **Ninguna (Sin OpenGL ni 3D)** | *PvZ-Portable* usa OpenGL ES 2.0 por defecto | Desactivar OpenGL; implementar rasterizador software sobre superficies SDL2; pool DMA de 16 MB (`SDL_MMIYOO_TEXTURE_POOL=1`); evaluar aceleración 2D `MI_GFX` |
| **Memoria RAM** | 128 MB SiP (**~80–95 MB utilizables**) | PvZ PC consume 400–500 MB en RAM | Pipeline de reescalado offline al 80% (640×480); empaquetado en `main_mmp.pak`; audio a 22.05 kHz mono |
| **Pantalla** | 3.5" IPS LCD, **640×480 (4:3)** | PvZ PC original es 800×600 (factor 0.8×) | Texturas al 80% en `main_mmp.pak`; transformación global de renderizado $0.8\times$ en viewport; coordenadas lógicas preservadas en 800×600 |
| **Almacenamiento** | MicroSD FAT32 / exFAT | Lecturas aleatorias de cientos de archivos sueltos congelan el juego | Archivo consolidado cifrado `main_mmp.pak` (lectura lineal en stream) |
| **Audio** | DAC SigmaStar (`MI_AO`) / ALSA | `audioserver` de OnionOS bloquea el dispositivo | `killall -9 audioserver` al iniciar, reinicio al salir en el trap de cleanup |
| **Entorno OS** | Linux 4.9.84 (OnionOS) | Botón MENU físico emite `SDLK_HOME` / `SDLK_RALT` | Mapeo en el motor a `KEYCODE_ESCAPE`; trap de señales con `sync` |

---

## 4. Decisiones de Diseño y Arquitectura de Subsistemas

### 4.1. Esquema de Control Híbrido y Experiencia de Usuario
- **Menús Principales y Diálogos:** Navegación por cruceta D-pad para resaltar botones/opciones y botones frontales para confirmar.
- **Batallas en el Jardín (Gameplay Principal):**
  - **Barra de Semillas:** Los gatillos **L** y **R** ciclan de forma inmediata y secuencial entre los paquetes de plantas disponibles.
  - **Movimiento:** La cruceta D-pad desplaza el indicador visual por **saltos discretos celda a celda (*grid snapping* en 9×5)**.
  - **Auto-recolección de Soles al Sobrevolar:** Cuando el cursor visual pasa sobre un sol o moneda, este se recolecta automáticamente al vuelo sin necesidad de presionar ningún botón.
  - **Botón A:** Plantar la semilla seleccionada en la casilla activa / confirmar.
  - **Botón B:** Cancelar selección activa o devolver la semilla a la barra.
  - **Botón X:** Acceso directo a la pala para desenterrar la planta de la casilla actual.
- **Tienda de Dave el Loco (*Twiddydinkies*):**
  - Navegación directa por listas y estantes verticales mediante D-pad (Arriba/Abajo/Izquierda/Derecha), **A** para comprar y **B** para salir.
- **Jardín Zen, Zombatar y Minijuegos de Interacción Libre (*Whack-a-Zombie*, *Zombiquarium*):**
  - El motor activa contextualmente un **cursor virtual suave** controlado por el D-pad con aceleración progresiva y retardo de desaceleración para permitir clics libres precisos.
- **Botón MENU (físico):** Mapeado a `KEYCODE_ESCAPE` para abrir pausa y opciones en cualquier pantalla.

### 4.2. Ingesta de Datos, Conversión y Detección Inteligente
- **Método Primario (Asistente en PC - `Launcher.py`):**
  - Asistente multiplataforma en Python/Tkinter (sin dependencias externas, como en Zuma Deluxe).
  - Detecta la instalación de Steam/EA App de PvZ GOTY.
  - Extrae `main.pak`, redimensiona texturas al 80% (640×480), remuestrea WAVs a 22.05 kHz mono y empaqueta `main_mmp.pak` en ~3 segundos.
  - Copia los archivos preparados a la MicroSD y escribe el indicador `.installed`.
- **Método Secundario (Conversión Autónoma en Consola - `setup_miyoo`):**
  - Para usuarios que copian la carpeta de Steam directamente a la MicroSD sin pasar por el asistente de PC.
  - Al iniciar por primera vez, `launch.sh` evalúa si el juego ya fue preparado.
  - **Lógica de Detección y Limpieza:**
    - **Si `main_mmp.pak` o `.installed` existe:** La conversión en PC ya se completó. `launch.sh` **elimina el binario `setup_miyoo`** para liberar almacenamiento en la MicroSD y arranca el juego directamente.
    - **Si solo existe `main.pak` (datos brutos de PC):** Se ejecuta automáticamente `setup_miyoo`.
      - Muestra interfaz gráfica en `/dev/fb0` con barra de progreso.
      - Descomprime y procesa secuencialmente en memoria (consumo RAM < 10 MB).
      - Genera `main_mmp.pak` de forma atómica.
      - Tras finalizar y verificar la integridad al 100%, renombra `main.pak` a `main.pak.bak` (operación no destructiva) y crea `.installed`.
      - Elimina el binario `setup_miyoo` tras el primer uso exitoso.

### 4.3. Subsistema Gráfico y Espacio de Coordenadas
- **Renderizador por Software:**
  - Supresión de OpenGL ES 2.0 en CMake.
  - Integración de un backend de renderizado por software sobre `SDL_Surface` / `SDL_Renderer` con operaciones de blitting optimizadas.
  - Preasignación de 16 MB de memoria contigua DMA mediante `SDL_MMIYOO_TEXTURE_POOL=1`.
- **Coordenadas Lógicas vs. Texturas al 80%:**
  - Las coordenadas lógicas internas del juego se mantienen intactas en **800×600** (preservando offsets de huesos Reanim, XMLs de TodParticleSystem, fuentes y cajas de colisión sin modificar archivos de datos).
  - El pipeline de renderizado aplica una **matriz de transformación global de escala de $0.8\times$** en la cámara/viewport, mapeando exactamente el canvas lógico de 800×600 a la pantalla física de 640×480.
  - Las texturas físicas en `main_mmp.pak` ya se encuentran al 80%, por lo que se renderizan 1:1 en el búfer de salida sin escalado redundante por software.
- **Aceleración NEON:**
  - Vectorización de transformaciones afines de Reanim y mezclas alfa de TodParticleSystem mediante intrínsecos ARM NEON (`-mcpu=cortex-a7 -mfpu=neon-vfpv4`).

### 4.4. Subsistema de Audio
- Reemplazo de la librería propietaria BASS por `libopenmpt` integrado en SDL-Mixer-X para decodificación y reproducción fiel de los archivos `.mo3`.
- Streaming de música desde disco en bloques pequeños para preservar el presupuesto de RAM.
- Aislamiento del hardware de audio en `launch.sh`:
  ```bash
  killall -9 audioserver 2>/dev/null
  ```

### 4.5. Ciclo de Vida y Resiliencia en OnionOS
- **Frecuencia de CPU:** Overclock seguro establecido en **1700 MHz** mediante la utilidad oficial `cpuclock` de OnionOS:
  ```bash
  /mnt/SDCARD/.tmp_update/bin/cpuclock 1700
  ```
- **Limpieza y Protección de Tarjeta MicroSD:**
  ```bash
  cleanup() {
      sync
      /mnt/SDCARD/.tmp_update/bin/cpuclock 1200
      /customer/app/audioserver &
  }
  trap cleanup EXIT INT TERM HUP
  ```
- **Prevención de Pantalla Negra al Salir:**
  - Desenrollado limpio del bucle principal de eventos (prohibición de `exit(0)`).
  - Invocación de `ResetFramebuffer()` tras `SDL_Quit()` para restablecer el controlador `MI_DISP` y los offsets de `/dev/fb0`.
- **Mapeo de Tecla Menú:**
  - Manejo de `SDLK_HOME` (scancode 102) y `SDLK_RALT` (scancode 100) en el despachador de eventos mapeándolos a `KEYCODE_ESCAPE`.

---

## 5. Estructura de Directorios en OnionOS

Sigue estrictamente el estándar de **Pure Ports Collection** de OnionOS:

```text
MicroSD Root
└── Roms/
    └── PORTS/
        ├── Imgs/
        │   └── Plants vs. Zombies.png          # Boxart para el menú de Ports
        ├── Shortcuts/
        │   └── Strategy/
        │       └── Plants vs. Zombies.port     # Script lanzador del menú
        └── Games/
            └── Plants vs. Zombies/
                ├── PvZ                         # Binario ejecutable ARMv7 Cortex-A7 (0755)
                ├── setup_miyoo                 # Conversor autónomo (se autodestruye al instalar)
                ├── launch.sh                   # Script principal con traps y frecuencias (0755)
                ├── main_mmp.pak                # Contenedor de assets optimizados (640x480)
                ├── properties/                 # Archivos de definición y configuración XML
                ├── libs/                       # Librerías compartidas (SDL2 Miyoo fork, libopenmpt)
                │   ├── libSDL2-2.0.so.0
                │   └── libopenmpt.so
                └── userdata/                   # Partidas guardadas y perfiles (.dat)
```

---

## 6. Tareas del Constructor (Builder Implementation Plan)

1. **Entorno Docker:** Configurar compilación cruzada con `aemiii91/miyoomini-toolchain`.
2. **PvZ-Portable Build & Software Blitter:** Desactivar GLES2 e integrar el backend de renderizado por software SDL2 con soporte para escala $0.8\times$ y DMA texture pool.
3. **Conversor de PC (`tools/build_release.py` / `Launcher.py`):** Crear el generador de releases con empaquetador de assets y GUI Tkinter.
4. **Conversor en Consola (`setup_miyoo`):** Implementar la utilidad de contingencia para framebuffer `/dev/fb0` con procesamiento seguro y autodestrucción tras detección.
5. **Capa de Control Híbrida:** Implementar *grid-snapping* 9×5 con auto-hover de soles en batalla, navegación directa en Dave's shop y cursor virtual suave en Zen Garden/minijuegos.
6. **Auditoría de Memoria y Rendimiento:** Verificar con Massif y profiling en hardware real que el consumo sostenido no exceda los 85 MB de RAM a 1700 MHz.
