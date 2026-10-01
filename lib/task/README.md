# lib/task

A cooperative task framework for FujiNet-PC, where there is no FreeRTOS to hand long-running work
to.

## Layout
| File | Defines |
|---|---|
| `fnTask.h`, `fnTask.cpp` | `fnTask`, a state machine (READY, RUNNING, PAUSED, DONE) with `start` and `step` to implement and optional `pause`, `resume`, `abort`; `fnTestTask` is a sample |
| `fnTaskManager.h`, `fnTaskManager.cpp` | `fnTaskManager`: `submit_task`, `service`, `pause_task`, `resume_task`, `abort_task`; the global `taskMgr` |

## How it fits
- `src/main.cpp` calls `taskMgr.service()` once per pass of the PC main loop.
- The only production task is `fnHttpSendFileTask` in `lib/http/mgHttpService.cpp`, which streams
  a file download without blocking the server.

## Build
PC only: both sources are wrapped in `#ifndef ESP_PLATFORM`, so the ESP glob compiles them to
nothing.
