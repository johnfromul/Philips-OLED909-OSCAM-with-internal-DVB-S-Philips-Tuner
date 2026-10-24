# Validation record — 24 October 2026

All times and dates in the project narrative use Europe/Prague. This is a curated record; raw device captures, credentials and CAS tokens are excluded.

## Results that were actually observed

| Check | Result / scope |
| --- | --- |
| Firmware | Read directly: 65OLED909/12, TPM231WW_R.201.100.146.220, Android 14, exact kernel/fingerprints in the JSON snapshot |
| Root | Magisk 31.0; UID 0 in `u:r:magisk:s0`; SELinux Enforcing |
| Device access | Root could open the relevant DVB demux and CA nodes |
| Vendor ABI | Current driver inspection and BTF matched the public UAPI structures used by the bridge |
| Plain host tests | PAT/PMT, CA descriptors, service/transport mismatch, bounds and fragmented protocol-frame tests passed |
| Initial hardware test | 30-second encrypted-channel test delivered CW to hardware |
| Extended initial test | 180 seconds, 19 ECM sections and 20 CW messages, `hardware_applied=1`, native exit 0 |
| Original Philips player | Owner confirmed encrypted Prima HD picture and sound |
| Automatic channel changes | Prima HD → CT24 HD (free-to-air / different transport) → Nova HD → Prima HD; old resources released and correct new PIDs/CW applied |
| Startup | Actual TV reboot; watchdog, OSCam and supervisor were already running without a manual start after boot |
| Duplicate start | Existing watchdog/supervisor remained the single active instance |
| Recovery | Forced termination of the owned Java supervisor; its native child exited, watchdog started a replacement and CW application resumed |
| WebIf | Authentication required; HTTP 200 with the current account; local scripts and state API returned HTTP 200 |
| Upstream upgrade | Installed 2.26.09-11968@39e78a46; required the documented ECM_INFO version guard |
| File preservation | Hash checks matched pre-upgrade config, bridge, Java service and startup/control files |
| Post-upgrade playback | Prima HD → Nova HD → Prima HD passed; owner again confirmed picture and sound |

## Broadcast snapshot

These identifiers describe the services observed during the tests, not static settings that the supervisor requires:

| Channel | Service ID | TSID | PMT PID | ECM PID used |
| --- | --- | --- | --- | --- |
| Prima HD | 13202 / 0x3392 | 3229 | 7020 | 7030 |
| Nova HD | 13201 | 3229 | 7000 | 7010 |
| CT24 HD | 8003 | 3233 | 1300 | Free-to-air |

The encrypted Prima HD path used CAID 0624 and elementary stream PIDs 7021–7027. The service obtains the active channel URI, service/transport IDs and live PMT dynamically.

## Deployed build reference

```text
OSCam version: 2.26.09-11968@39e78a46
Upstream commit: 39e78a4688b30abaab62b5c9e13672b4e34bfca0
OSCam SHA-256: 0f54a9508e2500c5158008c81383e0a0972db2ce81a17fdbc8d056c6834a983e
Native bridge SHA-256: 731faf62c8e231c72bbc77359c092041519150c16512669bfa6a461242ea595a
Java service JAR SHA-256: 04c19c8850c147e05cc28f1c97f2596d76409d4231f56674dfcc3520d417f8bd
```

These checksums identify the tested artifacts. New builds can differ due to timestamps, source paths, line numbers and compiler metadata. Repository source includes licensing comments and portable build tooling added while packaging the project; no claim is made that its rebuilt outputs are byte-identical to the earlier TV artifacts.

## Repository packaging checks

The published project was checked independently of the already running TV:

- Plain host protocol tests passed from this checkout.
- All provided shell files passed `sh -n`; firmware JSON and relative documentation links were validated.
- The native ARM64 bridge and Android service JAR were built successfully from the packaged source.
- OSCam was built successfully from a fresh official r11968 checkout using the supplied patch and compiler wrapper.
- The tracked publication set was inspected for live configuration, raw logs, device binaries, known session credentials, device-specific network addresses and personal build-host paths. Only placeholder configuration and curated firmware/test metadata were included.

The outputs from these packaging checks were not installed over the running TV. Build outputs, compiler caches and the fetched upstream checkout are ignored by Git.

## Limits of the evidence

- The sanitizer-enabled host test did not complete; only the plain test is marked passed.
- Picture and sound were confirmed by the owner, not inferred solely from API status.
- The discarded system-context prototype coincided with a reboot and was removed.
- CAS session refusal after repeated restarts and a subsequent TV reboot remain unresolved.
- The fresh-install and generalized upgrade wrappers were packaged from the recorded manual steps and syntax-checked. The already configured TV was not reset solely to test a new clean-install wrapper.
- Autostart was reboot-tested before the OSCam-only upgrade; the unchanged boot hook was preserved, and the final upgrade did not add another reboot test.
- No long-duration soak test, PIP/recording matrix or second firmware/TV test is claimed.
