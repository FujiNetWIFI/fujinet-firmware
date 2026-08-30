# Implementation Plan: A8CAS FSK Chunk Playback

## Overview

This task list implements the approved requirements and technical design for generic A8CAS raw `fsk ` chunk playback in the Atari FujiNet cassette subsystem.

Implementation must remain generic and standards-based. Do not add filename-, game-, country-, or Night Knight-specific detection. `Night Knight.cas` is a local/manual acceptance fixture only and must not be committed to the repository.

Tasks should be completed in order. Each task includes the requirements it traces to.

---

## Tasks

- [x] 1. Add the pure FSK parsing/timing module
  - [x] 1.1 Create `lib/device/sio/fsk_plan.h`
    - Define `FSK_MAX_PORTION_TICKS = 32767`.
    - Define `FSK_RMT_TICKS_PER_A8CAS_UNIT = 100`.
    - Implement `static inline uint32_t fsk_ticks_for_value(uint16_t value)`.
    - Implement `static inline uint16_t fsk_decode_le16(const uint8_t *p)`.
    - Implement `static inline bool fsk_level_for_index(size_t value_index)`.
    - Implement `static inline uint32_t fsk_next_portion(uint32_t remaining_ticks)`.
    - Implement `static inline size_t fsk_value_count(size_t data_len_available)`.
    - Define `FskChunkView`, `FskStep`, and `fsk_view_init`.
    - Keep ISR-used helpers allocation-free, I/O-free, logging-free, and safe to inline into the RMT callback.
    - _Requirements: 2.1, 2.3, 2.4, 2.5, 4.1, 4.2, 4.6, 6.2, 6.4_
  - [x] 1.2 Create `lib/device/sio/fsk_plan.cpp`
    - Implement host-test-only `fsk_view_step(FskChunkView &view)`.
    - Use the shared decode/parity/scale/split helpers from `fsk_plan.h`.
    - Skip zero-duration values while preserving original value-index parity.
    - Never read outside `data_len_available`.
    - Never allocate or materialize the complete waveform.
    - _Requirements: 2.1, 2.3, 2.4, 2.5, 4.6, 6.2, 6.4_
  - [x] 1.3 Compile the pure module independently on the host
    - Confirm `fsk_plan.cpp` has no FujiNet hardware, filesystem, GPIO, RMT, or global-state dependency.
    - _Requirements: 8.1_

- [x] 2. Add automated doctest coverage for the pure FSK rules
  - [x] 2.1 Create `tests/FskPlanTests.cpp`
    - Add the A8CAS worked example from Requirement 2.7.
    - Test zero-length payload behavior.
    - Test odd-length payload handling.
    - Test zero-duration values preserving parity.
    - Test values 1, 256, 6818, 40000, and 65535.
    - Verify 6818 maps to 681,800 ticks and 21 portions.
    - Verify 65535 maps to 6,553,500 ticks and 201 portions.
    - _Requirements: 2.7, 2.8, 4.6, 6.4_
  - [x] 2.2 Implement deterministic generated-loop tests for correctness properties
    - Property 1: `fsk_value_count(len) == len / 2`.
    - Property 2: emitted portion ticks for each value sum to `fsk_ticks_for_value(fsk_decode_le16(pair))`.
    - Property 3: every emitted portion level follows original value-index parity.
    - Property 4: cursor byte reads stay within the available payload.
    - Property 5: portions are `<= 32767`, preserve total duration, and have the correct portion count.
    - Include the full uint16 value range for split arithmetic where practical.
    - Use deterministic/fixed-seed inputs only.
    - _Requirements: 2.1, 2.3, 2.4, 2.5, 4.1, 4.6, 6.2, 6.4_
  - [x] 2.3 Add synthetic FSK payload fixtures
    - Build test byte arrays in source; do not add copyrighted `.cas` files.
    - Include representative short, long, zero, and odd-tail values.
    - Keep the tests generic and independent of filenames.
    - _Requirements: 9.4, 9.8_
  - [x] 2.4 Register `fsk_plan_tests` in `tests/CMakeLists.txt`
    - Follow the existing standalone doctest executable pattern.
    - Link only `FskPlanTests.cpp`, `fsk_plan.cpp`, and the required include paths.
    - _Requirements: 8.1_

