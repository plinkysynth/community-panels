/*
@Name: TNR-p12
@Author: okyeron
@Tags: sequencer, groovebox, midi
@Preferred Panels: all
@Description: An 8-layer 16-step grid sequencer with Score, Random, and Bounce modes, inspired by the Yamaha Tenori-On.

Tenori-On: a 16x15 step sequencer with 8 layers, inspired by the Yamaha Tenori-On.

Inital inspiration based on js code by Chris Pirillo "Tenori-Online: Interactive Web-Based Music Sequencer"
https://pirillo.com/arcade/tenori-on.html


Grid layout:
  - Rows 0-14, columns 0-15: the 15-pitch x 16-step note grid (240 pads).
  - Row 15 (bottom control row):
      (0)-(7): layer select / mute — tap to select; tap active layer to toggle mute
      (8):     mode toggle — cycles active layer: Score → Random → Bounce
      (9):     loop range — hold to enter loop range editor (Score mode only)
               upper half rows 0-6: slide to set loop start column
               lower half rows 7-14: slide to set loop end column
      (10):    anim type — tap to cycle trigger animation for active layer
               None → Diamond → Square → Lines → Cross → Circle → None
      (12):    clear current layer
      (14):    stop transport
      (15):    play / arm transport

Layer modes:
  Score  — left-to-right step sequencer; playhead sweeps columns each step.
            Per-layer loop range set with button (9). Pitches on rows (low=bottom).
  Random — each step, one lit pad is chosen at random (xorshift32 RNG).
            Last-fired pad flashes brightest; no playhead shown.
  Bounce — each column is an independent pitch (low=left, high=right, pentatonic C3-C6).
            Tap a pad (rows 0-13) to place/move a ball; ball falls to row 14, fires the
            note, and bounces back to the entry row, repeating. Entry row height sets the
            interval between fires. Tap row 14 of a column to stop that column's ball.

Note grid interaction (Score / Random modes):
  - Tap a pad to toggle it on/off
  - Drag across pads to paint notes (NOT_ISOLATED)
  - Hold and squeeze a lit pad to set its individual note velocity via touch pressure

Note pitches (Score / Random):
  Pentatonic-major scale, C3 (MIDI 48) at row 14 up to A5 (MIDI 81) at row 0.

Each layer uses one built-in synth preset slot (0-7) and one MIDI channel (0-7).
The built-in synth gets up to 12 voices shared across active layers; all notes are
also forwarded as MIDI for full polyphony on external gear.

Pages:
  0 — main grid (this page)
  1 — panel save / load picker
  2 — per-layer synth editor (ADSR, filters, delay, reverb, pitch, granular)
*/

struct tenori_on : panel_t {

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    bool grid[8][15][16]; // grid[layer][row][col]
    uint8_t active_layer;

    // Sequencer clock and per-layer playheads
    clock_divider_t clock_div;
    playhead_t layer_playhead[8]; // one per layer; loop range set via loop_start/loop_end

    // Per-layer current column - written from on_sequence, read in on_ui.
    // uint8_t is single-byte so reads/writes are atomic on ARM Cortex-M.
    volatile uint8_t layer_col[8];

    // Per-layer loop range (Score mode). Default 0-15 (full 16 steps).
    uint8_t loop_start[8];
    uint8_t loop_end[8];

    // Voice pool shared across all layers.
    // Active layers split the 12 available voices equally; inactive layers get none.
    // e.g. 1 layer active -> 12 voices; 4 active -> 3 each; 8 active -> 1 each.
    static constexpr int NUM_LAYERS      = 8;
    static constexpr int MAX_VOICES_USED = 12;
    int8_t  voice_note[MAX_VOICES_USED];      // -1 = silent, else MIDI note number
    int8_t  voice_layer[MAX_VOICES_USED];     // which layer owns this voice (for play_synth preset)
    bool    voice_retrigger[MAX_VOICES_USED]; // true on the first on_sequence call for a new note
    bool    layer_muted[NUM_LAYERS];          // true = that layer's notes are silenced
    uint8_t note_velocity[NUM_LAYERS][15][16]; // 0-127 per pad, set by touch pressure
    int     voice_vel[MAX_VOICES_USED];       // velocity carried from assignment to play_synth

    // Touch pressure -> MIDI velocity.
    //
    // Uses the framework's touch_pressure_curve_q7() so pad response matches the
    // system Sens page bargraph and the user's calibrated sensitivity.
    //
    // The previous hand-rolled curve p*(256-p)/64 was non-monotonic: raw pressure
    // can reach 255 (not just 127), and that formula collapsed above ~p=200
    // (p=255 -> velocity 3), so the hardest presses produced near-silent notes.
    static int pressure_to_velocity(int pressure) {
        return clampi(touch_pressure_curve_q7(pressure), 1, 127);
    }

    // Layer playback modes
    static constexpr int MODE_SCORE  = 0;
    static constexpr int MODE_RANDOM = 1;
    static constexpr int MODE_BOUNCE = 2;
    static constexpr int MODE_PLAY   = 3;
    static constexpr int NUM_MODES   = 4;

    uint8_t layer_mode[NUM_LAYERS];

    // Bounce mode: per-column ball state (written audio core, read UI core).
    // int8_t is single-byte → atomic reads/writes on ARM Cortex-M.
    volatile int bounce_pos[NUM_LAYERS][16]; // current row, -1 = no ball
    volatile int bounce_dir[NUM_LAYERS][16]; // +1 = falling, -1 = rising

