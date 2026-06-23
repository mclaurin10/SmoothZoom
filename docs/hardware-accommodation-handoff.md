# SmoothZoom — Hardware Accommodation: Session Handoff

**Read this top-to-bottom at the start of next session; it is self-contained.**

**Date of handoff:** 2026-06-23
**Branch:** `master`
**Fix commit:** `25f785b` — *"fix: adjust touchpad sensitivity for scroll normalization and update related tests"*
**Hardware under test:** HP EliteBook 1040 14-inch G11 Notebook PC (BIOS W90 Ver. 01.08.01), **I2C HID Precision Touchpad**.

---

## 1. Next-session goal (one line)

Use the findings below to make SmoothZoom's input layer **robust across different
trackpad/mouse hardware** — generalize the precision-touchpad (PTP) handling, improve
the on-device diagnostic story, and decide how sensitivity should adapt per device. This
is squarely **Phase 8 (Input Interoperability)** territory — read it alongside
[`docs/input-interop-handoff.md`](input-interop-handoff.md).

## 2. TL;DR of what happened this session

1. Signed + built (Release) + installed v1.0 to `C:\Program Files\SmoothZoom\` on a **new
   machine** (HP EliteBook 1040 G11). This is **not** the dev hardware prior work was tested on.
2. User reported: **Shift + two-finger scroll zooms in Command Prompt but does nothing in
   Edge / File Explorer / Settings.**
3. Root-caused to a **real bug** in PTP two-finger contact tracking (not a config/Shift
   issue). Fixed it. Then fixed a **secondary over-sensitivity** that made the zoom erratic.
4. User confirmed: **"Everything feels right."** Changes committed as `25f785b`.

> ⚠️ **The commit message undersells the commit.** `25f785b` is titled as a *sensitivity*
> change, but its `main.cpp` diff (+76/−27) also contains the **primary root-cause fix
> (slot-indexed contact tracking)** plus diagnostic/robustness improvements (env-driven log
> level, message-window creation error logging). Don't judge the commit by its title.

---

## 3. The bug and root cause (primary fix)

### Symptom
Shift + two-finger scroll zoomed in **Command Prompt** but not in **Edge / File Explorer /
Settings** (any Direct-Manipulation app).

### Why it was app-dependent
SmoothZoom has **three scroll-ingest paths** feeding one accumulator (see
`input-interop-handoff.md` §4):
1. **LL mouse hook** `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` (`InputInterceptor.cpp`) — global, but
   only sees **legacy wheel** messages.
2. **Raw Input mouse** `WM_INPUT`/`RIM_TYPEMOUSE` (`main.cpp`).
3. **Precision-Touchpad HID** two-finger (`main.cpp` `handlePtpHidReport`).

- **Command Prompt** (no Direct Manipulation): the OS synthesizes a **legacy
  `WM_MOUSEWHEEL`** from the trackpad scroll → the LL hook (path 1) catches it → zoom works.
- **Edge / Explorer / Settings** (Direct Manipulation): the trackpad scroll is consumed by
  DManip and **never becomes a legacy wheel message** → path 1 is blind → only the **PTP HID
  path (path 3)** can deliver it. Path 3 was broken (below), so nothing happened.

The dev hardware almost certainly used a **mouse wheel**, where path 1 works everywhere — so
this gap was never exercised until this trackpad-only machine.

### The actual bug (PTP contact tracking)
`handlePtpHidReport()` tracked per-contact state in `s_ptpContacts[5]` indexed by the HID
**Contact ID** (usage 0x51). On this trackpad, **inactive contact slots report
`contactId = 0`**. Every report, the loop walked all 5 slots and, for each empty slot
(tipSwitch off), wrote `active = false` to `s_ptpContacts[contactId]`. Because empty slots
carried `contactId = 0`, they **clobbered the real finger that happened to hold ID 0**.

Result: the code never saw two simultaneously-valid contacts. `scrollFingers` (contacts that
are `active && hasPrev`) **capped at 1**, so the two-finger branch always bailed at
`scrollFingers < 2` and no scroll delta was ever produced.

**Evidence from a DEBUG trace during a real Edge repro (pre-fix):**
- `contactCount = 2`: 1855 reports (two fingers correctly detected)
- `scrollFingers = 1 < 2`: **1772** (only one contact survived)
- `scrollFingers >= 2`: **0** ← never advanced; dedup and the Shift/modifier checks were
  never even reached.

### The fix
Index contact state by **slot** (its HID link collection, which is stable for the lifetime of
a contact) instead of by `contactId`. An empty slot now clears only its own slot and can't
overwrite a different finger. `contactId` is now read best-effort for diagnostics only.

Post-fix trace (Edge): `scrollFingers = 2` passes (596×), `avgDeltaY` ramps smoothly, zoom
accumulates. Confirmed working by the user.

Code: `src/app/main.cpp`, `handlePtpHidReport()` — see the `auto& cs = s_ptpContacts[slot];`
block and its comment.

---

## 4. The "erratic feel" and the secondary fix (sensitivity)

After the primary fix, zoom worked but felt erratic. Two causes:

1. **Diagnostic-build latency (artifact, not shipped):** the diagnostic build forced DEBUG
   logging, which does a **synchronous file flush per log line**. During a scroll that's many
   lines per report on the thread that also services the hooks/`WM_INPUT` — adding real input
   lag. The shipped build has logging compiled out, so this is gone.

2. **Over-sensitive PTP normalization (fixed):** `kPtpSurfaceFractionPerNotch` was `0.02`
   (2% of pad travel = one wheel notch). With this pad's `logicalRangeY = 972`, that's
   `unitsPerNotch ≈ 19.4`, so a single report with `avgDeltaY = 28` produced
   `wheelEquiv = 172` (≈1.4 notches) and a **half-pad flick spanned the entire 1×→10× range**
   (~24 notches) — slamming to max zoom and back. Changed to `0.08` (≈4× gentler;
   `unitsPerNotch ≈ 77.8`; full 1×→10× ≈ two full-pad swipes). User confirmed the feel.

`kPtpSurfaceFractionPerNotch` lives in
[`include/smoothzoom/input/ScrollNormalizer.h`](../include/smoothzoom/input/ScrollNormalizer.h).
Users can further fine-tune **live** (no rebuild) via `scrollSensitivity` in
`%APPDATA%\SmoothZoom\config.json` (multiplier, range 0.1–5.0; the SettingsManager file watcher
reloads it). The Phase 8 A3-UI task (sensitivity slider in TrayUI) is still pending.

---

## 5. This pad's HID descriptor (reference for generalization)

From `initPtpDevice` at startup (DEBUG):

```
UsagePage=0x0D Usage=0x05 (Precision Touchpad), InputValueCaps=17, InputButtonCaps=11
5 contact link collections (LC 1..5), each:
    ContactID  usage 0x51  (BitSize 3)
    X          usage 0x30  (BitSize 16)
    Y          usage 0x31  (BitSize 16)
