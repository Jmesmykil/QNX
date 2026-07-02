# C++ Evolution Plan — Q OS Manager

Version: v0.6.1 → target v1.0 (refactor) → v2.0 (system applet)
Date: 2026-04-18
Standard in use: `gnu++23` (already set in Makefile CXXFLAGS)

---

## Current C++ baseline

uManager already uses `gnu++23` in its Makefile. The code is clean enough — no raw owning pointers except in `man_AppScanner.cpp` (two `new` / `delete[]` pairs). The main technical debt areas are:

1. Heap-allocated `NsApplicationControlData` and icon buffers managed with naked `new`/`delete`.
2. `Version::FromString` uses a manual string-splitting loop that should be a ranges pipeline.
3. `HasConnection()` contains a logic bug (`gethostid() == INADDR_LOOPBACK` is always `false` on Switch — it should use `nifmInitialize` + `nifmIsAnyInternetRequestAccepted`).
4. Global mutable state (`g_MainApplication`, `g_IsAvailable`, `g_GotVersion`, `g_CurrentOnProgressCallback`) is scattered across translation units.
5. The `Version` struct's `IsLower` / `IsHigher` semantics are inverted relative to their names (see man_Manager.hpp — `IsLower` returns true when `this > other`).
6. No unit tests for the SMI client code or AppScanner.
7. No structured error propagation — `UL_RC_ASSERT` terminates on failure; `UL_RC_TRY` propagates `Result` codes but the callers rarely handle them.

---

## Phase 1 — Memory safety (target: v0.7)

### 1.1 Replace raw heap allocations in man_AppScanner.cpp

```cpp
// Before (man_AppScanner.cpp line 228)
auto ctrl_data = new NsApplicationControlData();
// ... work ...
delete ctrl_data;

// After
auto ctrl_data = std::make_unique<NsApplicationControlData>();
// no delete — RAII handles it even on early-return paths
```

`NsApplicationControlData` is ~786 KiB — must stay on heap, but `unique_ptr` gives safe ownership with zero overhead.

```cpp
// Before — icon buffer
info.icon_data = new uint8_t[icon_bytes];
// ...
delete[] app.icon_data;

// After — use std::vector<uint8_t> in AppInfo
struct AppInfo {
    uint64_t    app_id;
    char        name[512];
    char        author[256];
    std::vector<uint8_t> icon_data; // empty = no icon
};
// WriteIconFile signature becomes:
static size_t WriteIconFile(uint64_t app_id, std::span<const uint8_t> jpeg);
```

`std::span<const uint8_t>` (C++20) is a zero-overhead non-owning view — no copy.
Manual `delete[]` loop in step 7 of `ScanAndWriteAppList` disappears entirely.

### 1.2 Remove global mutable state

Replace the three file-scope globals in main.cpp with a single `AppContext` aggregate and pass it by reference:

```cpp
// main.cpp — after
struct AppContext {
    bool    is_available;
    bool    version_match;
    Version got_version;
};
```

`g_MainApplication` must stay a Ref (Plutonium global handle) but `g_IsAvailable` / `g_GotVersion` can move into `AppContext`.

---

## Phase 2 — C++20 modernization (target: v0.8)

### 2.1 Version parsing with ranges

```cpp
// Before — manual delimiter loop in man_Manager.cpp
while((pos = ver_str_cpy.find(delimiter)) != std::string::npos) { ... }

// After — C++20 ranges + string_view split
static Version FromString(std::string_view ver_str) noexcept {
    Version v{};
    u32 idx = 0;
    for (auto part : ver_str | std::views::split('.')) {
        std::string_view sv(part.begin(), part.end());
        u32 n = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), n);
        switch (idx++) {
            case 0: v.major = n; break;
            case 1: v.minor = n; break;
            case 2: v.micro = static_cast<s32>(n); break;
        }
    }
    return v;
}
```

`std::from_chars` is constexpr-friendly, no exceptions, no locale, no allocation.

### 2.2 Three-way comparison for Version