    // Random mode: last-fired pad for flash display (written on audio core, read on UI core)
    volatile int random_fired_row[NUM_LAYERS];
    volatile int random_fired_col[NUM_LAYERS];

    // Simple xorshift RNG (stdlib rand() not available in this build context)
    unsigned int rng_state;

    bool was_playing;

    // Play mode: live touch surface. on_ui writes these; on_sequence reads them.
    // Volatile int — 32-bit aligned, atomic on ARM; no lock guard needed per element.
    volatile int play_held[15][16]; // pressure per pad (0 = not held)
    volatile int play_held_layer;   // which layer owns play_held; -1 = none

    // Trigger animation: ripple pattern on note fire. Per-layer type toggled at (10,15).
    static constexpr int  ANIM_DURATION_FRAMES = 4;
    static constexpr int  ANIM_SIMPLE  = 0; // no animation
    static constexpr int  ANIM_DIAMOND = 1; // Manhattan rings: 4 adjacent dim, 8-pad diamond bright
    static constexpr int  ANIM_SQUARE  = 2; // Chebyshev rings: 8 adjacent dim, 16-pad square bright
    static constexpr int  ANIM_LINES   = 3; // Plus/axis: 4 cardinal dim, 4 cardinal-far bright
    static constexpr int  ANIM_CROSS   = 4; // Diagonal X: 4 diagonal dim, 4 diagonal-far bright
    static constexpr int  ANIM_CIRCLE  = 5; // Euclidean ring: 4 cardinal dim, 12-pad circle bright
    static constexpr int  NUM_ANIM_TYPES = 6;
    uint8_t layer_anim_type[NUM_LAYERS];   // animation type per layer
    int     layer_anim_frames[NUM_LAYERS]; // countdown frames per layer
    uint8_t layer_anim_col[NUM_LAYERS];    // last column that triggered animation per layer

    // Save/load UI (page 1)
    panel_page_t panel_page;

    // Synth edit UI (page 2)
    preset_pages_t synth_pages;
    bool           synth_preset_pick_mode;

    // -----------------------------------------------------------------------
    // panel_t overrides
    // -----------------------------------------------------------------------

    int get_num_pages() override { return 3; }

    // Everything NOT covered by on_serialise(): playheads, clock divider, voices,
    // held gates, balls, transient UI. Seeded at startup and reset again after a
    // panel load, where none of it carries over meaningfully from the old song.
    void reset_runtime_state() {
        clock_div = {};
        for (int i = 0; i < NUM_LAYERS; i++) {
            layer_playhead[i]         = {};
            layer_playhead[i].from    = loop_start[i];
            layer_playhead[i].to      = loop_end[i];
            layer_playhead[i].looping = true;
            layer_playhead[i].reset(-1, loop_start[i]);
            layer_col[i]              = loop_start[i];
            random_fired_row[i]       = -1;
            random_fired_col[i]       = -1;
            layer_anim_frames[i]      = 0;
            layer_anim_col[i]         = 255;
            for (int c = 0; c < 16; c++) {
                bounce_pos[i][c] = -1;
                bounce_dir[i][c] =  1;
            }
        }
        memset(voice_note,      -1,    sizeof(voice_note));
        memset(voice_layer,      0,    sizeof(voice_layer));
        memset(voice_retrigger, false, sizeof(voice_retrigger));
        memset(layer_muted,     false, sizeof(layer_muted));
        for (int i = 0; i < MAX_VOICES_USED; i++) voice_vel[i] = 100;
        rng_state        = 1;
        was_playing      = false;
        play_held_layer  = -1;
        for (int y = 0; y < 15; y++)
            for (int x = 0; x < 16; x++)
                play_held[y][x] = 0;
        synth_preset_pick_mode = false;
    }

    void setup_default_panel_state() override {
        panel_t::setup_default_panel_state();
        memset(grid, 0, sizeof(grid));
        active_layer = 0;
        for (int i = 0; i < NUM_LAYERS; i++) {
            loop_start[i]      = 0;
            loop_end[i]        = 15;
            layer_anim_type[i] = ANIM_SIMPLE;
        }
        memset(layer_mode,    0,   sizeof(layer_mode));
        memset(note_velocity, 100, sizeof(note_velocity));
        reset_runtime_state();
    }

    // A staged panel load restores serialised song state only. Without this the
    // outgoing song's playheads, sounding voices, bounce balls and held Play-mode
    // pads survive into the newly loaded song.
    void on_load_finished(void) override {
        reset_runtime_state();
    }

    // -----------------------------------------------------------------------
    // Note mapping: row -> MIDI note number
    //
    // Mirrors the web version's pentatonic-major formula:
    //   scale = [0, 2, 4, 7, 9], base = 48
    //   n = (ROWS - 1 - row)   where ROWS was 16 in the web version
    //
    // Here ROWS for notes is 15 (rows 0-14), so:
    //   n = 14 - row  (row 14 = n=0 = C3, row 0 = n=14 = A5)
    // -----------------------------------------------------------------------
    static int row_to_note(int row) {
        const int scale[5] = {0, 2, 4, 7, 9};
        int n = 14 - row;
        return 48 + (n / 5) * 12 + scale[n % 5];
    }

    // Bounce mode: columns 0-15 map to pitches left=low, right=high (pentatonic, C3-C6).
    static int col_to_note(int col) {
        const int scale[5] = {0, 2, 4, 7, 9};
        return 48 + (col / 5) * 12 + scale[col % 5];
    }

