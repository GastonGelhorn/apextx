# Release process

ApexTX uses the version in the root `APEXTX_VERSION` file. A public release
tag has the form `v0.1.0-alpha.1` and must match that file exactly. The upstream
base remains recorded separately as EdgeTX 2.12.4.

Before tagging, complete the physical procedure in
`rf/BENCH_ACCEPTANCE.md`, add the hash-bound record to `qualification.json`, set
its status to `release`, and regenerate the checked-in RF profile:

```sh
python3 tools/nb4-generate-qualified-profile.py \
  docs/nb4/rf/qualification.json
```

Run the tool tests, native firmware tests, and a clean ARM build. A public build
must use both of these options and CMake rejects it unless physical acceptance
is complete:

```text
NB4_RF_PROFILE=QUALIFIED
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
flash that exact `firmware.bin` to the acceptance radio for a final smoke test.
If any result differs from the recorded acceptance, delete the release and tag,
correct the cause, increment the version, and repeat the procedure.
