# White Lightning — Design Document (pre-implementation)

Status: design only. No White Lightning code exists yet. Phase 1 starts after this document is approved.

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
| Missing vs. White Lightning spec | Pitch correction, 8 bands, extra filter types, slopes, dynamic band, low-mid dynamics, Basic/Advanced views, presets, A/B, undo, tooltips/context menus/keyboard entry, AU, AAX, latency measurement tests |

Reusable: the biquad math, the analyser FIFO pattern, the test-harness pattern, the build and installer scripts.
Not reused: the FET/opto compressor models (they are modeled on specific hardware and are outside the product scope), all parameter IDs, all GUI code.

### 0.2 Decisions made with the user

| Topic | Decision |
| --- | --- |
| Relation to old plug-in | White Lightning **replaces** Channel Strip. Phase 1 deletes `ChannelStrip/` and creates `WhiteLightning/` with a new plug-in code. Sessions saved with Channel Strip will not load White Lightning. |
| Latency budget | Total reported plug-in latency of **≤ 128 samples at 48 kHz** (2.67 ms), target ~96. The earlier 74-sample figure is superseded. |
| Pitch correction character | Classic, period-based, low-latency correction (not a modern formant-preserving mode). Best quality that fits the budget. |
| Colors | Navy and white base, one electric ice-blue accent, amber for warnings, red only for clipping. |
| Platform | Intel Mac (2018 hardware), macOS Sequoia. Formats: VST3, AU, AAX. Architecture x86_64 (universal optional). |

### 0.3 Platform facts that affect the plan

- JUCE 8 ships the AAX SDK, so an AAX build needs no separate SDK download. Running AAX in retail Pro Tools requires PACE signing. Avid provides the PACE signing tools free to registered AAX developers; signing needs an iLok. The user must register with Avid (devauth@avid.com) for the Pro Tools Developer build and signing access. Until then, AAX is built and tested in the Pro Tools Developer build only, and retail Pro Tools continues to use VST3 through Blue Cat PatchWork.
- macOS Sequoia is the last macOS release that supports 2018 Intel Macs. Pro Tools 2024.10 and later support Sequoia. Avid lists an audio-performance issue on high-core-count Intel Macs under Sonoma, Sequoia, and Tahoe.
- Waves Tune Real-Time reports 0 samples to the host; its actual delay varies from 0 to 4 ms with the pitch period. White Lightning reports a fixed latency instead (see section 4) so that host delay compensation is correct.

---

## 1. Module architecture