    // -----------------------------------------------------------------------
    // on_sequence: timing-critical step sequencer logic
    // -----------------------------------------------------------------------
    void on_sequence(int delta_time_us) override {
        (void)delta_time_us;

        // 4 steps per quarter note = 16th notes
        bool step    = clock_div.update(get_clock_phase(), 4);
        bool advance = step && sequencer_should_advance_playhead();
        for (int i = 0; i < NUM_LAYERS; i++) {
            layer_playhead[i].from = loop_start[i];
            layer_playhead[i].to   = loop_end[i];
            layer_playhead[i].update(advance);
            if (advance)
                layer_col[i] = layer_playhead[i].position;
        }

        // Play mode: level-triggered voice assignment, runs every frame.
        // Voices 0..play_next_voice-1 are reserved for the active play mode layer.
        int play_next_voice = 0;
        int play_layer_seq  = play_held_layer;
        if (play_layer_seq >= 0 && layer_mode[play_layer_seq] == MODE_PLAY && !layer_muted[play_layer_seq]) {
            for (int row = 14; row >= 0 && play_next_voice < MAX_VOICES_USED; row--) {
                for (int x = 0; x < 16 && play_next_voice < MAX_VOICES_USED; x++) {
                    int p = play_held[row][x];
                    if (p <= 0) continue;
                    int vel = pressure_to_velocity(p);
                    int note = row_to_note(row);
                    bool retrig = (voice_note[play_next_voice] != note);
                    voice_note[play_next_voice]      = note;
                    voice_layer[play_next_voice]     = play_layer_seq;
                    voice_vel[play_next_voice]       = vel;
                    voice_retrigger[play_next_voice] = retrig;
                    play_next_voice++;
                }
            }
            // Release play-mode voices that are no longer held.
            for (int v = play_next_voice; v < MAX_VOICES_USED; v++) {
                if (voice_layer[v] == play_layer_seq)
                    voice_note[v] = -1;
            }
        }

        if (step && is_transport_playing()) {

            // Advance Bounce mode balls; track which columns fire (reach bottom) this step.
            bool bounce_fires[NUM_LAYERS][16];
            for (int i = 0; i < NUM_LAYERS; i++)
                for (int c = 0; c < 16; c++) bounce_fires[i][c] = false;
            for (int layer = 0; layer < NUM_LAYERS; layer++) {
                if (layer_mode[layer] != MODE_BOUNCE || layer_muted[layer]) continue;
                for (int col = 0; col < 16; col++) {
                    int top = -1;
                    for (int row = 0; row < 14; row++)
                        if (grid[layer][row][col]) { top = row; break; }
                    if (top < 0) { bounce_pos[layer][col] = -1; continue; }
                    if (bounce_pos[layer][col] < 0) {
                        bounce_pos[layer][col] = top;
                        bounce_dir[layer][col] = 1;
                    }
                    int pos = bounce_pos[layer][col] + bounce_dir[layer][col];
                    if (pos >= 14) {
                        pos = 14;
                        bounce_dir[layer][col] = -1;
                        bounce_fires[layer][col] = true;
                    } else if (pos <= top) {
                        pos = top;
                        bounce_dir[layer][col] = 1;
                    }
                    bounce_pos[layer][col] = pos;
                }
            }

            // Count unmuted step-sequencer layers that have at least one note.
            int num_active = 0;
            for (int layer = 0; layer < NUM_LAYERS; layer++) {
                if (layer_muted[layer]) continue;
                if (layer_mode[layer] == MODE_PLAY) continue;
                int col = layer_col[layer];
                bool has_notes = false;
                if (layer_mode[layer] == MODE_SCORE) {
                    for (int row = 0; row < 15; row++)
                        if (grid[layer][row][col]) { has_notes = true; break; }
                } else if (layer_mode[layer] == MODE_BOUNCE) {
                    for (int c = 0; c < 16; c++)
                        if (bounce_fires[layer][c]) { has_notes = true; break; }
                } else { // MODE_RANDOM
                    for (int row = 0; row < 15 && !has_notes; row++)
                        for (int c = 0; c < 16 && !has_notes; c++)
                            if (grid[layer][row][c]) has_notes = true;
                }
                if (has_notes) num_active++;
            }

            // Divide the voice pool equally among active layers (at least 1 each).
            int vpL = (num_active > 0) ? (MAX_VOICES_USED / num_active) : 0;

            // Assign voice blocks to each active, unmuted step-sequencer layer in order.
            // Voices 0..play_next_voice-1 are already claimed by play mode.
            int next_voice = play_next_voice;
            for (int layer = 0; layer < NUM_LAYERS && next_voice < MAX_VOICES_USED; layer++) {
                if (layer_muted[layer]) continue;
                if (layer_mode[layer] == MODE_PLAY) continue;
                int col = layer_col[layer];

                if (layer_mode[layer] == MODE_SCORE) {
                    bool layer_active = false;
                    for (int row = 0; row < 15; row++) {
                        if (grid[layer][row][col]) { layer_active = true; break; }
                    }
                    if (!layer_active) continue;

                    int assigned = 0;
                    for (int row = 0; row < 15 && assigned < vpL && next_voice < MAX_VOICES_USED; row++) {
                        if (grid[layer][row][col]) {
                            voice_note[next_voice]      = (int8_t)row_to_note(row);
                            voice_layer[next_voice]     = (int8_t)layer;
                            voice_vel[next_voice]       = note_velocity[layer][row][col];
                            voice_retrigger[next_voice] = true;
                            next_voice++;
                            assigned++;
                        }
                    }
                } else if (layer_mode[layer] == MODE_BOUNCE) {
                    int assigned = 0;
                    for (int c = 0; c < 16 && assigned < vpL && next_voice < MAX_VOICES_USED; c++) {
                        if (!bounce_fires[layer][c]) continue;
                        int vel = 100;
                        for (int row = 0; row < 14; row++)
                            if (grid[layer][row][c]) { vel = note_velocity[layer][row][c]; break; }
                        voice_note[next_voice]      = col_to_note(c);
                        voice_layer[next_voice]     = layer;
                        voice_vel[next_voice]       = vel;
                        voice_retrigger[next_voice] = true;
                        next_voice++;
                        assigned++;
                    }
                } else { // MODE_RANDOM: pick one pad at random from all lit pads in this layer
                    int lit_count = 0;
                    for (int row = 0; row < 15; row++)
                        for (int c = 0; c < 16; c++)
                            if (grid[layer][row][c]) lit_count++;

                    if (lit_count > 0 && next_voice < MAX_VOICES_USED) {
                        rng_state ^= rng_state << 13;
                        rng_state ^= rng_state >> 17;
                        rng_state ^= rng_state << 5;
                        int target = (int)(rng_state % (unsigned int)lit_count);
                        int found  = 0;
                        bool done  = false;
                        for (int row = 0; row < 15 && !done; row++) {
                            for (int c = 0; c < 16 && !done; c++) {
                                if (grid[layer][row][c] && found++ == target) {
                                    voice_note[next_voice]      = (int8_t)row_to_note(row);
                                    voice_layer[next_voice]     = (int8_t)layer;
                                    voice_vel[next_voice]       = note_velocity[layer][row][c];
                                    voice_retrigger[next_voice] = true;
                                    random_fired_row[layer]     = row;
                                    random_fired_col[layer]     = c;
                                    next_voice++;
                                    done = true;
                                }
                            }
                        }
                    }
                }
            }
            // Release any voices not claimed this step.
            for (int v = next_voice; v < MAX_VOICES_USED; v++) {
                voice_note[v]      = -1;
                voice_retrigger[v] = false;
            }
        }

        // Silence all voices and reset playheads when transport is stopped.
        bool playing_now = is_transport_playing();
        if (!playing_now) {
            memset(voice_note,       -1,    sizeof(voice_note));
            memset(voice_retrigger,  false, sizeof(voice_retrigger));
            for (int i = 0; i < NUM_LAYERS; i++) random_fired_row[i] = -1;
            for (int i = 0; i < NUM_LAYERS; i++) random_fired_col[i] = -1;
            for (int i = 0; i < NUM_LAYERS; i++)
                for (int c = 0; c < 16; c++) bounce_pos[i][c] = -1;
            if (was_playing) {
                // reset() rather than writing .position: it also clears the
                // playhead's runtime latch state without eating the next edge.
                for (int i = 0; i < NUM_LAYERS; i++) {
                    layer_playhead[i].reset(-1, loop_start[i]);
                    layer_col[i] = loop_start[i];
                }
            }
        }
        was_playing = playing_now;

        // Drive the built-in synth (level-triggered: call every frame).
        for (int v = 0; v < MAX_VOICES_USED; v++) {
            if (voice_note[v] >= 0) {
                play_synth(v, voice_layer[v], voice_vel[v], (int)voice_note[v] * 256, voice_retrigger[v]);
                voice_retrigger[v] = false;
            } else {
                play_synth(v, voice_layer[v], 0, 0, false);
            }
        }

        // MIDI output: declare every active note across unmuted layers and channels.
        if (is_transport_playing()) {
            for (int layer = 0; layer < NUM_LAYERS; layer++) {
                if (layer_muted[layer]) continue;
                int col = layer_col[layer];
                if (layer_mode[layer] == MODE_SCORE) {
                    for (int row = 0; row < 15; row++) {
                        if (grid[layer][row][col]) {
                            declare_midi_note(layer, row_to_note(row), note_velocity[layer][row][col]);
                        }
                    }
                } else if (layer_mode[layer] == MODE_BOUNCE) {
                    // Sustain note for any ball currently at the bottom row
                    for (int c = 0; c < 16; c++) {
                        if (bounce_pos[layer][c] == 14) {
                            int vel = 100;
                            for (int row = 0; row < 14; row++)
                                if (grid[layer][row][c]) { vel = note_velocity[layer][row][c]; break; }
                            declare_midi_note(layer, col_to_note(c), vel);
                        }
                    }
                } else { // MODE_RANDOM: sustain the last-fired note
                    int fired_row = random_fired_row[layer];
                    int fired_col = random_fired_col[layer];
                    if (fired_row >= 0) {
                        declare_midi_note(layer, row_to_note(fired_row), note_velocity[layer][fired_row][fired_col]);
                    }
                }
            }
        }
        // Play mode MIDI: transport-independent — declare notes for all held pads.
        // De-duplicate by row so each pitch is declared once at max pressure.
        if (play_layer_seq >= 0 && layer_mode[play_layer_seq] == MODE_PLAY && !layer_muted[play_layer_seq]) {
            for (int row = 0; row < 15; row++) {
                int best = 0;
                for (int x = 0; x < 16; x++) {
                    int p = play_held[row][x];
                    if (p > best) best = p;
                }
                if (best > 0) {
                    int vel = pressure_to_velocity(best);
                    declare_midi_note(play_layer_seq, row_to_note(row), vel);
                }
            }
        }
        send_declared_midi_notes();
    }

