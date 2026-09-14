# Contributing to ApexTX

ApexTX welcomes focused bug fixes, hardware findings, tests,
documentation, translations, and car-oriented interface improvements.

Open an issue before a large change so its hardware impact and scope can be
reviewed. Create a topic branch from `main`, keep changes narrowly scoped, and
write source code, comments, commit messages, tests, and engineering
documentation in English.

## User-facing text

NB4 interface strings currently use `nb4Text("es", "en")`, which selects a
literal at runtime from the configured interface language. Follow that pattern
in NB4 code so the whole target stays consistent, and keep the English literal
idiomatic because it is the fallback for every language that is not Spanish.

This is deliberate for the initial targets and is not the long-term plan: it
holds both literals in flash and it diverges from the upstream `TR_`
localization tables, which remain in use for inherited screens. Migrating NB4
strings onto the upstream translation system is planned for the first release
that needs a third interface language. Do not introduce a third literal
argument; add the language through the translation tables instead.

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
