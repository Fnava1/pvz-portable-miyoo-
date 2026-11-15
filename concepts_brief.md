# Estabilización de la Tienda de Crazy Dave en el Interludio de Supervivencia

* **Author:** Fnava1
* **Date:** 2026-11-14

## Overview

Este documento establece el plan técnico y de arquitectura para erradicar los cierres inesperados (fallos de segmentación `SIGSEGV` y terminación forzada por falta de memoria `OOM SIGKILL`) al ingresar y salir de la tienda de Crazy Dave desde la pantalla de selección de semillas (`SeedChooserScreen`) durante el interludio entre oleadas en los modos de Supervivencia (`Survival: Endless` y `Survival: Pool Hard`) en la consola portátil Miyoo Mini Plus (SoC SigmaStar SSD202D, ARM Cortex-A7, 128 MB RAM, techo OOM de 85 MB).

La solución preserva el 100% de la funcionalidad, diálogos, fonemas Foley, movimientos de boca y presencia visual de Crazy Dave en la tienda, aislando por completo su renderizado y pausando el tablero de fondo sin purgar la caché activa de plantas.

## Analysis

### Síntesis Multi-Lente de Goldfishes (Hallazgos y Mecanismos Forenses)

1. **Vulnerabilidad de Puntero Invalido (Use-After-Free en `SeedChooserScreen.cpp`):**
   * En `SeedChooserScreen::ButtonDepress` (líneas 765-768), se ejecuta:
     ```cpp
     StoreScreen* aStore = mApp->ShowStoreScreen();
     aStore->mBackButton->SetLabel("[STORE_BACK_TO_GAME]");
     aStore->WaitForResult(); // autoKill por defecto es TRUE en Dialog.h
     if (aStore->mGoToTreeNow) // <-- USE-AFTER-FREE
     ```
   * Al hacer clic en "Regresar al juego", `Dialog::WaitForResult(true)` invoca `gSexyAppBase->KillDialog(mId)`, removiendo y encolando `aStore` para su destrucción. Evaluar `aStore->mGoToTreeNow` lee memoria invalidada del heap, provocando un fallo de segmentación instantáneo.
   * *Resolución:* Invocar `aStore->WaitForResult(false)`, almacenar la variable local `bool aGoToTree = aStore->mGoToTreeNow;` y destruir explícitamente el diálogo con `mApp->KillStoreScreen()`.

2. **Colisión de Renderizado y Duplicación de Crazy Dave (`Board.cpp`):**
   * El interludio de supervivencia se ejecuta bajo el estado `mGameScene == GameScenes::SCENE_LEVEL_INTRO`.
   * En `Board::DrawUITop` (líneas 8373-8382), el motor posee una guarda que evalúa `if (mApp->mGameScene == GameScenes::SCENE_LEVEL_INTRO)` e invoca `mApp->DrawCrazyDave(&aScreenSpace)`, dibujando a Dave sobre las coordenadas del césped.
   * Al abrir la tienda, `StoreScreen::StorePreload()` invoca `mApp->CrazyDaveEnter()`, inicializando `mCrazyDaveReanimID`. Acto seguido, `StoreScreen::Draw()` renderiza a Dave en la camioneta (`&gCrazyDave`).
   * Debido a esto, el motor intenta renderizar dos instancias de Crazy Dave en matrices de coordenadas contradictorias en el mismo fotograma.
   * *Resolución:* Excluir el dibujado de Dave en `Board::DrawUITop` si el diálogo modal `DIALOG_STORE` está activo o si la cinemática no demanda explícitamente a Dave (`mCutScene->IsShowingCrazyDave()`).

3. **Reentrancia y Ejecución en Segundo Plano del Tablero (`Board.cpp`):**
   * Durante la selección de semillas, `mBoard->mPaused` es `false`.
   * Aunque `StoreScreen` es modal, el bucle de eventos `UpdateAppStep` continúa actualizando el gestor de widgets (`WidgetManager::UpdateAll`), ejecutando `Board::Update()` por debajo de la tienda.
   * Esto mantiene activas las partículas de agua de la piscina, la cinemática de la cámara (`CutScene::Update`) y físicas de objetos, desperdiciando ciclos de CPU en el Cortex-A7.
   * *Resolución:* Establecer `mBoard->mPaused = true` de forma incondicional al abrir la tienda y saltar `CutScene::Update()` mientras `DIALOG_STORE` esté en pantalla.

