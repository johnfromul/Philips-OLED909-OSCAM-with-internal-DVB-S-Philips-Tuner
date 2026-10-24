# First installation and end-to-end validation

Use this procedure for the tested firmware after satisfying [the prerequisites](prerequisites.md) and [building the software](build.md). For an existing installation, use [the OSCam-only upgrade procedure](operations.md#upgrade-oscam-without-replacing-the-bridge).

The TV-specific paths below are part of the compiled integration:

```text
/data/local/tmp/oscam-mtk/
/data/local/tmp/oscam-mtk/config/
/data/local/tmp/oscam-mtk/camd.socket
/data/adb/service.d/oscam-mtk.sh
```

## 1. Create private configuration outside the checkout

```sh
umask 077
export PRIVATE_CONFIG="$HOME/.config/philips-oscam"
mkdir -p "$PRIVATE_CONFIG"
chmod 700 "$PRIVATE_CONFIG"
cp -n examples/oscam.conf.example "$PRIVATE_CONFIG/oscam.conf"
cp -n examples/oscam.server.example "$PRIVATE_CONFIG/oscam.server"
cp -n examples/oscam.user.example "$PRIVATE_CONFIG/oscam.user"
chmod 600 "$PRIVATE_CONFIG/oscam.conf" "$PRIVATE_CONFIG/oscam.server" "$PRIVATE_CONFIG/oscam.user"
```

Edit the files with your local editor. Replace `CHANGE_ME_WEBIF_PASSWORD`, `READER_USERNAME`, `READER_PASSWORD` and `reader.example.invalid`. Use your actual reader port and protocol configuration. The validated build includes CCcam; other protocols are not compiled in by this procedure.

Keep these DVBAPI settings:

```ini
[dvbapi]
enabled = 1
boxtype = pc-nodmx
pmt_mode = 4
user = mtk_bridge
au = 0
```

The native bridge supplies validated CA_PMT messages and ECM sections. `pc-nodmx` prevents OSCam from trying to program incompatible standard CA devices directly. The `mtk_bridge` account in `oscam.user` selects group 1, matching the reader's group. This account is separate from both the external reader login and the WebIf login.

WebIf settings:

```ini
[webif]
httpport = 8888
httpuser = admin
httppwd = CHANGE_ME_WEBIF_PASSWORD
httpallowed = 127.0.0.1,192.168.1.1-192.168.1.254
```

Replace the example password and adjust the allowlist to your LAN. This build uses HTTP Digest authentication over HTTP and does not include TLS. The configured allowlist limits who can reach the administrative interface; credentials and reader settings are stored under the root-only directory on the TV.

## 2. Connect to the TV

```sh
adb connect TV_IP:5555
export TV_SERIAL=TV_IP:5555
adb -s "$TV_SERIAL" shell "su -c 'id; getenforce'"
```

Use the endpoint actually authorized on your TV. If your platform-tools executable is outside PATH, set `ADB` to its absolute path before running the helper scripts.

## 3. Install the files

```sh
sh tools/install.sh "$TV_SERIAL" "$PRIVATE_CONFIG"
```

The host helper checks for built artifacts, unresolved placeholders and an existing installation. It transfers files into a unique shell-private staging directory and runs `service/install-device.sh` as root. The device helper checks the tested firmware and required character devices before installing.

Installed runtime files:

| File | Mode | Purpose |
| --- | --- | --- |
| `oscam` | 0700 | Static OSCam executable |
| `mtk-bridge` | 0700 | Native vendor DVB bridge |
| `bridge-service.jar` | 0600 | Main-channel/CAS supervisor |
| `watchdog.sh`, `control.sh` | 0700 | Process supervision and control |
| `config/oscam.conf`, `.server`, `.user` | 0600 | Private configuration |
| Magisk `service.d/oscam-mtk.sh` | 0700 | Boot hook |

The runtime and configuration directories have mode 0700 and are created by root. The helper removes the three staged configuration files after copying them into the private root directory. Non-secret staged binaries can remain for inspection. A failed partial installation is left for inspection rather than silently overwritten on retry.

The repository's fresh-install wrapper packages the same installation steps used in the recorded work. It was syntax-checked, but was not rerun against a factory-clean TV during publication because the tested television already had a working installation. The build and live runtime tests are recorded separately in [validation](validation.md).

## 4. Check service status

```sh
adb -s "$TV_SERIAL" shell "su -c '/data/local/tmp/oscam-mtk/control.sh status'"
adb -s "$TV_SERIAL" shell "su -c 'tail -30 /data/local/tmp/oscam-mtk/bridge.log'"
```

Expected status includes enabled startup and running watchdog, supervisor and OSCam processes. When no main DVB program is active, having no native bridge process is normal.

Open the original Philips Play TV application and choose an encrypted channel supported by your reader. The service reads the current channel, obtains its service ID and transport stream ID, finds the PMT from the live PAT, and starts a channel-specific bridge.

Expected log sequence includes:

```text
CHANNEL content://android.media.tv/channel/...
START service=... tsid=...
PMT_READY service=... pmt_pid=...
CA_READY device=ca2 engine=... slot=... source=0
OSCAM_SERVER protocol=3 name=OSCam 2.26.09-11968 ...
CW_PID_ASSOCIATION ... hardware_applied=1
CW_RECEIVED ... hardware_applied=1
```

The client announces protocol version 1; the server's own advertised version 3 is expected and does not mean v3 framing has been selected for this client.

## 5. Verify actual picture and sound

An ioctl success or `found` response alone is insufficient. Confirm both picture and sound in the original TV application. A free-to-air channel such as the tested CT24 HD cannot validate CW delivery by itself.

The recorded encrypted test used Prima HD: service ID 13202, PMT PID 7020, ECM PID 7030 and CAID 0624. These values are evidence from that broadcast snapshot, not a permanent channel database. The running service discovers the active program dynamically.

Test a channel change to a free-to-air channel, then another encrypted channel, and return. The old bridge should stop and release its own PID bindings before a new one starts. For free-to-air PMTs, the native helper returns `FREE_TO_AIR` and does not program hardware CA keys.

## 6. Verify WebIf

Open `http://TV_IP:8888/` and sign in using your configured WebIf account. Confirm that the current reader and DVBAPI client appear. The main page, local JavaScript files and status JSON API were verified with HTTP 200 during the recorded upgrade; unauthenticated requests required authentication.

If Brave displays the page but controls do not respond, see [the browser troubleshooting section](troubleshooting.md#webif-opens-but-controls-do-not-respond). Changing the reader's on/off switch is unnecessary just to verify that the page and its assets load.

## 7. Verify automatic startup

After the first successful picture/sound test, reboot the TV using its normal controls or an authorized `adb reboot`. Reconnect to the current ADB endpoint and check service status without manually running `start`.

The Magisk hook starts the watchdog; it waits for `sys.boot_completed=1`, starts OSCam and the Java service, and waits for an active main DVB session. Open Play TV if the TV boots into its home screen. Verify picture and sound again.

Automatic startup after a real reboot was tested during the original installation. The later OSCam-only upgrade retained the verified boot hook and did not perform an additional reboot test.
