# third_party_patches/ — vendored drift fixes

The third-party trees live outside the repo (see `tools/fetch_third_party.sh`)
and are cloned at **pinned upstream refs** — never hand-edited without a
trace. When upstream code breaks against the current toolchain/SDK, the fix
lands here as a numbered `.patch` (one per independent issue, `NNNN-<topic>.patch`)
and `tools/fetch_third_party.sh` applies every patch in order after the clone:

```
third_party_patches/<dep>/0001-<topic>.patch
```

Rules:
- patches are generated with `git -C <vendored> diff` so they always match;
- application is idempotent (`git apply --check` skips already-applied);
- the comment header inside each patch states the symptom + root cause;
- when a pinned ref is bumped to an upstream that already contains the fix,
  drop the patch in the same change.

## portaudio

- `0001-win-sdk-26100-rtlgetversion-ntstatus.patch` — `pa_win_wasapi.c`'s
  `GetWindowsVersion()` typedefs `LPFN_RTLGETVERSION` as returning
  `NTSTATUS`; Windows SDK 10.0.26100 no longer exposes `NTSTATUS` to user
  mode (moved to the driver-only `wdm.h`) → C2065/C2143 cascade. Replaced
  with the ABI-identical `LONG` (NTSTATUS is a 32-bit signed LONG).
  DirectSound/WDM-KS host apis are disabled in `runtime-asi/CMakeLists.txt`
  instead (same SDK drift; the DualSense endpoint runs on WASAPI).
