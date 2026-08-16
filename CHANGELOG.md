# Changelog

All notable changes to KI1H are documented here.

## [2.2.0]

- LFO: when a cable is patched into the S&H clock input, the Sample Rate knob
  becomes a clock multiplier/divider for the clock output (÷8 … ×1 … ×8, centre
  detent = a clean clone of the input). The sample & hold itself still samples on
  the raw incoming clock; only the CLOCK OUT signal is multiplied/divided.
- FILTER: give each filter +/-12 V of internal headroom with a soft-clip output
  stage that leaves ordinary levels untouched and trends toward +/-10 V, so high
  resonance no longer sends the outputs far past viable range.

## [2.0.0]

Current beta. Seven modules: VCO, LFO, MIX, FILTER, ENVELOPE, KAOS, VCA.
