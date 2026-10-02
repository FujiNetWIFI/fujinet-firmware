# test

A stale PlatformIO Unity test skeleton. Nothing references it: no script, workflow or CMake file
runs `pio test`, and the host test suite lives in [tests/](../tests/).

## Layout
| File | Defines |
|---|---|
| `main.cpp` | ESP `app_main()` that runs the Unity test list |
| `test_pass.cpp`, `test_pass.h` | A placeholder test |
| `test_networkprotocol_translation.cpp`, `test_networkprotocol_translation.h` | An old test of `NetworkProtocol` EOL translation |

## Notes
Do not add tests here. New tests go in [tests/](../tests/), as `CONTRIBUTING.md` describes.
