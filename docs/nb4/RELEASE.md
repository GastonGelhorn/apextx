# Release process

ApexTX uses the version in the root `APEXTX_VERSION` file. A public release
tag has the form `v0.1.0-alpha.1` and must match that file exactly. The upstream
base remains recorded separately as EdgeTX 2.12.4.

Before tagging, run the hardware checklist in `rf/BENCH_ACCEPTANCE.md` on a
real radio, then the tool tests, the native firmware tests and a clean ARM
build. The checklist is a recommendation: nothing in the build enforces it, and
the published binaries carry no certification.

A published build uses the RF route the firmware ships plus the flag that marks
the image as published:

```text
NB4_RF_PROFILE=RECOVERED_USART6
APEXTX_PUBLIC_RELEASE=ON
```

Create and push a signed or annotated tag only from a clean, reviewed `main`:

```sh
version="$(cat APEXTX_VERSION)"
git tag -a "v$version" -m "ApexTX $version"
git push origin "v$version"
```

The release workflow rebuilds and verifies the image, creates a package with
firmware, storage resources, validation metadata, licenses, a manifest, and
SHA-256 checksums, then uploads the artifacts to a GitHub Release. Do not create
or publish a tag merely to test the workflow; use its manual artifact-only run
for that purpose.

After GitHub finishes, download the release archive, verify `SHA256SUMS`, and
flash that exact `firmware.bin` to a radio for the checklist. If a result
differs from what you expect, delete the release and the tag, correct the
cause, increment the version, and repeat the procedure.
