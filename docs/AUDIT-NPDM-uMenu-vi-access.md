# AUDIT — uMenu NPDM + Service Access (v0.7 Tesla Renderer Readiness)

**Date:** 2026-04-18  
**Auditor:** cpp-pro agent  
**Files inspected:**
- `archive/v0.2.1-prep/upstream/projects/uMenu/uMenu.json`
- `archive/v0.2.1-prep/upstream/projects/uSystem/uSystem.json`
- `archive/v0.2.1-prep/upstream/projects/uManager/uManager.json`
- `staging/upstream-diff/extracted-v1.2.0/ulaunch/bin/uMenu/main.npdm` (binary, 964 bytes)

---

## 1. uMenu.json — Full Service Access List

```
"service_access": [ "*" ]
"service_host":   [ "*" ]
```

**Wildcard. All services granted.** This is not an explicit enumeration — it is the npdmtool `"*"` shorthand that expands to allow-all at the ACID/ACI0 SAC layer. Confirmed by binary parse of main.npdm (SAC raw bytes `80 2a 00 2a` = host `*` + access `*`).

### v0.7 Service Checklist

| Service | Required | Present (via `*`) | Notes |
|---------|----------|-------------------|-------|
| `vi:m`  | YES — `viInitialize(ViServiceType_Manager)` + `viCreateManagedLayer` | YES | Wildcard covers it |
| `vi:u`  | Fallback only | YES | Not needed if `vi:m` works |
| `vi:s`  | Fallback only | YES | Not needed if `vi:m` works |
| `pl:`   | YES — `plGetSharedFontByType` | YES | Shared font for stbtt |
| `appletAE` | YES — LibraryApplet proxy | YES | Already used by uMenu today |
| `hid`   | YES — normal input | YES | |
| `hid:sys` | NO — do NOT use | YES (blocked by policy) | Focus steal; overlay-only; uMenu must NOT call `hidsysEnableAppletToGetInput` |
| `pmdm`  | NO | YES | PID lookup only needed with hid:sys path |
| `nvdrv:*` | NO — Tesla path uses no nvdrv | YES (moot) | Pure vi + libnx framebuffer; no Mesa |

**`vi:m` verdict: PRESENT.** No NPDM patch needed for service access.

---

## 2. Key NPDM Fields

| Field | uMenu.json value | Upstream binary (main.npdm) | Match |
|-------|-----------------|------------------------------|-------|
| `name` | `"Q OS Menu"` | `uMenu` | DIFFER (see §4) |
| `program_id` | `0x010000000000FFFF` | `0x010000000000FFFF` | YES |
| `is_64_bit` | `true` | `true` (flags=0x03, bit0=1) | YES |
| `address_space_type` | `1` (39-bit) | `1` | YES |
| `main_thread_priority` | `44` | `44` (byte@0x0E) | YES |
| `default_cpu_id` | `0` | `0` (byte@0x0F) | YES |
| `main_thread_stack_size` | `0x00100000` | `0x00100000` (bytes@0x1C) | YES |
| `system_resource_size` | not present | `0` | YES — absent = 0 |
| `pool_partition` | `0` | `0` (ACID flags bits 2-5) | YES |
| `process_category` | `0` (regular) | `0` | YES |
| `filesystem_access.permissions` | `0xFFFFFFFFFFFFFFFF` | Full rights (ACID FSA) | YES |

---

## 3. fw 20.0.0 NPDM Schema Issues

### Known-bad field: `system_resource_size`

Per `archive/v0.5.1-revertNPDM/CHANGELOG.md` (real hardware evidence, 2026-04-18):

> `"system_resource_size": "0xC00000"` caused Process Manager to silently reject `svcCreateProcess` on fw 20.0.0 — applet pool budget exhausted.

**uMenu.json does NOT have `system_resource_size`.** Confirmed absent. This is safe.

**uSystem.json has `pool_partition: 2` and `address_space_type: 3`.** Those are appropriate for a system-privileged process (SysModule). Not a uMenu concern.

### Suspicious fields in uMenu.json

