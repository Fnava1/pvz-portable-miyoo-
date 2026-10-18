---
title: Intro cutscene transition and award screen reentrancy
author: Fnava1
date: 2026-10-18
branch: main
touches:
  - src/Lawn/CutScene.cpp
  - src/LawnApp.cpp
  - src/Lawn/Widget/AwardScreen.cpp
  - src/Lawn/Widget/SeedChooserScreen.cpp
---

## Decided
- Defer board transitions that trigger inside CutScene::Update() or Widget::Update() using LawnApp::mDeferredNewGame rather than destroying the Board synchronously mid-tick.
- ShowSeedChooserScreen() and MakeNewBoard() must defensively kill any existing SeedChooserScreen before instantiating a new one.
- SeedChooserScreen primitive members must be explicitly initialized.

## Tried and ruled out
- Synchronous PreNewGame() inside CutScene::UpdateIntro(): deletes the Board while Board::Update() and WidgetManager::UpdateAll() are executing on that exact object, causing immediate use-after-free segfault.
- Allowing physical button bounce on AwardScreen: without reentrancy guard, double click invokes PreNewGame() twice and asserts or corrupts widget tree.

## Watch out
- Any transition between game scenes called from inside a widget update or cutscene tick must be deferred to the top-level loop (LawnApp::UpdateFrames) via mDeferredNewGame.
- On ARM Linux / OnionOS, division by zero in preview calculations immediately raises SIGFPE.
