# Kaminari Vocal — Design Document (pre-implementation)

Status: design only. No Kaminari Vocal code exists yet. Phase 1 starts after this document is approved.

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
| `ds_lookahead` | Lookahead | float | 0 … 10 | 0 (off) | ms | A |
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
- Advanced view sets the detector range with a horizontal two-handle frequency slider (1–20 kHz) over a live spectrum; dragging the band moves both edges.
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

---

## 3. Signal flow

```
 Input ──► [In Gain] ──► [TUNE] ──► [EQ] ──► [MULTIBAND] ──► [COMPRESSION] ──► [DE-ESS] ──► [RESONANCE] ──► [Out Gain] ──► [Safety] ──► Output
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
4. De-ess after Compression: makeup gain and release can raise sibilance; de-essing afterwards catches it.
5. Resonance last: removes harsh, ringing resonances that remain after all gain changes.
The UI's module order (Tune, Multiband, Compression, De-ess, Resonance) follows this order.

The order is fixed in version 1. Routing is postponed.

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
| EQ Match | 0 | **0** | 0 | 0 | 0 | generates normal minimum-phase bands |

Tune variable delay: a period-based shifter repeats or drops whole pitch periods. Around the fixed 2.0 ms base, the instantaneous delay varies by up to about one pitch period (about 0–4 ms for typical vocals, similar to Waves Tune Real-Time). Phase 6 measures the average and range of this delay per vocal range and documents it. If 2.0 ms causes audible artifacts, the base may rise to at most 2.67 ms (128 samples at 48 kHz).

Lower-latency option (Phase 6): Antares describes Auto-Tune Hybrid on Avid DSP hardware as zero-latency, with a Classic mode. A period-based shifter can run with a base delay near 0 if it accepts a larger delay variation, as Waves Tune Real-Time does. Phase 6 will test base delays of 0, 32, 64, and 96 samples on real vocals. If a lower value sounds as good, the reported latency is reduced in that release.

Measurement method: an impulse and a 1 kHz tone burst through the full plug-in, cross-correlated against the input, at every sample rate and for each module alone and all modules active. The test fails if measured delay ≠ reported delay (± 1 sample for the IIR group delay, which is phase response, not latency).

---

## 5. Basic / Advanced UI wireframes

Default size 1100 × 680 px, minimum 880 × 544, scale 75–200 % in 25 % steps plus free drag-resize at fixed aspect ratio.

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

### 6.5 Lightning slider

Used for Tune Strength, Level Compression, De-ess, and Low-Mid (Basic view). Input and Output are plain circular knobs beside their always-visible meters. Advanced detail controls stay plain knobs (no bolt pointer).

- No box or track. A dark thunder cloud (shaded billows) sits at the top; a row of full-length lightning strikes hangs below it, each with a jagged trunk, side branches, and fine filaments. Strike count follows the width of the storm area (odd, 3–9).
- A brass fader in the R-Vox style (long dark slot, wide brass cap with three grip ridges) sits to the right of the cloud. Scale marks beside the slot are shown on every lightning slider except Compression.
- Parameters where a lower value means more effect (Retune Speed, thresholds) use an inverted mapping, so more strikes always means more effect. **Pulling the fader down raises the value**; pushing up lowers it. Mouse wheel and arrow keys follow the same direction (Down/PageDown = more). The whole control is the drag area.
- The value adds strikes **across the width, from the centre outwards** (centre, then right, left, right, …). A strike that is on glows over its full length; the next strike fades in as the value approaches it. Strikes that are off are grey; they turn white (with glow) as they switch on. The cloud is always visible. The underside of the cloud lights up with the number of active strikes.
- The drawing is driven only by the parameter value through a `ParameterAttachment`, so automation, preset loads, A/B, undo, and host changes update it exactly like a drag. Range, default, and automation behavior are the parameter's own.
- Glow: active strikes pulse with the host's beat. The audio thread publishes tempo, ppq position, and play state once per block (sequence-locked, no allocation); the GUI extrapolates from that block's anchor only (≤ 250 ms), so it is re-synchronized on every block and cannot drift. Pulse peaks on the beat (cosine, no hard edges); above 144 BPM it pulses every 2 beats so it stays ≤ 2.4 Hz. No tempo, stopped transport, or no recent block → steady glow.
- Vector drawing; strike shapes are generated per parameter and size from a fixed seed, so they stay the same between sessions and scale cleanly.
- Accessibility: role slider, name = parameter name, value = parameter text with unit, range = parameter range; screen-reader set-value goes through the same attachment.

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
