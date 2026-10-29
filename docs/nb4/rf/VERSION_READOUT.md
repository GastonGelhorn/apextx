# RF module and receiver version readout

The Receiver & RF page shows the internal RF module's reported version, the
receiver model and its firmware version. These values are informational,
read-only hexadecimal identifiers, not decimal version numbers. A receiver
product ID shared by several models lists those models together; unknown IDs
are shown in hexadecimal rather than guessed.

Each stable AFHDS3 connection starts one bounded check without a visible
receiver page. Passive API getters do not start queries. Reads wait for five
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
18. Only the exact 26-byte reply currently establishes its application version;
the RF-library word is at offset 22. Receiver replies retain all five identity
words: hardware, RF hardware, bootloader, application and RF library. The
application word is the fourth, at byte offset 10, not the first.
The reported module product ID and its catalog image ID are distinct and mapped
explicitly. A version word alone does not authorize an image.

Version queries never update firmware or alter configuration. Normal builds
keep RF writes disabled. See [Integrated maintenance](../INTEGRATED_UPDATE.md)
and the [separate private RF bench procedure](../RF_BENCH.md) for installation,
qualification and recovery status.
