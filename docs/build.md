# Building the software

Run these commands from the repository root. Build artifacts, compiler downloads and upstream checkouts are ignored by Git.

## Compiler and Android SDK

On the tested Apple Silicon macOS host:

```sh
sh tools/bootstrap-zig.sh
export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
export JAVAC=/opt/homebrew/opt/openjdk/bin/javac
```

The bootstrap downloads the official Zig 0.15.2 Apple Silicon archive and verifies SHA-256:

```text
3cc2bab367e185cdfb27501c4b30b1b0653c28d9f73df8dc91488e66ece5fa6b
```

On another host, install Zig 0.15.2 and set `ZIG_EXECUTABLE` to its executable. Set `ANDROID_SDK_ROOT` to your SDK. The bootstrap's archive selection is intentionally limited to the host that was tested.

## Native bridge and Android service

```sh
sh tools/build-bridge.sh
```

This runs the host protocol tests, cross-compiles the native bridge as a static `aarch64-linux-musl` executable, compiles the Java service with `--release 17`, and uses D8 with API 34 as its minimum API.

Outputs:

```text
build/mtk-bridge
build/bridge-service.jar
```

The TV has a 64-bit kernel that can execute the static ARM64 bridge even though the Android application environment is 32-bit. Static linking avoids depending on Android's Bionic userspace libraries for the native helper. Java runs through the TV's existing `app_process` executable.

To run only host checks:

```sh
sh tools/check.sh
```

The tests validate PMT boundaries and service ID, CA descriptor detection, PAT service-to-PMT mapping, transport ID mismatches, truncated input and protocol frame sizes. They do not simulate a MediaTek CA device. The original sanitizer-enabled host test did not complete; only the plain host test is recorded as passing.

## OSCam r11968

```sh
sh tools/build-oscam.sh
```

The script:

1. Fetches `https://git.streamboard.tv/common/oscam.git`, tag `11968`.
2. Checks commit `39e78a4688b30abaab62b5c9e13672b4e34bfca0`.
3. Applies [the local patch](../patches/oscam-11968-android-dvbapi-v1.patch).
4. Enables WebIf, LiveLog, internal jQuery, compressed templates, DVBAPI, extended CW API, debug, CCcam and CCCshare.
5. Uses the same static ARM64/musl cross-compiler and config directory `/data/local/tmp/oscam-mtk/config`.

Output: `build/oscam-arm64`.

Physical smartcard readers, SSL, Stream Relay, extra network protocols and other optional features were not added to the validated feature set. They are independent build choices that would need separate testing.

## Why the OSCam patch is required

The patch changes eight Unix socket path entries from `/tmp/camd.socket` to `/data/local/tmp/oscam-mtk/camd.socket`. Android does not provide the desktop `/tmp` layout expected by that path.

It also restores a protocol-version check for `DVBAPI_ECM_INFO`. The protocol documentation introduces that optional metadata packet in version 2. This bridge intentionally announces version 1. The unmodified r11968 call site sent the packet to this client anyway, causing `UNSUPPORTED_OSCAM_FRAME opcode=ffff0003` after successful CW writes. Requiring client protocol version at least 2 at that call site allows the existing bridge to remain unchanged.

The deployed binary reported:

```text
Version:  2.26.09-11968@39e78a46
Compiler: aarch64-unknown-linux-musl
```

Its recorded checksum is in [the firmware snapshot](tested-firmware.json). Build timestamps and debug paths can change checksums between builds. Verify the binary you actually transfer instead of expecting a fresh build to match the historical checksum exactly.

## Source and license availability

OSCam's complete pinned source is fetched into `upstream/oscam` and includes its license. The bridge source and public UAPI headers are in this repository. The compiler archive, Android SDK, device firmware and CAS plugin binaries are not bundled. See [third-party notices](../THIRD_PARTY_NOTICES.md).