- [ ] 3. Extend `sioCassette` declarations for FSK playback
  - [ ] 3.1 Update `lib/device/sio/cassette.h`
    - Include or otherwise expose the pure FSK helper declarations where needed.
    - Add cross-platform `play_fsk_chunk(size_t offset, uint16_t chunk_length, uint16_t irg_ms)`.
    - Under `ESP_PLATFORM`, add:
      - `fsk_signal_begin()`
      - `fsk_signal_emit()`
      - `fsk_signal_end()`
      - `IRAM_ATTR fsk_encode_cb(...)`
      - RMT handles/state
      - preloaded buffer pointer/state
      - value index, byte position, remaining ticks, and logical-level state
    - Keep ESP-specific declarations behind `#ifdef ESP_PLATFORM`.
    - _Requirements: 4.1-4.6, 5.1-5.5, 8.1_

- [ ] 4. Implement the ESP RMT FSK encoder
  - [ ] 4.1 Implement `fsk_encode_cb`
    - Mirror the existing Turbo 2000 simple-encoder callback pattern.
    - Set `*done = false` at the beginning of every callback invocation.
    - Decode values from the already preloaded payload only.
    - Use `fsk_decode_le16`, `fsk_level_for_index`, `fsk_ticks_for_value`, and `fsk_next_portion`.
    - Preserve value-index parity across zero-duration values.
    - Split long durations into consecutive same-level portions of at most 32767 ticks.
    - Emit the whole signal in original value order.
    - Set `*done = true` only after the final portion of the final value is emitted.
    - Perform no file I/O, heap allocation, or logging from the callback.
    - _Requirements: 2.1, 2.4, 2.5, 4.1, 4.2, 4.6_
  - [ ] 4.2 Implement `fsk_signal_begin`
    - Flush pending UART output before taking control of the TX pin.
    - Use `PIN_UART2_TX` and the same detach/reattach routing approach as the existing cassette RMT/QROS code.
    - Configure RMT TX at 1 MHz using `RMT_CLK_SRC_DEFAULT`.
    - Create a stateful simple encoder with `callback = fsk_encode_cb` and `arg = this`.
    - Use the established RMT channel lifecycle pattern from Turbo 2000.
    - Return failure cleanly for `GPIO_NUM_NC` or any RMT setup failure.
    - Fully undo partial setup on failure.
    - _Requirements: 4.1, 4.3, 4.5, 5.3-5.5_
  - [ ] 4.3 Implement `fsk_signal_emit`
    - Issue exactly one `rmt_transmit` for the complete preloaded FSK payload.
    - Wait for completion before buffer teardown.
    - Do not use repeated transmit batches.
    - _Requirements: 4.1, 4.2_
  - [ ] 4.4 Implement idempotent `fsk_signal_end`
    - Safely wait for any in-flight transmission.
    - Disable/delete the RMT channel and encoder.
    - Reattach UART2 TX routing.
    - Clear RMT handles/state.
    - Preserve the existing UART baud divisor.
    - _Requirements: 4.3, 4.5, 5.1, 5.3-5.5_

