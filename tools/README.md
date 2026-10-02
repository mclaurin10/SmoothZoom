# tools/: non-elevated build → deploy → bench loop (dev only)

`scripts/` is the release pipeline: machine-store cert, `C:\Program Files\SmoothZoom\`, and a UAC
prompt on every deploy. `tools/` is the inner loop for performance tuning. After one elevated
setup, it builds, signs, installs and launches a **dev-tools build** without any UAC prompt, so
`gate.ps1` can run unattended (for example from a Stop hook).

| Script | Elevated? | Purpose |
|---|---|---|
| `dev-setup.ps1` | **yes, once** | Dev signing cert (CurrentUser\My, trusted in LocalMachine\Root) and a user-writable secure folder `C:\Program Files\SmoothZoom Dev\`. Ends with a self-check. |
| `dev-revert.ps1` | **yes** | Undoes all of `dev-setup.ps1`: both cert copies, the folder and the state file. |
| `deploy.ps1` | no | Build (`build-dev\`, `-DSMOOTHZOOM_DEV_TOOLS=ON`), sign, install and launch through ShellExecute. `-Wait`/`-TimeoutSec` for bench runs. |
| `observe-wheel.ps1` | no | Observe-only LL hook that prints how wheel/touchpad events arrive (WHEEL vs HWHEEL, modifiers). |
| `bench/run-bench.ps1` | no | Run one trace N times; aggregate `summary.json` files. |
| `bench/gate.ps1` | no | Deploy, bench every trace under `bench/traces/` and compare with `bench/criteria.json`. Exits 0 on pass, 2 on fail (Stop-hook compatible). |

## Security tradeoff of `dev-setup.ps1`. Read before running.

UIAccess requires a binary that is (a) signed by a chain trusted in LocalMachine\Root and
(b) launched from a secure path (Program Files / System32). The secure-path check is
**path-based**, not ACL-based. To deploy without UAC, the setup makes both conditions
satisfiable by your account alone:

1. **Your account can sign UIAccess-capable code.** The key sits in CurrentUser\My. It is
   *non-exportable*, which stops it being copied off the machine but not used on it.
2. **Your account can write into a secure path.** `C:\Program Files\SmoothZoom Dev\` grants
   you Modify.

**Consequence:** any code already running as you (medium IL) could sign a binary, drop it in that
folder and get UIAccess, which bypasses UIPI and can drive elevated windows. That is a
medium → high escalation path that does not exist on a default install. The cert is also
trusted for code signing machine-wide, for every user of this PC.

**Measured on TEMPLAPTOP (2026-10-01).** The dev build launched this way runs with:
- `TokenUIAccess = 1`
- the *limited* token (Administrators deny-only, not elevated)
- integrity level **High (0x3000)**, raised from the shell's Medium

So it can send input to and drive elevated windows without holding admin rights. A
medium-IL shell gets **Access denied** on `Stop-Process -Force` against it. That is why
`deploy.ps1` stops it through the named quit event instead.

**Bounded by:**
- Basic Constraints `CA=false`, so the cert cannot issue other certs.
- Code-signing EKU only.
- 6-month validity.
- A dedicated subject and folder, separate from the release `CN=SmoothZoom Dev` cert and
  `C:\Program Files\SmoothZoom\`.
- `dev-revert.ps1` removes everything.

**Safer alternative:** keep using `scripts/deploy.ps1`, which elevates on every deploy. Its cost is
that every unattended iteration stops at a UAC prompt.

## Shared state with the release build

The dev build is the same app, so it shares `%APPDATA%\SmoothZoom\` with any installed release
build: `config.json`, `smoothzoom.log` and the crash sentinel. It also shares the single-instance
mutex, so **only one of the two can run at a time**. `--bench` does *not* load or save
`config.json`; it runs on built-in defaults so baselines don't drift with user settings.
`--record` runs with your real config unless you pass `--config <path>`.

**Use `--config <path>` for every test and bench run.** It replaces
`%APPDATA%\SmoothZoom\config.json`, and `smoothzoom.log` and the `.running` sentinel go
next to it, so your real config, log and crash sentinel are never read or written. A path
that does not exist gives built-in defaults (a fresh install: Shift modifier, keyboard
shortcuts off); the app creates it on the first settings change or on exit. `run-bench.ps1`
passes `<run dir>\config.json` for every run. Example:

    tools\deploy.ps1 -NoBuild -LaunchArgs '--config','C:\src\SmoothZoom\bench\results\manual\config.json'

The single-instance mutex is shared, so quit any running SmoothZoom first.
