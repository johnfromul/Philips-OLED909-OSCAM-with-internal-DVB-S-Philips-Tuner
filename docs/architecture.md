# Architecture and investigation findings

## The integration boundary

The original Philips application continues to tune and play the channel. The bridge does not retune the frontend, replace the player, load a kernel module or change SELinux mode. It observes the main DVB session, reads its own section filters on the live transport stream, and programs its own vendor CA resources.

The verified device paths are `/dev/dvb0.demux20` and `/dev/dvb0.ca2`, source 0. These choices are specific to the observed main-session path on this firmware. They should not be generalized to recording, PIP or a different television without checking the resource ownership and ABI.

## Why standard direct DVBAPI did not work

Full Magisk root made the character devices accessible, but the driver was not a standard OSCam descrambler backend:

- Read-only queries on ca0–ca2 returned vendor slot information (`type=8`, unused flags 0); standard capability/descrambler queries returned EINVAL.
- Static inspection of the current `mtk_dmx.ko` dispatcher found no standard `CA_SET_DESCR` path. A live invalid-slot test also returned EINVAL, but that invalid-argument result alone would not prove the operation unsupported.
- The ioctl number often used for extended Linux `CA_SET_PID` identifies a different vendor allocation operation in this driver. Forwarding that raw number would be incorrect.
- The public MediaTek header describes token, keyslot, algorithm, PID attachment and even/odd key operations. Their structure sizes were compared with BTF from the actual TV module.

The repository therefore translates protocol requests into vendor operations. The firmware/module captures used for analysis are deliberately not distributed. Public header provenance is in [third-party notices](../THIRD_PARTY_NOTICES.md).

## Java service: select the right channel and own the CAS lifetime

`BridgeService` runs through Android's existing `app_process` as root. It queries the TV input service's read-only current-tuning information and considers only the non-recording main session of the tested MediaTek DVB input. It checks interactive power state as well.

The service accepts a numeric `content://android.media.tv/channel/...` URI and obtains `service_id` and `transport_stream_id` using a separate `/system/bin/content query` subprocess. It rechecks the current channel after this query to avoid starting a stale selection.

For an encrypted-service attempt, Java creates a public `MediaCas` instance for the firmware plugin system ID **6210**, opening a live session with scrambling mode 1. This plugin ID is a hardware-resource allocation choice on this firmware; it is separate from the broadcast CAID selected by OSCam.

The session ID is expected to be 12 bytes. The first eight bytes encode the vendor token in big-endian order; those bytes are passed to the child through a private pipe, rather than a command-line argument, file or log. Java holds the session open for the native child's full lifetime. On a channel change, it terminates and waits for the child before closing the old session and starting another.

No application-process injection or private memory writes are used by this implementation. The discarded investigation prototypes are not part of the runtime.

## Native bridge: validate and translate

The child acquires `bridge.lock`, reads the live PAT, finds the requested service's PMT PID and checks the expected transport stream ID. It validates PMT lengths, service ID, descriptors and elementary streams before constructing CA_PMT and a stream-PID allowlist.

A free-to-air PMT results in `FREE_TO_AIR` / exit status 20 before hardware CA programming. For encrypted PMTs, the bridge uses a currently unused ca2, source 0, the token from its own MediaCas session and the vendor-allocated engine/keyslot. It requests CSA and retains the slot bounds checks.

The bridge connects to the root-private Unix socket and announces DVBAPI protocol version 1. It supports the relevant filter, PID, CW and mode frames. ECM sections are returned to OSCam; CW bytes received from OSCam are mapped to vendor even/odd key operations. Requested stream PIDs must be present in the validated PMT. Only its own recorded PID bindings are removed during cleanup.

CW bytes and the session token are never deliberately printed. Logs report counters, service/PID information and successful API results. Actual picture and sound were separately confirmed by the owner.

## Resource lifetime and recovery

- `launcher.lock`: prevents duplicate watchdog launchers.
- `supervisor.lock`: prevents duplicate Java supervisors.
- `bridge.lock`: prevents duplicate native bridge ownership.
- SIGTERM/SIGINT: native cleanup closes filters, removes its own PID bindings and closes CA/socket descriptors.
- `PR_SET_PDEATHSIG`: sends SIGTERM to the native child if the Java parent disappears; parent identity is checked during setup to cover the startup race.
- PAT/PMT changes: cause the child to stop and be recreated for the current selection.
- No CW for 45 seconds: triggers a retry. General retries use a bounded 2–10-second backoff; free-to-air programs are rechecked after a minute.

The observed CAS refusal/reboot issue remains a limitation. The recovery mechanisms are useful, but are not evidence that every vendor failure state can be repaired without a TV reboot.

## Investigation chronology

1. Compared limited root with full Magisk root and verified device access under SELinux Enforcing.
2. Inspected the current module and public UAPI definitions; rejected the incompatible direct-ioctl approach.
3. Tested an independently owned CAS/keyslot allocation and corrected token byte order to big-endian.
4. Read live PMT and ECM sections using independent demux filters.
5. Authenticated OSCam to the owner's configured reader and verified `found` responses.
6. Connected stream PIDs and actual CW to the hardware; the owner confirmed encrypted Prima HD picture and sound.
7. Added channel monitoring, cleanup, Magisk startup and recovery; tested encrypted/free-to-air transitions and a reboot.
8. Replaced the initial old mirror build r11760 with pinned official r11968, adding the protocol-v2 metadata guard while retaining the bridge and configuration.

Details of tests and their limits are in [validation](validation.md).
