# Channel Strip (VST3)

JUCE 8 channel strip for an Intel Mac (x86_64, macOS Sequoia).

Signal chain: input gain, phase invert, de-esser, EQ, compressor, mono / pan, output gain.
The EQ can be moved after the compressor with the POST COMP button.

| Section | Details |
| --- | --- |
| Gain | Input and output +/-24 dB, phase invert, mono sum, constant-power pan (0 dB at centre) |
| De-esser | Split-band. Frequency 2-12 kHz, threshold, range (max reduction), listen |
| EQ | 6 bands: bell, low shelf, high shelf, high pass, low pass, notch. Analyser, draggable nodes, mouse wheel for Q |
| Compressor, FET mode | 1176-style: input, output, attack 20 us - 800 us, release 50 ms - 1.1 s, ratio 4 / 8 / 12 / 20 / ALL (distortion) |
| Compressor, Opto mode | LA-2A-style: peak reduction, gain, compress / limit, program-dependent two-stage release |
| Compressor | Parallel mix, needle gain-reduction meter, GUI changes with the mode |
| Analyser | Resolution Low / Medium / High / Maximum (1024-8192-point FFT at 48 kHz), speed Very Slow to Very Fast; default High + Fast |
| Lightning sliders | Input, Output, De-ess threshold, FET input and Opto peak reduction. A thunder cloud with a row of strikes: pull the grip down from the cloud to raise the value and switch on more strikes from the centre outwards. Active strikes glow in time with the host tempo; steady when the transport is stopped |

The compressors are style models, not component-level emulations of the hardware.

## Build on the Mac

Requirements: Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`).

```
cd ChannelStrip
./build_mac.sh
```

This fetches JUCE 8.0.8, builds an x86_64 VST3 and copies it to
`~/Library/Audio/Plug-Ins/VST3/Channel Strip.vst3`. A locally built plugin is not quarantined,
so Gatekeeper does not block it. If you move a copy to another Mac, run
`xattr -cr "Channel Strip.vst3"` and `codesign --force --deep --sign - "Channel Strip.vst3"` there.

## Installer (.pkg)

On the Mac:

```
./installer/make_installer.sh
```

This builds the plugin, signs it (ad-hoc by default) and writes `dist/ChannelStrip-0.1.0.pkg`.
The package installs `Channel Strip.vst3` system-wide to `/Library/Audio/Plug-Ins/VST3`. Double-click it to install.
To remove the plugin, run `./installer/uninstall_mac.sh`.

Without an Apple Developer ID the package is unsigned. On your own Mac that is fine. On any other Mac that receives it by download,
Gatekeeper blocks the double-click; right-click the package, choose Open, or allow it in System Settings > Privacy & Security.
For a package that opens without warnings, set these variables (needs a paid Apple Developer account, a Developer ID Application and a
Developer ID Installer certificate, and a `notarytool store-credentials` profile):

```
CODESIGN_IDENTITY="Developer ID Application: Name (TEAMID)" \
INSTALLER_SIGN_IDENTITY="Developer ID Installer: Name (TEAMID)" \
NOTARY_PROFILE=my-profile \
./installer/make_installer.sh
```

## Pro Tools through Blue Cat PatchWork

Pro Tools does not load VST3 itself. Insert Blue Cat PatchWork as an AAX plug-in and load Channel Strip inside it.
If PatchWork does not list the plugin, add `~/Library/Audio/Plug-Ins/VST3` to its plugin scan folders and rescan.

## AAX

An AAX build needs the Avid AAX SDK (Avid developer account), and a retail Pro Tools only loads AAX plug-ins that are
PACE signed. The SDK and signing tools are not available here, so this project builds VST3 only.

## Tests

```
cmake -S . -B build-test -DCHANNELSTRIP_BUILD_TESTS=ON
cmake --build build-test --target ChannelStripTests
./build-test/ChannelStripTests_artefacts/Release/ChannelStripTests snapshots
```

The test executable checks gain, polarity, mono, pan, EQ, both compressors and the de-esser against measured levels,
the host-tempo hand-off and glow timing, the lightning slider (host updates, drag direction, Shift fine drag, reset,
accessibility) and the analyser (resolution, level, release speed).
It also writes PNG snapshots of the editor in both compressor modes.
