# Hardware checklist before a release

Passing the native tests and compiling the firmware do not prove that the RF
module, controls, USB transitions, storage and power behaviour work together on
a real radio. This checklist is what to run on an original Noble NB4 with an
AFHDS3 receiver before tagging a version.

It is a recommendation, not a gate. Nothing in the build or the release
workflow checks that it was done, and the published binaries carry no
certification. Whoever installs a build accepts that risk, as the
[README](../../../README.md) states.

Run the whole list on the exact `firmware.bin` that the release workflow
produced, not on a local build, and record what you observed somewhere you can
find later. If a result differs from what you expect, delete the release and
the tag, fix the cause, raise the version and start again.

## Power and installation

1. The DFU write reads back byte for byte and the radio leaves DFU on its own.
2. A two-second hold powers the radio on, from the battery and from the base.
3. Shutdown needs the button released first, and the radio stays off with the
   base attached and with USB attached.

## Controls

4. Steering and throttle move live, the correct side and the correct channel.
5. The curve editor redraws while a point is dragged.
6. Trims, assignments and the grip keys do what the assignment page says.

## Receiver and RF

7. Binding completes and the receiver connects.
8. The link returns on its own after the transmitter is power cycled.
9. The link returns on its own after the receiver is power cycled.
10. Failsafe moves the servos where the model configured them, checked by
    switching the receiver off with the wheels clear of the ground.
11. Receiver voltage and link quality read plausibly and track reality.
12. Telemetry values are marked stale when the link drops rather than freezing.
13. The receiver-off alarm fires.

## Radio

14. Serial USB connects and disconnects without freezing the interface.
15. Storage USB connects and disconnects without losing data.
16. Settings, models and lap history survive a power cycle.
17. Audio plays, in both the tones-only and voice modes.
18. Portrait and landscape both lay out without clipping.

## Notes to keep out of any record you publish

Never include a transmitter serial number, a USB serial, OTP contents, factory
calibration, a device identifier or a firmware dump. Describe the setup and the
measurement, not the radio's identity.
