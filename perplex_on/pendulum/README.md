# PENDULUM

[Download the PDF manual](manual.pdf)

A pendulum music box for Plinky 12. Swipe a row and a damped pendulum swings across it, playing a note every time it hits a wall.

**See it in action:** https://www.instagram.com/reel/Dds5bK9Ct8s/?stkn=MTZlcWFsb2o5ejh3bg==


## Play

- **Swipe a row** to launch a pendulum. Each of the 16 rows is its own lane.
- **Row height = pitch.** The top row is the highest note.
- **Swipe direction** sets the starting wall.
- **Swipe speed** sets the swing rate, quantised to the main clock.
- **Pressure** sets velocity and energy.
- **Left wall** plays the row note, **right wall** plays the in-key fifth.
- Every hit costs energy. The pendulum slows down and finally comes to rest.
- **Double-tap a row** to clear it.

## Pages

Scroll right from the play page:

1. **Sound**: edit the synth preset.
2. **Save / Load**: store and recall your sounds.

Scroll left from the play page for the settings:

| Setting | What it does |
| --- | --- |
| key | Root note |
| scl | Scale |
| oct | Octave shift (-2 to +2) |
| damp | How fast energy fades (off = endless swing) |
| mode | `time` = swing rate from swipe speed, `dist` = from swipe distance (edge to mid-grid) |

## Info

- Panel ID: `perplex_on/pendulum`
- Author: PERPLEX ON
- Outputs: internal synth plus MIDI (one channel per row)