```cpp
// Replace IsLower / IsHigher / IsEqual with spaceship operator
auto operator<=>(const Version&) const = default;
```

The three hand-written comparison methods (which also contain a naming bug — IsLower
returns true when `this > other`) disappear. Callers become:

```cpp
if (last_ver == cur_ver) { ... }
else if (last_ver > cur_ver) { ... }   // newer available
else { ... }                            // we are ahead
```

### 2.3 Concepts for service result wrappers

```cpp
// ul/ul_Result.hpp — add a concept
template<typename T>
concept ServiceResult = requires(T r) {
    { R_SUCCEEDED(r) } -> std::convertible_to<bool>;
};

// Then constrain helper templates
template<ServiceResult R>
[[nodiscard]] constexpr bool Succeeded(R rc) noexcept {
    return R_SUCCEEDED(rc);
}
```

### 2.4 Designated initializers already used — enforce them

`BinHeader hdr{};` + field assignment is correct. Convert to designated initializer style:

```cpp
BinHeader hdr{
    .magic    = QAppMagic,
    .version  = QAppVersion,
    .count    = static_cast<uint32_t>(apps.size()),
    .reserved = 0,
};
```

Already done in the existing code for some structs. Apply consistently.

### 2.5 Structured error handling — Result<T, E> wrapper

Introduce a thin `ul::Expected<T>` (equivalent to `std::expected<T, Result>` from C++23)
wrapping the libnx `Result` code:

```cpp
// ul/ul_Expected.hpp
template<typename T>
using Expected = std::expected<T, Result>;

// Usage in AppScanner
Expected<std::vector<AppInfo>> EnumerateInstalledTitles();
```

`std::expected` is available in libstdc++13 / libc++17 and in devkitPro's bundled compiler
once dkp-pacman packages catch up to gcc-14. Until then, a thin manual wrapper covers the
same API surface with a trivial `#if __cpp_lib_expected` guard.

`UL_RC_ASSERT` (hard abort) should be reserved for unrecoverable init failures only.
All scan / network operations should propagate `Expected<T>` to callers.

---

## Phase 3 — Modules and compile-time infrastructure (target: v0.9)

### 3.1 Module partitions (C++20 modules — gated on devkitPro compiler support)

devkitPro ships arm-none-eabi-g++ 14.x as of early 2026. Module support in GCC 14
is functional for simple cases. Recommended partition structure:

```
module ul.man;
export module ul.man:scanner;   // man_AppScanner
export module ul.man:network;   // man_Network
export module ul.man:manager;   // man_Manager
export module ul.man:ui;        // ui_* — separate partition to avoid circular deps
```

Gate behind `#ifdef __cpp_modules` to keep the NRO build path working.

### 3.2 constexpr path constants

All `constexpr const char[]` path constants in `man_AppScanner.hpp` and `man_Manager.hpp`
should migrate to `constexpr std::string_view`:

```cpp
constexpr std::string_view QosAppsDir = "sdmc:/switch/qos-apps";
```

`std::string_view` is constexpr-constructible from string literals and avoids the hidden
null-terminator ambiguity of `const char[]`.

---

## Phase 4 — Unit test harness for SMI client (target: v1.0)

### 4.1 SMI mock

The SMI client code (`sf_PublicService.cpp`) wraps three `serviceDispatch` calls.
Testing them on-device requires uSystem to be running. A host-side mock is feasible:

```cpp
// test/mock/mock_Service.hpp
// Provides a stub Service object and stubs for serviceDispatchOut / tipcDispatchInOut
// that return pre-programmed Result codes and output values.
```

Test targets:
- `IsAvailable()` — returns true when SM probe returns success with `has = true`
- `GetVersion()` — parses Version struct correctly
- `Version::FromString` — round-trip parsing of "0.6.1", "1.0", "2.3.4"
- `Version` comparison operators (especially the naming-bug regression: `v{1,0,0} < v{0,9,0}` must be false)
- `ScanResult` defaults — `ScanAndWriteAppList` on zero-title device returns `ok=true, count=0`