Contact Count  usage 0x54  LC 0  (BitSize 8)
Scan Time      usage 0x56  LC 0  (BitSize 16)
logicalRangeY = 972
reportSize    = 30 bytes  (one report carries all 5 slots; 2 fingers => 2 slots tipSwitch=1)
Windows "natural scrolling" = ON   (PTP path compensates direction; see handlePtpHidReport)
```

**The trap for next session:** empty slots on this device report `contactId = 0` and `tip = 0`.
Any logic keyed on `contactId` must not let an empty slot mutate another contact's state. The
slot-indexed approach is the robust pattern; validate it holds on Synaptics/Elan/Goodix pads.

---

## 6. Environment & tooling gotchas discovered (important — these cost real time)

These are machine/build realities that will bite again if not known up front:

1. **`SMOOTHZOOM_LOGGING` is OFF in Release.** It's only auto-enabled for the **Debug** config
   (`CMakeLists.txt`). In Release, `initLogFile` still writes the **session header**
   (unconditional), but **every `SZ_LOG_*` macro compiles to `((void)0)`**. So
   `%APPDATA%\SmoothZoom\smoothzoom.log` is **header-only and tells you nothing** in a stock
   Release build. This masqueraded as "no code is running" for a while. To get logs you must
   build with `-DSMOOTHZOOM_LOGGING=ON`.

2. **The env-driven log level does NOT reach the app through a normal launch.** A
   `SMOOTHZOOM_LOGLEVEL` env var (the feature added this session) is read at startup, but:
   - The binary is **UIAccess** and runs from `C:\Program Files\` → its launch is **brokered**
     (like elevation). `Start-Process` (ShellExecute) does **not** pass the caller's in-memory
     env; a plain `CreateProcess` (UseShellExecute=false) **fails with "requires elevation"**;
     and a **User-scope registry env var also did not propagate** to the brokered launch.
   - **Net:** to capture DEBUG logs this session, the level was **temporarily hard-coded to
     Debug** in the diagnostic build, then reverted. **Highest-value next-session improvement:
     make the log level config-driven** (`config.json` "logLevel"), since the app reads
     `config.json` reliably and the env path is unreliable for the shipped launch.

3. **DEBUG logging changes the feel.** Synchronous per-line flush during high-rate touchpad
   reports adds input latency on the hook/`WM_INPUT` thread (this is *why* the author defaults
   to Info). Never evaluate "feel" on a DEBUG build; never ship DEBUG/`SMOOTHZOOM_INPUT_DIAG`.

4. **`SMOOTHZOOM_INPUT_DIAG`** adds `[SZ-DIAG]` `OutputDebugStringW` traces **inside the LL
   hook** (path 1 visibility). Useful, but it does I/O in a hook callback (R-05) — diagnostic
   only, and it goes to the debugger (DebugView), not the file. Capturing it needs a DBWIN
   reader. The file log (`SMOOTHZOOM_LOGGING`) covers paths 2 & 3, which was enough here.

5. **Code-signing on this machine needs the machine cert store, not CurrentUser.** The login
   account `jhairport\dmclaurin` is **not a local admin**; UAC elevates to a **separate admin
   account** (`jhairport\dmclaurin-wa`). So the repo's `dev_sign_setup.ps1` /
   `sign-binary.ps1` (which put the cert in `CurrentUser\My` and sign without `/sm`) **fail** —
   the private key lands in the admin account's store, invisible to the signing step. Working
   approach (used this session): create the cert in **`LocalMachine\My`**, trust it in
   **`LocalMachine\Root`**, and sign with **`signtool /sm /sha1 <thumbprint> /fd SHA256`**,
   all in one elevated batch. Active dev cert: `CN=SmoothZoom Dev`, thumbprint
   `891381A83FE870D6AAE2FC62E8A6E1BE54BA0DB3`.

---

## 7. How to build / sign / install / diagnose on THIS machine

Toolchain present: **VS 2022 Community**, **CMake** (`C:\Program Files\CMake\bin\cmake.exe`),
**signtool** at `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`
(not on PATH). UIAccess requires the binary signed + run from `C:\Program Files\SmoothZoom\`.

```powershell
# Configure + build (Release). Add -DSMOOTHZOOM_LOGGING=ON (and optionally
# -DSMOOTHZOOM_INPUT_DIAG=ON) ONLY for a diagnostic build:
cmake -S C:\src\SmoothZoom -B C:\src\SmoothZoom\build
cmake --build C:\src\SmoothZoom\build --config Release --parallel

