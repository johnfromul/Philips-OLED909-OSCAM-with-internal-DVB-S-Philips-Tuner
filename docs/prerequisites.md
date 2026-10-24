# Prerequisites and firmware verification

## 1. Confirm the actual firmware

Connect using your own authorized ADB endpoint. In examples, `TV_SERIAL` means the address and port shown by `adb devices`, such as `TV_IP:5555`; it is not the television's hardware serial number.

```sh
adb devices -l
export TV_SERIAL=TV_IP:5555
adb -s "$TV_SERIAL" shell getprop ro.odm.tpvision.product.swver
adb -s "$TV_SERIAL" shell getprop persist.odm.tpv.sys.ACR_ModName
adb -s "$TV_SERIAL" shell getprop ro.build.display.id
adb -s "$TV_SERIAL" shell getprop ro.build.version.incremental
adb -s "$TV_SERIAL" shell getprop ro.build.version.release
adb -s "$TV_SERIAL" shell getprop ro.build.version.security_patch
adb -s "$TV_SERIAL" shell uname -r
```

The tested results are in [the firmware snapshot](tested-firmware.json). The most useful Philips version string is `TPM231WW_R.201.100.146.220`, not just the generic Android build label `UKN2.241117.001`.

You can also run `sh tools/capture-firmware.sh "$TV_SERIAL"` from the repository root. It reads an explicit allowlist of properties and root status. It does not collect a hardware serial number, MAC address, channel database or OSCam credentials.

## 2. Confirm full root

The working environment reported:

```text
su version: 31.0:MAGISKSU
Magisk version: 31.0:MAGISK:R
uid=0(root) gid=0(root) groups=0(root) context=u:r:magisk:s0
SELinux: Enforcing
```

Read-only checks:

```sh
adb -s "$TV_SERIAL" shell "su -c 'id; su -v; magisk -v; getenforce'"
adb -s "$TV_SERIAL" shell "su -c 'ls -l /dev/dvb0.ca2 /dev/dvb0.demux20'"
```

An earlier limited `tpvdebug` root context could not provide the required device access. The successful tests used Magisk root and kept SELinux Enforcing. Merely seeing a `su` executable, opening a device node, or playing a free-to-air channel does not establish end-to-end decoding.

Full-root installation itself is a prerequisite. The exact bootloader/root acquisition procedure was not captured in this work, so no reconstructed or untested rooting instructions are presented here.

## 3. Confirm the playback environment

The tested main TV activity is:

```text
org.droidtv.playtv/.PlayTvActivity
```

The monitored input is:

```text
com.mediatek.dtv.tvinput.dvbtuner/.DvbTvInputService/HW0
```

The system has a working MediaTek tuner HAL and CAS HAL. The prototype uses the main live tuner source, rather than creating a replacement player. A read-only state dump can help confirm that the tuner input is active:

```sh
adb -s "$TV_SERIAL" shell dumpsys tv_input
```

Session tokens and PIDs change over time. The service detects the current main channel dynamically instead of hardcoding a previous session token or PID. The TV may advertise different wireless ADB ports after a reboot; verify the connection again when needed.

## 4. Prepare the build host

Required tools:

- Git, Python 3 and Clang.
- Zig **0.15.2**; the included bootstrap verifies the tested Apple Silicon macOS archive checksum.
- A JDK capable of `javac --release 17`.
- Android SDK platform **34**, build-tools **36.1.0** with D8, and platform-tools with ADB.

Install and accept Android SDK components using your normal Android SDK tooling. Set `ANDROID_SDK_ROOT` and, if needed, `JAVAC` and `ZIG_EXECUTABLE` explicitly. See [build instructions](build.md).

## 5. Prepare your reader and LAN settings

Have your own working reader address, protocol, username and password available locally. The validated configuration used a CCcam reader. The example uses a reserved invalid hostname and placeholder credentials so that it cannot accidentally connect to someone else's reader.

Reserve a stable IP for the TV in the router if you want a predictable WebIf address. Adjust `httpallowed` to your actual LAN. A working raw TCP connection to the reader does not establish authentication: during the investigation, an unauthenticated connection test appeared as `anonymous disconnected`, whereas actual OSCam requests later authenticated and returned `found` responses.
