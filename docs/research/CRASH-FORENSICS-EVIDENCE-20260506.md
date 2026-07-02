# Crash Forensics Evidence — 2026-05-06

**Target:** Creator-owned QOS hardware (Switch SD card)
**Authorization:** Owned — full analysis permitted
**Analysis date:** 2026-05-06
**Source:** `/Volumes/SWITCH SD/atmosphere/crash_reports/`
**Reference:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libvapours/include/vapours/results/`

---

## Per-Crash Table

All crash report files on the SD card, today (May 6) only.

| File ID | Time | Process | Prog ID | Result code | Decoded | uSystem MD5 (build ID) | uMenu MD5 | uSystem binary |
|---------|------|---------|---------|-------------|---------|----------------------|-----------|----------------|
| 01778094692 | 09:11:32 | uLoader_apl | 010000000000100d | 0x00A8 (2168-0000) | ams::creport::ResultUndefinedInstruction | aa267e1a (unknown build ID) | 9a4c3b7c | aa267e1a |
| 01778112200 | 14:03:20 | uSystem | 0100000000001000 | 0xCC0B (2011-0102) | ams::sf::hipc::ResultOutOfSessionMemory | 1ed9b81b (99AB4DB4) | 2e6d8c36 | 1ed9b81b |
| 01778112201 | 14:03:21 | am | 0100000000000023 | 0xC880 (2128-0100) | nn::os module, desc 100 (not in AMS headers) | — | 2e6d8c36 | 1ed9b81b |
| 01778113079 | 14:17:59 | uSystem | 0100000000001000 | 0xCC0B (2011-0102) | ams::sf::hipc::ResultOutOfSessionMemory | 1ed9b81b (99AB4DB4) | bcaf108c | 1ed9b81b |
| 01778113080 | 14:18:00 | am | 0100000000000023 | 0xC880 (2128-0100) | nn::os module, desc 100 | — | bcaf108c | 1ed9b81b |
| 01778113261 | 14:21:01 | uSystem | 0100000000001000 | 0xCC0B (2011-0102) | ams::sf::hipc::ResultOutOfSessionMemory | 5b22a1cd (8BBD5A46) | 3beeb544 | 5b22a1cd |
| 01778113263 | 14:21:03 | am | 0100000000000023 | 0xC880 (2128-0100) | nn::os module, desc 100 | — | 3beeb544 | 5b22a1cd |
| 01778115044 | 14:50:44 | uSystem | 0100000000001000 | 0xCC0B (2011-0102) | ams::sf::hipc::ResultOutOfSessionMemory | 5b22a1cd (8BBD5A46) | 30d020929060 | 5b22a1cd |
| 01778115045 | 14:50:45 | am | 0100000000000023 | 0xC880 (2128-0100) | nn::os module, desc 100 | — | 30d020929060 | 5b22a1cd |

### Per-crash detail: uLoader (09:11)

- **File:** `01778094692_010000000000100d.log`
- **Exception type:** Undefined Instruction
- **PC:** `0x0000000177141014` (`[00000000]` + 0x14) — module ID all-zeros = unmapped/null page
- **LR:** `0x0000000025c021f8` (uLoader + 0x21f8)
- **Stack trace:** uLoader + 0x20cc → uLoader + 0x6f4 → uLoader + 0x2a0
- **Module 00 build ID:** 0000000000000000 (null — the page being executed at crash)
- **Module 01 build ID:** `11707509` (uLoader)
- **Process Flags:** 000000b3
- **Crashed Thread:** 0000000000000284
- **Interpretation:** Jumped to a null or unmapped page at virtual address 0x177141014. Opcode = 0x00000000 = null word. uLoader_apl branched to an uninitialized/unmapped code region.

### Per-crash detail: uSystem (all four May 6 pairs)

All four uSystem crashes share the same crash structure:
- **Exception type:** User Break, Break Reason 0x0, Break Size 0x4
- **Crashed Thread ID:** 0000000000000238 (same thread ID across all four crashes)
- **Process Flags:** 000000b7

**Build 99AB4DB4 (1ed9b81b) crashes — PC and stack:**

| File | PC offset | LR offset | Stack [0] | Stack [1] | Stack [2] | Stack [3] |
|------|-----------|-----------|-----------|-----------|-----------|-----------|
| 01778112200 | uSystem + 0x3a3cc | uSystem + 0x3ed2c | +0xd084 | +0xde4c | +0xca4 | +0x2b590 |
| 01778113079 | uSystem + 0x3a3cc | uSystem + 0x3ed2c | +0xd084 | +0xde4c | +0xca4 | +0x2b590 |

**Build 8BBD5A46 (5b22a1cd) crashes — PC and stack:**

| File | PC offset | LR offset | Stack [0] | Stack [1] | Stack [2] | Stack [3] |
|------|-----------|-----------|-----------|-----------|-----------|-----------|
| 01778113261 | uSystem + 0x3643c | uSystem + 0x3acdc | +0xbda4 | +0xcb4c | +0xca4 | +0x28290 |
| 01778115044 | uSystem + 0x3643c | uSystem + 0x3acdc | +0xbda4 | +0xcb4c | +0xca4 | +0x28290 |

Stack frame `+0xca4` appears in all four uSystem crashes (both builds). This is a consistent entry point across both binaries.

### Per-crash detail: am (all four May 6 pairs)

All four am crashes share an identical signature:
- **Module build ID:** `E3722DA9` (same am binary in all four crashes and in all May 5 crashes)
- **Exception type:** User Break, Break Reason 0x0
- **PC offset (crashed thread, thread 0x97):** `[e3722da9]` + 0x4056c (all four crashes)
- **LR offset (crashed thread):** `[e3722da9]` + 0x2318 (all four crashes)
- **Stack trace (crashed thread, all four):**
  - `[e3722da9]` + 0x21b8
  - `[e3722da9]` + 0x2218
  - `[e3722da9]` + 0x22f8
  - `[e3722da9]` + 0x5048c
  - `[e3722da9]` + 0x552d4
  - `[e3722da9]` + 0x575e4
  - `[e3722da9]` + 0x56f80
  - `[e3722da9]` + 0x31b00
  - `[e3722da9]` + 0x34994
- **Process Flags:** 00001137
- **Crashed Thread ID:** 0000000000000097

The am crash stack trace is byte-for-byte identical (offset-wise) across all four crash events.

---

## Result Code Frequency Table

| Result code | Hex | Source | Affected process | Count (May 6) | Count (May 5) |
|-------------|-----|--------|-----------------|--------------|--------------|
| 2011-0102 | 0xCC0B | `hipc_results.hpp` line 25: `R_DEFINE_ERROR_RESULT(OutOfSessionMemory, 102)` | uSystem | 4 | 0 |
| 2128-0100 | 0xC880 | nn::os (module 128, not in AMS headers) | am | 4 | 0 |
| 2168-0000 | 0x00A8 | `creport_results.hpp` line 23: `R_DEFINE_ERROR_RESULT(UndefinedInstruction, 0)` | uLoader_apl | 1 | 0 |
| 2001-0132 | 0x10801 | `svc_results.hpp` line 69: `R_DEFINE_ERROR_RESULT(LimitReached, 132)` | am | 0 | 6 |

**Module namespace mapping (from libvapours headers):**
- Module 11 = `ams::sf::hipc` (`hipc_results.hpp` line 20: `R_DEFINE_NAMESPACE_RESULT_MODULE(ams::sf::hipc, 11)`)
- Module 168 = `ams::creport` (`creport_results.hpp` line 20: `R_DEFINE_NAMESPACE_RESULT_MODULE(ams::creport, 168)`)
- Module 1 = `ams::svc` (`svc_results.hpp` line 22: `R_DEFINE_NAMESPACE_RESULT_MODULE(ams::svc, 1)`)
- Module 128 = Nintendo proprietary (nn::os) — not defined in libvapours headers

---

## Same-Binary Same-Result Correlation

### uSystem crashes

Every uSystem crash today produced **2011-0102 (OutOfSessionMemory)** regardless of which uSystem binary was deployed:
- 1ed9b81b (build 99AB4DB4): 2 crashes, both 2011-0102
- 5b22a1cd (build 8BBD5A46): 2 crashes, both 2011-0102

The uSystem PC offset and stack differ between the two builds (expected — different code layout), but the result code is identical. The HIPC session exhaustion is reproducible across both uSystem versions.

### am crashes

Every am crash today produced **2128-0100** at the identical PC offset `[e3722da9] + 0x4056c`. The am build ID `E3722DA9` is unchanged across all today's crashes AND all May 5 crashes. The am binary was not swapped during any of today's testing.

The May 5 am crashes produced **2001-0132 (LimitReached)** — a different result code from a different PC offset (`[e3722da9] + 0x4051c` on May 5 vs `+ 0x4056c` on May 6). This indicates a different code path within the same am binary was triggered, corresponding to a different failure mode.

### uMenu binary correlation

Four different uMenu binaries were deployed across today's four crash windows:
- 2e6d8c36 → crash pair 14:03
- bcaf108c → crash pair 14:17
- 3beeb544 → crash pair 14:21
- 30d020929060 → crash pair 14:50

All four produced the identical result code pair (2011-0102 + 2128-0100). The crash result is **invariant with respect to uMenu binary identity.**

---

## Apr 26 Pair Control

**Claim under test:** uSystem=5b22a1cd + uMenu=3beeb544 booted without crash "this morning" (user's report).

**Evidence from crash reports:**

Zero crash report files exist with timestamps between Apr 26 and May 5 07:24. The crash report directory contains no logs from the Apr 26 window. This is consistent with the claim that the Apr 26 pair did not trigger an Atmosphère crash report during that session.

**Crash report evidence from the 14:21 and 14:50 windows (uSystem=5b22a1cd + uMenu=3beeb544 and 30d020929060):**

Both crashes during the 14:21 window (uSystem=5b22a1cd + uMenu=3beeb544) and 14:50 window (uSystem=5b22a1cd + uMenu=30d020929060=v2.3.7) produced result 2011-0102 in uSystem and 2128-0100 in am.

The 14:21 pair is the only data point where uSystem=5b22a1cd and uMenu=3beeb544 ran together during today's testing. It crashed.

**What the crash data says about "earlier today successful boot":**

The uMenu log (`/Volumes/SWITCH SD/qos-shell/logs/uMenu.0.log`) confirms that boot sequences seq=529 (May 6 05:48) and seq=530 (May 6 07:46) completed successfully (logged "Alive!" with no terminal error). At those times:
- uSystem deployed = 5b22a1cd (confirmed: bak-pre-1ed9b81b saved at 09:16 = 5b22a1cd, meaning 5b22a1cd was there until 09:16)
- uMenu deployed at seq=529-530 = 17ee4cf0 (bak-pre-stabilization saved at 09:06, capturing what was there before stabilization was deployed)

uMenu=3beeb544 was **not deployed until 14:20 today**. Any "successful boot" with 3beeb544 references either an Apr 26 session (no crash reports from that date) or a different SD deployment path.

---

## Strict Factual Conclusions

1. **Every uSystem crash today produced 2011-0102 (ams::sf::hipc::ResultOutOfSessionMemory).** Source file: `hipc_results.hpp` line 25. This code means the HIPC IPC session memory pool was exhausted when uSystem attempted an IPC operation.

2. **Every am crash today produced 2128-0100 at identical PC offset `[e3722da9] + 0x4056c`.** The am binary (build E3722DA9) is the same binary that crashed on May 5 with a different result code. The same binary survived boot sequences 529-561 with various uMenu partners before today's afternoon testing began.

3. **uMenu binary identity does not correlate with the crash result code.** Four different uMenu binaries (2e6d8c36, bcaf108c, 3beeb544, 30d020929060) produced identical crash signatures in uSystem and am. The crash is not unique to any single uMenu build.

4. **uSystem binary identity does not prevent the crash.** Both 1ed9b81b and 5b22a1cd produced 2011-0102, though at different code offsets within each build. The HIPC session exhaustion is reproducible regardless of which uSystem binary is running.

5. **The uLoader crash at 09:11 is structurally separate.** It is an Undefined Instruction at a null-module address. It predates the uSystem/am crash window and involves a different process (uLoader_apl, prog ID 010000000000100d). uSystem and am were not involved.

6. **May 5 crashes (6x, all am, 2001-0132=svc::LimitReached) are a distinct failure mode** from May 6 crashes. Different result code, different PC offset within same am binary, no paired uSystem crash. These represent a kernel resource limit being hit, not an HIPC session pool exhaustion.

7. **No crash reports exist from Apr 25–May 5 09:32 window.** The crash directory gap from Apr 25 16:21 to May 5 07:24 corresponds to a period with no Atmosphère crash reports. This neither confirms nor denies stability during that window — it only confirms no crash report was generated.

8. **The uSystem crash at 14:21 (5b22a1cd + 3beeb544) is the only data point today with the target binary pair.** It crashed with result 2011-0102, identical to the three other crash windows. The crash is not unique to this pair.

---

*Files referenced:*
- `/Volumes/SWITCH SD/atmosphere/crash_reports/` (15 log files analyzed)
- `/Volumes/SWITCH SD/ulaunch/bin/uMenu/` (binary MD5s via `md5` command)
- `/Volumes/SWITCH SD/atmosphere/contents/0100000000001000/` (uSystem binary MD5s)
- `/Volumes/SWITCH SD/qos-shell/logs/uMenu.0.log` (boot sequence timestamps)
- `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libvapours/include/vapours/results/hipc_results.hpp`
- `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libvapours/include/vapours/results/creport_results.hpp`
- `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/Atmosphere-libs/libvapours/include/vapours/results/svc_results.hpp`