4. **Presupuesto de Memoria RAM y Supresión de Precarga Ciega (`CutScene.cpp`):**
   * En Supervivencia avanzada, el tablero retiene el césped, plantas sembradas, podadoras y el `SpriteCachePool` (ocupando ~58.5 MB combinados).
   * Al abrir la tienda, `DelayLoad_Store` añade ~10.2 MB de texturas de fondo y coche, y Dave añade ~3.8 MB.
   * Adicionalmente, `CutScene::PreloadResources()` forzaba la precarga ciega de 6 atlas masivos de zombies calcinados (`REANIM_ZOMBIE_CHARRED`, `_IMP`, `_DIGGER`, `_ZAMBONI`, `_CATAPULT`, `_GARGANTUAR`), consumiendo ~11.4 MB adicionales.
   * El total acumulado alcanzaba 89.4 MB, superando el límite de 85 MB del kernel Linux de la Miyoo Mini y provocando el cierre forzado por *OOM Killer*.
   * *Resolución:* Eliminar la precarga universal e indiscriminada de los 6 zombies calcinados. Condicionar la precarga de calcinados únicamente a la presencia de plantas explosivas (Petacereza, Jalapeño, Petaseta, Mazorcañon) en el banco de semillas del jugador, y solo para los tipos de zombies permitidos en la oleada actual (`mBoard->mZombieAllowed[...]`).

## Goals vs. Non-Goals

### In-Scope Goals
* **G1:** Habilitar la apertura y retorno sin fallos de la tienda de Crazy Dave desde `SeedChooserScreen` en cualquier momento del interludio de supervivencia.
* **G2:** Preservar al 100% la presencia, animaciones (`anim_enter`, `anim_idle`, `anim_blahblah`), diálogos, sonidos Foley y apertura de maletero de Crazy Dave en la tienda.
* **G3:** Pausar la actualización del tablero (`Board::Update`) y cinemáticas (`CutScene::Update`) mientras la tienda esté abierta.
* **G4:** Suprimir el renderizado duplicado de Crazy Dave en el césped en `Board::DrawUITop` cuando `DIALOG_STORE` esté presente.
* **G5:** Reemplazar la precarga indiscriminada de 6 zombies calcinados por precarga selectiva basada en las semillas del jugador y los zombies de la ronda, asegurando un consumo de RAM pico inferior a 80 MB (margen de 5 MB bajo el techo de 85 MB).
* **G6:** Corregir el Use-After-Free en `SeedChooserScreen.cpp` mediante `WaitForResult(false)` y extracción segura de variables antes de `mApp->KillStoreScreen()`.

### Out-of-Scope Non-Goals
* **NG1:** No se modificará la apariencia, diálogos, catálogo ni comportamiento de Crazy Dave en la tienda.
* **NG2:** No se vaciarán los slots de la caché de plantas (`SpriteCachePool`) en el interludio, evitando caídas de FPS al volver al combate.
* **NG3:** No se reestructurará el sistema de guardado de partidas ni la economía de monedas.

## Alternatives Weighed

* **Alternativa A (Eliminar la reanimación de Dave en la tienda):** Rechazada por el autor. Rompe la identidad visual y la inmersión del juego.
* **Alternativa B (Vaciar todo el pool de sprites de plantas al entrar a la tienda):** Descartada. Solo recupera ~1.8 MB y genera un congelamiento perceptible al cerrar la tienda para redecodificar las plantas en el césped.
* **Alternativa C (Destruir y reconstruir el tablero completo al abrir la tienda):** Descartada. Destruir `mBoard` en pleno interludio corrompe el estado de oleada de supervivencia y fragmenta la memoria RAM.
* **Alternativa D (Precarga on-demand / perezosa de zombies calcinados durante el combate):** Descartada en favor de precarga selectiva. La decodificación en caliente de un Gargantuar calcinado durante una explosión masiva provocaría un tirón severo de varios fotogramas en la CPU Cortex-A7.

## Key Decisions & Staging for Next Steps

1. **Decisión 1 (Propiedad Exclusiva de Dave):** `StoreScreen` asume la propiedad visual temporal de Crazy Dave. `Board::DrawUITop` subordina su dibujado a la ausencia de diálogos modales de tienda.
2. **Decisión 2 (Ciclo de Vida Seguro del Diálogo):** En `SeedChooserScreen.cpp`, `WaitForResult(false)` previene la autodestrucción prematura de `StoreScreen`, garantizando lecturas seguras de flags antes de liberar la memoria.
3. **Decisión 3 (Presupuesto de RAM Acotado):** La precarga selectiva de animaciones de calcinado en `CutScene.cpp` garantiza que el proceso no supere los 79.8 MB en el peor escenario en la Miyoo Mini.
