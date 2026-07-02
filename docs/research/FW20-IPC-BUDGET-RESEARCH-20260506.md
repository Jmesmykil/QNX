# FW20 IPC Budget Research — 2026-05-06

**Hypothesis under test:** FW20 reduced the SystemApplet IPC session pool ceiling, causing
uSystem to crash with `2011-0102 ams::sf::hipc::ResultOutOfSessionMemory` during init.
Updating Atmosphère would fix this.

---

## 1. Actual mechanism of `ResultOutOfSessionMemory`

**Source file/line:**
`libvapours/include/vapours/results/hipc_results.hpp:25`
`R_DEFINE_ERROR_RESULT(OutOfSessionMemory, 102)` — module 11 (HIPC) → 2011-0102.

**Single call site:**
`libstratosphere/include/stratosphere/sf/hipc/sf_hipc_server_session_manager.hpp:109`

```cpp
ServerSession *session_memory = this->AllocateSession();
R_UNLESS(session_memory != nullptr, sf::hipc::ResultOutOfSessionMemory());
```

`AllocateSession()` (`sf_hipc_server_manager.hpp:355–368`) walks a
`util::TypedStorage<ServerSession> m_session_storages[MaxSessions]` array tracked by
`bool m_session_allocated[MaxSessions]`. Both arrays are **compile-time fixed members** of the
`ServerManager<MaxServers, Options, MaxSessions>` template instantiation — they live in BSS, not
on a heap, and there is no runtime reallocation path. When all slots are marked allocated the
function returns nullptr. The result is bubbled up through `R_ABORT_UNLESS` in
`ServerManagerBase::WaitAndProcessImpl` → user break.

**What triggers it in uSystem:** an incoming IPC connection to uSystem's own server (via
`ProcessForServer → AcceptSession → CreateSessionImpl`) when all pool slots are taken. This is a
server-side exhaustion, not a client-side handle table limit.

**Not any of these:**
- Not a kernel svc::LimitReached (that is 2001-0132, distinct error, seen May 5)
- Not handle table overflow (uSystem.json `handle_table_size: 512` — client handles go there, not here)
- Not mmio/NAND state
- Not heap exhaustion

---

## 2. uSystem's actual session pool at startup

`sf_IpcManager.hpp` (uSystem server):

```
MaxPrivateSessions  = 1
MaxEcsExtraSessions = 5
MaxSessions         = 6   (template parameter to ServerManager)
Port_Count          = 2   (MaxServers)
```

The kernel accept-queue depth for the public port (`MaxPublicSessions = 32`) is a separate
parameter to `RegisterServer` — it controls how many pending connection requests queue in the
kernel, not how many `ServerSession` objects the pool holds. The pool is 6 slots.

**Outgoing client sessions held open at startup** (uSystem as client, from `InitializeSystemModule`
lines 1648–1656):

| Service | Sessions |
|---------|----------|
| `sm::Initialize` | 1 |
| `fsInitialize` (`__nx_fs_num_sessions = 3`) | 3 |
| `appletInitialize` | 1 |
| `nsInitialize` | 1 |
| `ldrShellInitialize` | 1 |
| `pmshellInitialize` | 1 |
| `setsysInitialize` | 1 |
| **Total** | **9** |

These consume handle table slots (cap 512). They do not consume the ServerManager pool.
`accountInitialize` at line 1446 is in the event loop — opened and closed dynamically, not held
at boot.

---

## 3. Did FW20 change the SystemApplet session pool ceiling?

**No.** The `ServerManager` pool (MaxSessions = 6) is a compile-time constant in uSystem's own
source, not a kernel-enforced limit. Nintendo made no change in FW20 that alters how many
`ServerSession` slots a custom SystemApplet may allocate in its own BSS.

What FW20 (HOS 20.0.0) **did** change is the **stealable applet pool memory**: from 40 MB
(FW18.1) to 14 MB, acknowledged in the AMS 1.9.0 changelog: *"We can only steal a maximum of
14MB from the applet pool, down from 40MB."* This is the shared SystemApplet address-space
memory budget — a separate resource from the libstratosphere IPC server-session array. The two
are often conflated because both are called "pool" in common usage.

