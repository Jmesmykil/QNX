# FW20 NPDM Field Analysis: uSystem Boot Regression

- **Target:** uSystem NPDM (creator-owned, title ID 0x0100000000001000)
- **Authorization:** Creator-owned hardware + CFW, public-OSS sources only
- **Analysis Date:** 2026-05-05
- **Sources:** libstratosphere `ldr_types.hpp`, `svc_types_common.hpp`, libnx `svc.h`, npdmtool binary strings, `ldr_types.hpp` Npdm MetaFlag enum, Atmosphere-libs git log, uSystem.json HEAD 9246dc1b

---

## 1. What Fields npdmtool Added for FW 20 Support

The six warning strings all originate from a single npdmtool JSON-ingestion path. The binary (Oct 23 2024, 70672 bytes, arm64 Mach-O) emits `"Failed to get %s (field not present)."` for any of these keys that are absent. The keys and their canonical locations:

| JSON key | Binary struct location | Introduced |
|---|---|---|
| `optimize_memory_allocation` | `Npdm::flags` bit 4 (`MetaFlag_OptimizeMemoryAllocation`) | FW 7.0.0 |
| `disable_device_address_space_merge` | `Npdm::flags` bit 5 (`MetaFlag_DisableDeviceAddressSpaceMerge`) | FW 11.0.0 |
| `enable_alias_region_extra_size` | `Npdm::flags` bit 6 (`MetaFlag_EnableAliasRegionExtraSize`) | FW 18.0.0 |
| `prevent_code_reads` | `Npdm::flags` bit 7 (`MetaFlag_PreventCodeReads`) | FW 20.0.0 (new in this npdmtool build) |
| `signature_key_generation` | `Npdm::signature_key_generation` u32 at offset +0x04 | FW 9.0.0 |
| `force_debug_prod` | `debug_flags` KAC entry (`m_is_force_debug_prod` in `KDebugBase`) | FW 19.0.0 |

Sources: `libstratosphere/include/stratosphere/ldr/ldr_types.hpp` lines 231–234 and 252; `libvapours/include/vapours/svc/svc_types_common.hpp` lines 438–444 (firmware annotations); libnx `include/switch/kernel/svc.h` lines 364–366; mesosphere `kern_k_debug_base.hpp` line 35.

---

## 2. What Each Field Controls

### `optimize_memory_allocation` — FW 7.0.0+
- **Kernel role:** Sets `CreateProcessFlag_OptimizeMemoryAllocation` (bit 11 of the process flags word). Instructs the kernel memory manager to use a compact allocation strategy that reduces physical-page fragmentation at the cost of not pre-reserving a large contiguous pool. Documented in libnx `svc.h` line 364: "Only allowed in combination with `is_application`."
- **Default when absent:** bit is 0 (disabled). The npdmtool warning is informational; absence does not set the bit.
- **am.nss enforcement on FW 20 system applets:** Not enforced. This flag is relevant only for Application processes (`is_application = 1`). uSystem is `application_type: 2` (SystemApplet), not an Application. am does not gate system-applet proxy registration on this bit.
- **Can trigger svc::LimitReached at proxy-registration?** No.

### `disable_device_address_space_merge` — FW 11.0.0+
- **Kernel role:** Sets `CreateProcessFlag_DisableDeviceAddressSpaceMerge` (bit 12). When set, the kernel's device address-space allocator does not coalesce adjacent mappings. Required for processes that use many small, independent IOMMU regions and need precise VA layout. Source: `svc_types_common.hpp` line 441 comment.
- **Default when absent:** bit is 0 (merge enabled). Normal behavior.
- **am.nss enforcement:** Not enforced for system applets.
- **Can trigger svc::LimitReached?** No.

### `enable_alias_region_extra_size` — FW 18.0.0+
- **Kernel role:** Sets `CreateProcessFlag_EnableAliasRegionExtraSize` (bit 13). Extends the alias region (transfer-memory staging area) beyond its default size. `InfoType_AliasRegionExtraSize = 28` allows querying the additional bytes granted. Source: `svc_types_common.hpp` lines 193 and 443–444.
- **Default when absent:** bit is 0, alias region is standard size.
- **am.nss enforcement:** Not enforced for system applets in any analyzed path.
- **Can trigger svc::LimitReached?** No direct path. If a FW 20 system applet is expected to use the extended alias region internally and am tries to map transfer memory into that region, a mapping failure is possible — but this manifests as a different result code, not 0x10801.

