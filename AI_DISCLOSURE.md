# AI Assistance Disclosure

Following best practices for transparent AI-usage disclosure in
open-source projects (see e.g. the OpenSSF AI/ML Working Group
guidance and the Linux Foundation's "Generative AI Contributions"
statement templates), this file documents where and how generative AI
was used during the creation of this repository.

## Scope

- **AI tool used:** Anthropic Claude (Opus model), accessed via the
  Claude Code CLI.
- **Time period:** 2026-07-22 (single working session), plus follow-up
  live-hardware verification.
- **Human maintainer:** [@klausellus-wallace](https://github.com/klausellus-wallace)

## What the AI produced

The AI assisted with:

1. **Reverse engineering the LUX/Chigo IR protocol** from raw ESPHome
   `remote_receiver: dump: raw` log captures. Frame decoding, byte-field
   mapping, and the LSB-first-sum checksum formula were discovered
   iteratively in dialogue between the human maintainer and the AI.
2. **Drafting the ESPHome external-component skeleton**: the
   `climate_ir::ClimateIR` subclass wiring, `climate.py` codegen, and
   the pure-C++ `protocol.h`/`protocol.cpp` encoder/decoder.
3. **Producing the differential-testing harness** (`tests/*.cpp`,
   fixture extractor) and the initial README + docs.

Every code file and doc paragraph in this repository was AI-drafted,
then reviewed and (where needed) manually edited by the human
maintainer. Live-hardware verification (transmit and receive with the
physical AC) was performed by the human maintainer.

## Verification against generated content

All AI-produced protocol claims are verified against captured
Frame-B data from the original LUX remote (see `tests/fixtures.json`):
100 % checksum match on 15+ unique fixtures, 100 % round-trip
encode/decode match.

## Licensing implications

The MIT license (`LICENSE`) applies to all files in this repository
regardless of authorship. AI-drafted code is contributed under the
same terms; the human maintainer takes responsibility for the
copyright disposition of the contribution.
