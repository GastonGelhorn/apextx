# RF module and receiver version readout

The Receiver & RF page shows the internal RF module's reported version, the
receiver model and its firmware version. These values are informational,
read-only hexadecimal identifiers, not decimal version numbers. A receiver
product ID shared by several models lists those models together; unknown IDs
are shown in hexadecimal rather than guessed.

Opening a visible AFHDS3 receiver page requests the information. Ordinary
driving and passive API getters do not start queries. Reads wait for five
seconds of settled module/receiver state, are disabled during binding, and
receiver queries require a two-way connection. Cached results avoid repeated
reads. Missing replies produce **Unavailable**, not an endless loading state.

Each read is a single UART frame with a separately tracked sequence number.
It does not enter the transport's blocking retransmission path: channel
frames and regular link-state polls continue while the diagnostic response is
pending. There are at most three attempts per module/session, at least a
second apart. Each attempt accepts responses for 300 ms. Receiver reads wait
for the asynchronous `COMMAND_RESULT`, not merely the UART `SEND_COMMAND`
reply. Disconnecting invalidates receiver information; binding, model/RF
reconfiguration and transport reinitialisation invalidate the caches.

The internal NB4 module uses a short version reply rather than every field of
the legacy `ModuleVersion` structure. The displayed word is at byte offset
18, matching the contributor's stock-firmware readout; at least 22 payload
bytes are required. This is not a verified cross-device image compatibility
format and must not be used to select or approve firmware images.

**This feature does not update RF modules or receivers.** It does not write
their firmware, alter their configuration, or extract FlySky updater files.
Those are separate proposals in issue #23. RF module/receiver firmware updates
remain unimplemented.
