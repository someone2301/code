# Kaminari Vocal — Design Document (pre-implementation)

Status: implemented in `KaminariVocal/`: all seven channel modules (first versions, section 2.12; Distortion 2.13), Tune's vibrato and tremolo (2.2.1), the four sends (2.9; Flanger 2.14), presets (2.10), session versioning (2.11), mono/stereo layouts, and the Mac build script and installer. The GUI follows the preview; the Distortion and Flanger pages and the Tune vibrato/tremolo groups were added after the preview and follow its style.

## 0. Current project state and recorded decisions

### 0.1 What is in the repository today

| Item | State |
| --- | --- |
| `ChannelStrip/` | JUCE 8.0.8 VST3 plug-in "Channel Strip" v0.1.0, ~2,250 lines, Intel macOS (x86_64) build |
| DSP | RBJ biquads, 6-band EQ (bell, shelves, HP, LP, notch), split-band de-esser (one band-pass), FET- and opto-style compressor, gain/pan/mono |
| GUI | Custom LookAndFeel, EQ graph with draggable nodes, mouse-wheel Q, FFT analyser, needle GR meter |
| State | `AudioProcessorValueTreeState`, XML state save/restore, parameter version 1 |
| Latency | 0 samples reported (no lookahead, no oversampling, IIR only) |
| Tests | One console executable: level checks for gain, EQ, compressors, de-esser; PNG editor snapshots |
| Packaging | `build_mac.sh`, `.pkg` installer and uninstaller scripts |
| Missing vs. Kaminari Vocal spec | Pitch correction, 8 bands, extra filter types, slopes, dynamic band, low-mid dynamics, Basic/Advanced views, presets, A/B, undo, tooltips/context menus/keyboard entry, AU, AAX, latency measurement tests |

Reusable: the biquad math, the analyser FIFO pattern, the test-harness pattern, the build and installer scripts.
Not reused: the FET/opto compressor models (they are modeled on specific hardware and are outside the product scope), all parameter IDs, all GUI code.

### 0.2 Decisions made with the user

| Topic | Decision |
| --- | --- |
| Relation to old plug-in | Kaminari Vocal **replaces** Channel Strip. Phase 1 deletes `ChannelStrip/` and creates `KaminariVocal/` with a new plug-in code. Sessions saved with Channel Strip will not load Kaminari Vocal. |
| Latency budget | Total reported plug-in latency of **≤ 128 samples at 48 kHz** (2.67 ms), target ~96. The earlier 74-sample figure is superseded. |
| Pitch correction character | Classic, period-based, low-latency correction (not a modern formant-preserving mode). Best quality that fits the budget. |
| Colors | Navy and white base, one electric ice-blue accent, amber for warnings, red only for clipping. |
| Platform | Intel Mac (2018 hardware), macOS Sequoia. Formats: VST3, AU, AAX. Architecture x86_64 (universal optional). |
| Host categories | EQ, Dynamics, Pitch Correction. VST3: `Fx EQ Dynamics "Pitch Shift"`. AAX: `EQ Dynamics PitchShift` (bit flags, combined). AU has no category list; Logic files it under the manufacturer name. |
| Reported latency | 96 samples at 48 kHz for now. To be revisited in Phase 6 (see section 4). |
| Identity | Product **Kaminari Vocal**, company **Kaminari Audio**. Manufacturer code `Kmni`, plug-in code `KmVc`, bundle ID `com.kaminariaudio.kaminarivocal`. Codes are permanent after the first shared build. A name search found no audio company or plug-in using either name; no trademark search has been done. |

### 0.3 Platform facts that affect the plan

- JUCE 8 ships the AAX SDK, so an AAX build needs no separate SDK download. Running AAX in retail Pro Tools requires PACE signing. Avid provides the PACE signing tools free to registered AAX developers; signing needs an iLok. The user must register with Avid (devauth@avid.com) for the Pro Tools Developer build and signing access. Until then, AAX is built and tested in the Pro Tools Developer build only, and retail Pro Tools continues to use VST3 through Blue Cat PatchWork.
- macOS Sequoia is the last macOS release that supports 2018 Intel Macs. Pro Tools 2024.10 and later support Sequoia. Avid lists an audio-performance issue on high-core-count Intel Macs under Sonoma, Sequoia, and Tahoe.
- Waves Tune Real-Time reports 0 samples to the host; its actual delay varies from 0 to 4 ms with the pitch period. Kaminari Vocal reports a fixed latency instead (see section 4) so that host delay compensation is correct.

---

## 1. Module architecture

```
KaminariVocal/
  CMakeLists.txt                FORMATS VST3 AU AAX, x86_64, macOS 10.15+
  Source/
    PluginProcessor.{h,cpp}     owns APVTS, engine, presets, A/B, undo manager
    PluginEditor.{h,cpp}        root component, view switching, scaling
    params/
      ParamIDs.h                every permanent ID as a constant (never edited after release)
      ParamLayout.cpp           ranges, defaults, text conversion
      ParamSnapshot.h           POD block of all values, built once per audio block
    engine/
      Engine.{h,cpp}            fixed chain, bypass crossfades, latency bookkeeping
      LatencyLine.h             fixed delay used by the dry path of the tuner
      BypassFader.h             10 ms equal-gain crossfade per module
      SafetyStage.h             NaN/Inf guard, DC blocker, optional clip guard
    dsp/
      Biquad.h, Svf.h           TPT state-variable filters (smooth modulation), biquads for display math
      eq/  EqBand.h, EqEngine.h, EqResponse.h
      tune/ PitchDetector.h, ScaleMapper.h, PeriodShifter.h, TuneEngine.h
      level/ Detector.h, GainComputer.h, Leveler.h, AutoMakeup.h
      deess/ DeEsser.h
      lowmid/ BandSplit.h, LowMidDynamics.h
      analysis/ AnalyzerTap.h   lock-free FIFO, pre-EQ and post-EQ taps
    state/
      PresetManager.{h,cpp}     user presets as XML files (message thread only)
      ABSlots.{h,cpp}           two ValueTree snapshots
      UiState.h                 view, selected module, analyzer settings, zoom, scale (non-automatable)
    ui/
      theme/ Theme.h, Bolt.h, Fonts.h      design tokens, lightning glyphs, embedded font
      controls/ WLKnob, WLSlider, WLButton, WLMeter, ValueEntry, ParamContextMenu, TooltipManager
      basic/ BasicView, TuneStrip, LevelStrip, DeEssStrip, LowMidStrip
      advanced/ AdvancedView, TuneEditor, EqEditor, LevelEditor, DeEssEditor, LowMidEditor
      eq/ EqGraph, BandNode, AnalyzerPainter
      chrome/ HeaderBar, MeterRail, LatencyBadge
  Tests/
    Main.cpp + one file per module (JUCE UnitTest)
    fixtures/                   generated tones; real vocal recordings stored outside git or with Git LFS
```

Rules:
- Audio thread reads parameters only through cached `std::atomic<float>*` pointers into a `ParamSnapshot`. No allocation, locks, file I/O, or UI calls in `processBlock`.
- All buffers are allocated in `prepareToPlay` for the maximum block size; blocks larger than that are split.
- GUI reads meters from atomics and the analyzer from a single-producer/single-consumer FIFO.
- Basic and Advanced views attach to the same `RangedAudioParameter` objects. View choice lives in `UiState`, which is saved with the session but is not a parameter.

---

## 2. Parameter table

Conventions:
- ID format `<module>_<name>`; EQ bands `eq<N>_<name>` with N = 1..8. JUCE `ParameterID` version hint = 1 for all.
- An ID is permanent once a build is shared. It is never renamed, reused, or given a different range meaning. New features add new IDs.
- Vis: **B** = shown in Basic and Advanced, **A** = Advanced only, **–** = not shown as a control (host/automation only or internal).
- "Non-auto" = not host-automatable (`withAutomatable(false)`), still saved in state.

### 2.1 Global

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `gl_in_gain` | Input Gain | float | −24 … +24 | 0 | dB | B |
| `gl_out_gain` | Output Gain | float | −24 … +24 | 0 | dB | B |
| `gl_clip_guard` | Clip Guard | bool | off/on | on | – | A |
| `gl_oversample` | Oversampling | choice | Off, 2x, 4x | Off | – | A (Phase 7; reserved) |

Host bypass uses JUCE's `getBypassParameter` with a crossfaded bypass (not a separate ID).

### 2.2 Tune

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `tn_on` | Tune On | bool | | on | | B |
| `tn_key` | Key | choice | C, C♯/D♭ … B | C | | B |
| `tn_scale` | Scale | choice | Chromatic, Major, Natural Minor, Harmonic Minor, Melodic Minor, Major Pentatonic, Minor Pentatonic, Blues, Dorian, Mixolydian, Custom | Chromatic | | B |
| `tn_range` | Vocal Range | choice | High (175–1100 Hz), Middle (110–700 Hz), Low (80–520 Hz), Deep (60–350 Hz) | Middle | | B |
| `tn_speed` | Retune Speed | float, skewed | 0 … 400 | 40 | ms | B (lightning slider, inverted: more strikes = faster) |
| `tn_humanize` | Humanize | float | 0 … 100 | 20 | % | A |
| `tn_note_0` … `tn_note_11` | Note C … B | bool | | per scale | | A |

Retune Speed is how quickly a note is pulled to the target (0 ms = instant, hard-tuned sound). Humanize slows correction only on held notes, so sustained notes keep natural movement while short notes are still corrected.

Scale behavior: choosing a named scale writes the 12 `tn_note_*` values. If the user edits a note afterwards, `tn_scale` is set to Custom. The note map is saved with presets and sessions.

### 2.3 EQ (repeat for N = 1..8)

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `eq_on` | EQ On | bool | | on | | B |
| `eq<N>_used` | Band N Used | bool | | off | | B (node exists) |
| `eq<N>_on` | Band N Active | bool | | on | | B (bypass ≠ delete) |
| `eq<N>_type` | Band N Type | choice | Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt | Bell | | B |
| `eq<N>_freq` | Band N Freq | float, log | 10 … 30000 (clamped to 0.45·fs) | 1000 | Hz | B |
| `eq<N>_gain` | Band N Gain | float | −30 … +30 | 0 | dB | B |
| `eq<N>_q` | Band N Q | float, log | 0.025 … 40 | 1.0 | | B |
| `eq<N>_slope` | Band N Slope | choice | 6, 12, 18, 24, 36, 48 | 12 | dB/oct | A (cuts and shelves) |

Reserved IDs, created only if the feature passes its tests: `eq<N>_dyn_on`, `eq<N>_dyn_thresh`, `eq<N>_dyn_range`, `eq<N>_dyn_attack`, `eq<N>_dyn_release`, `eq<N>_chan` (Stereo/Left/Right/Mid/Side).

Pro-Q 3–based additions (own implementation): shapes Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt; slopes 6–96 dB/oct plus Brickwall for cuts; per-band dynamic EQ (`eq<N>_dyn_range` ring around Gain, `eq<N>_dyn_thresh`; program-dependent timing) — the reserved dynamic IDs become real; per-band stereo placement (Stereo, Left, Right, Mid, Side) — the reserved `eq<N>_chan` becomes real; global `eq_auto_gain`, `eq_gain_scale` (0–200 %), `eq_phase_invert`, `eq_out_gain`, `eq_out_pan`; processing mode Zero latency (default) / Natural / Linear phase (the last two add reported latency); solo by click-and-hold on a band; Spectrum Grab. Band count stays 8 (Pro-Q 3 allows 24; can be raised later).

