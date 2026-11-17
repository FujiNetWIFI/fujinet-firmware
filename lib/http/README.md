# lib/http

The FujiNet web UI and REST API, served by `esp_http_server` on the ESP32 and by mongoose on
FujiNet-PC, plus the outbound HTTP client each build uses.

## How it fits
- Started from `src/main.cpp`; the PC main loop also calls `fnHTTPD.service()`.
- Pages come from the www directory on `fsFlash`, which is the image generated from
  [data/webui/](../../data/webui/).
- Reads and writes `Config` from [lib/config/](../config/), and uses `fnPassword`, `fnSystem`,
  `fnWiFi`, `fnSDFAT`, `SYSTEM_BUS.theFuji` and the printer list.
- Consumers pick a client with a macro: `HTTP_CLIENT_CLASS` in [lib/FileSystem/](../FileSystem/),
  `GDRIVE_HTTP_CLIENT` and `GCAL_HTTP_CLIENT_CLASS` in
  [lib/network-protocol/](../network-protocol/).

## Build
ESP: globbed by `src/CMakeLists.txt` into every target; `mgHttpService.cpp` and `mgHttpClient.cpp`
compile to nothing. PC: `fujinet_pc.cmake` lists everything except `httpService.cpp`,
`fnHttpClient.cpp` and `webdav/`.

## Notes
The two servers have different handler signatures. Logic both need lives in `httpServiceApi`,
`httpServiceBrowse`, `fnSession`, `appKeyManager` and `fileManager` so the two cannot drift.