---

## 4. Static vs dynamic classification

**Static — fully deterministic per binary per firmware.**

The pool is a fixed BSS array. Same source constants → same pool size → same failure point on
the same connection attempt, every boot. The crash PC offsets are byte-for-byte identical across
all four May 6 crash events (two builds, two crashes each — `CRASH-FORENSICS-EVIDENCE-20260506.md`).
A power-cycle releases the process and resets the array to all-unallocated. If the triggering
condition recurs on the next boot (same peer connects at the same time), the crash recurs.
Any apparent intermittency is a startup-ordering race between uSystem registering its ports and
a peer process (am or uMenu) connecting, not a non-deterministic pool state.

---

## 5. What actually fixes it — evidence-based

The crash is invariant across all four uMenu binaries tested May 6 (2e6d8c36, bcaf108c, 3beeb544,
30d020929060). The uMenu binary does not determine whether the crash occurs. The crash is
uSystem-side.

Fix options in priority order:

1. **Raise `MaxEcsExtraSessions` in `sf_IpcManager.hpp`** — if more than 6 concurrent inbound
   sessions are arriving (ECS + uMenu private + am lifecycle on FW20). Bump to 8–10, rebuild.

2. **Audit the AMS 1.11 clean-exit IPC contract** — AMS 1.11.0 requires applets to call
   clean-exit IPC on teardown. If uSystem or uMenu never closes its server-side session on
   exit, slots leak across the process lifetime until the pool is full. The `ON_RESULT_FAILURE`
   guard in `CreateSessionImpl` covers construction failures only; callers must close on
   application-level error paths.

3. **Recompile against libnx ≥ 4.10.0** — FW21 broke userland↔kernel TLS ABI; the fork may
   be on an older libnx that produces wrong session teardown behavior on FW20+.

Updating Atmosphère alone cannot fix this. AMS does not own the pool constant; it lives in
uSystem's source.

---

## 6. Verdict on the hypothesis

| Claim | Verdict | Evidence |
|-------|---------|----------|
| FW20 reduced the SystemApplet IPC session pool ceiling | **FALSE** | The pool is a compile-time BSS array in uSystem's ServerManager, not a kernel-controlled ceiling. FW20 reduced stealable applet *memory*, not the HIPC session slot count. |
| uSystem crashes with 2011-0102 during init | **TRUE** | 4/4 May 6 crash reports confirm `0xCC0B` at deterministic PC offsets across two uSystem builds. |
| Updating Atmosphère would fix this | **FALSE** | (a) AMS 1.11.1 is already installed (`1.11.1-master-d04c20a04`). (b) AMS does not own the pool constant. (c) AMS 1.11.0 introduced a clean-exit IPC break that makes it the disruptor, not the fix. The pool/lifecycle bug lives in the uLaunch fork source. |

**Overall: FALSE / PARTIALLY TRUE.** The crash is real and is caused by an HIPC session pool
exhaustion in uSystem's own server. FW20's involvement is indirect — its memory and lifecycle
changes increase session demand on uSystem's fixed pool. Updating AMS is the wrong direction.

---

*Sources:*
- `libvapours/include/vapours/results/hipc_results.hpp:20,25`
- `libstratosphere/include/stratosphere/sf/hipc/sf_hipc_server_session_manager.hpp:106–118`
- `libstratosphere/include/stratosphere/sf/hipc/sf_hipc_server_manager.hpp:295–368`
- `src/projects/uSystem/include/ul/system/sf/sf_IpcManager.hpp:33–42`
- `src/projects/uSystem/source/main.cpp:1644–1656,1705`
- `src/projects/uSystem/uSystem.json:162`
- `docs/research/CRASH-FORENSICS-EVIDENCE-20260506.md`
- AMS 1.9.0 changelog (raw.githubusercontent.com): 14 MB applet pool note