Not parameters (UI state, saved with the session): band solo/audition, selection, analyzer on/off, pre/post, speed, resolution, tilt, freeze, range, zoom/scroll.

Analyzer (defaults: **Resolution High, Speed Fast**):

| Resolution | FFT at 44.1/48 kHz | Bin width at 48 kHz |
| --- | --- | --- |
| Low | 1024 | 46.9 Hz |
| Medium | 2048 | 23.4 Hz |
| High | 4096 | 11.7 Hz |
| Maximum | 8192 | 5.9 Hz |

The FFT length doubles at 88.2/96 kHz and again at 176.4/192 kHz (cap 16384) so the time window stays the same. The audio thread only writes samples into a lock-free ring; the GUI analyses the newest window on every display frame (overlapping windows), so the refresh rate stays at the display rate at every resolution. Hann window, amplitude-corrected so a full-scale sine reads 0 dB. Each 2-px display column shows the loudest bin it covers (narrow peaks stay visible); below one bin per column the bins are interpolated. Tilt 4.5 dB/oct around 1 kHz.

| Speed | Very Slow | Slow | Medium | Fast | Very Fast |
| --- | --- | --- | --- | --- | --- |
| Release time constant | 2000 ms | 900 ms | 400 ms | 150 ms | 50 ms |

Attack is immediate. When the host stops sending audio for more than 100 ms the display falls at the selected release speed.

### 2.4 Compression (IDs keep the `lv_` prefix; the module is named Compression in the UI)

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `lv_on` | Compression On | bool | | on | | B |
| `lv_thresh` | Threshold | float | −50 … 0 | −14 | dB | B (as Compression) |
| `lv_ratio` | Ratio | float, skewed | 1 … 20 | 3 | :1 | A |
| `lv_attack` | Attack | float, log | 0.1 … 100 | 8 | ms | A |
| `lv_release` | Release | float, log | 10 … 2000 | 150 | ms | A |
| `lv_knee` | Knee | float | 0 … 24 | 8 | dB | A |
| `lv_range` | Range (max reduction) | float | 0 … 40 | 15 | dB | A |
| `lv_detector` | Detector | choice | Peak, Smooth (RMS 10 ms) | Smooth | | A |
| `lv_mix` | Mix (parallel) | float | 0 … 100 | 100 | % | A |
| `lv_wet_gain` | Wet Gain | float | −12 … +12 | 0 | dB | A |
| `lv_sc<N>_used/on/type/freq/gain/q/slope` | Detector EQ band N (N = 1..8) | same types and ranges as the main EQ bands | | band 1 Low Cut 100 Hz, others unused | | A |
| `lv_sc_listen` | Detector Audition | bool | | off | | A (non-auto) |

Detector EQ: 8 bands with the same filter types, ranges and graph as the main EQ, plus the same analyzer. The analyzer can show the detector signal before the detector EQ (Pre) and after it (Post); each view is switched on or off separately. It shapes only the signal the compressor reacts to; the vocal is not filtered.

Parallel compression: `lv_mix` blends the compressed (wet) signal with the uncompressed (dry) signal; dry and wet stay time-aligned because the compressor has no latency. `lv_wet_gain` sets the wet level before the blend.

Additional compression parameters (functionality modelled on the FabFilter Pro-C 2 manual; own implementation and names):

| ID | Name | Type | Range | Default | Unit |
| --- | --- | --- | --- | --- | --- |
| `lv_style` | Style | choice | Clean, Vocal, Opto, Classic, Punch | Clean | |
| `lv_auto_release` | Auto Release | bool | | on | |
| `lv_hold` | Hold | float | 0 … 500 | 0 | ms |
| `lv_lookahead` | Lookahead | float | 0 … 20 (0 = off) | 0 | ms |
| `lv_oversample` | Oversampling | choice | Off, 2x, 4x | Off | |
| `lv_sc_source` | Sidechain | choice | Internal, External | Internal | |
| `lv_stereo_link` | Stereo Link | float | 0 … 100 % linked, then Mid only / Side only | 100 % | |
| `lv_out_gain` | Output | float | −24 … +24 | 0 | dB |
| `lv_dry` | Dry (parallel dry level added to the compressed signal) | float | Off, −36 … +36 | Off | dB |
| `lv_sc_level` | Side Chain Level (detector gain) | float | −36 … +36 | 0 | dB |

`lv_mix` range becomes 0–200 % (above 100 % increases the processing). Attack range becomes 0.005–250 ms. The level display, knee display and meters share one meter scale (30/60/90 dB). Styles: Clean (low-distortion feed-forward, default), Vocal (automatic knee and ratio, so the threshold is the main control), Opto (slow, very soft knee), Classic (feedback, program dependent), Punch (analog-like). Lookahead and oversampling add reported latency and are off by default.

Compressor type: a clean digital feed-forward compressor (VCA-style behavior; not an opto or FET model). Soft knee, peak or smooth (RMS) detection, user attack and release, fixed (not program-dependent) timing. The Basic one-fader control works like the single compression fader of Waves R-Vox: one control sets how hard it works, with bounded automatic makeup gain. Audition plays the detector signal (latched, amber "AUDITION" tag, Escape exits).

Advanced Compression display: a scrolling level view (input level as a translucent area, output level as a brighter area, gain reduction as a line hanging from the top, threshold as a draggable dashed line), a small knee/transfer-curve inset, and IN / GR / OUT meters on the right. No separate gain-reduction history graph.

Basic "Compression" control = `lv_thresh` displayed as `Compression % = −thresh / 50 × 100` (default −14 dB = 28 %). The readout shows both: "28 % (−14.0 dB)".

Auto makeup (exact behavior): makeup = `min(12 dB, 0.5 · G)`, where `G` is the static gain reduction the curve applies to a 0 dBFS signal: `G = min(lv_range, (0 − thresh) · (1 − 1/ratio))`, with the knee applied. It depends only on threshold, ratio, knee, and range, never on the signal, so it cannot chase syllables. It is smoothed with a 200 ms one-pole. It is capped at +12 dB. The GUI shows the current makeup value.

External sidechain: postponed (see section 9).

### 2.5 De-ess

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `ds_on` | De-Ess On | bool | | on | | B |
| `ds_thresh` | Threshold | float | −60 … 0 | −28 | dB | B (as De-ess) |
| `ds_range` | Range (max reduction) | float | 0 … 24 | 8 | dB | A |
| `ds_lookahead` | Lookahead | float | 0 … 15 | 0 (off) | ms | A |
| `ds_mode` | Mode | choice | Single Vocal, Allround | Single Vocal | | A |
| `ds_stereo_link` | Stereo Link | float | 0 … 100 | 100 | % | A |
| `ds_link_mode` | Link Mode | choice | Stereo, Mid, Side | Stereo | | A |
| `ds_sc_source` | Sidechain | choice | Internal, External | Internal | | A |
| `ds_audition_trigger` | Audition Triggering (hear only what is removed) | bool | | off | | A (non-auto) |
| `ds_oversample` | Oversampling | choice | Off, 2x, 4x | Off | | A |
| `ds_detect` | Detection | choice | Voice Focus, Full Band | Voice Focus | | A |
| `ds_det_lo` | Detector Low Edge | float, log | 1000 … 16000 | 3500 | Hz | A |
| `ds_det_hi` | Detector High Edge | float, log | 2000 … 20000 | 8600 | Hz | A |
| `ds_process` | Processing | choice | Split Band, Wideband | Split Band | | A |
| `ds_split_freq` | Split Frequency | float, log | 2000 … 16000 | 4500 | Hz | A |
| `ds_listen` | Detector Listen | bool | | off | | A (non-auto) |

- Detector center = √(lo · hi) = 5.49 kHz by default. Width (octaves) = log2(hi / lo) = 1.3 oct. Advanced view lets the user drag the region (moves both edges) or each edge; there are no separate center/Q parameters, so no duplicate state.
- Voice Focus: detector = band-pass (lo…hi, 12 dB/oct each side) and a 0.3 ms peak follower. Full Band: detector = high-pass at `ds_det_lo` only, for non-vocal sources.
- The detector filters only feed the detector. They do not filter the audio.
- Basic "De-ess" control = `ds_thresh` displayed as `De-ess % = −thresh / 60 × 100` (default −28 dB = 47 %). Range, attack, release, and filters stay at their own values.
- No user attack/release: timing is automatic (0.3 ms attack, program-dependent 30–80 ms release).
- Lookahead and oversampling are off by default; each adds latency that is reported to the host and shown in the header badge.
- Advanced view sets the trigger range with a horizontal two-handle frequency slider (2–20 kHz, as in Pro-DS) over a live spectrum of the sidechain; dragging the band moves both edges. In Split Band processing the split frequency follows the low edge automatically (`ds_split_freq` is removed). A circular detector meter surrounds the Threshold knob. Functionality modelled on the FabFilter Pro-DS manual; own implementation and names.
- Internal ratio is fixed at 6:1; the gain reduction is `min(range, over · (1 − 1/6))`. Documented in the tooltip.
- Split Band: the signal is split complementarily (`high = x − LP(x)`, so `low + high = x` exactly) at `ds_split_freq`; only `high` is attenuated. Wideband: the whole signal is attenuated.
- Stereo: detector is linked (max of channels). Unlinked mode is postponed.

### 2.6 Multiband (default: one low-mid band, 100–500 Hz)

Global:

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `mb_on` | Multiband On | bool | | off | | B |
| `mb_count` | Bands | int | 1 … 6 | 1 | | A |
| `mb_slope` | Crossover Slope | choice | 6, 12, 24 | 12 | dB/oct | A |
| `mb_detector` | Detector | choice | Peak, Smooth | Smooth | | A |
| `mb_oversample` | Oversampling | choice | Off, 2x, 4x | Off | | A |
| `mb_lookahead` | Lookahead | choice | Off, 1, 3, 5 ms | Off | | A |

Per band (N = 1..6; band 1 defaults shown, bands 2–6 are created on demand):

| ID | Name | Type | Range | Default (band 1) | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `mb<N>_lo` | Low Edge | float, log | 20 … 16000 | 100 | Hz | A |
| `mb<N>_hi` | High Edge | float, log | 40 … 20000 | 500 | Hz | A |
| `mb<N>_thresh` | Threshold | float | −60 … 0 | −24 | dB | B (band 1) |
| `mb<N>_ratio` | Ratio | float | 1 … 10 | 2 | :1 | A |
| `mb<N>_attack` | Attack | float, log | 1 … 100 | 10 | ms | A |
| `mb<N>_release` | Release | float, log | 20 … 1000 | 150 | ms | A |
| `mb<N>_knee` | Knee | float | 0 … 24 | 6 | dB | A |
| `mb<N>_range` | Range (max reduction) | float | 0 … 24 | 6 | dB | A |
| `mb<N>_makeup` | Auto Makeup | bool | | off | | A |
| `mb<N>_solo` | Band Solo | bool | | off | | A (non-auto) |

Pro-MB–based additions (own implementation): per band `mb<N>_mode` (Compress, Expand), `mb<N>_range` becomes −24 … +24 dB (negative = downward, positive = upward), `mb<N>_gain` (band output gain, ±24 dB), `mb<N>_lookahead`, `mb<N>_trigger` (Band, Free with its own sidechain range), `mb<N>_sc_source` (Internal, External), `mb<N>_stereo_link`; Attack/Release shown as 0–100 % (program dependent); global `mb_processing` (Minimum phase default, zero latency; Linear phase adds latency).

