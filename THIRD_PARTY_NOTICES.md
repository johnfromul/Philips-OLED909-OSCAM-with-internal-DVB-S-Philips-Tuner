# Third-party notices and provenance

## Project code

The bridge, service, host scripts and documentation in this repository are distributed under GPL-3.0-or-later, except where a file carries a separate license notice. The full GPL text is in [LICENSE](LICENSE).

## OSCam

- Official source: https://git.streamboard.tv/common/oscam
- Pinned tag: `11968`
- Commit: `39e78a4688b30abaab62b5c9e13672b4e34bfca0`
- License: GNU GPL version 3 or later; the fetched source contains its original notices and COPYING file.
- Local modifications: [patches/oscam-11968-android-dvbapi-v1.patch](patches/oscam-11968-android-dvbapi-v1.patch).

The full upstream checkout is obtained by the build script and is not vendored or rewritten as project-owned code. The initial r11760 proof of concept used OpenVisionE2/oscam commit `d4b2b405a881eecaf8b45e85ccc33f02244c2f13` dated 5 April 2024; that old mirror was replaced by the official pinned source for the final deployment.

## MediaTek public UAPI headers

`src/dmx2.h` and `src/dsc_mtk.h` were obtained from the published kernel source at:

https://github.com/TCLOpenSource/mt9653/tree/85db067ea371bb87753370ab99ced1e1f8ced3d8/include/uapi/linux/dvb

The original MediaTek copyright notices and SPDX identifiers are retained:

```text
((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
```

These headers are redistributed under their BSD-3-Clause alternative. The [BSD license text](LICENSES/BSD-3-Clause.txt) is included. Their published ABI definitions were compared with the actual TV driver's BTF; that comparison is a separate compatibility observation, not a claim that the TCL source tree is the TV's complete matching firmware source.

`src/compat/linux/compiler.h` is a small userspace compatibility shim that defines `__user`; it is project code, not a copied kernel implementation.

## Build tools

Zig 0.15.2 is downloaded from https://ziglang.org/download/0.15.2/ when the optional bootstrap is used. The archive itself is not distributed. Android SDK/JDK/Clang are supplied by the build environment and retain their respective licenses. No proprietary firmware, CAS plugin libraries, kernel module captures, channel database dumps or live configuration archives are included.