# Sign + install must be ELEVATED and use the MACHINE cert store (separate-admin-account
# machine). The session used a one-shot elevated script that: stops any running instance,
# ensures the LocalMachine\My cert + LocalMachine\Root trust, signs with /sm /sha1, and
# copies to C:\Program Files\SmoothZoom. It was written to build\deploy_elevated.ps1
# (build/ is gitignored and transient — consider persisting this under scripts\ as
# e.g. scripts\deploy-machinestore.ps1 next session).
Start-Process powershell -Verb RunAs -Wait -ArgumentList @(
  '-NoProfile','-ExecutionPolicy','Bypass','-File','C:\src\SmoothZoom\build\deploy_elevated.ps1')

# Launch (UIAccess granted because signed + secure folder + uiAccess manifest):
Start-Process 'C:\Program Files\SmoothZoom\SmoothZoom.exe'
```

**Verifying UIAccess is actually granted** (the R-12 sanity check): query the running
process's `TokenUIAccess` flag via P/Invoke (`OpenProcessToken` + `GetTokenInformation`,
class 26). It returned **1 (TRUE)** this session — signed + secure-location + manifest all OK.

**Getting DEBUG logs** (until log level is config-driven): build with
`-DSMOOTHZOOM_LOGGING=ON`, **temporarily hard-code** `setLogLevel(LogLevel::Debug)` in
`WinMain` (env won't reach the brokered launch), install, reproduce, then read
`%APPDATA%\SmoothZoom\smoothzoom.log`. Expect **huge volume** at DEBUG during scroll — mark
the line count before the repro and filter (e.g. for `scrollFingers`, `modHeld`, `PTP scroll`,
`contactCount=[2-9]`). Revert the hard-code before shipping.

---

## 8. The hardware-accommodation problem (next-session focus)

What varies across devices and needs attention:

- **PTP report formats / descriptors.** Contact count location, contact-ID reliability,
  tipSwitch (usage 0x42), one-report-all-contacts vs hybrid one-contact-per-report,
  confidence bits. The slot-indexing fix is robust here, but **validate on Synaptics / Elan /
  Goodix / Apple Magic Trackpad**. Watch specifically for: empty-slot `contactId` defaults,
  slots that reuse link collections, and devices that don't expose a usable `logicalRangeY`
  (fall back path: `kPtpFallbackUnitsPerNotch = 25`).
- **Sensitivity normalization.** One global `kPtpSurfaceFractionPerNotch` (now 0.08) over the
  device `logicalRangeY`. Different pads → different feel. Decide: keep one constant + user
  `scrollSensitivity`, or per-device calibration. **Ship the TrayUI sensitivity slider**
  (Phase 8 A3-UI) so users can fix feel without editing JSON.
- **Mice vs trackpads.** Mouse wheel goes through paths 1/2 (already wheel units, identity in
  `ScrollNormalizer`). Only PTP needs device normalization. High-res / free-spin wheels (B2)
  still un-tuned. Notched vs free-spin feel.
- **Dedup window.** The PTP↔LL-hook dedup is a global **50 ms** window (`lastLLHookScrollTime`).
  On high-rate devices this can drop events (B1 in the interop handoff: switch to per-device
  dedup via `RAWINPUTHEADER.hDevice`).
- **Direction.** Windows "natural scrolling" state is queried at startup and compensated in
  the PTP path. Verify on devices/users with the opposite setting; `reverseScrollDirection`
  is also a config field (was `true` on this machine).
- **Diagnostics.** The single biggest force-multiplier for hardware work: **config-driven log
  level** + a lightweight **"PTP characterization" mode** that, on first two-finger scroll,
  logs the descriptor + a handful of normalized samples. This turns "diagnose blind over
  several rebuilds" (what happened this session) into "read one file."

Possible larger direction (out of scope this session, flagged in interop handoff §9):
adopting the **pointer-input stack** (`EnableMouseInPointer` / `WM_POINTER*`) could unify
device handling and sidestep the DManip-swallows-the-wheel problem — but it's a big change.

---

## 9. Files changed this session (all in commit `25f785b`)

- **`src/app/main.cpp`** (+76/−27):
  - `handlePtpHidReport()`: **slot-indexed contact tracking** (the root-cause fix);
    `contactId` now best-effort/diagnostic; added a per-slot DEBUG trace.
  - `createMessageWindow()`: now logs `RegisterClassExW` / `CreateWindowExW` **failures**
    (previously both return values were ignored → silent failure); added an else-branch
    ERROR if `g_msgWindow` is null.
  - `WinMain`: **env-driven log level** (`SMOOTHZOOM_LOGLEVEL`, default Info). NOTE: does not
    reach the brokered UIAccess launch — see §6.2; prefer config-driven next.
- **`include/smoothzoom/input/ScrollNormalizer.h`** (+−): `kPtpSurfaceFractionPerNotch`
  `0.02f → 0.08f` (+ comment).
- **`tests/unit/test_ScrollNormalizer.cpp`**: stale-arithmetic comment update (test asserts
  against the symbol, not the literal — unaffected by the value change).

Unit tests: **422 assertions / 146 cases pass.** Note the PTP report parsing in `main.cpp`
is **not** unit-tested (no Win32 in tests) — it can only be validated on real hardware.

---

## 10. Verification status

- ✅ v1.0 built (Release), signed (machine-store cert, Authenticode **Valid**), installed to
  `C:\Program Files\SmoothZoom\`, UIAccess token **TRUE**.
- ✅ Unit tests green (422 assertions).
- ✅ Two-finger Shift-scroll zoom now works in Edge / Explorer / Settings **and** Command
  Prompt on the HP EliteBook 1040 G11.
- ✅ Sensitivity/feel confirmed by user ("everything feels right").
- ✅ Changes committed (`25f785b`).
- ⚠️ Only validated on **one** trackpad (this EliteBook). Generalization to other vendors is
  the next-session goal.

## 11. Pointers

- Phase 8 / input plan: [`docs/input-interop-handoff.md`](input-interop-handoff.md)
- Earlier (already-fixed) Shift+scroll *direction* bug:
  [`docs/smoothzoom-shift-scroll-fix-prompt.md`](smoothzoom-shift-scroll-fix-prompt.md)
- Normalizer: [`include/smoothzoom/input/ScrollNormalizer.h`](../include/smoothzoom/input/ScrollNormalizer.h)
- PTP parsing: `src/app/main.cpp` (`initPtpDevice`, `handlePtpHidReport`, `msgWndProc` WM_INPUT)
- Logger gotchas: `include/smoothzoom/support/Logger.h`, `CMakeLists.txt` (the
  `SMOOTHZOOM_LOGGING` / `$<$<CONFIG:Debug>>` logic)
- Risks: `docs/05_Technical_Risks_and_Mitigations.md` (R-05 hooks, R-07 modifier conflicts,
  R-08 touchpad formats, R-12 UIAccess)