    // -----------------------------------------------------------------------
    // on_ui: LED drawing and touch handling (~8 ms cadence, main core)
    // -----------------------------------------------------------------------
    void on_ui(int delta_time_us) override {
        (void)delta_time_us;
        leds_clear();

        // One colour per layer (8 rainbow-spread colours)
        const uint32_t layer_colors[8] = {
            WHITE, CYAN, GREEN, YELLOW, ORANGE, PINK, BLUE, TEAL
        };

        int  col     = layer_col[active_layer]; // per-layer playhead position
        bool playing = is_transport_playing();

        // Auto-save settings to SD when transport stops
        static bool prev_playing_ui = false;
        if (prev_playing_ui && !playing) {
            save_settings_to_sd(false);
        }
        prev_playing_ui = playing;

        // Detect new sequencer step per layer to trigger animations
        if (playing) {
            for (int i = 0; i < NUM_LAYERS; i++) {
                int lc = layer_col[i];
                if (lc != (int)layer_anim_col[i]) {
                    layer_anim_col[i]    = lc;
                    layer_anim_frames[i] = ANIM_DURATION_FRAMES;
                }
            }
        } else {
            for (int i = 0; i < NUM_LAYERS; i++) layer_anim_frames[i] = 0;
        }

        // Loop range edit mode: hold (9,15) while in Score mode to adjust loop start/end
        bool loop_edit = get_touch_down(9, 15) && (layer_mode[active_layer] == MODE_SCORE);

        // ------------------------------------------------------------------
        // Note grid: rows 0-14, cols 0-15
        // ------------------------------------------------------------------
        bool is_score  = (layer_mode[active_layer] == MODE_SCORE);
        int  fired_row = random_fired_row[active_layer];
        int  fired_col = random_fired_col[active_layer];

        if (loop_edit) {
            // Loop range editor: show notes, intercept touches to set loop start/end
            int ls = loop_start[active_layer];
            int le = loop_end[active_layer];
            for (int x = 0; x < 16; x++) {
                for (int y = 0; y < 15; y++) {
                    bool active   = grid[active_layer][y][x];
                    bool in_range = (x >= ls && x <= le);
                    uint32_t color;
                    if (active && in_range)
                        color = BRIGHTER(layer_colors[active_layer]);
                    else if (active)
                        color = DIMMEST(layer_colors[active_layer]);
                    else if (in_range)
                        color = DIMMEST(WHITE);
                    else
                        color = 0;
                    button(x, y, color, NOT_ISOLATED);
                }
                // Column start/end markers
                if (x == ls)
                    set_led(x, 0, BRIGHTEST(layer_colors[active_layer]));
                if (x == le)
                    set_led(x, 14, BRIGHTEST(layer_colors[active_layer]));
            }
            // Touch in upper half (y 0-6) sets loop start; lower half (y 7-14) sets loop end
            for (int x = 0; x < 16; x++) {
                for (int y = 0; y < 15; y++) {
                    if (get_touch_down(x, y)) {
                        if (y <= 6) {
                            int ns = x;
                            if (ns > (int)loop_end[active_layer]) ns = loop_end[active_layer];
                            loop_start[active_layer] = ns;
                        } else {
                            int ne = x;
                            if (ne < (int)loop_start[active_layer]) ne = loop_start[active_layer];
                            loop_end[active_layer] = ne;
                        }
                    }
                }
            }
        } else if (layer_mode[active_layer] == MODE_PLAY) {
            // Play mode: live touch surface. Rows = pitches (same as Score).
            // Update play_held so on_sequence can assign voices and MIDI.
            play_held_layer = active_layer;
            for (int y = 0; y < 15; y++)
                for (int x = 0; x < 16; x++)
                    play_held[y][x] = get_touch_pressure_xy(x, y);
            // Trigger animation on the first held pad each frame the timer is idle.
            if (layer_anim_type[active_layer] != ANIM_SIMPLE && layer_anim_frames[active_layer] == 0) {
                for (int y = 0; y < 15; y++) {
                    for (int x = 0; x < 16; x++) {
                        if (play_held[y][x] > 0) {
                            random_fired_row[active_layer] = y;
                            random_fired_col[active_layer] = x;
                            layer_anim_frames[active_layer] = ANIM_DURATION_FRAMES;
                            goto play_anim_done;
                        }
                    }
                }
                play_anim_done:;
            }
            // Render: off when not held, BRIGHTEST when held.
            for (int x = 0; x < 16; x++) {
                for (int y = 0; y < 15; y++) {
                    uint32_t color = (play_held[y][x] > 0)
                        ? BRIGHTEST(layer_colors[active_layer])
                        : 0;
                    set_led(x, y, color);
                }
            }
        } else if (layer_mode[active_layer] == MODE_BOUNCE) {
            // Bounce mode: columns are pitches; balls drop to row 14 to fire a note.
            // Tap rows 0-13 to set/move a column's entry point. Tap row 14 to stop it.
            for (int x = 0; x < 16; x++) {
                int ball = bounce_pos[active_layer][x]; // -1 = no ball
                for (int y = 0; y < 15; y++) {
                    bool is_entry = (y < 14 && grid[active_layer][y][x]);
                    bool is_ball  = (ball >= 0 && y == ball);
                    uint32_t color;
                    if (is_ball)        color = BRIGHTEST(layer_colors[active_layer]);
                    else if (is_entry)  color = BRIGHTER(layer_colors[active_layer]);
                    else                color = 0;
                    if (button(x, y, color, NOT_ISOLATED)) {
                        on_sequence_lock_guard_t guard;
                        if (y == 14) {
                            for (int r = 0; r < 15; r++) grid[active_layer][r][x] = false;
                            bounce_pos[active_layer][x] = -1;
                        } else {
                            bool already = grid[active_layer][y][x];
                            for (int r = 0; r < 15; r++) grid[active_layer][r][x] = false;
                            if (!already) grid[active_layer][y][x] = true;
                            bounce_pos[active_layer][x] = -1; // restart from new entry
                        }
                    }
                    if (is_entry) {
                        int p = get_touch_pressure_xy(x, y);
                        if (p > 0)
                            note_velocity[active_layer][y][x] = pressure_to_velocity(p);
                    }
                }
            }
        } else {
            for (int x = 0; x < 16; x++) {
                bool is_ph = playing && is_score && (x == col);
                for (int y = 0; y < 15; y++) {
                    bool active   = grid[active_layer][y][x];
                    bool is_fired = !is_score && playing && (y == fired_row) && (x == fired_col);

                    uint32_t color;
                    if (active && (is_ph || is_fired))
                        color = BRIGHTEST(layer_colors[active_layer]);
                    else if (active)
                        color = BRIGHTER(layer_colors[active_layer]);
                    else if (is_ph)
                        color = DIMMEST(WHITE);
                    else
                        color = 0;

                    // NOT_ISOLATED allows drag-painting across the grid
                    if (button(x, y, color, NOT_ISOLATED)) {
                        on_sequence_lock_guard_t guard;
                        grid[active_layer][y][x] = !grid[active_layer][y][x];
                    }
                    if (active) {
                        int p = get_touch_pressure_xy(x, y);
                        if (p > 0)
                            note_velocity[active_layer][y][x] = pressure_to_velocity(p);
                    }
                }
            }
        }

        // Trigger animations: per-layer ripple outward from each fired pad
        if (!loop_edit) {
            static const int dmnd_ndx[4]   = {-1, 1,  0,  0};
            static const int dmnd_ndy[4]   = { 0, 0, -1,  1};
            static const int dmnd_fdx[8]   = {-2, 2,  0,  0, -1, -1,  1,  1};
            static const int dmnd_fdy[8]   = { 0, 0, -2,  2, -1,  1, -1,  1};

            static const int sq_ndx[8]     = {-1,-1,-1, 0, 0, 1, 1, 1};
            static const int sq_ndy[8]     = {-1, 0, 1,-1, 1,-1, 0, 1};
            static const int sq_fdx[16]    = {-2,-2,-2,-2,-2, 2, 2, 2, 2, 2,-1, 0, 1,-1, 0, 1};
            static const int sq_fdy[16]    = {-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-2,-2, 2, 2, 2};

            static const int ln_ndx[4]     = {-1, 1,  0,  0};
            static const int ln_ndy[4]     = { 0, 0, -1,  1};
            static const int ln_fdx[4]     = {-2, 2,  0,  0};
            static const int ln_fdy[4]     = { 0, 0, -2,  2};

            static const int cross_ndx[4]  = {-1, 1, -1,  1};
            static const int cross_ndy[4]  = {-1,-1,  1,  1};
            static const int cross_fdx[4]  = {-2, 2, -2,  2};
            static const int cross_fdy[4]  = {-2,-2,  2,  2};

            static const int circ_ndx[4]   = {-1, 1,  0,  0};
            static const int circ_ndy[4]   = { 0, 0, -1,  1};
            static const int circ_fdx[12]  = { 2,-2, 0, 0, 2, 2,-2,-2, 1,-1, 1,-1};
            static const int circ_fdy[12]  = { 0, 0, 2,-2, 1,-1, 1,-1, 2,-2,-2, 2};

            for (int al = 0; al < NUM_LAYERS; al++) {
                int anim_type = layer_anim_type[al];
                if (al != (int)active_layer)         continue;
                if (layer_mode[al] == MODE_BOUNCE)   continue;
                if (anim_type == ANIM_SIMPLE)        continue;
                if (layer_anim_frames[al] <= 0)  continue;
                if (layer_muted[al])             continue;
                layer_anim_frames[al]--;

                const int *ndx, *ndy, *fdx, *fdy;
                int nn, fn;
                if (anim_type == ANIM_SQUARE) {
                    ndx = sq_ndx;    ndy = sq_ndy;    nn = 8;
                    fdx = sq_fdx;    fdy = sq_fdy;    fn = 16;
                } else if (anim_type == ANIM_LINES) {
                    ndx = ln_ndx;    ndy = ln_ndy;    nn = 4;
                    fdx = ln_fdx;    fdy = ln_fdy;    fn = 4;
                } else if (anim_type == ANIM_CROSS) {
                    ndx = cross_ndx; ndy = cross_ndy; nn = 4;
                    fdx = cross_fdx; fdy = cross_fdy; fn = 4;
                } else if (anim_type == ANIM_CIRCLE) {
                    ndx = circ_ndx;  ndy = circ_ndy;  nn = 4;
                    fdx = circ_fdx;  fdy = circ_fdy;  fn = 12;
                } else { // ANIM_DIAMOND
                    ndx = dmnd_ndx;  ndy = dmnd_ndy;  nn = 4;
                    fdx = dmnd_fdx;  fdy = dmnd_fdy;  fn = 8;
                }

                int num_pads = 0;
                int pad_xs[15], pad_ys[15];

                if (layer_mode[al] == MODE_SCORE) {
                    int ac = layer_anim_col[al];
                    for (int row = 0; row < 15; row++)
                        if (grid[al][row][ac] && num_pads < 15)
                            { pad_xs[num_pads] = ac; pad_ys[num_pads] = row; num_pads++; }
                } else {
                    int fr = random_fired_row[al];
                    int fc = random_fired_col[al];
                    if (fr >= 0)
                        { pad_xs[0] = fc; pad_ys[0] = fr; num_pads = 1; }
                }

                for (int f = 0; f < num_pads; f++) {
                    int cx = pad_xs[f], cy = pad_ys[f];
                    for (int d = 0; d < nn; d++) {
                        int nx = cx + ndx[d], ny = cy + ndy[d];
                        if (nx >= 0 && nx < 16 && ny >= 0 && ny < 15)
                            set_led(nx, ny, DIMMER(layer_colors[al]));
                    }
                    for (int d = 0; d < fn; d++) {
                        int nx = cx + fdx[d], ny = cy + fdy[d];
                        if (nx >= 0 && nx < 16 && ny >= 0 && ny < 15)
                            set_led(nx, ny, BRIGHTER(layer_colors[al]));
                    }
                }
            }
        }

        // ------------------------------------------------------------------
        // Control row (y = 15)
        // ------------------------------------------------------------------

        // Layer select: pads (0,15)-(7,15)
        // Press active layer to toggle mute; press any other layer to select it.
        for (int i = 0; i < NUM_LAYERS; i++) {
            uint32_t c;
            if (i == (int)active_layer)
                c = layer_muted[i] ? DIMMER(layer_colors[i]) : BRIGHTER(layer_colors[i]);
            else
                c = layer_muted[i] ? DIMMEST(layer_colors[i]) : DIMMER(layer_colors[i]);
            if (button(i, 15, c, ISOLATED, "Layer")) {
                if (i == (int)active_layer) {
                    layer_muted[i] = !layer_muted[i];
                } else {
                    // Clear play_held so the old layer's held notes release.
                    play_held_layer = -1;
                    for (int y = 0; y < 15; y++)
                        for (int x = 0; x < 16; x++)
                            play_held[y][x] = 0;
                    active_layer = (uint8_t)i;
                }
            }
        }

        // Mode toggle: (8,15) - cycles the active layer through available modes
        {
            static const char* mode_name[NUM_MODES] = { "Score", "Random", "Bounce", "Play" };
            int cur_mode = layer_mode[active_layer];
            uint32_t mc = (cur_mode == MODE_SCORE)  ? DIMMEST(WHITE)  :
                          (cur_mode == MODE_RANDOM) ? DIMMEST(ORANGE) :
                          (cur_mode == MODE_BOUNCE) ? DIMMEST(GREEN)  : DIMMEST(PINK);
            if (button(8, 15, mc, ISOLATED, mode_name[(cur_mode + 1) % NUM_MODES])) {
                layer_mode[active_layer] = (cur_mode + 1) % NUM_MODES;
                random_fired_row[active_layer] = -1;
                random_fired_col[active_layer] = -1;
                for (int c = 0; c < 16; c++) bounce_pos[active_layer][c] = -1;
                if (cur_mode == MODE_PLAY) {
                    play_held_layer = -1;
                    for (int y = 0; y < 15; y++)
                        for (int x = 0; x < 16; x++)
                            play_held[y][x] = 0;
                }
            }
        }

        // Loop range: (9,15) - hold to enter loop range edit mode (Score mode only)
        {
            bool is_full = (loop_start[active_layer] == 0 && loop_end[active_layer] == 15);
            uint32_t lc;
            if (layer_mode[active_layer] != MODE_SCORE)
                lc = 0;
            else if (loop_edit)
                lc = BRIGHTER(CYAN);
            else if (!is_full)
                lc = DIMMER(CYAN);
            else
                lc = DIMMEST(CYAN);
            button(9, 15, lc, ISOLATED, "Loop range");
        }

        // Anim type toggle: (10,15) - cycles active layer's trigger animation type
        {
            static const char* anim_names[NUM_ANIM_TYPES] = {
                "Anim: None", "Anim: Diamond", "Anim: Square",
                "Anim: Lines", "Anim: Cross",  "Anim: Circle"
            };
            int cur_anim = layer_anim_type[active_layer];
            bool anim_held = get_touch_down(10, 15);
            uint32_t ac;
            if (anim_held)
                ac = BRIGHTER(layer_colors[active_layer]);
            else if (cur_anim == ANIM_SIMPLE)
                ac = DIMMEST(layer_colors[active_layer]);
            else
                ac = DIMMER(layer_colors[active_layer]);
            if (button(10, 15, ac, ISOLATED, anim_names[(cur_anim + 1) % NUM_ANIM_TYPES])) {
                layer_anim_type[active_layer] = (cur_anim + 1) % NUM_ANIM_TYPES;
            }
        }

        // Clear current layer: (12,15)
        if (button(12, 15, DIMMER(RED), ISOLATED, "Clear layer")) {
            on_sequence_lock_guard_t guard;
            memset(grid[active_layer], 0, sizeof(grid[active_layer]));
        }

        // Standard transport controls at the bottom-right corner
        stop_button(14, 15);
        play_button(15, 15);

        // ------------------------------------------------------------------
        // Page 1: panel save / load picker
        // ------------------------------------------------------------------
        panel_page.saveload(16);

        // ------------------------------------------------------------------
        // Page 2: per-layer synth editor
        //   y=32..38  top: 16 sliders full-width (ADSR|Pan|Tone|Delay|Reverb|Vol)
        //   y=39..45  bottom: 8 sliders x=0..7 | XY morph pad x=8..15
        //   y=47      layer select (x=0..7) + load preset (x=9) + stop (x=14) + play (x=15)
        //   Mix params encoded as MIX_PARAM_* + 128 for synth_param_slider.
        // ------------------------------------------------------------------
        {
            constexpr int page_y = 32;

            if (synth_preset_pick_mode) {
                int rv = synth_pages.saveload_action(active_layer, page_y);
                if (rv == -1) {
                    synth_preset_pick_mode = false;
                    scroll_to_page(2); // cancel stays on synth page
                } else if (rv != 0) {
                    synth_preset_pick_mode = false; // ok/save → saveload_action already scrolled to page 0
                }
            } else {
                synth_pages.edit(active_layer, page_y);
                synth_pages.xy_pad(active_layer, 8, page_y + 10);

                for (int i = 0; i < NUM_LAYERS; i++) {
                    uint32_t c;
                    if (i == (int)active_layer)
                        c = layer_muted[i] ? DIMMER(layer_colors[i]) : BRIGHTER(layer_colors[i]);
                    else
                        c = layer_muted[i] ? DIMMEST(layer_colors[i]) : DIMMER(layer_colors[i]);
                    if (button(i, page_y + 15, c, ISOLATED, "Layer")) {
                        if (i == (int)active_layer) {
                            layer_muted[i] = !layer_muted[i];
                        } else {
                            play_held_layer = -1;
                            for (int y = 0; y < 15; y++)
                                for (int x = 0; x < 16; x++)
                                    play_held[y][x] = 0;
                            active_layer = (uint8_t)i;
                        }
                    }
                }
                {
                    static const char* mode_name[NUM_MODES] = { "Score", "Random", "Bounce", "Play" };
                    int cur_mode = layer_mode[active_layer];
                    uint32_t mc = (cur_mode == MODE_SCORE)  ? DIMMEST(WHITE)  :
                                  (cur_mode == MODE_RANDOM) ? DIMMEST(ORANGE) :
                                  (cur_mode == MODE_BOUNCE) ? DIMMEST(GREEN)  : DIMMEST(PINK);
                    if (button(8, page_y + 15, mc, ISOLATED, mode_name[(cur_mode + 1) % NUM_MODES])) {
                        layer_mode[active_layer] = (cur_mode + 1) % NUM_MODES;
                        random_fired_row[active_layer] = -1;
                        random_fired_col[active_layer] = -1;
                        for (int c = 0; c < 16; c++) bounce_pos[active_layer][c] = -1;
                        if (cur_mode == MODE_PLAY) {
                            play_held_layer = -1;
                            for (int y = 0; y < 15; y++)
                                for (int x = 0; x < 16; x++)
                                    play_held[y][x] = 0;
                        }
                    }
                }
                if (button(9, page_y + 15, DIMMER(WHITE), ISOLATED, "Load preset")) {
                    synth_preset_pick_mode = true;
                    synth_pages.picker.reset_which_slot_is_selected();
                }
                stop_button(14, page_y + 15);
                play_button(15, page_y + 15);
            }
        }
    }

