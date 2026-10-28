# Desktop app updates

This updates **ApexTX Flasher**, not the transmitter firmware. No FlySky service
is contacted. The updater uses public GitHub releases in `GastonGelhorn/apextx`;
users need no GitHub account, Python, terminal or paid developer membership.

The desktop app checks once a day while idle, independently of radio detection.
Open **ApexTX Flasher updates** to check manually, choose the app's preview
channel, postpone for a day or skip one version. The firmware's preview switch is
independent. Only **Update and restart** downloads or installs the app. A failed
network check never disables local radio operations or claims the app is current.

A radio transfer, write, read-back verification, backup or pending bootloader
guide blocks app installation and window closure. An uncertain radio-write
result also blocks restarting until the user finishes recovery and explicitly
acknowledges it. The backend reservation prevents a radio operation starting
between the busy check and process shutdown. All future audio/RF/maintenance
operations must enter `FlasherSession.begin()` and call `finish()`; they must not
write USB outside this shared reservation. A per-user process lease prevents two
new flasher copies (or CLI writes) from operating concurrently. Old apps cannot
participate in this lock; close them before using this app.

## Trust and community signing

`FLASHER_VERSION` and `FLASHER_BUILD` independently version the desktop app.
Increase both for a new release; the build integer must always increase.
Tags are `flasher-v<FLASHER_VERSION>` and do not change `APEXTX_VERSION`.

`flasher-release.yml` builds all three packages, verifies them, creates a manifest
with their exact SHA-256/size, and signs its exact UTF-8 bytes using Ed25519.
`flasher-update-keys.json` is the installed trust anchor. Neither a checksum nor
an unsigned GitHub asset can authorize executable installation. Downloads are
bounded, use HTTPS/CA validation and stay on permitted GitHub hosts.

The private key is **never** committed or bundled. The initial key was generated
outside the repository with mode 0600. Configure its PEM contents as the
`APEXTX_FLASHER_SIGNING_KEY` secret in the `flasher-release` GitHub environment;
keep a secure offline backup. Protect that environment and the release workflow
from unreviewed changes. PRs never receive this secret. Missing or mismatched
keys fail publication rather than producing unsigned updates.

To create a replacement key, from the repository root:

```sh
python tools/flasher-release.py --generate-key /secure/location/new-flasher-key.pem
```

The command prints only its public key. Add the new public key **alongside** the
old one in a release signed by the old key. Only after distributing that bridge
may releases use the new private key. Keep the old key for users that missed the
bridge, or require one manual install. A compromised or lost only trusted key
cannot be repaired by trusting an unsigned key downloaded from GitHub.

Windows packages have no Authenticode certificate; macOS bundles retain ad-hoc
signing and have no Developer ID/notarization. Our update signature authenticates
the release but cannot grant SmartScreen reputation or suppress Gatekeeper.
Never remove quarantine or disable platform checks automatically. Initial launch
may require platform permission. The macOS update manifest conservatively
requires macOS 15; lower versions need separate qualification of every bundled
native dependency before reducing that requirement. Windows x64 requires Windows
10 build 17763 or later. Windows ARM64 is not qualified. An Intel app under
Rosetta keeps its existing architecture; native migration is deliberate.

## Installation and recovery

The existing EXE/APP formats are preserved. A copied **complete old app** runs as
an app-only helper outside the installation, so its libraries remain available
after replacement. Both processes recheck the signed package. The new app runs
`--self-test` without discovery before closing the old one. The old app exits
only after the helper is ready. The helper waits for exit; it never kills a
flasher or touches USB.

Windows uses same-volume `ReplaceFileW` with a retained old EXE. macOS exchanges
whole bundles atomically with `RENAME_SWAP`. If replacement is unsupported or
blocked, the old app is retained. A non-writable folder, disk image or translocated
Mac app can be migrated explicitly with **Install in my user folder** to
`~/Applications` or `%LOCALAPPDATA%\Programs\ApexTX Flasher`. No elevation or
privileged service is used. The original is retained on relocation; open the new
copy from that folder afterwards. An existing different user installation is not
overwritten silently.

A private transaction journal and old copy survive crashes. The new app starts
with radio discovery disabled; the UI handshake and helper confirmation must
complete before device access. Early process failure rolls back and relaunches
the old app. A hung process is never forcibly killed; an unconfirmed transaction
is recovered on the next ordinary launch, under the process lease. App settings
and `~/ApexTX-Backups` remain outside the replacement. Transaction and backup
files are retained for diagnosis; remove completed transaction directories only
when recovery is no longer needed.

Old packages without the feature need **one manual bridge installation**. Source
runs and Linux remain manually managed; they can check releases but never replace
the Python interpreter or checkout. Offline use preserves all local radio paths;
a completely downloaded signed package can be reused offline after a preparation
failure. A fresh app download requires Internet and can be retried from the interface. App notices, progress and errors never overwrite radio status.

## Release acceptance

Run Python host-tool tests, `node tools/tests/flasher_ui.cjs`, and the packaged
smoke test on Windows x64, Intel macOS and Apple Silicon. Verify real old-to-new
replacement, startup failure/rollback, non-writable folders, antivirus locks,
Gatekeeper, Rosetta, interrupted network and process/power failure in each journal
phase. Race app installation against every radio operation and pending recovery.
The CI smoke tests do not constitute real-radio or desktop replacement acceptance.
Do not publish a stable version until those scenarios pass on the actual packages.
