# Troubleshooting and known limitations

## Start with status and the current channel

```sh
adb -s "$TV_SERIAL" shell "su -c '/data/local/tmp/oscam-mtk/control.sh status'"
adb -s "$TV_SERIAL" shell "su -c 'tail -40 /data/local/tmp/oscam-mtk/bridge.log'"
```

Main-session tokens and PIDs are temporary. When the TV application is not using its main DVB input, an idle service without a native bridge is expected. A stale ADB address is not evidence that OSCam stopped; check the TV network/ADB settings after a reboot.

## WebIf cannot be reached

The initial installation had `httpport = 0`; port 8888 was therefore not listening until WebIf was explicitly configured and OSCam restarted.

Check the actual port and listener, without printing passwords:

```sh
adb -s "$TV_SERIAL" shell "su -c 'grep -E \"^httpport|^httpallowed\" /data/local/tmp/oscam-mtk/config/oscam.conf'"
adb -s "$TV_SERIAL" shell "su -c 'ss -ltn | grep :8888'"
```

Check that your client belongs to `httpallowed` and that the TV IP is correct. HTTP 401 without credentials is expected. HTTP 200 with the configured account was verified. Reader credentials are not the WebIf credentials.

## WebIf opens but controls do not respond

During the session, Brave displayed the page but the owner initially reported unresponsive controls. Both local JavaScript files exactly matched their source and were served with JavaScript MIME type; Readers and the JSON API returned HTTP 200. The owner later reported that it worked, but the exact browser-side cause was not confirmed.

For a recurrence:

1. Check whether Brave blocks scripts for the TV's address; use the site-specific setting rather than changing unrelated sites.
2. Hard-refresh the page: Cmd+Shift+R on macOS, Ctrl+Shift+R on Windows/Linux.
3. Try `http://TV_IP:8888/readers.html` directly.
4. Inspect the browser console and network responses for `jquery.js`, `oscam.js` and `oscamapi.json?part=status`.

Brave documents its [site-specific Shields controls](https://support.brave.com/hc/en-us/articles/360023646212-How-do-I-configure-global-and-site-specific-Shields-settings). A successful HTTP response alone does not establish that client-side click handlers executed.

## Reader shows anonymous or no CW is returned

A raw socket test made during investigation appeared as `anonymous disconnected` because it did not perform a CCcam login. Actual OSCam reader requests later authenticated successfully. Distinguish an unauthenticated connectivity probe from a configured protocol client.

Verify your own reader's host, port, protocol, username/password and group mapping locally. Look for `found` responses and then hardware CW application. Do not copy live reader settings or raw logs into Git. The repository's example hostname and passwords are placeholders.

## CW is found but the TV stays black

Check that you selected an encrypted program covered by the reader, that the main DVB input is active, and that the bridge found the requested service and expected transport stream ID in live PAT/PMT data. Look for `CA_READY`, valid stream PID bindings and `hardware_applied=1`.

Standard Linux CA ioctl success assumptions do not apply to this vendor ABI. The bridge uses its own token/keyslot and a PID allowlist. A free-to-air picture does not test this path. Confirm actual picture and sound rather than treating `found` or an ioctl return value as sufficient proof.

## `UNSUPPORTED_OSCAM_FRAME opcode=ffff0003`

This happened with unpatched upstream r11968: ECM_INFO metadata was sent to the bridge's protocol-v1 connection. Apply the supplied pinned OSCam patch and rebuild. It gates ECM_INFO on negotiated protocol version at least 2, preserving the existing bridge framing. Do not suppress arbitrary unknown frame bytes, because that can desynchronize the parser.

## CAS `IllegalStateException` and TV restart

After several service restarts, the vendor CAS path temporarily refused new sessions with `IllegalStateException`; a TV restart followed during that investigation. The cause is still unknown. A later boot restored the working configuration and playback, but this is not a demonstrated permanent fix.

Avoid repeatedly opening additional diagnostic CAS sessions in that state. Save a minimal, private error report and use the TV's normal restart if necessary. Do not restart vendor HALs or alter SELinux as an unverified workaround. The failed prototypes and proprietary plugin captures are not shipped in this repository.

A separate early read-only prototype using `ActivityThread.systemMain` followed by provider access coincided with a system_server crash/reboot. The runtime now queries the TV service without that system context and runs metadata queries through the already tested `content` command. A definitive causal diagnosis of that early crash was not established.

## Unsupported combinations

The record covers main live DVB playback on the listed firmware. PIP, simultaneous recording, other firmware/kernel builds, other algorithms, physical smartcard readers and firmware updates need their own ABI/resource validation. Capture the firmware again after an update and compare it to the recorded profile.