    // -----------------------------------------------------------------------
    // on_serialise: persist grid data and all eight synth presets
    //
    // FIELD_SYNTH_PRESET expands to `o.synth_presets[index]`, so we create a
    // local alias `o` that the macro can resolve without a standalone function.
    // -----------------------------------------------------------------------
    bool on_serialise(serialiser_t &s, int version) override {
        (void)version;
        auto &o = *this; // required by FIELD_SYNTH_PRESET macro
        OBJECT_BEGIN(s);
        // Each o.grid[i] is a bool[15][16] — the 2-D array template handles it.
        FIELD("grid0", o.grid[0]);
        FIELD("grid1", o.grid[1]);
        FIELD("grid2", o.grid[2]);
        FIELD("grid3", o.grid[3]);
        FIELD("grid4", o.grid[4]);
        FIELD("grid5", o.grid[5]);
        FIELD("grid6", o.grid[6]);
        FIELD("grid7", o.grid[7]);
        FIELD("layer",  o.active_layer);
        FIELD("modes",  o.layer_mode);
        FIELD("anim",   o.layer_anim_type);
        FIELD("lstart", o.loop_start);
        FIELD("lend",   o.loop_end);
        FIELD("vel0",  o.note_velocity[0]);
        FIELD("vel1",  o.note_velocity[1]);
        FIELD("vel2",  o.note_velocity[2]);
        FIELD("vel3",  o.note_velocity[3]);
        FIELD("vel4",  o.note_velocity[4]);
        FIELD("vel5",  o.note_velocity[5]);
        FIELD("vel6",  o.note_velocity[6]);
        FIELD("vel7",  o.note_velocity[7]);
        FIELD_SYNTH_PRESET("preset0", 0);
        FIELD_SYNTH_PRESET("preset1", 1);
        FIELD_SYNTH_PRESET("preset2", 2);
        FIELD_SYNTH_PRESET("preset3", 3);
        FIELD_SYNTH_PRESET("preset4", 4);
        FIELD_SYNTH_PRESET("preset5", 5);
        FIELD_SYNTH_PRESET("preset6", 6);
        FIELD_SYNTH_PRESET("preset7", 7);
        FIELD_MIX_PRESET("mix");
        OBJECT_END(s);
        return true;
    }

    bool on_serialise_settings(serialiser_t &s, int version) override {
        return on_serialise(s, version);
    }
};
