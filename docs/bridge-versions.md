# Bridge versions

## Previous TV build (25 November 2026)

The release archive contains the exact files saved from the TV. This version cached channel metadata to speed up returning to the previous channel, logged PID binding failures, and collected diagnostics on USB with an internal Download fallback. The archived JAR is the reference for the exact supervisor build. The matching historical Java source snapshot was unavailable.

## Reliability build (25 November 2026)

- Channel metadata queries run in the background and are cancelled on retune. Recovery distinguishes temporary observation failures, stale DVB tables and free-to-air channels.
- DVB filter replacement discards readiness events belonging to the old filter.
- The native collector drains rotated logs and saves a checkpoint. It prefers USB with an internal Download fallback. Adjust the USB volume ID in the wrapper. The first start can replay existing rows.
- Two returns from Prima Zoom HD to Prima Cool HD rendered the first frame in 6.2 and 5.0 seconds. The scrambled warning still appeared briefly. USB writes and simulated fallback, rotation and resume passed.

Run `sh tools/build-bridge.sh` to build the JAR and both ARM64 binaries. The releases package the deployed bridge files. For optional diagnostics, place the collector and wrapper beside the core bridge and invoke the wrapper from a Magisk boot hook.