### `prevent_code_reads` — FW 20.0.0 (new in this npdmtool build)
- **Kernel role:** Sets `MetaFlag_PreventCodeReads` (bit 7 of `Npdm::flags`, `u8 flags` at offset 0x0C). Instructs the loader to map `.text` segments with execute-only (XO) permission so that `svcDebugActiveProcess` / `svcReadDebugProcessMemory` cannot read code pages. Source: `ldr_types.hpp` line 234; libnx `NsoHeader::Flag_PreventCodeReads` (bit 6 of NSO flags, which feeds the same loader policy). NSO header definition at `ldr_types.hpp` line 103.
- **Default when absent:** bit is 0, code pages are readable (RX, not XO). This is the safe default. No behavior change for a non-debug system applet.
- **am.nss enforcement:** am does not read-check uSystem's code pages as a precondition to proxy registration.
- **Can trigger svc::LimitReached?** No.

### `signature_key_generation` — FW 9.0.0
- **Kernel role:** `Npdm::signature_key_generation` at binary offset +0x04 (u32). Selects which ACID RSA-2048 signing key the loader uses to verify the ACID signature of this NPDM. Generation 0 = original prod key, generation 1 = second key rotation. For Atmosphère with patched signature verification this field is overridden; the value stored in the NPDM is verified against the key set in `fssystem_crypto_configuration.cpp` (`NxAcidSignatureKeyGenerationMax = 1`). Source: `ldr_types.hpp` line 252; `fssystem_crypto_configuration.cpp` lines 25 and 301–316.
- **Default when absent from JSON:** npdmtool writes 0. This is correct for all pre-rotation titles.
- **am.nss enforcement:** am does not re-verify the ACID signature at proxy registration time. Signature verification happens in ldr at process launch. Once uSystem is running, this field has no further effect.
- **Can trigger svc::LimitReached?** No. A wrong key generation causes a launch failure, not an am abort.

### `force_debug_prod` — FW 19.0.0
- **Kernel role:** Stored in the KAC `debug_flags` descriptor. Sets `KDebugBase::m_is_force_debug_prod` when the kernel attaches a debug session. When true, kernel memory reads via `svcReadDebugProcessMemory` bypass production-mode security checks. Source: `kern_k_debug_base.hpp` lines 35 and 66–68. The npdmtool binary validates that at most one of `allow_debug`, `force_debug`, `force_debug_prod` is true at a time.
- **Default when absent:** false (production debug reads blocked, normal behavior).
- **am.nss enforcement:** Not checked at proxy-registration. Debug flags are a per-debug-session attribute, not a process state that am inspects.
- **Can trigger svc::LimitReached?** No. am does not attach a debug session to uSystem during proxy acquisition.

---

## 3. Did the Kernel-Capability ABI Change Between FW 19 and FW 20?

**No ABI-breaking change between FW 19 and FW 20 for system-applet NPDMs.**

- `CreateProcessFlag_All` in the installed `svc_types_common.hpp` contains exactly bits 0–3, 4–6, 11, 12, 13. No new mandatory bit was introduced for system processes in FW 20.
- The Atmosphere-libs commit `723142e9` (Dec 13 2024) bumped the submodule to add `force_debug_prod` to debug flag handling for FW 19.0.0. That was the only structural ABI change near this era.
- The four MetaFlags (bits 4–7 in `Npdm::flags`) have been present in this Atmosphere-libs tree since before the FW 20 npdmtool build (Oct 23 2024). Their absence in a built NPDM leaves those bits zero, which is safe for all four.
- libnx `svc.h` annotates `enable_alias_region_extra_size` as `[18.0.0+]`, meaning FW 18 introduced the kernel support; FW 19 and FW 20 carry it forward. No new `CreateProcessFlag` bit was added specifically at FW 20.

**Conclusion:** The six warnings are cosmetic. Npdmtool introduced parsing support for these fields in a build that post-dates when uSystem.json was authored. The NPDM bytes produced are identical to what a newer uSystem.json would produce, because all six absent fields default to 0/false — which is what npdmtool would write if they were present with false/0 values.

---

## 4. Is `pool_partition: 1` or `pool_partition: 2` Correct for FW 20 System Applets?

The mapping is defined in two places:

`ldr_types.hpp` `Acid::PoolPartition`:
- `PoolPartition_Application     = 0`
- `PoolPartition_Applet          = 1`
- `PoolPartition_System          = 2`
- `PoolPartition_SystemNonSecure = 3`

libnx `svc.h` `PhysicalMemorySystemInfo`:
- `_Application  = 0`
- `_Applet       = 1`
- `_System       = 2`
- `_SystemUnsafe = 3`

uSystem at title ID `0x0100000000001000` is `SystemAppletMenu` (qlaunch). Its correct pool partition is **1 (Applet)**, not 2 (System).