| Field | Value | Assessment |
|-------|-------|------------|
| `debug_flags` | absent | OK — absence means `allow_debug=false, force_debug=false` |
| `handle_table_size` | absent | OK — npdmtool default (128) applies |
| `kernel_capabilities.min_kernel_version` | `"0x30"` (= HOS 3.0) | LOW RISK — fw 20.0.0 is version 0x140; min 0x30 is permissive, not strict |
| `kernel_capabilities.application_type` | `2` (LibraryApplet) | CORRECT for uMenu |
| `svcMapPhysicalMemoryUnsafe` (0x48) + `svcSetUnsafeLimit` (0x4A) | present | YELLOW — fw 20.0.0 may gate these more strictly for LibraryApplets; only needed if heap extends into physical unsafe range; Tesla path stays within `svcSetHeapSize` quota so these are never called |
| Debug SVCs 0x60-0x6D | present | YELLOW — fw 20.0.0 reportedly tightens debug SVC access for non-debug builds; since `allow_debug=false` these should never trigger, but their presence in the KC descriptor could trip new ACID validation |

### fw 20.0.0 ACID validation note

The upstream extracted NPDM ACID section's SAC and KC offsets point beyond the embedded file because the binary is unsigned (dev/homebrew) — the 0x200-byte RSA signature placeholder is zeroed, so ACID content starts at `acid_file_offset + 0x200 = 0x280`. The binary is structurally valid for Atmosphère's unofficial loader but would fail Nintendo's `svcCreateProcess` in a real production signed context. This is expected and not a new fw 20 regression.

---

## 4. Binary Diff vs Upstream (main.npdm)

| Difference | uMenu.json | Upstream main.npdm | Risk |
|------------|-----------|-------------------|------|
| Name string | `"Q OS Menu"` (compiled in) | `uMenu` | ZERO — name field is cosmetic only; not validated by PM |
| SAC | `["*"]` wildcard | `["*"]` wildcard (ACI0: `80 2a 00 2a`) | ZERO — identical |
| ACID flags | pool_partition=0, unqualified=0, production=0 | Same | ZERO |
| KC set | Very broad (includes debug SVCs, unsafe mem SVCs) | Same set present | LOW — see §3 |
| min_kernel_version KC | `0x30` | `0x30` | ZERO — same |
| application_type KC | `2` (LibraryApplet) | Encoded as `0x1` in raw KC[0] word | INVESTIGATE (see below) |

**KC application_type mismatch detail:** The ACI0 KC in the upstream binary starts with word `0x00000001` (trailing_ones=1). In npdmtool's kernel capability encoding, a single trailing-1 pattern with value=0 is effectively a "no-op" placeholder, not `application_type=2`. The real application_type comes from the JSON at build time and is encoded differently in a full signed NPDM. The unsigned binary's KC section is a minimal stub — Atmosphère's PM uses the JSON/NPDM metadata directly for homebrew, not the KC field. This is NOT a fw 20.0.0 issue; it is a property of unsigned Atmosphère forwarders.

---

## 5. Recommended NPDM Patch for v0.7

**uMenu.json requires NO changes for the Tesla renderer path.** The wildcard `"*"` service_access already includes `vi:m`, `pl:`, and every other service Tesla uses.

The only NPDM changes to consider are **hardening** (optional, not required for v0.7 boot):

```json
// OPTIONAL hardening — add ONLY after Tesla port is stable
// Replace "service_access": ["*"] with explicit list:
"service_access": [
    "vi:m",
    "vi:u",
    "vi:s",
    "pl:",
    "appletAE",
    "hid",
    "set:",
    "acc:u0",
    "ns:am2",
    "amssu:",
    "sm:"
],
"service_host": []
```

Do NOT add `hid:sys` — focus steal is overlay-only, not appropriate for uMenu.
Do NOT add `nvdrv:*` — Tesla rendering path is pure vi+libnx, no NV needed.

---

## 6. Rebuild Recommendation

**Do NOT rebuild uMenu with a patched NPDM now.** The current wildcard NPDM already passes `vi:m`. The Tesla port itself is the blocker, not the NPDM.

Rebuild sequence:
1. Execute Tesla renderer port (v0.7 source changes per RESEARCH-libtesla-rendering.md §9 Option A)
2. After Tesla init succeeds on hardware, optionally harden the NPDM to an explicit service list
3. The `system_resource_size` field is absent from uMenu.json — this is correct and must stay absent

The only real fw 20.0.0 NPDM landmine in the codebase (`system_resource_size` in uSystem.json v0.5.0) has already been resolved in v0.5.1-revertNPDM. uMenu.json is clean.
