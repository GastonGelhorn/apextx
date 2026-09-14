# Physical release acceptance

Every public ApexTX firmware must be backed by a physical acceptance
record for an original Noble NB4 and an AFHDS3 receiver. Passing native tests
and compiling the firmware are necessary, but they do not prove that the RF
module, controls, USB transitions, storage, and power behavior work together on
the actual radio.

Copy `bench-acceptance.example.json` to a versioned file such as
`bench-acceptance-0.1.0-alpha.1.json`. Build and flash the candidate, record its
source commit, firmware SHA-256, and the output of
`python3 tools/nb4_source_digest.py` before testing. Change a result to `true`
only after observing it on hardware. Notes should describe the setup or a useful
measurement without containing a transmitter serial number, USB serial, OTP,
factory calibration, device identifier, or private firmware dump.

The source-tree digest covers the CMake and radio build inputs, including
submodule revisions. It excludes the generated qualification descriptor so the
passing record can be added without changing the implementation it certifies.

The complete checklist covers verified DFU readback, two-second power-on hold,
safe shutdown, live steering and throttle, the curve editor, binding and both
reconnection directions, failsafe, receiver voltage and link quality,
telemetry expiration and the receiver-off alarm, both USB modes, persistent
storage, audio, and both screen orientations.

After every result passes, calculate the record SHA-256 and reference it from
`qualification.json`:

```json
"bench_acceptance": {
  "path": "bench-acceptance-0.1.0-alpha.1.json",
  "sha256": "..."
}
```

Set `status` to `release`, regenerate the qualified profile, and run the full
validation suite. The generator rejects missing, modified, incomplete, or
failed acceptance evidence. CMake also rejects `APEXTX_PUBLIC_RELEASE=ON` unless
that release-qualified profile is selected.
