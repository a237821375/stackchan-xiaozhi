# Cat purr recording

- Author: Kerzoven
- Title: Cat Purr & Meow — `cat_purrsleepy_loop.wav`
- Source: https://opengameart.org/content/cat-purr-meow
- Download: https://opengameart.org/sites/default/files/cat_purrsleepy_loop.wav
- License: CC0 1.0 https://creativecommons.org/publicdomain/zero/1.0/
- Retrieved: 2026-09-29

`purr_pcm.h` is a derivative: 24 kHz, mono, signed 16-bit PCM with a 40 ms
crossfade at the loop seam. Recreate with Python 3.11:
`python tools/convert_purr.py /path/to/cat_purrsleepy_loop.wav`.
Source checksum is recorded in the generated header.

Runtime gain is 25% of the recording, with approximately 333 ms fade-in/out.
Master volume is unchanged. Only idle playback is eligible; speech, listening,
network transitions, alerts and pending decoder work take priority. Petting state
already includes the existing three-second release hold; audio adds no extra hold.
