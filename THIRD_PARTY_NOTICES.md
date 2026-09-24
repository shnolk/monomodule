# Third-party notices

Monomodule is © Shnolk and is licensed under the GNU Affero General Public License v3.0 (see `LICENSE`).
It is built from, or was written with the help of, the projects below. Each keeps its own copyright and licence.
The build downloads the libraries (JUCE and dsp56300); none of them is copied into this repository.

## Libraries compiled into Monomodule

| Project | Used for | Licence |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 8.0.9, © Raw Material Software Limited | plugin formats, UI, audio and MIDI I/O | GNU AGPL v3 (JUCE is dual-licensed: AGPLv3 or the commercial JUCE licence; Monomodule uses it under the AGPLv3) |
| [dsp56300](https://github.com/dsp56300/dsp56300), © the dsp56300 authors | Motorola DSP56300 emulator that runs the sound engine | GNU GPL v3 |
| [AsmJit](https://github.com/asmjit/asmjit), © the AsmJit authors | JIT compiler back end used by dsp56300 | zlib |
| Intel ITT / JIT profiling API (part of dsp56300, Windows and Linux builds), © Intel Corporation | profiler hooks of the dsp56300 JIT | BSD 3-Clause (dual-licensed: GPL-2.0-only or BSD-3-Clause; used under the BSD licence) |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk), © Steinberg Media Technologies GmbH (bundled with JUCE) | VST3 plugin format | GNU GPL v3 (dual-licensed: Steinberg VST3 licence or GPLv3; used under the GPLv3) |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK), © Apple Inc. (bundled with JUCE, macOS only) | Audio Unit plugin format | Apache 2.0 |
| Other JUCE dependencies (zlib, libpng, jpeglib, FLAC, Ogg Vorbis, HarfBuzz, SheenBidi and others) | as listed in JUCE's `LICENSE.md` | as listed there |

The GNU GPL v3 and the GNU AGPL v3 may be combined (section 13 of each), and the combined work is distributed under
the AGPL v3.

### Modifications to dsp56300

Monomodule applies `ext/patches/0001-dsp56300-mnm.patch` to dsp56300 commit
`c051afad31612c2d2c7a81a7ab23e1c5ac9e61af`. The patch adds:
- DSP56300 arithmetic saturation mode (SR.SM), in the interpreter and in both JIT back ends (x64 and AArch64)
- the MPYRI and PFLUSH instructions
- sign extension of 24-bit immediate multiply operands in the JIT
- an option to treat the interrupt vector region as ordinary code
- a recoverable failure path for when the JIT cannot allocate or generate code
- two Win64 calling-convention fixes in the x64 JIT: XMM6-XMM15 are saved and restored in full 128 bits, and
  a dead spill move that wrote a callee-saved XMM register without saving it is removed

The changes are marked `MNM patch` in the source. The patch is licensed under the GNU GPL v3, like dsp56300 itself.

## Reference and acknowledgements

- **[MCL](https://github.com/jmamma/MCL)**: Justin Mammarella, Yatao Li and Manuel Odendahl (BSD 3-Clause). Its
  documentation of the Monomachine sysex formats was the reference for Monomodule's own sysex codec
  (`src/core/library`). No MCL code is included.
- **[elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool)**: Marcel Bierling (MIT). A
  reference for the Elektron OS file container and compression. No code from it is included.

## Elektron

Monomodule is an independent project. It is not affiliated with, endorsed by or sponsored by Elektron Music Machines
MAQ AB. "Elektron" and "Monomachine" are trademarks of Elektron Music Machines MAQ AB, used here only to identify the
hardware that Monomodule emulates. Monomodule does not include any part of Elektron's software. It needs the
Monomachine OS file, which each user downloads from Elektron and selects when Monomodule first runs.
