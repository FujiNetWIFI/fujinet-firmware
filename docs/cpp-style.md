# FujiNet C++ and code-structure guide

The detailed companion to `AGENTS.md`. Read this before writing device-side code, adding a command,
or making any non-trivial change. `AGENTS.md` carries the short version; where the two disagree,
this file is the more specific and wins.

## C++ practices

C++20. Exceptions are **disabled** in firmware builds (`CONFIG_COMPILER_CXX_EXCEPTIONS` is unset), so
a `throw` aborts the device at runtime: return status/error codes on firmware paths and do not add
`try`/`catch` to them. The tree is long-lived and inconsistent — the rules below govern code you
write; do not convert surrounding code to match as a side effect of an unrelated change.

- **Never return a bare `bool` for success or failure.** Return `success_is_true` or
  `error_is_true` from `include/global_types.h`, whichever that file already uses — do not mix the
  two within a file. Return them with `RETURN_SUCCESS_AS_TRUE()`, `RETURN_ERROR_AS_TRUE()`,
  `RETURN_SUCCESS_IF(expr)`, `RETURN_ERROR_IF(expr)` and friends.
- **Test those results with `.is_error()` or `.is_success()`, always.** Never compare one against
  `true`, `false`, `0` or `1`, and never rely on its truthiness — the two types carry opposite
  polarity, so `if (result)` is ambiguous at a glance and wrong half the time.
- **Compare a `fujiError_t` against `FUJI_ERROR::NONE` and nothing else.** `if (err != FUJI_ERROR::NONE)`
  is the failure test; `if (err == FUJI_ERROR::NONE)` is the success test. Never test for
  `FUJI_ERROR::UNSPECIFIED`. The enum in `include/global_types.h` currently holds only `NONE = 0` and
  `UNSPECIFIED = 1`, but `UNSPECIFIED` is a stand-in for a real list of error codes that is expected
  to be filled in — likely by merging it with `NDEV_STATUS` in
  `lib/network-protocol/status_error_codes.h`. Code that tests against `UNSPECIFIED` silently stops
  detecting failures as soon as a second code exists. For the same reason, do not write a `switch`
  that assumes the enum has exactly two values, and do not copy the `NDEV_STATUS` list anywhere.
- **Use the endian-sized integer types for anything that goes on the wire**: `u16le_t`, `u24le_t`,
  `u32le_t`, `u16be_t`, `u24be_t`, `u32be_t`, and `u16ne_t`, `u24ne_t`, `u32ne_t` for platform
  native (all in `include/global_types.h`, each `static_assert`ed to its exact byte width). Embed
  them directly in packed protocol structs and pass the address of one straight to a read or write
  call. Do not hand-assemble values with bit shifts over a `uint8_t` array, and do not call
  `htole*()`/`htobe*()`.
- Own every resource. Prefer a `std::unique_ptr`, a container, or a small RAII wrapper to a bare
  `new`/`delete` pair, and never leave a raw owning pointer across an early return. Heap leaks on
  error paths are a recurring bug class here (see `[all] fix heap leaks on error paths`).
- Check every allocation and every fallible call before use, including on error and teardown paths.
- Give a base class a `virtual` destructor; mark every derived function `override`; do not repeat
  `virtual` on an override.
- Use `nullptr`, not `NULL`. Use `static_cast`/`reinterpret_cast`, not C-style casts. Use
  `enum class` for new enumerations.
- Bounds-check all buffer work. Use `snprintf`, never `sprintf`, `strcpy` or `strcat`, and prefer
  the existing `mstr::`/`util_` string helpers to hand-rolled buffer arithmetic.
- Pass non-trivial parameters by `const&`; return by value. Mark methods `const` when they do not
  mutate. Initialise every member at declaration or in the constructor's init list.
- Keep functions short enough to read whole. If a switch on a device command grows a long inline
  body, factor the body into a named method.

## Separation of concerns and readable code

Read `lib/device/fujiDevice/` and `lib/device/NDevice/` before writing device-side code. They are
the model the rest of the tree is being moved toward, and new code is reviewed against them.

- **One feature per class, one class per file pair.** `Base64Mixin`, `HashMixin`, `QRMixin` and
  `AppKeyMixin` each own one capability and nothing else. A new self-contained feature is a new
  mixin, not more methods bolted onto `fujiDevice`.
- **Dispatch through a table, not a switch.** Each mixin fills a `FujiMixinCommandHandlers` map of
  command ID to member function and `FujiDeviceMixin::processCommand` looks the handler up. Adding a
  command means one table entry plus one small handler; it never means growing a switch.
- **One handler, one job, named for it.** `qr_input`, `qr_length`, `qr_output` — short methods whose
  name states what they do, so the dispatch table reads as documentation.
- **Vary behaviour by overriding a virtual.** `QRMixin::qr_encode(const FUJI_COMMAND_PACKET&)` is
  virtual precisely so a platform can change how parameters are unpacked without touching shared
  code. That override point is the intended extension mechanism.
- **Use a strategy object for pluggable behaviour.** `NDevice` holds an `NParser` and
  `JSONParser`/`XMLParser`/`HTMLParser` implement it; supporting another format is a new subclass,
  not a branch in `NDevice`.

Readability rules that follow from the above:

- A function should do one thing and fit on a screen. If you need a comment to mark a section
  inside a function, extract that section into a named method instead.
- Return early on error rather than nesting the success path inside `if` blocks.
- Name things for the domain — the command, the protocol field, the disk slot — not for their type
  or for the loop they sit in. Avoid abbreviations that are not already used in the protocol docs.
- Do not add a parameter, a flag, or a `bool` argument to steer an existing function down a second
  path; add the second function or the override. A `bool` in a signature here is almost always
  either a steering flag or a misused error return — see C++ practices above for what to return
  instead.
- Keep header files to declarations and small inline accessors; put logic in the `.cpp`.
