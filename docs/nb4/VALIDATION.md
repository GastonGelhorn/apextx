# Validation

Run these checks before publishing a firmware revision:

```sh
cmake --build build/nb4-native --target gtests-radio -j8
./build/nb4-native/gtests-radio
python3 -m unittest discover -s tools/tests -p 'test_nb4_*.py'
cmake --build build/nb4-device --target firmware-size -j8
tools/nb4-generate-qualified-profile.py --check docs/nb4/rf/qualification.json
git diff --check
```

On the radio, verify:

- Steering and throttle remain responsive on the home and curve pages.
- The receiver reconnects after both devices are power-cycled.
- Receiver voltage and link quality expire cleanly when the receiver is off.
- Bind completes and survives a radio restart.
- Steering travel, reverse, center, throttle travel, brake, and failsafe affect
  the intended side and channel.
- Serial and storage USB modes connect and disconnect without freezing the UI.
- Race timing, lap input, assignments, audio, storage, and emergency shutdown
  work with and without an attached USB cable.

The checked-in RF profile remains `development` until the complete physical
record described in `rf/BENCH_ACCEPTANCE.md` passes. A public build is rejected
unless that record is present, hash-bound, and selected through the `QUALIFIED`
RF profile.