Bands may not overlap; the DSP keeps each band's high edge ≥ 1.5 × its low edge without rewriting stored values. Each band is extracted as `LP_hi(HP_lo(x))` and processed as `x − band · (1 − g)`, so at 0 dB reduction the output equals the input exactly; regions outside every band pass untouched. Zero latency unless lookahead or oversampling is on. The Advanced view has no transfer-curve graph.

### 2.7 Resonance (resonant suppressor)

Follows the Resonant Suppressor description supplied by the user (soft/hard modes, depth curve, side panel). Stereo only in version 1; multichannel (up to 9.1.6) is postponed.

Main controls:

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `rs_on` | Resonance On | bool | | off | | B |
| `rs_mode` | Mode | choice | Soft (adaptive threshold), Hard (level-dependent) | Soft | | A |
| `rs_depth` | Depth | float | 0 … 20 (relative; up to 40 dB of reduction at maximum) | 4 | | B (lightning slider) |
| `rs_detail` | Detail | float | 0 … 100 | 50 | % | A |
| `rs_attack` | Attack | float | 0 … 100 (frequency-dependent time) | 50 | % | A |
| `rs_release` | Release | float | 0 … 100 (frequency-dependent time) | 50 | % | A |
| `rs_mix` | Mix | float | 0 … 100 | 100 | % | A |
| `rs_out_gain` | Out Gain (after Mix) | float | −12 … +12 | 0 | dB | A |
| `rs_delta` | Delta | bool | | off | | A |
| `rs_bypass` | Bypass (internal, keeps processing for glitch-free A/B) | bool | | off | | A |
| `rs_quality` | Quality | choice | Normal, High, Ultra | Normal | | A |
| `rs_low_latency` | Low Latency Mode | bool | | **on** | | A |
| `rs_linear_phase` | Linear phase | bool | | off | | A |
| `rs_sc` | Sidechain (SC) | bool | | off | | A |
| `rs_sc_listen` | Sidechain listen | bool | | off | | A (non-auto) |

Side panel (collapsible; indicator lights show settings that differ from their defaults):

| ID | Name | Type | Range | Default | Unit |
| --- | --- | --- | --- | --- | --- |
| `rs_stereo_mode` | Stereo Mode | choice | Left/Right, Mid/Side | Mid/Side | |
| `rs_link` | Link | float | 0 … 100 | 100 | % |
| `rs_focus` | Stereo Focus | float | −100 … +100 | 0 | |
| `rs_detail_tilt_lo` / `rs_detail_tilt_hi` | Detail Tilt low (< ~500 Hz) / high (> ~2 kHz) | float | −100 … +100 | 0 | |
| `rs_attack_tilt_lo` / `rs_attack_tilt_hi` | Attack Tilt low / high | float | −100 … +100 | 0 | |
| `rs_release_tilt_lo` / `rs_release_tilt_hi` | Release Tilt low / high | float | −100 … +100 | 0 | |
| `rs_max_cut` | Max Cut | float | 0 … 40, Off | Off | dB |
| `rs_wet_trim` | Wet Trim (before Mix) | float | −12 … +12 | 0 | dB |

Depth Curve bands (K = 1..8, created by double-clicking the Reduction Graph; band 1 is not created by default):