```
WhiteLightning/
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
| `tn_strength` | Strength | float | 0 … 100 | 70 | % | B |
| `tn_speed` | Response | float | 0 … 400 | 40 | ms | A |
| `tn_natural` | Sustain Natural | float | 0 … 100 | 0 | % | A |
| `tn_note_0` … `tn_note_11` | Note C … B | bool | | per scale | | A |

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

### 2.4 Level

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `lv_on` | Level On | bool | | on | | B |
| `lv_thresh` | Threshold | float | −50 … 0 | −14 | dB | B (as Compression) |
| `lv_ratio` | Ratio | float, skewed | 1 … 20 | 3 | :1 | A |
| `lv_attack` | Attack | float, log | 0.1 … 100 | 8 | ms | A |
| `lv_release` | Release | float, log | 10 … 2000 | 150 | ms | A |
| `lv_knee` | Knee | float | 0 … 24 | 8 | dB | A |
| `lv_range` | Range (max reduction) | float | 0 … 40 | 15 | dB | A |
| `lv_detector` | Detector | choice | Peak, Smooth (RMS 10 ms) | Smooth | | A |
| `lv_sc_hpf` | Detector High-Pass | float, log | 20 … 500 | 100 | Hz | A |
| `lv_auto_makeup` | Auto Makeup | bool | | on | | A |

Basic "Compression" control = `lv_thresh` displayed as `Compression % = −thresh / 50 × 100` (default −14 dB = 28 %). The readout shows both: "28 % (−14.0 dB)".

Auto makeup (exact behavior): makeup = `min(12 dB, 0.5 · G)`, where `G` is the static gain reduction the curve applies to a 0 dBFS signal: `G = min(lv_range, (0 − thresh) · (1 − 1/ratio))`, with the knee applied. It depends only on threshold, ratio, knee, and range, never on the signal, so it cannot chase syllables. It is smoothed with a 200 ms one-pole. It is capped at +12 dB. The GUI shows the current makeup value.

External sidechain: postponed (see section 9).

### 2.5 De-ess

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `ds_on` | De-Ess On | bool | | on | | B |
| `ds_thresh` | Threshold | float | −60 … 0 | −28 | dB | B (as De-ess) |
| `ds_range` | Range (max reduction) | float | 0 … 24 | 8 | dB | A |
| `ds_attack` | Attack | float, log | 0.05 … 10 | 0.5 | ms | A |
| `ds_release` | Release | float, log | 10 … 300 | 60 | ms | A |
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
- Internal ratio is fixed at 6:1; the gain reduction is `min(range, over · (1 − 1/6))`. Documented in the tooltip.
- Split Band: the signal is split complementarily (`high = x − LP(x)`, so `low + high = x` exactly) at `ds_split_freq`; only `high` is attenuated. Wideband: the whole signal is attenuated.
- Stereo: detector is linked (max of channels). Unlinked mode is postponed.

### 2.6 Low-Mid Dynamics

| ID | Name | Type | Range | Default | Unit | Vis |
| --- | --- | --- | --- | --- | --- | --- |
| `lm_on` | Low-Mid On | bool | | off | | B |
| `lm_lo` | Low Crossover | float, log | 40 … 400 | 100 | Hz | A |
| `lm_hi` | High Crossover | float, log | 200 … 2000 | 500 | Hz | A |
| `lm_slope` | Crossover Slope | choice | 6, 12, 24 | 12 | dB/oct | A |
| `lm_thresh` | Threshold | float | −60 … 0 | −24 | dB | B |
| `lm_ratio` | Ratio | float | 1 … 10 | 2 | :1 | A |
| `lm_attack` | Attack | float, log | 1 … 100 | 10 | ms | A |
| `lm_release` | Release | float, log | 20 … 1000 | 150 | ms | A |
| `lm_knee` | Knee | float | 0 … 24 | 6 | dB | A |
| `lm_range` | Range (max reduction) | float | 0 … 24 | 6 | dB | A |
| `lm_detector` | Detector | choice | Peak, Smooth | Smooth | | A |
| `lm_auto_makeup` | Auto Makeup | bool | | off | | A |
| `lm_solo` | Band Solo | bool | | off | | A (non-auto) |

The DSP enforces `lm_hi ≥ 1.5 · lm_lo` without rewriting the stored parameter. Band extraction: `band = LP_hi(HP_lo(x))`; output = `x − band · (1 − g)`. At 0 dB gain reduction the output equals the input exactly. Zero latency.

### 2.7 Non-parameter state saved with the session

`ui_view` (Basic/Advanced), `ui_module` (selected module), `ui_scale` (75–200 %), analyzer settings, EQ zoom/scroll, A/B slot contents and active slot, preset name and "modified" flag.

---

## 3. Signal flow

```
 Input ──► [In Gain] ──► [TUNE] ──► [EQ] ──► [LEVEL] ──► [DE-ESS] ──► [LOW-MID] ──► [Out Gain] ──► [Safety] ──► Output
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
2. EQ before Level: cuts and tone shaping happen before dynamics, so the leveler reacts to the corrected tone.
3. De-ess after Level: the compressor's makeup gain and release can raise sibilance; de-essing afterwards catches it.
4. Low-Mid last: controls body and proximity effect that remain after leveling.

The order is fixed in version 1. Routing is postponed.

---

## 4. Latency budget

Reported latency is constant in time (2.0 ms) for the Tune module and zero for all other modules. It is reported whether or not Tune is enabled, so toggling Tune never changes host delay compensation or causes a click. The dry path inside Tune is delayed by the same amount.

