# One-click launcher validation — 2026-09-23

`UmbraCam.cmd` opens a Windows PowerShell menu using its own repository-relative
path. It does not require a pre-opened terminal, a specific current directory,
an execution-policy change persisted on the machine, or administrator privileges
for ordinary capture. Existing setup scripts still own UAC elevation.

## Tested

- 21 dependency-free mocked tests in `tests/control-menu-tests.ps1`: menu routing,
  duplicate prevention, enable, first-run installation ordering, declined/failed
  setup, failed enumeration, missing binaries, build confirmation, active-publisher
  build refusal, idempotent disable, stop verification, timeout/missing-event
  failures, uninstall confirmation/ordering/failure, informational color and status.
- PowerShell parser and Git whitespace checks passed.
- Actual CMD entry point exercised with status, experimental color, invalid input
  and exit, including from a different current directory. Paths contain spaces.
  These choices do not open a camera. A concurrent menu was rejected by the
  session-local mutex, preventing overlapping menu setup/start/stop actions.
- Actual interactive-user test: virtual camera enumeration passed, Enable launched
  the publisher, Status found its PID, Disable signaled the native session-local
  stop event and waited for process exit. Repeated Disable succeeded idempotently.
  No publisher remained afterward. No frames or audio were recorded.

## Safety and limits

Disable keeps the virtual device registered; it is not a Device Manager disable.
It stops current-session UmbraCam publishers, including a publisher launched from
another checkout. Stop failure is reported, never disguised as success, and no
process is force-killed. Other apps/Windows Hello may independently hold a camera.

Exit closes only the menu. Camera capture continues until Disable is selected.
The menu says this both on launch and after enabling. Status reports process and
enumeration state, not frame health. Startup can still fail after the two-second
early-exit check; native errors retain their existing UI/diagnostic log.

Setup/removal paths were tested with mocks, not by uninstalling/reinstalling the
user's working camera. The existing elevated setup implementation is unchanged.
Uninstall requires an exact confirmation, stops capture first, and checks removal
afterward. A missing local build still needs to be rebuilt before using that setup.
Versioned Program Files runtimes are intentionally retained by existing uninstall.

Color is an explicitly unavailable live option: it points to offline experiment
instructions and starts no GPU worker or camera. No color model, driver, native
publisher code, virtual camera registration or user footage changed in this work.

Run the regression tests without opening a camera:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\control-menu-tests.ps1
```