Build the test suite with CMake on the host (x86-64 / arm64 macOS) using a libnx shim layer.

### 4.2 Test CMakeLists.txt sketch

```cmake
cmake_minimum_required(VERSION 3.25)
project(umanager-tests CXX)
set(CMAKE_CXX_STANDARD 23)

add_library(libnx-shim INTERFACE)
target_include_directories(libnx-shim INTERFACE test/shim)

add_executable(test-smi
    test/test_Version.cpp
    test/test_SmiClient.cpp
    source/ul/man/man_Manager.cpp
    source/ul/man/man_AppScanner.cpp
)
target_link_libraries(test-smi PRIVATE libnx-shim)
target_include_directories(test-smi PRIVATE include test/shim)
enable_testing()
add_test(NAME smi COMMAND test-smi)
```

---

## Q OS Manager v2.0 — System Applet Roadmap

A v2.0 would be a dedicated NSP launched as a system applet (not homebrew), holding its
TID 0x0500000051AFE003 slot permanently in the Atmosphère content store.

### Architecture delta from v1.0

| Area | v1.0 (NRO/NSP homebrew) | v2.0 (system applet NSP) |
|------|------------------------|--------------------------|
| Service access | `acc:u1`, `ns:am2` (read), `ulsf:u` | + `acc:su` (write), `ns:am2` lifecycle cmds, `ulsf:p` (private SMI pipe), `nifm:u`, `set:sys` |
| NPDM privileges | application pool, limited syscalls | system pool, broader kernel caps |
| SMI role | read-only observer via `ulsf:u` | full participant — can send SystemMessages to uSystem via `ulsf:p` |
| Update install | curl + zip extraction | `ns:am2` NIM path or own NIM shim |
| UI | Plutonium SDL2 | Plutonium SDL2 (unchanged) or migrate to libnx native applet Swkbd-style |

### v2.0 NSP requirements

1. NPDM must declare `system` pool partition and additional `set:sys`, `nifm:u`, `acc:su` service access.
2. Atmosphère `override_config.ini` entry to force-install the NSP at `0x0500000051AFE003`.
3. Add a `ForwardToUSystem(SystemMessage msg, ...)` abstraction over AppletStorage SMI.
4. Replace `HasConnection()` with proper `nifmIsAnyInternetRequestAccepted()` call.
5. Add firmware version display using `setsysGetFirmwareVersion`.

### Development sequence (estimated)

1. Fix `HasConnection()` bug — one function, verifiable on device immediately.
2. `std::unique_ptr` migration + `std::span` for icon buffer — no behavior change, Asan-clean.
3. `Version` spaceship operator + `FromString` ranges rewrite + unit tests (host-side).
4. `Result<T,E>` / `std::expected` wrapper — propagate through AppScanner and Network.
5. NSP NPDM upgrade to system pool — test on device under Atmosphère.
6. SMI private pipe — `ForwardToUSystem` + RestartMenu + ReloadConfig commands.
7. `acc:su` user management panel.
8. `set:sys` firmware version + `nifm:u` storage info panel.

---

## Priority Summary

| Priority | Item | Effort |
|----------|------|--------|
| P0 | Fix `HasConnection()` loopback bug | 15 min |
| P0 | `unique_ptr` for `ctrl_data` + `vector<uint8_t>` for icon bufs | 1 hr |
| P1 | Fix `Version::IsLower`/`IsHigher` naming inversion | 30 min |
| P1 | `Version` spaceship operator + `FromString` ranges | 1 hr |
| P1 | Host-side unit tests for Version + SMI client | 2 hr |
| P2 | `std::expected` error propagation through AppScanner | 3 hr |
| P2 | Concepts for ServiceResult helpers | 1 hr |
| P3 | Module partition scaffolding (gated on devkitPro gcc-14) | 4 hr |
| P4 | v2.0 NPDM system-pool NSP + private SMI pipe | 1-2 days |
