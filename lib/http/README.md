# lib/http

The FujiNet web UI and REST API, served by `esp_http_server` on the ESP32 and by mongoose on
FujiNet-PC, plus the outbound HTTP client each build uses.

## Layout
| File | Defines |
|---|---|
| `httpService.h` | `fnHttpService` and its global `fnHTTPD`; the class has an ESP half of `httpd_req_t` handlers and a PC half of `mg_connection` callbacks; `FNWS_FILE_ROOT` |
| `httpService.cpp` | The ESP implementation: URI table, session-checking dispatch, the print, app-key, file-manager and login pages, and `webdav_register` for the WebDAV server |
| `mgHttpService.cpp` | The PC implementation: mongoose event callback, server-sent printer events, a download endpoint, and `fnHttpSendFileTask` submitted to `taskMgr` |
| `httpServiceParser.*` | `fnHttpServiceParser`: page templating; a file whose extension starts with `html` is loaded whole and every tag is replaced by `substitute_tag()` |
| `httpServiceConfigurator.*` | `fnHttpServiceConfigurator`: maps the config form fields to `Config` setters |
| `httpServiceBrowse.*` | `fnHttpBrowse`: host browsing and device-slot mounting, HTML handed back through a chunk sink |
| `httpServiceApi.*` | `fnHttpApi`: the REST router under `FN_API_ROOT` |
| `fnSession.*` | `fnSession`: cookie-based login session helpers shared by both servers |
| `appKeyManager.*` | `AppKeyManager`: list, edit and delete the app keys stored in the FujiNet directory on SD |
| `fileManager.*` | `FileManager`, `MultipartFileWriter`: the SD file manager with multipart upload |
| `google_scopes.h` | `GOOGLE_OAUTH_SCOPES`: the single Google grant covering Drive, Gmail and Calendar, requested by both servers' OAuth pages |
| `fnHttpClient.*` | `fnHttpClient`, the ESP client over [lib/fn_esp_http_client/](../fn_esp_http_client/) |
| `mgHttpClient.*` | `mgHttpClient`, the PC client over mongoose |
| `webdav/` | The ESP WebDAV server; see [webdav/](webdav/) |

## How it fits
- Started from `src/main.cpp`; the PC main loop also calls `fnHTTPD.service()`.
- Pages come from the www directory on `fsFlash`, which is the image generated from
  [data/webui/](../../data/webui/).
- Reads and writes `Config` from [lib/config/](../config/), and uses `fnPassword`, `fnSystem`,
  `fnWiFi`, `fnSDFAT`, `theFuji` and the printer list.
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