| ID | Name | Type | Range | Default |
| --- | --- | --- | --- | --- |
| `rs_b<K>_used` | Band exists | bool | | off |
| `rs_b<K>_on` | Enable/Bypass | bool | | on |
| `rs_b<K>_shape` | Shape | choice | Low cut, Low shelf, High shelf, High cut, Bell, Bandpass, Band reject, Tilt | Bell |
| `rs_b<K>_freq` | Frequency | float, log | 20 … 20000 Hz | where created |
| `rs_b<K>_depth` | Band Depth (boost = more suppression) | float | −24 … +24 dB | 0 |
| `rs_b<K>_q` | Q or Slope | float / choice | 0.1 … 10 / 6 … 48 dB/oct | 1.0 / 12 |
| `rs_b<K>_focus` | Band Focus | float | −100 … +100 | 0 |
| `rs_b<K>_listen` | Band Listen (plays the band's delta) | bool | | off (non-auto) |

Display: Reduction Graph (frequency horizontally, reduction in dB vertically, Max Cut as a shaded limit, reflects Mix) with the Depth Curve overlaid. With unlinked stereo the curve and graph split in two (white = left/mid, dark = right/side).

Latency: minimum phase by default. Low Latency Mode (default on) uses reduced time resolution and no lookahead: 0 samples at 44.1/48 kHz, about 1 ms at higher rates. Normal/High/Ultra quality without Low Latency, and Linear phase, add a fixed reported latency (measured in their phase). The implementation is original; no third-party code is used.

### 2.7b Non-parameter state added

EQ: frequency scale Hz / Piano (shows note names and lets band frequencies snap to notes), EQ Match state (reference source: sidechain input or a captured spectrum, learn/apply, amount, up to 6 generated bands written as normal EQ bands).

### 2.8 Non-parameter state saved with the session

`ui_view` (Basic/Advanced), `ui_module` (selected module), `ui_scale` (75–200 %), analyzer settings, EQ zoom/scroll, A/B slot contents and active slot, preset name and "modified" flag.

### 2.9 Effect sends (implemented)

Four sends: Reverb, Delay, Widener, Flanger (2.14). They are sends, not inserts. Each one taps the vocal, scales the tap by its send level, runs a 100 % wet effect, and adds the return to the dry vocal on the main output. The dry vocal is never processed by a send. No send has a dry/wet Mix control, because a send level already sets the blend.

Routing (decided with the user: returns are summed into the plug-in's own output; a single plug-in has no host aux buses):

```
 vocal ──► In Gain ──► [channel modules] ──●── Out Gain ──●──────────────────────────► (+) ──► Output
                                    pre-fader tap      post-fader tap                    ▲
                                           └──── Tap (per send) ──► × send level ──► effect (100 % wet) ──► return guard ──┘
```

- **Tap**: Post-fader (default) follows Output Gain. Pre-fader taps before Output Gain. Until the channel modules exist, the pre-fader point sits directly after In Gain.
- **Send level**: Off (−60 dB is the bottom of the range and means gain 0), up to +6 dB, 20 ms smoothing. At Off the output is bit-identical to the dry signal.
- **On/Off**: Off fades the return out over 10 ms, then resets the effect and stops processing it. On fades the return in. A send that is on but receives nothing goes idle after its tail falls below −140 dBFS.
- **Return guard**: each return passes a soft limiter that is transparent below −2 dBFS and never exceeds 0 dBFS. The dry signal is not limited.
- **Latency**: 0 samples. The sends never delay the dry path. Tail length reported to the host: 30 s.
- **Mono output**: the two return channels are summed at half level.

Common IDs (`<s>` = `rv`, `dl`, `wd`):

| ID | Name | Type | Range | Default |
| --- | --- | --- | --- | --- |
| `<s>_on` | Send On | bool | | off |
| `<s>_send` | Send level | float | Off, −60 … +6 dB | rv −12, dl −15, wd −12 dB |
| `<s>_tap` | Tap point | choice | Post-fader, Pre-fader | Post-fader |
| `gl_in_gain`, `gl_out_gain` | Input / Output Gain | float | −24 … +24 dB | 0 |

#### 2.9.1 Reverb send

Twenty original algorithms on four engines. Each mode picks its engine and also sets the delay scale range, modulation type, diffusion, damping, early reflections and nonlinearity. No third-party code is used. The mode names come from the user's list, and each mode is built to its description. None of them copies another product's algorithm.

| Engine | Structure | Modes |
| --- | --- | --- |
| FDN | 8-line feedback delay network, Householder mixing, input allpass diffusion, per-line damping, optional early reflections, optional in-loop saturation and resolution reduction | Concert Hall, Bright Hall, Room, Chamber, Random Space, Chorus Space, Sanctuary, Dirty Hall, Smooth Room, Smooth Random, Chaotic Hall, Chaotic Chamber, Chaotic Neutral, Cathedral, Palace |
| Plate | figure-eight tank: two cross-coupled halves of modulated allpass, delay, damping, allpass, delay | Plate, Dirty Plate, Smooth Plate |
| Nonlin | 40 irregular taps over a length set by Size, envelope set by Attack (gated, truncated, reverse); no feedback | Nonlin |
| Ambience | early-reflection tap set plus a short FDN tail; Attack balances early (0) against late (100) | Ambience |

Modulation types: Chorus (sine per line), Random (smoothed random per line), Detune (triangle: steady pitch offsets that alternate direction between lines), Ensemble (three summed sines), Wow/Flutter (one shared tape-like drift plus a little per-line jitter). "Vintage digital" means sample-and-hold at a reduced rate and reduced bit depth inside the loop. "Saturation" means a soft clipper inside the loop.

| Mode | Family | Engine | Modulation | Character |
| --- | --- | --- | --- | --- |
| Concert Hall | Halls | FDN, large | Chorus | Density = echo density |
| Bright Hall | Halls | FDN, large | Chorus, 2.2× deeper | brighter damping |
| Plate | Plates | Plate | Chorus | bright, dense |
| Room | Rooms | FDN, small | Chorus, light | medium diffusion, early echoes, darker |
| Chamber | Rooms | FDN, medium | Random, light | dense, flat damping |
| Random Space | Spaces | FDN, very large | Random, deep | long diffusers, slow build, wide |
| Chorus Space | Spaces | FDN, very large | Chorus, deep | as Random Space |
| Ambience | Ambience | Ambience | Random, light | early/late balance via Attack |
| Sanctuary | Vintage | FDN | Detune | distinct spaced early reflections, fast build, 20 kHz / 13-bit |
| Dirty Hall | Dirty | FDN, large | Random | saturation, 14 kHz / 11-bit, darker |
| Dirty Plate | Dirty | Plate | Random | saturation, 14 kHz / 11-bit, extra-wide |
| Smooth Plate | Smooth | Plate | Random, very light | high diffusion |
| Smooth Room | Smooth | FDN, small | Random, very light | high diffusion |
| Smooth Random | Smooth | FDN, 0.2–2.0× scale | Random | Size spans small to large spaces |
| Nonlin | Effects | Nonlin | none | Size = length, Attack = envelope |
| Chaotic Hall | Chaotic | FDN, large | Wow/Flutter | soft saturation |
| Chaotic Chamber | Chaotic | FDN, medium | Wow/Flutter | soft saturation |
| Chaotic Neutral | Chaotic | FDN, large | Wow/Flutter | no saturation, flat damping |
| Cathedral | Large | FDN, 1.6–2.4× scale | Ensemble | long diffusers, strong high-frequency roll-off |
| Palace | Palace | FDN, 0.3–1.8× scale | Chorus | early reflections, 24 kHz / 14-bit |

The UI family guide reads:
- Dirty: vintage grit and character.
- Smooth: polished vocals and natural spaces.
- Chaotic: long, animated reverbs that still sit in a mix.
- Palace: room sounds from small spaces to large halls.
- Ambience: space that is felt more than heard.

| ID | Name | Range | Default | Shown for |
| --- | --- | --- | --- | --- |
| `rv_mode` | Mode | 20 modes above | Concert Hall | all |
| `rv_decay` | Decay (RT60) | 0.2 … 20 s, log | 2.2 s | all except Nonlin |
| `rv_size` | Size | 0 … 100 % | 50 % | all |
| `rv_predelay` | Pre-delay | 0 … 250 ms | 20 ms | all |
| `rv_hicut` | High Cut (input filter and in-loop damping) | 1 … 20 kHz | 8 kHz | all |
| `rv_locut` | Low Cut | 20 … 1000 Hz | 150 Hz | all |
| `rv_mod_rate` | Mod Rate | 0.05 … 5 Hz | 0.6 Hz | all except Nonlin |
| `rv_mod_depth` | Mod Depth | 0 … 100 % | 40 % | all except Nonlin |
| `rv_density` | Density (input diffusion) | 0 … 100 % | 70 % | all |
| `rv_attack` | Attack | 0 … 100 % | 50 % | Ambience, Nonlin |

Changing the mode fades the old algorithm out over 10 ms, resets it, and starts the new one. When the send starts from silence it switches at once.

Limitations:
- **Echo density**: Concert Hall's echo density is the Density control (input diffusion strength), not a separate control.
- **Palace scale**: Palace's adjustable scale is the Size control.
- **Detune**: Sanctuary's "detuned" modulation is triangle delay modulation, so the pitch offsets are steady but flip direction each half cycle. It is not a separate pitch shifter.
- **Algorithm basis**: no mode copies a commercial algorithm. Each is built from the sonic description in the brief.

#### 2.9.2 Delay send

Original implementation. The control set follows the user-supplied echo manual.

Each side runs two delay stages in series, A then B, with feedback from the end of B into A:
- Odd repeats leave stage A and even repeats leave stage B. Making A longer and B shorter (or the other way round) gives shuffle or swing (Groove). Different output gains for A and B give alternating accents (Accent).
- Style tone and saturation sit inside each stage, so every repeat is coloured once more than the one before.
- Ping-Pong sums the input to mono. Ping comes from A on the left and Pong from B on the right, so Pong follows Ping by the Pong time.
- Feel shifts every output read by a fixed time, so all echoes drag or rush without changing the repeat spacing.
- Feedback is capped at a loop gain of 0.97. The styles' saturation and the return guard bound the level.

| ID | Name | Range | Default | Shown in |
| --- | --- | --- | --- | --- |
| `dl_mode` | Mode | Single, Dual, Ping-Pong | Single | all |
| `dl_style` | Style | Clean Digital, Studio Tape, Worn Tape, Analog Bucket, Lo-Fi Radio, Diffused | Studio Tape | all |
| `dl_t1_unit` / `dl_t2_unit` | Echo 1 / 2 unit | Time, Note, Dot, Trip | Note | Echo 2: Dual, Ping-Pong |
| `dl_t1_ms` / `dl_t2_ms` | Echo 1 / 2 time | 1 … 2500 ms | 375 / 500 ms | when unit = Time |
| `dl_t1_note` / `dl_t2_note` | Echo 1 / 2 note | 1/2 … 1/64 | 1/8 / 1/4 | when unit = Note/Dot/Trip; host tempo (120 BPM without one) |
| `dl_feedback` | Feedback | 0 … 100 % | 30 % | all |
| `dl_locut`, `dl_hicut` | Low / High Cut (every repeat) | 20 … 2000 Hz / 1 … 20 kHz | 150 Hz / 6 kHz | all |
| `dl_saturation` | Saturation (style-dependent) | 0 … 100 % | 25 % | all |
| `dl_width` | Width (above 75 % adds out-of-phase spread) | 0 … 100 % | 50 % | all |
| `dl_offset` | L/R Offset | 0 … 25 ms | 8 ms | Single, Dual |
| `dl_accent` | Accent (Echo 1) | −100 … +100 | 0 | Single, Dual |
| `dl_accent2` | Accent 2 | −100 … +100 | 0 | Dual |
| `dl_balance` | Balance | −100 … +100 | 0 | Dual, Ping-Pong |
| `dl_fb_mix` | Feedback Mix (0 independent, 50 equal, 100 crossed) | 0 … 100 % | 0 | Dual |
| `dl_fb_bal` | Feedback Balance | −100 … +100 | 0 | Dual |
| `dl_groove` | Groove (− shuffle, + swing; full = triplet) | −100 … +100 | 0 | all |
| `dl_feel` | Feel (+ drag, − rush) | −50 … +50 ms | 0 | all |
| `dl_prime` | Prime Numbers (echo times rounded to prime sample counts) | off/on | off | all |
| `dl_wobble`, `dl_wobble_rate`, `dl_wobble_shape`, `dl_wobble_sync` | Wobble depth, rate, shape (Sine, Triangle, Square, Random Walk, Random S/H), sync (− drift apart, + opposed phase) | 0 … 100 %, 0.05 … 10 Hz | 0 %, 1 Hz, Sine, 0 | all |
| `dl_diffusion`, `dl_diff_size`, `dl_diff_loop` | Diffusion amount, size, position (Post / Loop) | 0 … 100 %, 0 … 100 %, Post/Loop | 0, 50 %, Post | all |

Style names are descriptive, own names, not hardware names.

Not implemented, and not shown in the UI:
- Rhythm mode (16-tap pattern editor, Shape, Repeats, Pan Shape, Warp, Grid, Length).
- The Style Editor (3-band EQ with per-repeat Gain/Decay).
- Tap Tempo and the MIDI clock switch. Note sync uses the host tempo instead.
- Separate Input/Output level and the echo-manual Mix knob. The send level and the 100 % wet return replace them.
- The Out/FB wobble switches. Wobble always acts on the delay reads inside the loop.
- Decay Sat / Out Sat saturation types.

#### 2.9.3 Widener send

`wd_type` selects MicroShift or SideWidener. They are separate algorithms: only the selected one runs, and a switch fades the old one out over 10 ms, resets it, then starts the new one. They are never layered.

MicroShift follows the user-supplied MicroShift manual:
- The left side is shifted up and the right side down by a few cents that vary continuously. Each side also gets a short delay that varies continuously.
- Pitch shifting uses two crossfaded delay taps.
- Style I: moderate variation and soft saturation.
- Style II: more delay variation and a darker, thinner response (150 Hz–10 kHz).
- Style III: much wider, randomly wandering delay variation, harder asymmetric saturation, and a short trapezoid crossfade (hard de-glitch).
- Focus is a 24 dB/oct crossover. Only content above it is widened and returned.
- The manual's Mix control is left out on purpose: the return is 100 % wet and the send level sets the blend.
- The band below Focus is not returned, because the dry vocal already carries it.

| ID | Name | Range | Default |
| --- | --- | --- | --- |
| `wd_ms_style` | Style | I, II, III | I |
| `wd_ms_detune` | Detune (100 % = style amount) | 0 … 200 % | 100 % |
| `wd_ms_delay` | Delay (100 % = style amount) | 0 … 200 % | 100 % |
| `wd_ms_focus` | Focus | 20 Hz … 10 kHz | 20 Hz |

SideWidener follows the user-supplied SideWidener manual:
- A decorrelated copy of the mid signal is returned as pure side (left +, right −), so the return's mono sum is exactly zero and the mono mix is unchanged.
- Mode 1: one short tap, no time smear.
- Mode 2: four alternating-sign taps.
- Mode 3: a six-allpass diffusion network (room-like smear).
- Tone moves the widened band from 350 Hz–4.5 kHz (0) to full range (100).
- The manual's Bypass is the send's On switch.
- The manual's double-click and right-click typing, Ctrl/Cmd-click reset and Shift fine control are covered by the shared control rules in section 7.1. Double-click resets and right-click opens Enter Value.

| ID | Name | Range | Default |
| --- | --- | --- | --- |
| `wd_sw_width` | Width | 0 … 100 | 50 |
| `wd_sw_mode` | Mode | Mode 1, Mode 2, Mode 3 | Mode 1 (the manual gives no default) |
| `wd_sw_tone` | Tone | 0 … 100 | 50 (the manual gives no default) |
| `wd_sw_output` | Output | −inf … 0 dB | 0 dB |

#### 2.9.4 UI

- **Basic view**: one compact strip per send with ON, a send-level knob, the current mode/style/type, a return meter and an Advanced button that opens that send's panel.
- **Advanced view**: tabs Reverb / Delay / Widener. Each panel starts with ON, Send, Tap point and the return meter, followed by the effect controls.
- **Controls with no effect are hidden**: per reverb mode, per delay mode and time unit, and per widener type.
- Every control has an accessible name, a tooltip with a one-sentence description, and the shared editing rules from section 7.1.
- View and open panel are saved with the session (`ui_view`, `ui_send`).
- Preview canvas: the header has the chain preset menu, and each Advanced module and send panel has its own preset menu (section 2.10). The Basic view gets a SENDS row under the module row (ON, level knob, mode, return meter, ADV link). The Advanced view gets a SENDS tab after the module tabs, with Reverb / Delay / Widener sub-tabs. The Widener has one artboard for each type.

#### 2.9.5 Tests (`KaminariVocal/Tests/Tests.cpp`)

- **Silent cases**: default state, and each send on at Off, give output bit-identical to the input.
- **Level and isolation**: raising each send raises only its return; the other two returns stay exactly zero.
- **Dry path**: the dry path is unchanged.
- **Pre/post fader**: pre-fader ignores Output Gain; post-fader follows it.
- **Bypass**: switching a send off gives dry-only output after the fade.
- **Clipping**: full-scale input with all sends at +6 dB, 100 % feedback and 20 s decay keeps every return ≤ 0 dBFS.
- **Reverb**: all 20 modes give finite, distinct impulse responses. Decay, Nonlin Attack and Ambience Attack each change the sound.
- **Delay**:
  - A 1/4 note at 120 BPM gives a 500 ms echo.
  - 0 % feedback gives a single echo.
  - Ping-Pong alternates left and right.
  - 100 % feedback stays bounded.
  - Groove moves the echoes.
- **Widener**:
  - The SideWidener mono sum is zero.
  - MicroShift and SideWidener differ.
  - Focus, Detune and Delay each change the return.
  - Switching type replaces the algorithm rather than layering.
- **Persistence**: parameter and UI-state round trip.
- **Editor**: views, Advanced buttons, hidden controls per mode, accessible names and tooltips.

#### 2.9.6 Reverb and Delay returns: EQ, ducking, routing (implemented)

Per return (`rv_` and `dl_` prefixes; part of each send's module presets):

| ID | Name | Range / choices | Default |
|---|---|---|---|
| `xx_eqN_used`, `_on`, `_type`, `_freq`, `_gain`, `_q`, `_slope` (N = 1..8) | Return EQ bands: the main EQ's layout (2.19) | as the main EQ | unused |
| `xx_duck_on` | Ducking | off / on | off |
| `xx_duck_thresh` | Threshold | −60 … 0 dB | −30 dB |
| `xx_duck_depth` | Depth (full depth 6 dB above the threshold) | 0 … 30 dB | 9 dB |
| `xx_duck_attack`, `xx_duck_release` | Attack, Release | 0.1 … 200 ms, 10 … 2000 ms | 10 ms, 250 ms |
| `xx_duck_source` | Source | Vocal (processed, what you hear), Raw Input (before the channel modules) | Vocal |
| `xx_wet_gain` | Wet Gain (after ducking) | −24 … +12 dB | 0 dB |

Global (chain presets only): `fx_route` Off / Delay into Reverb / Reverb into Delay (default Off) and `fx_route_amt` 0 … 100 % (default 30 %). One direction at a time, so the two returns can never form a loop. The source return is taken after its EQ and before ducking, follows its on/off fade, and is guarded like any return.

Order per return: effect → EQ (band solo replaces the output with the band's region, taken from the EQ's input) → routing tap → ducking → wet gain → on/off fade → return guard. The Sends page has Sound / EQ / Duck & Route views for Reverb and Delay; the EQ view is the shared EQ editor (2.19) with Before / After analyzer toggles.

### 2.10 Presets (implemented for the sends; data for every module)

Two levels, both with factory and user presets:

- **Module presets**:
  - Each Advanced module panel, including each send panel, has its own preset bar: previous, name menu, next, Save.
  - A module preset sets only that module's sound parameters. Anything it does not list returns to its default.
  - It never changes the module's On switch, a send's On, level or tap, or Tune's Key, Scale, Range and note map, because those belong to the song or the routing.
- **Chain presets**:
  - The header holds a chain preset bar, visible in both Basic and Advanced view. It is how the Basic view gets factory presets.
  - A chain preset picks one module preset per module. It then sets its overrides (module and send On switches, send levels), and every other global parameter returns to its default.
  - A new instance starts on the "Default" chain preset.
- **Modified flag**: a name shows `*` when any parameter in its scope differs from the values right after loading.
- **Storage**:
  - Factory data lives in `KaminariVocal/Presets/factory.json`, embedded in the plug-in.
  - User presets are `.kvpreset` XML files in `~/Library/Audio/Presets/Kaminari Audio/Kaminari Vocal/<Module>/` and `.../Chains/` on macOS.
  - Choice values are stored by name, so reordering a choice list does not break presets.
  - The loaded preset names are saved with the session.
- **Unbuilt modules**: parameters of modules that are not built yet are skipped when loading. Their presets are already defined, so they work as soon as each module exists.

**Tune** (6): Natural (Gentle correction that keeps the singer's movement); Subtle Polish (Slow, almost invisible correction for good takes); Tight Pop (Fast correction for modern pop leads); Hard Tune (Instant pitch snapping, the classic hard-tuned effect); Melodic Rap (Very fast with a little humanize on held notes); Slow Glide (Audible, smooth glides between notes).

**EQ** (9): Flat (No bands); Vocal Clean-up (Low cut plus a small dip in the low mids); Presence (Low cut and a broad lift around 3 kHz); Air (High shelf for breath and sheen); De-mud (Cuts boxiness and boom in the low mids); Warmth (Gentle low shelf lift and slightly softer top); Bright Pop (Clean-up, presence and air together); Podcast Voice (Clear speech: low cut, de-mud, presence and a little air); Telephone (Narrow band-limited effect voice).

**Multiband** (6): Low-Mid Control (The default: one band, 100-500 Hz); Proximity Tamer (Controls bass build-up from singing close to the mic); Box Remover (Dynamic cut of boxy 300-700 Hz only when it builds up); Harshness Control (Tames 2.5-6 kHz on loud, edgy notes); Even Vocal 3-Band (Low-mid, harshness and top-end control together); Upward Air (Brings up quiet breath and detail above 8 kHz).

**Compression** (7): Clean Vocal (Transparent leveling, the default); Smooth Leveler (Slow, soft, opto-like riding of the level); Upfront Pop (Holds a pop lead firmly in front of the mix); Rap Punch (Fast, punchy control that keeps consonants clear); Parallel Crush (Heavy compression blended with the dry vocal); Gentle Glue (Light, wide-knee compression); Broadcast (Dense, consistent level for speech; uses lookahead).

**De-ess** (7): Standard (The default for most voices); Gentle (Light touch for already smooth recordings); Bright Singer (Strong reduction for very sibilant voices); Higher Voice (Detection range shifted up for high voices); Lower Voice (Detection range shifted down for low voices); Wideband Soft (Turns the whole vocal down slightly on esses); Harsh S Fix (Deep, focused reduction for piercing esses).

**Resonance** (6): Gentle Smooth (Light, general smoothing (low latency)); Vocal Harshness (Targets ringing in the upper mids); Boxy Room (Reduces room and box resonances around 400 Hz); Mic Ringing (Hard mode with high detail for narrow, steady rings); Airy Polish (Smooths the top end, more above 2 kHz); Transparent (Subtle smoothing, blended at 70 %).

**Reverb** (11): Vocal Plate (Classic bright plate for lead vocals); Short Room (Small, natural room to place the voice in a space); Big Ballad Hall (Large, lush hall with a long pre-delay); Air Ambience (Felt more than heard: subtle space and air); Smooth Plate (Clear, polished plate); Vintage Hall (Gritty, warm vintage-digital hall); Gated Vocal (Short gated burst); Reverse Swell (Rising, reverse-style envelope); Animated Wash (Long, moving chaotic hall that still sits in a mix); Cathedral Pad (Huge, open space with a very long decay); Palace Room (Realistic small-to-medium room character).

**Delay** (9): Slapback (Single 110 ms tape slap, no repeats); 1/8 Throw (Tempo-synced eighth-note echoes); 1/4 Dotted (Dotted quarter echoes that fill gaps between phrases); Ping-Pong 1/8 (Eighth notes bouncing left and right); Wide Doubler (Two short, different echo times for width); Worn Tape Echo (Wobbly, dark tape repeats); Lo-Fi Radio Echo (Narrow, crunchy eighth-note echoes); Ambient Diffuse (Smeared, reverb-like quarter-note echoes); Swing 1/8 (Eighth-note echoes with a swing feel).

**Widener** (7): MicroShift Classic (Style I at its own amounts); Subtle Doubler (Light Style II thickening that keeps the low end tight); Huge Width (Style III, more detune and delay); Vocal Air (Widens only the presence and air above 2 kHz); Side Subtle (SideWidener Mode 1, midrange-focused, mono safe); Side Wide (SideWidener Mode 2, full range); Side Room (SideWidener Mode 3: widest, with a room-like smear).

**Chain presets** (11):

| Chain | Description | Module presets (Tune / EQ / Multiband / Compression / De-ess / Resonance / Reverb / Delay / Widener) | Sends on |
| --- | --- | --- | --- |
| Default | Neutral starting point. Sends off. | Natural / Vocal Clean-up / Low-Mid Control / Clean Vocal / Standard / Gentle Smooth / Vocal Plate / 1/8 Throw / MicroShift Classic | none |
| Pop Lead | Bright, tight, upfront lead with plate and eighth-note throws. | Tight Pop / Bright Pop / Low-Mid Control / Upfront Pop / Bright Singer / Vocal Harshness / Vocal Plate / 1/8 Throw / Vocal Air | Reverb -14 dB, Delay -20 dB, Widener -16 dB |
| R&B Smooth | Warm, smooth leveling with a lush hall. | Natural / Warmth / Proximity Tamer / Smooth Leveler / Standard / Gentle Smooth / Big Ballad Hall / 1/4 Dotted / Subtle Doubler | Reverb -15 dB, Delay -22 dB |
| Rap Vocal | Punchy and dry with a short room. | Melodic Rap / Presence / Proximity Tamer / Rap Punch / Standard / Gentle Smooth / Short Room / Slapback / Subtle Doubler | Reverb -22 dB, Delay -24 dB |
| Melodic Rap | Fast tuning, punchy compression, ping-pong throws. | Melodic Rap / Bright Pop / Low-Mid Control / Rap Punch / Standard / Vocal Harshness / Smooth Plate / Ping-Pong 1/8 / Vocal Air | Reverb -18 dB, Delay -20 dB, Widener -18 dB |
| Hard Tune | Instant pitch snapping with a wide plate. | Hard Tune / Bright Pop / Low-Mid Control / Upfront Pop / Standard / Gentle Smooth / Smooth Plate / 1/4 Dotted / MicroShift Classic | Reverb -16 dB, Delay -22 dB, Widener -18 dB |
| Rock Grit | Mid-forward with tape echo and a vintage hall. | Subtle Polish / Presence / Harshness Control / Parallel Crush / Gentle / Vocal Harshness / Vintage Hall / Worn Tape Echo / Huge Width | Reverb -18 dB, Delay -20 dB |
| Ballad Air | Open and breathy with a long hall. | Natural / Air / Upward Air / Smooth Leveler / Bright Singer / Airy Polish / Big Ballad Hall / 1/4 Dotted / Side Subtle | Reverb -12 dB, Delay -24 dB, Widener -14 dB |
| Podcast Voice | Clear, even speech. No tuning or sends. | Natural / Podcast Voice / Proximity Tamer / Broadcast / Standard / Boxy Room / Air Ambience / Slapback / Side Subtle | none |
| Backing Vocals Wide | Tucked-back, wide stacks. | Tight Pop / Vocal Clean-up / Even Vocal 3-Band / Gentle Glue / Harsh S Fix / Transparent / Smooth Plate / Wide Doubler / Huge Width | Reverb -14 dB, Delay -18 dB, Widener -10 dB |
| Lo-Fi Phone | Telephone tone with crunchy echoes. | Natural / Telephone / Low-Mid Control / Parallel Crush / Gentle / Gentle Smooth / Short Room / Lo-Fi Radio Echo / Side Subtle | Reverb -20 dB, Delay -16 dB |

### 2.11 Versioning and updates (implemented)

- **Saved with every session**:
  - `state_version`, currently 2. Sessions saved before versioning read as 1.
  - `engine_<module>` for all nine modules, currently 1.
- **Changing what an existing setting means**: raise `state_version` and convert older sessions in `setStateInformation`.
- **Improving an algorithm audibly**: raise that module's engine version and keep the old algorithm for sessions that saved the old number.
- **Never changed after the first shared build**:
  - The plug-in codes `Kmni`/`KmVc`.
  - The bundle ID.
  - Parameter IDs.
  - Parameter ranges and skews.
- **New parameters**: they get version hint 2 in the next release, then 3, and so on. AUv2 hosts that index parameters by position (Logic, GarageBand) need this to recall automation.
- **Updating**: raise `project(... VERSION ...)` in `CMakeLists.txt` and rerun the installer. It upgrades in place.

### 2.12 First implementation of the channel modules (what is in, what is postponed)

Bus layouts: mono → mono, mono → stereo (the input is processed as dual mono, so the sends' stereo returns stay stereo), and stereo → stereo.

Reported latency:
- Tune's fixed 96 samples at 48 kHz, always reported, also with Tune off.
- Plus the Compression lookahead and the De-ess lookahead.
- Changes are reported to the host from the message thread.

| Module | Implemented | Postponed |
| --- | --- | --- |
| Tune | YIN pitch detection on a decimated copy, nearest note of key/scale (or the 12 note switches when Scale = Custom) with hysteresis, Retune Speed, Humanize on held notes, period-locked delay-line shifter around a fixed 96-sample delay | formant handling, measured delay statistics per range (Phase 6), lower base latency trials |
| EQ | 8 bands, 9 shapes, cut slopes 6–48 dB/oct, smoothed coefficients, output gain, interactive graph | dynamic EQ, per-band stereo placement, auto gain, gain scale, natural/linear phase, analyzer, EQ Match, piano scale |
| Multiband | 1–6 bands, compress/expand, downward/upward range, per-band gain and solo, 6/12/24 dB/oct band filters, peak/smooth detector | lookahead, oversampling, free trigger range, sidechain, linear phase |
| Compression | 5 styles, threshold, ratio, attack, release, auto release, knee, range, hold, lookahead, peak/smooth detector, mix 0–200 %, dry, wet gain, side-chain level, stereo link, auto gain (`lv_auto_gain`, new ID), output | 8-band detector EQ, external sidechain, oversampling, audition |
| De-ess | threshold, range, detection range, Voice Focus / Full Band, Split Band / Wideband, Single Vocal / Allround, lookahead, stereo link, Stereo/Mid/Side, detector listen, audition | oversampling, external sidechain |
| Resonance | Soft/Hard, Depth, Detail, Attack, Release, Mix, Out Gain, Delta, Bypass, Quality (band density), L/R and M/S, Link, Focus, the six tilts, Max Cut, Wet Trim, 8 depth-curve bands, reduction graph | non-low-latency and linear-phase modes, sidechain, band focus/listen |

CPU: the whole chain with every module and send on (Resonance at Ultra) uses about 8 % of one 2.1 GHz Xeon core at 48 kHz. Measure again on the target Intel Mac.

---

### 2.13 Distortion (implemented)

Inline channel module between Compression and De-ess (index `ModDistortion`; IDs `dt_*`). Off by default; while off it is skipped entirely and adds no latency.

| ID | Name | Range / choices | Default |
|---|---|---|---|
| `dt_on` | Distortion On | bool | off |
| `dt_style` | Style | Tape, Tube, Warm, Fuzz, Clip, Lo-Fi | Tape |
| `dt_drive` | Drive | 0 … 36 dB | 9 dB |
| `dt_tone` | Tone (tilt around 1 kHz, ±6 dB) | −100 … +100 | 0 |
| `dt_bias` | Bias (asymmetry, even harmonics) | 0 … 100 % | 0 % |
| `dt_lowcut` | Low Cut before the curve | Off (20 Hz) … 1 kHz | Off |
| `dt_crush` | Crush (Lo-Fi only: 16 → 4 bits, 1 → 16 sample hold) | 0 … 100 % | 40 % |
| `dt_mix` | Mix (dry is latency-aligned) | 0 … 100 % | 100 % |
| `dt_out` | Output | −24 … +12 dB | 0 dB |
| `dt_auto_gain` | Auto Gain (output RMS follows input RMS, ~400 ms, −24 … +12 dB) | bool | on |
| `dt_os` | Oversampling (linear-phase half-band FIR, integer latency) | Off, 2x, 4x | 2x |

Latency while on: 49 samples at 48 kHz for 2x (measured by the tests), more for 4x, 0 for Off. The Basic card's hammer sets Drive (up = more) and shows "SAT": how far the driven peak goes above the curve's knee.

### 2.2.1 Tune: vibrato and tremolo (implemented)

Part of Tune (needs Tune on). `tn_correct` (Correct Pitch, default on) switches retuning off while keeping vibrato and tremolo; Retune Speed and Humanize are hidden while it is off.

| ID | Name | Range / choices | Default |
|---|---|---|---|
| `tn_vib_on` | Vibrato On | bool | off |
| `tn_vib_depth` | Depth (peak, cents) | 0 … 100 | 30 |
| `tn_vib_rate` | Rate | 1 … 12 Hz | 5.5 Hz |
| `tn_vib_delay` | Onset Delay after each new note | 0 … 1500 ms | 250 ms |
| `tn_vib_rise` | Onset Rise to full depth | 0 … 1500 ms | 300 ms |
| `tn_vib_variation` | Variation (rate ±25 %, depth ±40 % wander) | 0 … 100 % | 20 % |
| `tn_trem_on` | Tremolo On | bool | off |
| `tn_trem_depth` | Depth (100 % = to silence at the trough) | 0 … 100 % | 40 % |
| `tn_trem_rate` | Rate (Sync = Free) | 0.5 … 20 Hz | 5 Hz |
| `tn_trem_sync` | Sync | Free, 1/2, 1/4, 1/4 dot, 1/4 trip, 1/8, 1/8 dot, 1/8 trip, 1/16, 1/16 trip | Free |
| `tn_trem_shape` | Shape | Sine, Triangle, Square (rounded) | Sine |
| `tn_trem_stereo` | Stereo phase (180° = auto-pan) | 0 … 180° | 0° |
| `tn_trem_onset` | Tremolo follows the note onset | bool | off |

Vibrato is added to the correction on voiced notes only and goes through the same period-jump shifter, so it adds no latency. A new note (target change or a gap in voicing) restarts the onset. Synced tremolo locks its phase to the host's beat position while the transport runs.

### 2.14 Flanger (implemented, in the channel chain)

A channel module between Compression and Distortion (it was a send in the first version). It processes the vocal in place and mixes its output with the dry signal (`fl_mix`), so the comb forms inside the module and the result is the same with or without the sends. It has no latency, crossfades for 10 ms when switched on or off, and restarts its sweep from a clean state when switched on again. Presets: module scope `flanger` (everything but `fl_on`).

| ID | Name | Range / choices | Default |
|---|---|---|---|
| `fl_on` | Flanger On | off / on | off |
| `fl_mix` | Mix | 0 … 100 % | 50 % |
| `fl_rate` | Rate (Sync = Free) | 0.02 … 10 Hz | 0.3 Hz |
| `fl_sync` | Sync | Free, 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8 | Free |
| `fl_depth` | Depth (sweep up to +6 ms above Delay) | 0 … 100 % | 60 % |
| `fl_delay` | Delay (shortest delay of the sweep) | 0.1 … 10 ms | 1.5 ms |
| `fl_feedback` | Feedback (soft-limited in the loop) | −95 … +95 % | 40 % |
| `fl_stereo` | Stereo phase between L and R sweeps | 0 … 180° | 90° |
| `fl_shape` | Shape | Sine, Triangle | Triangle |
| `fl_hicut` | High Cut on the repeats | 1 … 20 kHz | 12 kHz |

Session state version 4: the Flanger tab sits before Distortion; older sessions' Advanced tab index is converted on load.

### 2.15 Oversampling (Multiband, De-ess, Resonance)

`mb_os`, `ds_os`, `rs_os`: Off, 2x, 4x (default Off). The module runs at the higher rate between JUCE's linear-phase half-band FIR up- and down-samplers (`kv::Oversampled`, one prepared instance per rate, so switching never allocates on the audio thread). The FIR latency is added to the plug-in's reported latency only while the module is on and oversampling; De-ess lookahead is scaled to the running rate.

### 2.16 EQ additions

- Each band's own response is shaded between its curve and 0 dB in the band's colour; curves that leave the graph simply leave it (no line along the bottom edge).
- Analyzer: Pre, Post, Both (default) or Off. Both draws the pre-EQ spectrum filled and the post-EQ spectrum as a light outline over it.
- Click on empty graph space creates a band whose type follows the frequency: below 60 Hz Low Cut, 60–150 Hz Low Shelf, 150 Hz–8 kHz Bell, 8–15 kHz High Shelf, above 15 kHz High Cut. Holding the mouse down drags the new band at once.
- Solo works for every type: around a bell, notch or band pass; below a low shelf or low cut; above a high shelf or high cut. The audition is taken from the EQ's input, so a band that cuts can still be heard.
- Keyboard under the graph (the Tune page's style): keys on the graph's frequency axis; click or drag sweeps the selected band to the note under the mouse (Shift: free sweep without note snapping).

### 2.17 Multiband additions

- Per band: `mbN_bypass` (band passes unchanged) and `mbN_mute` (band removed), as well as Solo.
- Pre / post analyzer in the display; click on empty space adds a band around the click (resets that band's settings).
- Bypassed or muted bands are drawn grey and flagged.

### 2.18 Compression side-chain detection EQ

An eight-band EQ on the detector signal only (the audio is not filtered), with the main EQ's band layout
(`lv_scN_used`, `_on`, `_type`, `_freq`, `_gain`, `_q`, `_slope`, N = 1..8), filter designs and editor. The Compression
page's DISPLAY switch shows either the level display with the compressor controls, or the side-chain EQ editor in
their place: spectrum of the main signal and of the detector signal (Main / Detector toggles), click to add a band
typed by frequency, drag, wheel = Q, band panel, keyboard sweep. Solo on a side-chain band plays the region that band
works on, taken from the compressor's input. In Vocal style the Ratio knob is shown inactive and reads Auto.

### 2.19 One EQ editor

Every EQ in the plug-in (main EQ, side-chain detection EQ, Reverb and Delay return EQs) uses the same parameters
layout, filters (`kv::EqDesign` / `kv::Equalizer`) and editor (`EqEditor`: `EqCurve` graph, `EqPiano` keyboard, floating
`EqBandPanel`, analyzer toggles, resolution and speed). Analyzer Pre and Post are two independent toggles everywhere
(also Multiband and Resonance In / Out), so both can be shown at once. The selected band's node crackles with
lightning in every EQ graph.

### 2.20 Widener algorithms (revised)

- MicroShift: one read tap per side; when the tap drifts out of range it splices one jump back, at the lag that best
  matches the outgoing waveform (normalised cross-correlation over 6 ms), with an equal-gain raised-cosine
  crossfade (Style I 12 ms, II 14 ms, III 8 ms). A steady tone keeps its level within 1 dB (the earlier two-tap
  version swung by 7 dB and flanged). Saturation is lighter (Style I 1.15, III 1.5).
- SideWidener: the side copy comes from allpass chains (Mode 1: 2 short stages, Mode 2: 4, Mode 3: 6 longer ones)
  and is at most 0.7 x the mid, so neither speaker gets an evenly spaced comb or a full cancellation.

## 3. Signal flow

```
 Input ──► [In Gain] ──► [TUNE] ──► [EQ] ──► [MULTIBAND] ──► [COMPRESSION] ──► [FLANGER] ──► [DISTORTION] ──► [DE-ESS] ──► [RESONANCE] ──► [Out Gain] ──► [Safety] ──► Output
   │                       │          │                                                                │
   ├─► In meter            │          ├─► analyzer tap (pre-EQ / post-EQ, copy only)                   ├─► Out meter
   │                       │          │                                                                │
   │                  fixed 96-sample                                                    NaN/Inf guard,
   │                  delay on dry/bypass path                                           DC blocker (5 Hz),
   │                                                                                     Clip Guard (optional)
 Each module: process ──► BypassFader (10 ms crossfade between processed and unprocessed signal)
```

Order rationale:
1. Tune first: the detector needs the cleanest, unprocessed signal; EQ boosts and compression can bias period detection.
2. EQ before dynamics: cuts and tone shaping happen first, so the compressor reacts to the corrected tone.
3. Multiband before Compression: low-mid boom and proximity effect are tamed before they drive the main compressor (the UI shows Multiband to the left of Compression).
4. Flanger, then Distortion, after Compression: the compressor evens the level, so the sweep and the saturation are consistent from word to word; saturating after the flanger colours the comb as well.
5. De-ess after Compression and Distortion: makeup gain, release and saturation can all raise sibilance; de-essing afterwards catches it.
6. Resonance last: removes harsh, ringing resonances that remain after all gain changes.
The UI's module order (Tune, Multiband, Compression, Flanger, Distortion, De-ess, Resonance) follows this order.

The order is fixed in version 1. Routing is postponed.

### 3.1 Effect sends

After the channel modules: pre-fader tap → Out Gain → post-fader tap → dry output. Each of the three sends (Reverb, Delay, Widener) taps pre- or post-fader, runs its effect 100 % wet, and its return is added to the dry output (section 2.9). The Reverb and Delay returns then pass through their return EQ, ducking and wet gain; with routing on, one return also feeds the other effect's input (section 2.9.6).

---

## 4. Latency budget

Reported latency is constant in time (2.0 ms) for the Tune module and zero for all other modules. It is reported whether or not Tune is enabled, so toggling Tune never changes host delay compensation or causes a click. The dry path inside Tune is delayed by the same amount.

| Module | 44.1 kHz | **48 kHz** | 88.2 kHz | 96 kHz | 192 kHz | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Input gain, meters | 0 | **0** | 0 | 0 | 0 | |
| Tune | 88 | **96** | 176 | 192 | 384 | fixed 2.0 ms base delay |
| EQ | 0 | **0** | 0 | 0 | 0 | minimum-phase IIR |
| Multiband | 0 | **0** | 0 | 0 | 0 | subtractive bands; lookahead/oversampling off |
| Compression | 0 | **0** | 0 | 0 | 0 | no lookahead |
| De-ess | 0 | **0** | 0 | 0 | 0 | complementary split; lookahead/oversampling off |
| Resonance (Low Latency Mode) | 0 | **0** | ≈ 1 ms | ≈ 1 ms | ≈ 1 ms | reduced time resolution, no lookahead |
| Analyzer, smoothing | 0 | **0** | 0 | 0 | 0 | analyzer reads a copy |
| Output, Safety | 0 | **0** | 0 | 0 | 0 | |
| **Total reported** | 88 | **96** | 176 | 192 | 384 | ceiling: 128 at 48 kHz (2.67 ms) |
| Reserve | | **32** | | | | for Tune tuning in Phase 6 |
| Optional: De-ess / Multiband lookahead | | + lookahead time | | | | off by default; e.g. 1 ms = +48 samples |
| Optional: oversampling 2x / 4x | | measured | | | | off by default; labeled as adding latency |
| Optional: Resonance without Low Latency, or Linear phase | | measured | | | | off by default |
| Effect sends (Reverb, Delay, Widener) | 0 | **0** | 0 | 0 | 0 | returns are added to the undelayed dry path |
| EQ Match | 0 | **0** | 0 | 0 | 0 | generates normal minimum-phase bands |

Tune variable delay: a period-based shifter repeats or drops whole pitch periods. Around the fixed 2.0 ms base, the instantaneous delay varies by up to about one pitch period (about 0–4 ms for typical vocals, similar to Waves Tune Real-Time). Phase 6 measures the average and range of this delay per vocal range and documents it. If 2.0 ms causes audible artifacts, the base may rise to at most 2.67 ms (128 samples at 48 kHz).

Lower-latency option (Phase 6): Antares describes Auto-Tune Hybrid on Avid DSP hardware as zero-latency, with a Classic mode. A period-based shifter can run with a base delay near 0 if it accepts a larger delay variation, as Waves Tune Real-Time does. Phase 6 will test base delays of 0, 32, 64, and 96 samples on real vocals. If a lower value sounds as good, the reported latency is reduced in that release.

Measurement method: an impulse and a 1 kHz tone burst through the full plug-in, cross-correlated against the input, at every sample rate and for each module alone and all modules active. The test fails if measured delay ≠ reported delay (± 1 sample for the IIR group delay, which is phase response, not latency).

---

## 5. Basic / Advanced UI wireframes

Default size 1100 × 760 px (680 px plus a 64 px send row added with the effect sends), minimum 880 × 608, scale 75–200 % in 25 % steps plus free drag-resize at fixed aspect ratio.

### 5.1 Basic view

```
┌───────────────────────────────────────────────────────────────────────────────────────────────┐
│ ⚡ KAMINARI VOCAL    [ BASIC | Advanced ]   Preset: Vocal Track ▾  ◀ ▶  [Save]  [A|B] [A→B]     │
│                                         ⚡ 96 smp · 2.0 ms   OS: Off   Undo ↶  Redo ↷   100% ▾  │
├────┬──────────────────────────────────────────────────────────────────────────────────────┬────┤
│ IN │  EQ  [⏻]                                     Analyzer [Pre|Post|Off]  Zoom [−][+]    │OUT │
│ ▮▮ │   +12 ┤                                                                           │ ▮▮ │
│ ▮▮ │       │           ②                                                               │ ▮▮ │
│ ▮▮ │     0 ┼────①──────────────────────────③──────────────────────────────           │ ▮▮ │
│ ▮▮ │       │                  ~~~ analyzer ~~~                         ④               │ ▮▮ │
│ ▮▮ │   −12 ┤                                                                           │ ▮▮ │
│ ▮▮ │       20     50    100    200    500   1k    2k    5k   10k   20k                 │ ▮▮ │
│ ▮▮ │  Selected: Band 2 · Bell · 240 Hz · +3.0 dB · Q 1.2       [Type ▾] [⏻] [Solo] [🗑]  │ ▮▮ │
│ in ├─────────────────┬──────────────────────┬────────────────────┬─────────────────────┤ out│
│gain│ TUNE  [⏻] ⚡    │ LEVEL  [⏻]           │ DE-ESS  [⏻]        │ LOW-MID  [⏻]        │gain│
│ ◯  │ Key [C ▾]       │      ◯ Compression   │     ◯ De-ess       │     ◯ Amount        │ ◯  │
│    │ Scale [Major ▾] │      28 % (−14 dB)   │     47 % (−28 dB)  │     (Threshold)     │    │
│    │ Range [Mid ▾]   │  GR ▮▮▮▯▯▯ −4.2 dB   │  GR ⋰⋰▯▯ −2.1 dB   │  GR ┆┆▯▯ −1.0 dB    │    │
│    │ ◯ Strength 70 % │  Makeup +2.1 dB      │  ~5.5 kHz voice    │  100–500 Hz          │    │
│    │ ● Tracking  A4  │                      │                    │                     │    │
└────┴─────────────────┴──────────────────────┴────────────────────┴─────────────────────┴────┘
```

Basic module cards are all the same size, with the same hammer area. The Tune card holds Key and Scale (Vocal Range is on the Tune page). Tune card tuning view (implemented): below the Retune Speed hammer, one line shows the detected note → the note it is tuned to and how far the voice is from it (♯ +n ct / ♭ −n ct, or "in tune" within 5 cents); under it a −50 … +50 cent bar with a marker at the voice's deviation, the span being corrected shaded from the marker towards the target, and a centre mark that lights when the voice is in tune. "-- no pitch" when nothing is detected.

### 5.2 Advanced view

The header and both meter rails stay. The module tabs replace the four strips; the selected module expands. Each tab shows its bypass state and a mini GR meter, so no module's state is hidden.

```
┌───────────────────────────────────────────────────────────────────────────────────────────────┐
│ ⚡ KAMINARI VOCAL    [ Basic | ADVANCED ]   Preset …   [A|B]   ⚡ 96 smp · 2.0 ms   OS: Off     │
├────┬──────────────────────────────────────────────────────────────────────────────────────┬────┤
│ IN │ [TUNE ⏻ ●] [EQ ⏻] [LEVEL ⏻ ▮▮] [DE-ESS ⏻ ⋰] [LOW-MID ⏻ ┆]     ← module tabs        │OUT │
│    ├──────────────────────────────────────────────────────────────────────────────────────┤    │
│    │ LEVEL — Advanced                                                                     │    │
│    │  ┌ transfer curve ────────┐   Threshold ◯  Ratio ◯   Knee ◯   Range ◯                │    │
│    │  │      ╱                 │   Attack ◯    Release ◯  Detector [Peak|Smooth]          │    │
│    │  │   ╭─╯                  │   Detector HPF ◯ 100 Hz   Auto Makeup [⏻] +2.1 dB (i)    │    │
│    │  │ ╱                      │                                                         │    │
│    │  └────────────────────────┘   Detector ▮▮▮▮▮▯  GR ▮▮▮▯▯  history ▁▂▅▃▂▁            │    │
└────┴──────────────────────────────────────────────────────────────────────────────────────┴────┘
```

Advanced editors:
- Tune: Key, Scale, Range, Strength, Response, Sustain Natural, 12-note keyboard (included notes filled, excluded notes outlined and struck through), detected-pitch readout with cents offset, tracking status.
- EQ: full-height graph, band table (all 8 bands), slope, analyzer settings, A/B.
- Level: as above.
- De-ess: detector spectrum with draggable detector region and split-frequency marker, threshold line on detector meter, Listen button.
- Low-Mid: band region on a frequency strip with two crossover handles, all dynamics controls, Solo.

---

Module tabs (implemented): the lightning icon on each module tab is that module's On switch. Bright with a glow = on, dim grey = bypassed; it brightens and shows a ring on hover and shrinks while pressed; its tooltip says what a click will do. It follows the module's On parameter everywhere else (page header, Basic card, automation). Clicking the rest of the tab shows the page; clicking the icon never changes the page.

## 6. Design system

### 6.1 Color tokens

| Token | Hex | Use |
| --- | --- | --- |
| `navy-950` | #070F1F | window background |
| `navy-900` | #0B1A33 | panels |
| `navy-800` | #12264A | raised controls, knob bodies |
| `navy-600` | #2B4A82 | outlines, grid lines, inactive tracks |
| `white` | #F4F7FC | primary text, knob pointers, Level GR meter |
| `mist` | #A9B8D6 | secondary text, units |
| `bolt` | #5CE1FF | active/selected, EQ curve, focus ring, De-ess GR meter |
| `bolt-glow` | #B8F3FF | hover highlights, 25 % alpha glow |
| `ice` | #8FB8FF | Low-Mid GR meter |
| `amber` | #FFB547 | warnings (latency increase, clip guard engaged, tracking lost) |
| `red` | #FF4D5E | clipping only |
| `disabled` | navy-600 at 50 % + text at 40 % | unavailable |

White on navy-900 has a contrast ratio above 15:1; mist on navy-900 is above 7:1. Bolt on navy-900 is above 9:1.

### 6.2 Lightning elements (original artwork, drawn as vector paths)

- Logo: a three-segment bolt glyph next to the wordmark "KAMINARI VOCAL" in white.
- Power/bypass buttons: a bolt icon; enabled = filled bolt in `bolt`; bypassed = outlined bolt in `mist` with a slash and the word "OFF".
- Knob pointer: a short zig-zag notch; value arc in `bolt`.
- Meters: segmented "charge" bars with 2 px gaps.
- Section dividers: a thin line with a single small zig-zag break at the left edge.
- EQ curve: 2 px `bolt` stroke with a 6 px 20 %-alpha glow; no animated effects.
- Latency badge: bolt icon + "96 smp · 2.0 ms"; turns amber with a "+" if oversampling adds latency.

### 6.3 Typography and spacing

- Embedded font: Inter (SIL Open Font License). Sizes: 11 (units, ticks), 13 (labels), 15 (values), 18 (module titles), 22 (wordmark). Numbers use tabular figures.
- 4 px base grid; component padding 8; module gutter 12; panel radius 6.

### 6.4 States (never color alone)

| State | Visual |
| --- | --- |
| Hover | `bolt-glow` outline at 25 % alpha, cursor change |
| Selected | `bolt` 2 px outline + filled node |
| Keyboard focus | 2 px dashed `bolt` ring outside the control |
| Bypassed | 40 % opacity, outlined bolt with slash, "OFF" label |
| Unavailable | 40 % opacity, no hover, tooltip says why |
| Audition/solo active | pulsing-free amber frame + "SOLO" or "LISTEN" text tag + exit "×" |
| Warning | amber triangle icon + text |
| Clip | red segment at meter top + "CLIP" latch, click to clear |

Meters: Level GR = solid white bars, De-ess GR = bolt bars with diagonal hatch, Low-Mid GR = ice bars with dotted segments. Each has a text label and numeric readout.

### 6.5 Hammer slider (Basic view)

Used for Tune Retune Speed, Multiband, Compression, De-ess and Resonance in the Basic view. Input and Output are plain knobs; Advanced controls are plain knobs.

- An original war-hammer drawing standing head up: chamfered head with engraved lines, collar, wrapped grip, pommel. No box or track. (A generic Norse-style hammer, not a copy of any film or comic design.)
- **Dragging down raises the value**: the hammer fills with colour (white at the top through the accent blue) from the top of the head down towards the pommel; dragging up empties it in reverse. Mouse wheel and arrow keys follow the same direction. The whole control is the drag area.
- At rest nothing surrounds the hammer. Past an activation point (fill 0.1) it charges up like a Super Saiyan 2 transformation (Dragon Ball Z), and everything follows the fill continuously (intensity 0 → 1):
  - aura: a jagged flame of sharp tongues that follows the hammer's silhouette (wide at the head, narrow along the grip), leans upwards and flickers fast, in three layers (translucent blue, lighter blue, white core) with crisp outlines; brighter with intensity and flashing on surges; tongues shorten at the control's edge rather than being cut off;
  - lightning, each bolt white-cored with a blue glow, flickering: fast small arcs crackling all over the head and grip, bolts crossing the hammer from side to side, branching medium bolts thrown off its edges, and slower surges (random intervals, shorter at high intensity) that run from pommel to head or far out and flash the aura;
  - ambient particles: rising sparks and curling wisps.
  Bolt count, brightness, speed and reach grow with intensity; all timing is random, so the pattern never repeats. The hammer body is drawn over the aura so it stays readable.
- Parameters where a lower value means more effect (Retune Speed, thresholds) use an inverted mapping, so a fuller hammer always means more effect.
- Basic-view mappings (fill 0 = hammer empty, 1 = full). Pulling each dynamics hammer down always increases gain reduction:

  | Hammer | Fill 0 | Fill 1 | Mapping |
  | --- | --- | --- | --- |
  | Multiband (band 1 threshold) | 0 dB | −60 dB | `mb1_thresh = −60 × fill` |
  | Compression | 0 dB | −50 dB | `lv_thresh = −50 × fill` |
  | De-ess | 0 dB | −60 dB | `ds_thresh = −60 × fill` |
  | Resonance | depth 0 | depth 20 | `rs_depth = 20 × fill` |

  Each strip's GR bar and readout follow the hammer.
- The drawing is driven only by the parameter value through a `ParameterAttachment`, so automation, preset loads, A/B, undo and host changes update it exactly like a drag. Range, default and automation behavior are the parameter's own.
- The aura pulses with the host's beat (tempo, ppq position and play state published once per block, sequence-locked, re-anchored every block so it cannot drift). Pulse peaks on the beat; above 144 BPM it pulses every 2 beats so it stays ≤ 2.4 Hz. No tempo or stopped transport → steady glow.
- Vector drawing; scales cleanly at any size. Accessibility: role slider, name = parameter name, value = parameter text with unit.

### 6.6 Scaling

All layout is in logical units; `setTransform` handles scale. Vector drawing only; the font is embedded. Tested at 100, 125, 150 % OS scaling and on Retina and non-Retina displays.

---

## 7. Interaction map

### 7.1 All parameter controls

| Input | Action |
| --- | --- |
| Hover 350 ms | Tooltip: name, value + unit, one-sentence description, hints. Placed beside the control, never over it. Hides on mouse-out or 4 s after motion stops. |
| Click-drag vertical | Adjust; 200 px = full range (lightning sliders: one track height = full range, drag down = increase) |
| Shift-drag | Fine, 10 % sensitivity |
| Double-click control | Reset to default |
| Double-click value readout | Text entry; Enter confirms, Escape cancels; accepts units ("5k", "−3 dB", "120ms") |
| Mouse wheel | 1 % of range per notch (or 1 step for choices) |
| Shift-wheel | 0.1 % per notch |
| Right-click / Control-click (macOS) | Menu: Enter Value, Reset to Default, Copy Value, Paste Value (MIDI Learn: postponed) |
| Tab / Shift-Tab | Move focus between controls in reading order |
| Arrow Up/Down | Adjust focused control by 1 %; with Shift 0.1 % |
| Page Up/Down | 10 % |
| Home / End | Minimum / maximum |
| Delete or Backspace | Reset focused control to default |
| Enter | Open value entry on focused control |

Every gesture calls `beginChangeGesture` / `endChangeGesture` so host automation and undo record one step per gesture. Control-click is not a modifier for adjustment.

### 7.2 Buttons

Single click toggles. Space or Enter toggles the focused button. Momentary audition (EQ band audition, De-ess Listen in momentary mode) is active only while the mouse button is held. Latched Solo/Listen shows an amber frame, a text tag, and an "×" to exit; Escape also exits.

### 7.3 EQ graph

| Input | Action |
| --- | --- |
| Hover node | Highlight; tooltip: band, type, frequency, gain, Q |
| Click node | Select |
| Drag node | Horizontal = frequency, vertical = gain (cuts: vertical = Q/resonance) |
| Shift-drag | Fine |
| Wheel over selected node | Q |
| Double-click empty space | New Bell band at that frequency, 0 dB (if a band slot is free; otherwise a message "All 8 bands in use") |
| Double-click node | Gain → 0 dB (band is not deleted) |
| Right-click node | Type ▸, Bypass, Solo, Duplicate, Delete |
| Click-drag empty space | Rubber-band multi-select; Shift-click adds/removes |
| Drag on multi-selection | Moves all selected bands relatively |
| Click on overlapping nodes | Each click cycles through the overlapping nodes; a small list appears if more than 2 overlap |
| Alt-click-hold node | Momentary band audition |
| Keyboard | Left/Right select previous/next band; arrows move the selected node; Delete removes it |

### 7.4 Module-specific

- Tune: Key/Scale menus with type-to-search; 12-note map: click toggles a note, hover shows "D♯ — included"; tracking LED + detected note + cents.
- Level: hover on GR meter shows current and peak-hold reduction (hover never pauses meters); Auto Makeup has an (i) tooltip with the formula.
- De-ess: hover on detector display shows detector level and threshold; drag the region to move both edges, drag an edge to move one; Listen shows "LISTENING: detector signal".
- Low-Mid: crossover handles show frequency and slope on hover; dragging moves only that handle; band region shaded and labeled "100–500 Hz".

### 7.5 Accessibility

All controls expose JUCE `AccessibilityHandler` titles, values, and value ranges. VoiceOver tested on macOS.

---

## 8. Test plan

### 8.1 Automated (run on every build, headless)

| Area | Tests |
| --- | --- |
| Parameters | Every ID in `ParamIDs.h` exists once; ranges and defaults match this document; a stored golden list detects renamed or removed IDs |
| State | Save → load round-trip of every parameter and UI state; load of an older state with missing IDs uses defaults |
| Views | Switch Basic ↔ Advanced while processing: output is bit-identical to no switch |
| A/B, bypass | A/B switch and every bypass: no sample step > −60 dBFS above the signal envelope (click detector) |
| Latency | Measured delay = reported delay at 44.1/48/88.2/96/192 kHz, per module and all modules |
| Block size | Blocks of 1, 7, 32, 64, 512, 2048, and variable sizes produce the same output as 512 |
| Safety | Silence, DC, full-scale noise, +24 dBFS, NaN/Inf input → finite output, no denormals, DC < −90 dBFS |
| Mono/stereo | Mono layout works; identical L/R input → identical L/R output |
| EQ | Each type: magnitude at f0, f0/2, 2·f0 within 0.1 dB of theory; slopes measured within 0.5 dB/oct; Q sweep 0.025–40 stable at all rates; fast automation of frequency produces no instability |
| Level | Static curve (threshold, ratio, knee, range) within 0.2 dB; attack/release time constants within 10 %; auto makeup formula and cap |
| De-ess | 6 kHz burst reduced by expected amount; 500 Hz tone untouched (< 0.05 dB) in Split mode; range caps reduction; Listen outputs detector signal |
| Low-Mid | 0 dB GR → output equals input; 250 Hz tone reduced, 2 kHz tone untouched; crossover constraint |
| Tune | Synthetic tones: sustained, ±50 cent vibrato at 5.5 Hz, slides, noise bursts, silence, octave-jump trap (strong 2nd harmonic); pitch accuracy within ±5 cents after response time; no octave errors on the test set; custom scale note map honored |
| CPU | Release build, 48 kHz / 64 samples, all modules on: measured per-block time recorded; regression fails if > 20 % worse than baseline |

### 8.2 Manual (each phase end)

- Build VST3, AU (`auval -v aufx WLtn …`), AAX (Pro Tools Developer).
- Pro Tools 48 kHz tracking session: record through the plug-in at 32/64-sample buffer; confirm reported 96 samples; listen.
- Logic and Reaper: automation write/read, state recall, sample-rate change, buffer change.
- Real vocal recordings (male low, female high, rap, breathy) at each phase.
- UI: tooltips, drag, Shift-drag, wheel, entry, context menu, keyboard-only pass, VoiceOver, 100/125/150 % scaling, window minimum size.

---

## 9. Technical risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Tune quality inside 2.0–2.67 ms | Artifacts on low voices, octave errors | Period detection on past audio only, range-limited search, hysteresis, voiced/unvoiced gate; budget reserve of 32 samples |
| Classic period-shift shifts formants | "Chipmunk" on large corrections | Acceptable for ≤ 1–2 semitone correction (intended use); documented |
| AAX signing | Plug-in will not load in retail Pro Tools | Needs Avid developer account, PACE tools, iLok; until then VST3 via PatchWork |
| High-core Intel Mac + Sequoia Pro Tools performance issue | Dropouts not caused by plug-in | Test on the user's 2018 Mac; CPU budget measured |
| Filter modulation artifacts | Zipper noise, instability on fast EQ automation | TPT SVF topology, per-sample coefficient smoothing for frequency/gain/Q |
| Subtractive band processing phase | Small comb effects when GR is large | Matched filters, measured null depth; documented |
| JUCE VST3 parameter hashing | Renaming an ID breaks automation | Golden ID list test |
| Hosts that resend latency late | Misaligned tracks | Latency is constant and set in constructor and `prepareToPlay` |
| Sidechain input across VST3 / AU / AAX | EQ Match reference and any external key may not reach the plug-in in some hosts | Optional second input bus; captured-spectrum reference as fallback; tested per host |
| Resonance suppression quality at 0 latency | Smearing or missed resonances compared with STFT methods | Low-latency IIR mode is default; High resolution mode for mixing; listening tests |
| Scope size | Delays | Phased delivery; each phase ends buildable and tested |

## 10. Postponed (not in version 1)

Graph-mode pitch editing, formant controls, harmony, vibrato editing, melody extraction, gate, linear-phase and natural-phase EQ, all-pass band (unless a clear use is found), inter-plugin spectrum features, surround, modular routing, preset marketplace, saturation and heavy modulation, external sidechain (until tested in all three formats), unlinked stereo de-essing, MIDI Learn, mid/side and L/R per-band processing (reserved IDs), dynamic EQ (reserved IDs; added only if tests pass), 8x oversampling, a "Tune removed / zero-latency" variant.

## 11. Phase plan (revised for the decisions above)

1. Delete `ChannelStrip/`. New `KaminariVocal/` CMake project (VST3, AU, AAX), pass-through, full parameter set from section 2, state, UI shell with theme, meters, bypass, constant latency report (96 samples at 48 kHz from the start), test runner with latency and ID tests.
2. EQ with graph and analyzer.
3. Compression (with detector EQ).
4. De-ess (range slider, lookahead, oversampling).
5. Multiband (1–6 bands, default one low-mid band).
6. Tune.
6b. Resonance (low-latency mode first, then High resolution).
6c. EQ piano scale and EQ Match (sidechain reference).
7. Oversampling evaluation, presets, A/B, undo, accessibility, AAX signing, installer update, DAW testing.

At the end of each phase: build, run tests, DAW check, measure latency, listen to real vocals, update this document.