- [ ] 5. Implement cross-platform `play_fsk_chunk`
  - [ ] 5.1 Implement file-boundary validation
    - Treat fewer than 8 bytes of remaining header as end-of-tape.
    - Compute available payload bytes using the declared length clamped to EOF.
    - Never read beyond the CAS image.
    - For overrun/truncated payloads, reproduce only complete values present and return the established end-of-tape result afterward.
    - For odd lengths, ignore the trailing unpaired byte.
    - _Requirements: 1.4, 6.1-6.6, 8.3, 8.4_
  - [ ] 5.2 Implement one-time payload pre-read
    - Allocate only `data_avail` bytes.
    - Read the clamped payload in one file read before waveform emission starts.
    - Never perform file I/O between FSK transitions.
    - On allocation failure, skip raw signal emission safely, preserve baud/UART state, and continue using the designed offset behavior.
    - Ensure the buffer is freed on every exit path.
    - _Requirements: 4.5, 5.4, 5.5, 6.2, 6.6_
  - [ ] 5.3 Implement FSK IRG handling
    - Honor `irg_ms` before raw signal emission.
    - Preserve the existing cassette gap/motor behavior.
    - If the pulldown is present and the motor line is de-asserted while more than 1000 ms remains, abort safely and return `starting_offset`.
    - Ensure LED state is restored on abort and normal completion.
    - _Requirements: 2.2, 2.8, 3.1-3.4_
  - [ ] 5.4 Implement ESP raw signal playback inside `play_fsk_chunk`
    - Initialize the encoder state only after `fsk_signal_begin()` succeeds.
    - Set `_fsk_buf`, data length/value count, byte position, value index, remaining ticks, and current level.
    - Call `fsk_signal_emit()` only when complete FSK values exist.
    - Ensure every success/failure path reaches the common cleanup.
    - _Requirements: 4.1-4.6, 5.1-5.5_
  - [ ] 5.5 Implement deterministic PC-build behavior
    - Use the same pre-read and bounds logic.
    - Honor the IRG through `SYSTEM_BUS.bus_idle`.
    - Do not invoke RMT, GPIO, or raw hardware signal-generation APIs.
    - Advance to the next well-formed chunk or terminate at EOT for an overrun/truncated chunk.
    - _Requirements: 8.1-8.4_
  - [ ] 5.6 Preserve Active_Baud_Rate through all paths
    - Do not call `SYSTEM_BUS.setBaudrate` from FSK processing.
    - Confirm success, motor abort, allocation failure, and RMT setup/transmit failure leave the active baud unchanged.
    - _Requirements: 5.1-5.5_

- [ ] 6. Integrate `fsk ` chunk handling into normal FUJI playback
  - [ ] 6.1 Update the chunk walker in `send_FUJI_tape_block`
    - Detect exactly the four bytes `'f','s','k',' '`.
    - Add the branch before the existing unknown-chunk catch-all.
    - Delegate the chunk to `play_fsk_chunk`.
    - Continue walking after a successfully handled FSK chunk rather than treating it as a terminating `data` record.
    - Preserve the current baud until an actual `baud` chunk changes it.
    - _Requirements: 1.1-1.7, 5.1, 9.1-9.4_
  - [ ] 6.2 Add truncated-header protection to the FUJI chunk walk
    - Check the number of header bytes actually read before using the header.
    - Return end-of-tape without reading invalid header fields when fewer than 8 bytes are available.
    - _Requirements: 6.1, 8.4_
  - [ ] 6.3 Preserve unknown-chunk behavior
    - Keep the existing `chunk_length + 8` skip for chunk types other than `data`, `baud`, and `fsk `.
    - Do not add any filename/game/country-specific handling.
    - _Requirements: 1.6, 1.7, 9.3, 9.4, 9.8_

- [ ] 7. Verify no regression in existing cassette paths
  - [ ] 7.1 Confirm dispatcher behavior is unchanged
    - Turbo 2000 continues to use `send_turbo2000_tape_block`.
    - QROS continues to use `send_QROS_tape_block`.
    - Only normal FUJI playback enters the new FSK path.
    - _Requirements: 7.2-7.6_
  - [ ] 7.2 Confirm no FSK code executes for standard `baud`/`data`-only images
    - Compare normal standard playback behavior before/after the feature where practical.
    - _Requirements: 7.1_
  - [ ] 7.3 Confirm Turbo 2000 and QROS detection outcomes remain unchanged
    - Do not modify detection unless required for compilation; if refactoring occurs, prove equivalent outcomes.
    - _Requirements: 7.2-7.6_

- [ ] 8. Run automated and build verification
  - [ ] 8.1 Configure and run `fsk_plan_tests`
    - Run the new doctest executable.
    - Confirm all example and generated-loop tests pass.
    - _Requirements: 2.1-2.8, 4.6, 6.2, 6.4_
  - [ ] 8.2 Build fujinet-pc
    - Confirm the new shared code compiles without ESP-only APIs leaking into the PC build.
    - Exercise a synthetic/interleaved CAS image where possible and verify safe FSK skipping plus IRG handling.
    - _Requirements: 8.1-8.4_
  - [ ] 8.3 Build at least one classic ESP32 Atari FujiNet target
    - Verify the RMT API/configuration compiles at 1 MHz.
    - Check for warnings around IRAM, callback signatures, pin routing, and RMT handles.
    - _Requirements: 4.1-4.5, 7.1_
  - [ ] 8.4 Build at least one ESP32-S3 Atari FujiNet target if supported by the normal project build matrix
    - Confirm the implementation remains portable across the supported ESP target family.
    - _Requirements: 4.1-4.5, 7.1_

