# Audio packs for the original Noble NB4

ApexTX uses one voice pack at a time. The interface language and the voice
language are independent: the menus may be Spanish while the installed and
selected voice pack is English, or the other way around.

The radio exposes about 7.9 MiB of usable USB storage. Complete upstream
sound trees for two languages cannot coexist there, and current upstream
files are not all encoded in a format the NB4 software decoder accepts.
ApexTX therefore publishes separate, size-bounded packages, one per language:

- `apextx-audio-en-VERSION.zip` (English)
- `apextx-audio-es-VERSION.zip` (Spanish)
- `apextx-audio-de-VERSION.zip` (German)
- `apextx-audio-cn-VERSION.zip` (Simplified Chinese / Mandarin)

Each package contains every upstream `SYSTEM` prompt for that language, the
ApexTX welcome prompt, and as many assignable tracks as fit in the 7 MiB audio
budget. Every installed file is mono, signed 16-bit PCM WAV at 8 kHz. The
remaining space is reserved for radio settings, cars, race history, themes,
scripts, filesystem metadata and migrations.

With the currently pinned upstream source, the English package contains 548
prompts (212 required system prompts and 336 assignable tracks), the Spanish
package contains 522 (250 required and 272 assignable tracks), and the German
package contains the complete upstream German set of 240 (211 required and 29
assignable tracks). The Chinese package includes the complete upstream set of
337 prompts (208 required system prompts and 129 assignable tracks), using
3,587,584 allocated bytes. "Assignable" means that the track can be chosen for a switch or condition; it does not mean
that another download is required. The manifest in each ZIP records the exact
file count and how many lower-priority assignable tracks did not fit.

The packages are derived reproducibly from the GPL-2.0-licensed
`EdgeTX/edgetx-sdcard-sounds` tag and commit recorded in their
`APEXTX-AUDIO.json` manifest. The source audio license and checksums are
included in every archive.

## Install with the helper

1. Download one audio ZIP from the same ApexTX release as the firmware.
2. On the radio, open **System > USB**, select **USB Storage**, and connect it.
3. Identify the newly mounted NB4 volume. Do not select another removable
   drive: the installer accepts only a volume whose capacity matches the NB4.
4. From the ApexTX repository or the extracted firmware package, first verify
   the audio package without changing the radio:

   ```sh
   python3 tools/nb4-install-audio.py \
     apextx-audio-es-VERSION.zip "/Volumes/NO NAME"
   ```

5. If the reported language and number of files are correct, install it:

   ```sh
   python3 tools/nb4-install-audio.py \
     apextx-audio-es-VERSION.zip "/Volumes/NO NAME" --yes
   ```

The installer verifies the archive, copies the previous `SOUNDS` directory to
a timestamped backup on the computer, replaces it with the selected language,
verifies every installed file, and synchronizes pending writes. On Windows,
replace `/Volumes/NO NAME` with the mounted drive root, such as `E:\`.

## Install manually

Back up the existing `SOUNDS` directory to the computer. Delete that directory
from the radio, extract exactly one ApexTX audio ZIP, and copy its `SOUNDS`
directory to the root of the NB4 volume. Do not merge packages of different
languages: doing so can fill the filesystem and leave too little space to save
cars or settings.

After copying, safely eject the volume through the operating system before
leaving USB Storage mode. Never disconnect the cable while files are being
written.

## Select the voice independently

After restarting the radio, open **Menu > System > General > Voice language**
and select the language of the installed package. Changing **Text language**
does not change or install voice files.

For Simplified Chinese, choose **Chinese** / **简体中文** as the text language.
Install the `cn` audio ZIP and choose the same voice language to hear Mandarin.
The internal code and storage directory are `cn` (`SOUNDS/cn`), matching EdgeTX;
the interface locale is Simplified Chinese (`zh-CN`). English text with Mandarin
audio, or Chinese text with an English voice pack, also works.

## Change to another voice language

Voice files live on the radio's USB storage, not in the firmware, so a
firmware update never changes them. To switch language, install the package
for the new language exactly as above: the helper replaces the whole `SOUNDS`
directory and keeps a backup of the old one on the computer. Then select the
new language under **Voice language**. Keep one language on the radio at a
time.

## When a voice file is missing

The radio keeps working without voice files. What you hear depends on the
sound:

- Alerts and warnings, such as low battery, inactivity, throttle and switch
  warnings and trim limits, fall back to beeps.
- Spoken values, voice assignments, lap announcements and the startup
  greeting are silent.

The same happens when **Voice language** names a language that is not
installed. **Menu > Sound & alerts > Sound > Playback > Tones only** uses
beeps on purpose, even when the voice files are present.

## Replace the startup greeting

At startup the radio plays `SOUNDS/<voice language>/SYSTEM/hello.wav`. Each
ApexTX package includes a short "Welcome to ApexTX" greeting in its language.
To use your own, replace that file with a mono, signed 16-bit PCM WAV at 8 kHz,
for example converted with:

```sh
ffmpeg -i greeting.mp3 -ac 1 -ar 8000 -c:a pcm_s16le hello.wav
```

Turning off **Menu > System > General > Startup sound** silences it as well.

## Assign a voice to a control

Open **Menu > Sound & alerts > Voice assignments**. Add a row, choose the
physical switch or radio condition, select a voice track and use **Play** to
preview it. Repeat controls whether it plays once or again while the trigger
remains active. The master switch at the top can temporarily disable all voice
assignments without deleting them.

These assignments belong to the radio, so they remain available when you
change cars. Automatic lap announcements and telemetry alarms remain in their
respective Race and Telemetry settings because those values are car-specific.
Only files at `SOUNDS/<voice language>/` are assignable; the `SYSTEM` folder is
reserved for firmware prompts such as startup, warnings and confirmations.

## Build the packages from source

The release workflow checks out the pinned upstream sound commit and runs:

```sh
python3 tools/nb4-audio-packs.py \
  --source build/edgetx-sdcard-sounds \
  --output dist \
  --release "$(cat APEXTX_VERSION)"
```

The ApexTX welcome prompts in `sdcard/SOUNDS/<language>/SYSTEM/hello.wav`
replace the upstream EdgeTX greeting. They speak the `hello` phrase of
`radio/util/tts_<language>.py` with a fixed macOS system voice and are
regenerated, on macOS only, with:

```sh
python3 tools/nb4-welcome-prompts.py
```

Building requires `ffmpeg`. The generator refuses an unpinned upstream
checkout, converts and validates every selected file, enforces the device
budget using the NB4 filesystem cluster size, and produces deterministic ZIPs.