The confusion arises because uSystem runs as the system home menu, but its memory comes from the Applet pool, not the System pool. Nintendo's own qlaunch uses pool partition 1. The System pool (2) is for background system modules, not interactive applets. Using pool partition 2 for a SystemApplet process would give it access to a different physical memory region — but the deeper problem is that doing so without matching what am.nss expects for `IAllSystemAppletProxiesService` clients may trigger an internal consistency assertion in am's session tracking.

The SP4.15.1 hotfix commit message (`9246dc1b`) documents this directly: `pool_partition: 1 → 2` was one of three "never-verified" changes that caused the black-screen regression, and was reverted to `pool_partition: 1` as part of restoring the SP4.13 hardware-validated baseline.

**`pool_partition: 1` is correct. Do not change it for FW 20.**

---

## 5. Exact Difference Between Apr 26 Binary NPDM and a Fresh Build Today

The deployed Apr 26 binary was built from `9246dc1b` (the SP4.15.1 hotfix commit, Apr 25 2026). A fresh build today from the same HEAD would use the same `uSystem.json`. The npdmtool binary has not changed (Oct 23 2024 timestamp, 70672 bytes). Therefore:

**The NPDM bytes are byte-for-byte identical between the deployed Apr 26 binary and a fresh build today.**

The six warning fields are absent in both cases. Npdmtool defaults all six to 0/false when absent. No npdmtool default changed between any relevant build date, because npdmtool itself has not been updated since Oct 23 2024. There is no NPDM delta to explain the regression.

---

## Field Table

| JSON key | NPDM binary location | Kernel feature gated | Default if absent | am.nss enforces on FW20 system applets | Can cause 0x10801? |
|---|---|---|---|---|---|
| `optimize_memory_allocation` | `Npdm::flags` bit 4 | Compact page allocation (application-only) | 0 (off) | No | No |
| `disable_device_address_space_merge` | `Npdm::flags` bit 5 | Prevents IOMMU VA coalescing | 0 (merge enabled) | No | No |
| `enable_alias_region_extra_size` | `Npdm::flags` bit 6 | Extends alias/transfer-memory region | 0 (standard size) | No | No |
| `prevent_code_reads` | `Npdm::flags` bit 7 | Execute-only `.text` mapping | 0 (code readable) | No | No |
| `signature_key_generation` | `Npdm` u32 at +0x04 | ACID signing key selection | 0 (generation 0) | No (verified at launch, not proxy-reg) | No |
| `force_debug_prod` | KAC `debug_flags` | Debug session bypasses prod memory checks | false | No | No |

---

## Recommendations

### Fields to Add to uSystem.json

None of the six fields are required for FW 20 functionality. The warnings are informational. Adding them with their default values produces an identical NPDM. If you want to silence the warnings and future-proof the JSON, add these top-level fields:

```json
"optimize_memory_allocation": false,
"disable_device_address_space_merge": false,
"enable_alias_region_extra_size": false,
"prevent_code_reads": false,
"signature_key_generation": 0
```

And add `"force_debug_prod": false` alongside the existing `"allow_debug": false` and `"force_debug": false` in the `debug_flags` KAC entry.

The exact addition to the `debug_flags` value block:
```json
{
    "type": "debug_flags",
    "value": {
        "allow_debug": false,
        "force_debug": false,
        "force_debug_prod": false
    }
}
```

### Does This Fix the Boot Regression?

**Confidence: LOW.**

The NPDM field analysis eliminates the NPDM as a root cause. None of the six absent fields control anything that am.nss checks during `IAllSystemAppletProxiesService` proxy registration. The NPDM bits have been zero since the beginning; FW 20 does not gate system-applet proxy acquisition on any of them.

The regression was confirmed (in SP4.15.1 hotfix commit) to co-occur with `pool_partition: 1 → 2` and a heap size change, both of which are already reverted. With nx-ovlloader also disabled and the crash still occurring, the highest-remaining probability cause is the cmd 100 vs cmd 110 issue documented in `AM-NSS-0X40570-ANALYSIS.md`: if the uSystem binary was built against a libnx that dispatches appletAE cmd 100 (`OpenSystemAppletProxyOld`) instead of FW-20-required cmd 110 (`OpenSystemAppletProxy`), am returns `UnknownCommandId`, and the abort fires at the 0x40570 thunk with LR at 0x2318.

Verify the uSystem binary's appletAE dispatch: check whether `appletInitialize()` in the compiled binary calls cmd 100 or cmd 110 by inspecting the compiled object or rebuilding against a libnx that explicitly supports FW 20 cmd 110. That is the next diagnostic action.
