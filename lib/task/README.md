# lib/task

A cooperative task framework for FujiNet-PC, where there is no FreeRTOS to hand long-running work
to.

## How it fits
- `src/main.cpp` calls `taskMgr.service()` once per pass of the PC main loop.
- The only production task is `fnHttpSendFileTask` in `lib/http/mgHttpService.cpp`, which streams
  a file download without blocking the server.

## Build
PC only: both sources are wrapped in `#ifndef ESP_PLATFORM`, so the ESP glob compiles them to
nothing.