| Module | 44.1 kHz | **48 kHz** | 88.2 kHz | 96 kHz | 192 kHz | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Input gain, meters | 0 | **0** | 0 | 0 | 0 | |
| Tune | 88 | **96** | 176 | 192 | 384 | fixed 2.0 ms base delay |
| EQ | 0 | **0** | 0 | 0 | 0 | minimum-phase IIR |
| Level | 0 | **0** | 0 | 0 | 0 | no lookahead |
| De-ess | 0 | **0** | 0 | 0 | 0 | no lookahead, complementary split |
| Low-Mid | 0 | **0** | 0 | 0 | 0 | subtractive band |
| Analyzer, smoothing | 0 | **0** | 0 | 0 | 0 | analyzer reads a copy |
| Output, Safety | 0 | **0** | 0 | 0 | 0 | |
| **Total reported** | 88 | **96** | 176 | 192 | 384 | ceiling: 128 at 48 kHz (2.67 ms) |
| Reserve | | **32** | | | | for Tune tuning in Phase 6 |
| Oversampling 2x / 4x | | measured in Phase 7 | | | | off by default; labeled as adding latency |

Tune variable delay: a period-based shifter repeats or drops whole pitch periods. Around the fixed 2.0 ms base, the instantaneous delay varies by up to about one pitch period (about 0–4 ms for typical vocals, similar to Waves Tune Real-Time). Phase 6 measures the average and range of this delay per vocal range and documents it. If 2.0 ms causes audible artifacts, the base may rise to at most 2.67 ms (128 samples at 48 kHz).

Measurement method: an impulse and a 1 kHz tone burst through the full plug-in, cross-correlated against the input, at every sample rate and for each module alone and all modules active. The test fails if measured delay ≠ reported delay (± 1 sample for the IIR group delay, which is phase response, not latency).

---

## 5. Basic / Advanced UI wireframes

Default size 1100 × 680 px, minimum 880 × 544, scale 75–200 % in 25 % steps plus free drag-resize at fixed aspect ratio.

### 5.1 Basic view

```
┌───────────────────────────────────────────────────────────────────────────────────────────────┐
│ ⚡ WHITE LIGHTNING   [ BASIC | Advanced ]   Preset: Vocal Track ▾  ◀ ▶  [Save]  [A|B] [A→B]     │
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
│ ⚡ WHITE LIGHTNING   [ Basic | ADVANCED ]   Preset …   [A|B]   ⚡ 96 smp · 2.0 ms   OS: Off     │
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

- Logo: a three-segment bolt glyph next to the wordmark "WHITE LIGHTNING" in white.
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

### 6.5 Scaling

All layout is in logical units; `setTransform` handles scale. Vector drawing only; the font is embedded. Tested at 100, 125, 150 % OS scaling and on Retina and non-Retina displays.

---

## 7. Interaction map

### 7.1 All parameter controls

| Input | Action |
| --- | --- |
| Hover 350 ms | Tooltip: name, value + unit, one-sentence description, hints. Placed beside the control, never over it. Hides on mouse-out or 4 s after motion stops. |
| Click-drag vertical | Adjust; 200 px = full range |
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
| Scope size | Delays | Phased delivery; each phase ends buildable and tested |

## 10. Postponed (not in version 1)

Graph-mode pitch editing, formant controls, harmony, vibrato editing, melody extraction, gate, linear-phase and natural-phase EQ, all-pass band (unless a clear use is found), EQ match, inter-plugin spectrum features, surround, modular routing, preset marketplace, saturation and heavy modulation, external sidechain (until tested in all three formats), unlinked stereo de-essing, MIDI Learn, mid/side and L/R per-band processing (reserved IDs), dynamic EQ (reserved IDs; added only if tests pass), lookahead modes, 8x oversampling, a "Tune removed / zero-latency" variant.

## 11. Phase plan (revised for the decisions above)

1. Delete `ChannelStrip/`. New `WhiteLightning/` CMake project (VST3, AU, AAX), pass-through, full parameter set from section 2, state, UI shell with theme, meters, bypass, constant latency report (96 samples at 48 kHz from the start), test runner with latency and ID tests.
2. EQ with graph and analyzer.
3. Level.
4. De-ess.
5. Low-Mid.
6. Tune.
7. Oversampling evaluation, presets, A/B, undo, accessibility, AAX signing, installer update, DAW testing.

At the end of each phase: build, run tests, DAW check, measure latency, listen to real vocals, update this document.
