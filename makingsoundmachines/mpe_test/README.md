# MPE Surface

By makingsoundmachines, based on mmalex’s MPE Test. This replaces the original example while keeping its `mpe_test` identity and stock panel-picker slot.

Play two expressive surfaces with pressure, pitch glide and brightness, using the internal synth or MPE output. Each sound has synth editing and preset load/save pages. Normal MIDI input uses channels 1 and 16; MPE uses two zones with seven member channels each.

Use the right-up button to reach the settings:

- **Root / scale:** choose the musical key and scale.
- **Stride:** spacing between strings, from 0–12 semitones.
- **Surface:** scale notes on every pad, or chromatic semitones with scale notes lit.
- **Direction:** vertical or horizontal strings.
- **Split:** side by side, top/bottom, or one full-size surface for sound A. Sound B remains available over MIDI.
- **Start:** begin on the root, or keep the lowest note near C1.
- **Glide:** smooth movement along strings, or independent pads.
- **Follow:** ignore incoming harmony, follow the key/scale, chord notes, or chord scale.

Settings, both sounds and the mix are saved with the song. Brightness is latched when a surface note starts. The unsplit surface still uses seven MPE member channels.

Old MPE Test songs retain their sounds, but use the new surface defaults and reset brightness. There is no save-format migration.
