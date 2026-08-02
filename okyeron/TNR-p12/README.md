# TNR-p12: a step sequencer for Plinky12 with 8 layers, inspired by the Yamaha Tenori-On.

Inspired in part by Chris Pirillo's "Tenori-Online: Interactive Web-Based Music Sequencer" https://pirillo.com/arcade/tenori-on.html Github: https://github.com/ChrisPirillo/tenori-on (code is posted with no license)


# Documentation

## Grid layout

- Rows 0-14, columns 0-15: the 15-pitch x 16-step note grid (240 pads)
- Row 15 (bottom control row):

| Button | Function |
|--------|----------|
| (0)-(7) | Layer select / mute — tap to select; tap active layer to toggle mute |
| (8) | Mode toggle — cycles active layer: Score → Random → Bounce |
| (9) | Loop range — hold to enter loop range editor (Score mode only); upper half rows 0-6: slide to set loop start column; lower half rows 7-14: slide to set loop end column |
| (10) | Anim type — tap to cycle trigger animation: None → Diamond → Square → Lines → Cross → Circle → None |
| (12) | Clear current layer |
| (14) | Stop transport |
| (15) | Play / arm transport |

## Layer modes

**Score** — left-to-right step sequencer; playhead sweeps columns each step. Per-layer loop range set with button (9). Pitches on rows (low = bottom row).

**Random** — each step, one lit pad is chosen at random. Last-fired pad flashes brightest; no playhead shown.

**Bounce** — each column is an independent pitch (low = left, high = right, pentatonic C3–C6). Tap a pad (rows 0-13) to place or move a ball; the ball falls to row 14, fires the note, and bounces back to the entry row, repeating. Entry row height sets the interval between fires. Tap row 14 of a column to stop that column's ball.

## Note grid interaction (Score / Random modes)

- Tap a pad to toggle it on/off
- Drag across pads to paint notes
- Hold and squeeze a lit pad to set its individual note velocity via touch pressure

## Note pitches

**Score / Random:** Pentatonic-major scale, C3 (MIDI 48) at row 14 up to A5 (MIDI 81) at row 0.

**Bounce:** Pentatonic-major scale, C3 (MIDI 48) at column 0 up to C6 (MIDI 84) at column 15.

## Voice and MIDI

Each layer uses one built-in synth preset slot (0-7) and one MIDI channel (0-7). Up to 12 synth voices are shared across active layers. All notes are also forwarded as MIDI for full polyphony on external gear.

## Pages

| Page | Content |
|------|---------|
| 0 | Main grid |
| 1 | Panel save / load picker |
| 2 | Per-layer synth editor (ADSR, filters, delay, reverb, pitch, granular) |
