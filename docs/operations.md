# Operations, WebIf, upgrades and backups

## Normal service control

Run these commands through an authorized root shell:

```sh
adb -s "$TV_SERIAL" shell "su -c '/data/local/tmp/oscam-mtk/control.sh status'"
adb -s "$TV_SERIAL" shell "su -c '/data/local/tmp/oscam-mtk/control.sh stop'"
adb -s "$TV_SERIAL" shell "su -c '/data/local/tmp/oscam-mtk/control.sh start'"
```

`stop` creates `/data/local/tmp/oscam-mtk/disabled`, waits for the watchdog and its launcher lock to be released, and stops the processes owned by the service. The flag also disables automatic startup at the next boot. `start` removes the flag and starts the exclusive-lock launcher. Repeated starts do not create another active instance.

The launcher lock, Java supervisor lock and native bridge lock protect three separate lifetimes. A fix in `control.sh` waits for the outer launcher lock after the watchdog PID file disappears; without this wait, an immediately following start could be lost in that brief interval.

The watchdog checks every three seconds. The Java service polls the main TV session approximately every 400 ms. On a channel change, it stops the old bridge, closes its own CAS session, reads the new channel metadata and rechecks the selected channel before starting. The native helper checks live PAT/PMT data as well.

The service waits outside the main DVB session and on non-interactive power state. These paths are implemented; extended standby/PIP/recording combinations have not all been tested.

## Logs

- `bridge.log`: rotating status log, 512 KiB plus one previous file. No CW bytes or CAS token are intentionally logged.
- `oscam.log`: OSCam log, configured with `maxlogsize = 512` in the example.
- `launcher.log`: startup diagnostics, bounded when the helper is relaunched.
- `oscam.version`: generated runtime feature and version report.

Read only the relevant tail during diagnosis. Raw OSCam and Android logs can contain device, reader or account information and should not be committed to the repository.

The Java log uses Europe/Prague. The watchdog sets `TZ=CET-1CEST,M3.5.0,M10.5.0/3` for the static OSCam process, so local daylight-saving transitions are represented without depending on Android's timezone database layout.

## WebIf

Address: `http://TV_IP:8888/`, using the username and password in your current private `oscam.conf`. The WebIf account, external reader account and local `mtk_bridge` DVBAPI account serve different purposes.

The tested upgrade preserved the owner's current WebIf credentials rather than resetting them to the example values. Internal jQuery and the main script are served by OSCam itself. The version query string changes with the OSCam version, allowing browsers to request the new assets.

The watchdog supervises OSCam. Consequently, stopping OSCam through WebIf may cause the watchdog to start it again. Use `control.sh stop` when you want the entire service disabled.

## Upgrade OSCam without replacing the bridge

First build the pinned and patched source:

```sh
sh tools/build-oscam.sh
sh tools/upgrade.sh "$TV_SERIAL"
```

This helper targets the already installed runtime and tested firmware. It copies only a new OSCam executable and its checksum. It does not upload example configuration or replace bridge/service files.

The device-side procedure:

1. Verifies root, firmware, checksum, executable version and owned process IDs.
2. Creates a root-private timestamped backup containing the previous binary, version report and `config.tar`.
3. Records hashes for configuration and bridge/service files, excluding the dynamic `oscam.ccache` from immutable-file checks.
4. Briefly pauses the watchdog and Java supervisor, keeping the Java CAS owner alive during the OSCam replacement.
5. Stops OSCam, atomically replaces its executable and starts the new binary with the same config/temp/PID paths.
6. Checks the new process/version and hashes of preserved files.
7. Resumes the existing supervisor and watchdog. On an installation error after replacement, it attempts to restore the previous binary and always resumes the paused processes.

The published host wrapper and generalized checksum handling were derived from the tested r11968 replacement procedure and syntax-checked. They are not a separate claim of a factory-state or failure-injection test. Review a generated backup and confirm picture/sound after every deployment. An unexpected CAS error still needs the diagnosis described in [troubleshooting](troubleshooting.md).

## Backups from the recorded TV session

The original working r11760 binary was retained on the owner's TV at:

```text
/data/local/tmp/oscam-mtk/backups/pre-11968-20261024-040649/oscam-r11760
/data/local/tmp/oscam-mtk/backups/pre-11968-20261024-040649/config.tar
```

The intermediate r11968 backup was retained at:

```text
/data/local/tmp/oscam-mtk/backups/pre-11968-20261024-040850/oscam.previous
/data/local/tmp/oscam-mtk/backups/pre-11968-20261024-040850/config.tar
```

These are TV-local paths. Their archives contain private credentials and are not distributed here. Future upgrades use `backups/pre-upgrade-YYYYMMDD-HHMMSS/` and `oscam.previous`.

## Manual rollback

Choose and inspect a known-good backup. Stop the service using `control.sh stop`. Copy the backup binary to a temporary file under the runtime directory, set mode 0700 and rename it to `oscam`, then use `control.sh start`. Check the generated version and actual playback.

Restore `config.tar` only when the configuration itself needs recovery. A binary-only rollback can retain the current configuration. Archive extraction must be directed to the intended runtime directory after inspecting the archive listing. The original r11760 backup predates the new OSCam build, but contains the configuration that was active immediately before the upgrade.

## Removing automatic startup

Stop the service, then remove only `/data/adb/service.d/oscam-mtk.sh`. The runtime directory and backups can remain root-private until you intentionally archive or remove them. The examples do not modify other Magisk scripts, change SELinux mode, replace the TV player or install firmware/kernel modules.
