# Stage 35: Garbage Generation Types

Garbage generation now distinguishes `GarbageHolePolicy` from `GarbageCell`. Runtime and Engine code no longer pass `2` for hole preservation or `7/8/9` for normal, solid and timed-solid cells.

Replay keeps its historical numeric payload: conversion is explicit in `replay/gameplay_application.cpp` and recording converts the enums back to their legacy values. Chessboard generation state is a named boolean parity.
