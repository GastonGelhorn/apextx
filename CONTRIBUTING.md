# Contributing to ApexTX

ApexTX welcomes focused bug fixes, hardware findings, tests,
documentation, translations, and car-oriented interface improvements.

Open an issue before a large change so its hardware impact and scope can be
reviewed. Create a topic branch from `main`, keep changes narrowly scoped, and
write source code, comments, commit messages, tests, and engineering
documentation in English.

## User-facing text

NB4 interface strings live in the shared translation tables, like the rest of
EdgeTX: add a `TR_NB4_*` define to `radio/src/translations/i18n/en.h` and
`es.h` (and the English text to the other language files, which are not
compiled for the NB4 but keep the table complete), list it in
`string_list.h` and `sim_string_list.h`, and use `STR_NB4_*` in code. The
firmware compiles English and Spanish together and switches at runtime.

Static data tables (the route catalogue, help entries, home templates,
palettes) cannot hold `STR_NB4_*` directly because it reads the active
language table at run time. They store an `Nb4Str` accessor from
`radio/src/nb4_i18n.h` instead, written `NB4_STR(NAME)`, and call it when the
text is shown. There is no other language-selection mechanism in the NB4 code.

Build the original NB4 target and run the maintained checks before opening a
pull request:

```sh
python3 -m unittest discover -s tools/tests -p 'test_nb4_*.py'
python3 tools/nb4-generate-qualified-profile.py \
  --check docs/nb4/rf/qualification.json
cmake -S . -B build/nb4-device \
  -DPCB=PL18 -DPCBREV=NB4 -DNB4_RF_PROFILE=RECOVERED_USART6 \
  -DAPEXTX_PUBLIC_RELEASE=OFF -DTRANSLATIONS=EN \
  -DDISABLE_COMPANION=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/nb4-device --target firmware-size -j8
cmake --build build/nb4-device --target native-configure -j8
cmake --build build/nb4-device/native --target tests-radio -j8
```

Describe what changed, why it is safe for the transmitter, and how it was
tested. Hardware claims need reproducible evidence. Never commit factory
firmware, transmitter backups, calibration, OTP, device identifiers, secrets,
or proprietary assets without redistribution permission.

Contributions are accepted under the repository's GPL-2.0 license and must
preserve applicable upstream and third-party notices. New project-owned source
files should carry an appropriate SPDX license identifier.