- [ ] 9. Perform local real-hardware acceptance with `Night Knight.cas`
  - [ ] 9.1 Keep `Night Knight.cas` outside the repository
    - Do not add the file to Git, tests, fixtures, or the PR.
    - Use it only for local/manual acceptance unless redistribution rights are established.
    - _Requirements: 9.4, 9.8_
  - [ ] 9.2 Verify the known fixture structure before hardware playback
    - Confirm locally: 36,785 bytes, 259 total chunks, 1 `FUJI`, 2 `baud`, 249 `data`, 7 `fsk `.
    - Confirm the baud transition 600 → 790.
    - Confirm the first FSK value is 6818 = 681.8 ms.
    - _Requirements: 9.5-9.7_
  - [ ] 9.3 Load `Night Knight.cas` on a physical Atari through FujiNet
    - Confirm the title loads successfully.
    - Confirm playback proceeds through interleaved `baud`, `data`, and `fsk ` chunks.
    - Confirm data following FSK playback continues at the active baud.
    - _Requirements: 5.2, 5.3, 9.1-9.8_
  - [ ] 9.4 Verify motor-line abort and recovery
    - During a long IRG, de-assert the motor line where practical.
    - Confirm safe abort/retry behavior, UART restoration, and baud preservation.
    - _Requirements: 3.3, 3.4, 5.4_
  - [ ] 9.5 Verify FSK setup failure degrades safely where practical
    - On a configuration/target without a valid cassette TX pin, confirm raw signal output is skipped without breaking subsequent data playback.
    - _Requirements: 4.5, 5.5_

- [ ] 10. Final review and pull-request readiness
  - [ ] 10.1 Review the diff for scope discipline
    - Confirm changes are limited to generic A8CAS FSK support, tests, and required build wiring.
    - Confirm there is no Night Knight/game/country-specific code.
    - Confirm no unrelated cassette behavior was modified.
    - _Requirements: 1.7, 7.1-7.6, 9.4, 9.8_
  - [ ] 10.2 Re-run tests/builds after final cleanup
    - Run `fsk_plan_tests`.
    - Rebuild fujinet-pc.
    - Rebuild the selected ESP32 Atari target(s).
  - [ ] 10.3 Verify working tree contents before commit
    - Ensure `Night Knight.cas` is untracked/ignored and not staged.
    - Ensure no generated binaries or temporary test artifacts are staged.
  - [ ] 10.4 Prepare implementation commits
    - Keep commits logically grouped (pure parser/tests, cassette integration/RMT, final fixes) where practical.
    - Do not modify `master`; commit only on `feature/a8cas-fsk-playback`.
  - [ ] 10.5 Prepare the future Pull Request description
    - Summarize the standards-based A8CAS `fsk ` support.
    - Explain the RMT 1 MHz implementation and preservation of existing formats.
    - Include automated-test/build results and manual hardware validation.
    - Mention `Night Knight.cas` only as a locally tested compatibility case; do not attach or redistribute it.

---

## Completion Criteria

The implementation is ready for Pull Request only when all of the following are true:

- The normal FUJI path recognizes and reproduces generic A8CAS `fsk ` chunks.
- A8CAS value timing is derived exclusively from the file (`value × 0.1 ms`) and emitted with index-parity logical levels.
- ESP playback uses one continuous 1 MHz RMT transaction with no file I/O during waveform emission.
- The active baud rate survives FSK playback and failure paths unchanged.
- Malformed, truncated, odd-length, allocation-failure, and hardware-setup cases terminate safely without out-of-bounds reads, hangs, or crashes.
- Standard playback, Turbo 2000, and QROS behavior remain unchanged.
- The PC build compiles and safely handles FSK chunks without unsupported hardware access.
- `fsk_plan_tests` passes.
- Selected ESP Atari firmware target(s) build successfully.
- `Night Knight.cas` loads successfully on real hardware in the local acceptance test.
- No copyrighted/commercial `.cas` fixture is committed.
