# Requirements Document

## Introduction

FujiNet firmware plays back Atari 8-bit cassette (CAS) images through the SIO cassette subsystem (`lib/device/sio/cassette.cpp`, guarded by `BUILD_ATARI`). It currently supports standard 600 baud A8CAS playback (`data`/`baud` chunks via `send_FUJI_tape_block`), Turbo 2000 PWM (`send_turbo2000_tape_block`), and QROS turbo (`send_QROS_tape_block`). A8CAS raw FSK chunks (chunk type `"fsk "`, where the fourth byte is a SPACE) are not reproduced during normal FUJI playback; the current FUJI loop skips unrecognized chunks by advancing past them, so any signal carried by an `fsk ` chunk is silently dropped.

This feature adds standards-based reproduction of A8CAS `"fsk "` chunks within the normal FUJI cassette playback path, following the authoritative A8CAS CAS format specification (https://a8cas.sourceforge.net/format-cas.html). The `"fsk "` type is treated as one chunk type among others; a single cassette image may interleave `FUJI`, `baud`, `data`, and `fsk ` chunks. The change must preserve compatibility with all existing cassette formats and must not introduce any filename-, game-, or country-specific detection. The Chilean commercial image "Night Knight.cas" is used only as a real-world interleaved-chunk acceptance example.

These requirements describe behavior only. The hardware mechanism used to reproduce raw FSK signals, the concrete PC-build behavior for FSK chunks, and any acceptable hardware timing tolerances are out of scope for this document and are to be determined and justified in the Technical Design and the Test Plan.

## Glossary

- **CAS_Image**: An A8CAS-format Atari cassette image file, beginning with a `FUJI` header chunk and containing a sequence of chunks.
- **Chunk**: A unit within a CAS_Image consisting of an 8-byte header (`chunk_type[4]`, `chunk_length` as uint16 little-endian, `aux`/`irg_length` as uint16 little-endian) followed by `chunk_length` bytes of data. Each Chunk occupies `chunk_length + 8` bytes. Represented in code as `struct tape_FUJI_hdr`.
- **Chunk_Type**: The 4-byte identifier at the start of a Chunk (e.g. `data`, `baud`, `fsk ` where the fourth byte is 0x20 SPACE).
- **FSK_Chunk**: A Chunk whose Chunk_Type equals the four bytes 0x66 0x73 0x6B 0x20 (`'f'`, `'s'`, `'k'`, `' '`). Its `aux` field carries the Inter-Record Gap length in milliseconds; its data is a sequence of alternating signal durations.
- **FSK_Signal_Value**: One unsigned 16-bit little-endian value inside an FSK_Chunk data area, expressed in units of 1/10 millisecond. Value 0x0100 (256) equals 25.6 ms.
- **FSK_Signal_Index**: The zero-based position of an FSK_Signal_Value within the FSK_Chunk data sequence. The A8CAS logical level of a value is determined by the parity of its index: an even index (0, 2, 4, ...) is logical 0 and an odd index (1, 3, 5, ...) is logical 1.
- **Inter_Record_Gap (IRG)**: A silent gap preceding a record, in milliseconds, carried in the Chunk `aux`/`irg_length` field.
- **Cassette_Subsystem**: The FujiNet Atari SIO cassette playback component (`sioCassette`) responsible for interpreting CAS_Image chunks and driving the SIO data line during playback.
- **FUJI_Playback_Path**: The normal (non-turbo) playback routine `send_FUJI_tape_block` that walks chunks, applies `baud` changes, and emits `data` records.
- **Standard_Playback**: Existing 600 baud A8CAS playback of `data` records through the SIO UART at the active baud rate.
- **Turbo2000_Playback**: Existing Turbo 2000 PWM playback path (`send_turbo2000_tape_block`).
- **QROS_Playback**: Existing QROS turbo playback path (`send_QROS_tape_block`), which explicitly skips `fsk` chunks.
- **Active_Baud_Rate**: The SIO baud rate currently applied to the bus (set from `baud` chunks via `SYSTEM_BUS.setBaudrate`) and used for subsequent `data` records.
- **Data_Line**: The SIO signal line driven to the Atari during cassette playback. The Cassette_Subsystem sets this line's logical level over time to convey both normal `data` records and raw FSK signals.
- **ESP_Build**: A firmware build where `ESP_PLATFORM` is defined (target hardware capable of raw FSK signal reproduction).
- **PC_Build**: The fujinet-pc build where `ESP_PLATFORM` is not defined and raw hardware signal generation is unavailable.
- **Motor_Line**: The cassette motor control signal; playback honors motor de-assertion during long gaps.

## Requirements

### Requirement 1: Detect FSK chunks within normal FUJI playback

**User Story:** As a FujiNet user playing an A8CAS cassette, I want `fsk ` chunks to be recognized during normal playback, so that images containing raw FSK records are reproduced instead of silently skipped.

#### Acceptance Criteria

1. WHEN the FUJI_Playback_Path encounters a Chunk whose Chunk_Type equals the bytes 0x66 0x73 0x6B 0x20, THE Cassette_Subsystem SHALL classify the Chunk as an FSK_Chunk.
2. WHEN the FUJI_Playback_Path classifies a Chunk as an FSK_Chunk whose declared chunk_length is greater than zero and whose payload lies entirely within the CAS_Image, THE Cassette_Subsystem SHALL reproduce the FSK_Chunk payload on the Data_Line and, upon completing the payload, SHALL advance the read offset by chunk_length + 8 bytes before reading the next Chunk.
3. WHEN the FUJI_Playback_Path classifies a Chunk as an FSK_Chunk whose declared chunk_length equals zero, THE Cassette_Subsystem SHALL advance the read offset by 8 bytes to the next Chunk without driving the Data_Line.
4. IF the FUJI_Playback_Path classifies a Chunk as an FSK_Chunk whose declared chunk_length would extend the payload past the end of the CAS_Image, THEN THE Cassette_Subsystem SHALL bound the data read to the bytes remaining in the CAS_Image, SHALL NOT read past the end of the file, and SHALL safely terminate processing of that FSK_Chunk, where whether the fully-present FSK_Signal_Values are partially reproduced or the FSK_Chunk is rejected is deferred to Technical Design unless the A8CAS specification requires a specific behavior.
5. THE Cassette_Subsystem SHALL treat `fsk ` as a Chunk_Type that may appear interleaved with `FUJI`, `baud`, and `data` Chunk_Types within a single CAS_Image.
6. WHERE a Chunk_Type is not one of `data`, `baud`, or `fsk `, THE Cassette_Subsystem SHALL advance the read offset by chunk_length + 8 bytes and continue to the next Chunk.
7. THE Cassette_Subsystem SHALL determine whether a Chunk is an FSK_Chunk solely from the 4-byte Chunk_Type and SHALL NOT use the CAS_Image filename, game identity, or country of origin as a detection input.

### Requirement 2: Parse FSK chunk structure per the A8CAS specification

**User Story:** As a developer maintaining cassette support, I want FSK chunks parsed exactly as the A8CAS format defines, so that reproduced signals match the recorded data without invented timings.

#### Acceptance Criteria

1. THE Cassette_Subsystem SHALL read the FSK_Chunk `chunk_length` and `aux` fields as unsigned 16-bit little-endian values.
2. THE Cassette_Subsystem SHALL interpret the FSK_Chunk `aux` field as the Inter_Record_Gap length in milliseconds preceding the record.
3. THE Cassette_Subsystem SHALL interpret the FSK_Chunk data area as a sequence of floor(`chunk_length` / 2) FSK_Signal_Values, each read as an unsigned 16-bit little-endian value.
4. THE Cassette_Subsystem SHALL interpret each FSK_Signal_Value as a signal duration in units of 1/10 millisecond.
5. THE Cassette_Subsystem SHALL assign each FSK_Signal_Value a logical level by its FSK_Signal_Index parity, so that the value at index 0 is logical 0, the value at index 1 is logical 1, the value at index 2 is logical 0, and so on.
6. THE Cassette_Subsystem SHALL derive all FSK signal timings from the FSK_Chunk data and `aux` fields and SHALL NOT substitute hardcoded or invented signal timings.
7. WHEN the FUJI_Playback_Path processes an FSK_Chunk with a `chunk_length` of 10, an `aux` of 0x0111, and a data area of 0x00 0x01 0x10 0x01 0x80 0x00 0x20 0x00 0x80 0x02, THE Cassette_Subsystem SHALL honor a 273 ms Inter_Record_Gap and reproduce five durations by FSK_Signal_Index of 25.6 ms at logical 0, 27.2 ms at logical 1, 12.8 ms at logical 0, 3.2 ms at logical 1, and 64 ms at logical 0.
8. WHEN the FUJI_Playback_Path processes an FSK_Chunk with a `chunk_length` of 0, THE Cassette_Subsystem SHALL honor the Inter_Record_Gap specified by the `aux` field and reproduce zero FSK_Signal_Values.

### Requirement 3: Honor the Inter-Record Gap before FSK signal output

**User Story:** As a FujiNet user, I want the gap before each FSK record honored, so that FSK records are timed correctly relative to surrounding records.

#### Acceptance Criteria

1. WHEN the FUJI_Playback_Path begins reproducing an FSK_Chunk, THE Cassette_Subsystem SHALL read the Inter_Record_Gap duration in milliseconds from the FSK_Chunk `aux` field, interpreted as an unsigned value in the range 0 to 65535 ms, and SHALL delay for that duration before emitting the first FSK_Signal_Value. Any acceptable hardware timing tolerance for this delay is out of scope for these requirements and is to be justified in Technical Design and the Test Plan.
2. IF the Inter_Record_Gap duration read from the FSK_Chunk `aux` field is 0 ms, THEN THE Cassette_Subsystem SHALL emit the first FSK_Signal_Value without applying any delay.
3. WHILE honoring an Inter_Record_Gap that exceeds 1000 ms, IF a pulldown is present and the Motor_Line is de-asserted, THEN THE Cassette_Subsystem SHALL abort reproduction of the current FSK_Chunk, SHALL stop emitting FSK_Signal_Value output, and SHALL return the read offset to the position at which the current record began.
4. WHEN reproduction is aborted due to a de-asserted Motor_Line during an Inter_Record_Gap, THE Cassette_Subsystem SHALL terminate reproduction of the current FSK_Chunk safely without hanging or crashing, SHALL preserve the normal playback state and the Active_Baud_Rate, and SHALL NOT corrupt subsequent normal `data` playback. The concrete mechanism, if any, for propagating that reproduction did not complete is to be determined in Technical Design after inspecting the existing cassette subsystem interfaces.

### Requirement 4: Reproduce the FSK signal on the data line (ESP build)

**User Story:** As a FujiNet user on target hardware, I want FSK signals emitted on the cassette data line, so that the Atari receives the raw FSK sequence recorded in the image.

#### Acceptance Criteria

1. WHERE the build is an ESP_Build, WHEN reproducing an FSK_Chunk, THE Cassette_Subsystem SHALL drive the Data_Line to reproduce each FSK_Signal_Value in FSK_Signal_Index order, holding each value at its A8CAS logical level for its A8CAS-specified duration.
2. WHERE the build is an ESP_Build, WHEN reproducing an FSK_Chunk, THE Cassette_Subsystem SHALL set the Data_Line logical level for each FSK_Signal_Value from its FSK_Signal_Index parity, so that an even index is logical 0 and an odd index is logical 1, independent of the durations of any preceding FSK_Signal_Values.
3. WHERE the build is an ESP_Build, WHEN FSK reproduction of a Chunk completes, THE Cassette_Subsystem SHALL restore normal `data` playback capability on the Data_Line before transmitting any subsequent `data` record.
4. IF an FSK_Chunk contains zero FSK_Signal_Values, THEN THE Cassette_Subsystem SHALL leave the Data_Line at its current logical level, honor the Inter_Record_Gap for its specified duration expressed in milliseconds, and advance to the next Chunk while preserving normal `data` playback capability.
5. WHERE the build is an ESP_Build, IF raw FSK signal reproduction cannot be performed, THEN THE Cassette_Subsystem SHALL terminate FSK reproduction of the current Chunk safely without hanging or crashing and SHALL NOT corrupt subsequent normal `data` playback. The concrete mechanism, if any, for propagating this failure is to be determined in Technical Design after inspecting the existing cassette subsystem interfaces.
6. WHERE the build is an ESP_Build, IF an FSK_Signal_Value specifies a duration of zero, THEN THE Cassette_Subsystem SHALL continue with the next FSK_Signal_Value while preserving the FSK_Signal_Index parity of all following FSK_Signal_Values, such that a zero-duration value does not alter the logical level assigned to any subsequent FSK_Signal_Value.

### Requirement 5: Preserve the active baud rate across FSK chunks

**User Story:** As a FujiNet user, I want the SIO baud rate unchanged by FSK records, so that `data` records after an FSK record still transmit at the correct rate.

#### Acceptance Criteria

1. THE Cassette_Subsystem SHALL leave the Active_Baud_Rate value unchanged when processing an FSK_Chunk, such that the Active_Baud_Rate after the FSK_Chunk completes equals the Active_Baud_Rate immediately before the FSK_Chunk began.
2. WHEN a `data` Chunk follows an FSK_Chunk without an intervening `baud` Chunk, THE Cassette_Subsystem SHALL transmit that `data` Chunk at the Active_Baud_Rate that was in effect immediately before the FSK_Chunk.
3. WHEN FSK reproduction of a Chunk completes, THE Cassette_Subsystem SHALL restore the normal data-transmission capability that was in effect before the FSK_Chunk, configured to the Active_Baud_Rate that was in effect immediately before the FSK_Chunk.
4. IF processing an FSK_Chunk fails before the FSK_Chunk completes, THEN THE Cassette_Subsystem SHALL terminate processing safely without hanging or crashing and SHALL retain the Active_Baud_Rate that was in effect immediately before the FSK_Chunk. The concrete mechanism, if any, for propagating this failure is to be determined in Technical Design after inspecting the existing cassette subsystem interfaces.
5. IF normal data-transmission capability cannot be restored after an FSK_Chunk, THEN THE Cassette_Subsystem SHALL retain the Active_Baud_Rate that was in effect immediately before the FSK_Chunk and SHALL NOT corrupt subsequent normal `data` playback. The concrete mechanism, if any, for propagating this failure is to be determined in Technical Design after inspecting the existing cassette subsystem interfaces.

### Requirement 6: Safe handling of malformed, truncated, and odd-length FSK chunks

**User Story:** As a FujiNet user, I want damaged or unusual images handled without crashing, so that playback degrades safely rather than reading past the file or hanging.

#### Acceptance Criteria

1. IF an FSK_Chunk header cannot be fully read because fewer than the required header bytes remain before the end of the CAS_Image file, THEN THE Cassette_Subsystem SHALL stop processing the FSK_Chunk without reading past the end of the file and SHALL safely terminate processing of the FSK_Chunk without hanging or crashing.
2. THE Cassette_Subsystem SHALL bound all FSK_Chunk data reads by the smaller of the declared `chunk_length` (valid range 0 to 65535 inclusive) and the number of bytes remaining in the CAS_Image file, SHALL perform no out-of-bounds reads, and SHALL NOT read past the end of the file.
3. IF an FSK_Chunk header or its declared data extends beyond the CAS_Image file size, THEN THE Cassette_Subsystem SHALL safely terminate processing of that FSK_Chunk at the available data boundary without reading past the end of the file and without crashing or hanging, where whether the fully-present FSK_Signal_Values are partially reproduced or the FSK_Chunk is rejected is deferred to Technical Design unless the A8CAS specification requires a specific behavior.
4. IF an FSK_Chunk `chunk_length` is an odd number, THEN THE Cassette_Subsystem SHALL handle the odd length with bounds checking and no out-of-bounds reads, where whether the fully-present 2-byte FSK_Signal_Values are partially reproduced or the FSK_Chunk is rejected, and how the trailing unpaired byte is treated, is deferred to Technical Design unless the A8CAS specification requires a specific behavior.
5. IF an FSK_Chunk is truncated so that fewer than `chunk_length` data bytes are available before the end of the file, THEN THE Cassette_Subsystem SHALL handle the truncation with bounds checking and no out-of-bounds reads, SHALL NOT read past the end of the file, and SHALL safely terminate processing of that FSK_Chunk, where whether the fully-present FSK_Signal_Values are partially reproduced or the FSK_Chunk is rejected is deferred to Technical Design unless the A8CAS specification requires a specific behavior.
6. WHEN a malformed, truncated, or odd-length FSK_Chunk is encountered, THE Cassette_Subsystem SHALL deterministically terminate processing of that FSK_Chunk and continue normal playback control flow without hanging or crashing.

### Requirement 7: No regression of existing playback formats

**User Story:** As an existing FujiNet user, I want standard, Turbo 2000, and QROS cassettes to keep working exactly as before, so that adding FSK support does not break current images.

#### Acceptance Criteria

1. WHEN a CAS_Image contains only `baud` and `data` Chunks and contains zero `fsk` Chunks, THE Cassette_Subsystem SHALL perform Standard_Playback and SHALL produce output byte-for-byte identical to the Standard_Playback output produced before FSK support was added for the same CAS_Image.
2. WHEN a CAS_Image is detected as Turbo 2000 PWM, THE Cassette_Subsystem SHALL use Turbo2000_Playback and SHALL execute zero FSK_Chunk reproduction operations for that CAS_Image.
3. WHEN a CAS_Image is detected as QROS, THE Cassette_Subsystem SHALL use QROS_Playback and SHALL skip every `fsk` Chunk in that CAS_Image without emitting any output for those Chunks, matching the pre-FSK-support behavior.
4. WHILE the active playback path is not the FUJI_Playback_Path, THE Cassette_Subsystem SHALL execute zero FSK_Chunk reproduction operations.
5. THE Cassette_Subsystem SHALL produce Turbo 2000 PWM and QROS detection outcomes that are equivalent to the pre-change behavior for any given CAS_Image, such that refactoring of the detection code is permitted only while the detection outcomes remain unchanged.
6. IF a CAS_Image is detected as both a FUJI file and either Turbo 2000 PWM or QROS, THEN THE Cassette_Subsystem SHALL select the non-FUJI playback path (Turbo2000_Playback or QROS_Playback) and SHALL execute zero FSK_Chunk reproduction operations for that CAS_Image.

### Requirement 8: Safe degradation on the PC build

**User Story:** As a developer using the fujinet-pc build, I want FSK chunks handled without hardware signal generation, so that the PC build compiles and runs without attempting unavailable operations.

#### Acceptance Criteria

1. WHERE the build is a PC_Build, THE Cassette_Subsystem SHALL compile and operate without accessing hardware that is unsupported on the PC_Build.
2. WHERE the build is a PC_Build, WHEN the FUJI_Playback_Path encounters an FSK_Chunk, THE Cassette_Subsystem SHALL handle the FSK_Chunk safely without invoking raw hardware Data_Line signal generation and SHALL continue processing subsequent Chunks without crashing or hanging, where the concrete PC_Build FSK playback behavior is deferred to Technical Design after inspecting platform capabilities.
3. WHERE the build is a PC_Build, THE Cassette_Subsystem SHALL bound all FSK_Chunk reads by the number of bytes remaining in the CAS_Image, SHALL perform no out-of-bounds reads, and SHALL NOT read past the end of the file.
4. WHERE the build is a PC_Build, IF an FSK_Chunk declares a chunk_length that would advance the read offset beyond the total byte length of the CAS_Image, or is truncated such that fewer than 8 header bytes remain, THEN THE Cassette_Subsystem SHALL safely terminate processing at the available data boundary without reading past the end of the file and without crashing or hanging.

### Requirement 9: Real-world interleaved-chunk playback (acceptance example)

**User Story:** As a FujiNet user with a Chilean commercial cassette image, I want an image that interleaves FSK records with normal records to play back, so that titles such as "Night Knight.cas" load correctly.

#### Acceptance Criteria

1. WHEN a CAS_Image interleaves `baud`, `data`, and `fsk ` Chunks, THE Cassette_Subsystem SHALL process every Chunk in ascending file order, reproducing each `fsk ` Chunk as an FSK_Chunk and transmitting each `data` Chunk at the Active_Baud_Rate, until all Chunks in the CAS_Image have been processed.
2. WHEN the Cassette_Subsystem encounters a `baud` Chunk, THE Cassette_Subsystem SHALL set the Active_Baud_Rate to the value specified by that `baud` Chunk and apply it to every subsequent `data` Chunk until the next `baud` Chunk is encountered.
3. IF the Cassette_Subsystem encounters a Chunk whose Chunk_Type is not `baud`, `data`, or `fsk `, THEN THE Cassette_Subsystem SHALL skip that Chunk, continue processing the next Chunk in file order, and retain the current Active_Baud_Rate and playback position.
4. THE Cassette_Subsystem SHALL reproduce the interleaved CAS_Image without applying any filename-specific, game-specific, or country-specific logic.
5. WHERE the "Night Knight.cas" test fixture is used, THE Cassette_Subsystem SHALL play back a 36,785-byte CAS_Image containing 259 Chunks in total, comprising 1 `FUJI` Chunk, 2 `baud` Chunks, 249 `data` Chunks, and 7 `fsk ` Chunks, processing every Chunk in ascending file order until all Chunks have been processed.
6. WHERE the "Night Knight.cas" test fixture is used, WHEN the Cassette_Subsystem processes the fixture's `baud` Chunks in file order, THE Cassette_Subsystem SHALL begin playback at an Active_Baud_Rate of 600 baud and later switch the Active_Baud_Rate to 790 baud.
7. WHERE the "Night Knight.cas" test fixture is used, WHEN the Cassette_Subsystem reproduces the fixture's first FSK_Chunk, THE Cassette_Subsystem SHALL treat the first FSK_Signal_Value as the raw value 6818, representing 681.8 ms.
8. THE Cassette_Subsystem SHALL derive all playback behavior solely from the contents of the CAS_Image Chunks and SHALL NOT derive behavior from the CAS_Image filename or from the fixture Chunk counts, sizes, or values stated for the "Night Knight.cas" test fixture.
