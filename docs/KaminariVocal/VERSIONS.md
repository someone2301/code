# Kaminari Vocal versions

| Edition | Plug-in name | Codes | Source | Installer |
| --- | --- | --- | --- | --- |
| Full | Kaminari Vocal | Kmni / KmVc, `com.kaminariaudio.kaminarivocal` | commit `231f676` on branch `claude/eager-feynman-fyj41s` | [Actions run 37689331900](https://github.com/someone2301/code/actions/runs/37689331900), artifact KaminariVocal-macOS |
| Alt | Kaminari Vocal Alt | Kmni / KmVa, `com.kaminariaudio.kaminarivocalalt` | the branch head after `231f676` | the latest Actions run, artifact KaminariVocalAlt-macOS |

The two editions have different plug-in codes, bundle IDs and user-preset folders, so they install side by side and
can be compared in one session. Sessions saved with one edition open only in that edition.

To build the full version again: `git checkout 231f676` and run
`KaminariVocal/installer/make_installer.sh` on a Mac, or re-run the Actions workflow on that commit. GitHub keeps run
artifacts for a limited time (90 days by default), so download installers you want to keep.

## Alt edition (differences from the full version)

- Tune: the original algorithm (fixed 96-sample delay at 48 kHz, period-locked delay-line shifter); no Tracking /
  High quality switch. Key, Scale, Range, Retune Speed, Humanize, Detune, Correct Pitch, vibrato and tremolo as before.
- Compression: one LA-2A style (optical) mode with Compression (peak reduction) and Gain (makeup); the side-chain EQ
  stays. No auto gain, threshold, ratio, timing or parallel controls. Calibrated on a dry vocal peaking at -6 dBFS:
  Compression 30 touches only the loudest words, 50 levels about 4 dB (7 dB on peaks), 100 about 19 dB. The cell
  reacts in about 10 ms and releases in two stages (half in about 60 ms, the rest in 0.5 to 3.5 s, slower after long,
  heavy leveling).
- Basic view: the Compression hammer sets Compression; a vertical Gain slider beside it sets the makeup.
- De-ess: Frequency and Range only. Esses are found by how loud the highs above Frequency are against the whole vocal,
  so the setting does not depend on the input level; only the range above Frequency is turned down.
- No Flanger, Distortion, or Reverb / Delay / Widener sends (and none of their parameters, pages or presets).
- Multiband bands are drawn like EQ bands: each band shaded between its curve and 0 dB in the EQ's band colours, with a
  numbered node (lightning on the selected one).
- Graph speed: the EQ, side-chain EQ and Resonance curves are computed only when a band, the size or the zoom changes;
  parameters are read through direct pointers; a dragged node is redrawn on every mouse move; the graphs no longer
  repaint their parents every frame; displays and hammers on a hidden page or view do no work at all (before, every
  page's analyzers and histories kept updating in the background).
- Sessions: state version 5. A full-edition session's open tab maps to the same module where this edition has it.
