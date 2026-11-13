/*
@Name: QU4TTRO
@Author: VRCVS (Filippo Rieder)
@Firmware: latest
@Description: Four melodic tracks side by side, plonky-style (strings a fifth apart, always in key), each with its own
  live/step looper, length, grid and direction: polyrhythmic loops. Inspired by Zack Scholl's norns script plonky.
@Documentation: https://github.com/vrcvs/plinky12_qu4ttro
@Discussion: https://github.com/vrcvs/plinky12_qu4ttro/issues
@Tags: looper, sequencer, polyrhythm, multitrack, plonky
@Preferred Panels: chords
@Category: looper / sequencer

Version 1.0
QU4TTRO by VRCVS - https://www.instagram.com/vrcvs/
Manual (IT/EN): see the QU4TTRO_by_VRCVS PDFs published with this panel.

CREDITS
  Freely inspired by "plonky", the norns script by Zack Scholl (schollz / infinitedigits),
  https://github.com/schollz/plonky (MIT licence) - which was itself inspired by mmalex's original Plinky.
  No plonky code is used: QU4TTRO is a new implementation for Plinky 12.
  Plinky 12 by mmalex and Making Sound Machines. Written with the help of Claude (Anthropic).

LICENCE
  MIT License - Copyright (c) 2026 Filippo Rieder (VRCVS)
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
  documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
  persons to whom the Software is furnished to do so, subject to the following conditions: the above copyright notice and
  this permission notice shall be included in all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
  WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
  OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

QUICK REFERENCE (Chords panel; rows/columns counted from 0, top-left)
  Rows 2-13   four 8x6 sectors: T1 top-left, T2 top-right, T3 bottom-left (bass), T4 bottom-right.
              Each column is a string: the note rises going up, the next column starts a fifth higher. Pressure = velocity,
              sliding between pads without lifting = glide (with the preset's GLIDE).
  Row 0       x0-3 select track | x4-7 mute | x8-11 play/stop per track | x12 start mode (white = step 1, yellow = resume,
              cyan = CUE) | x13 CUE (hold + tap a pad of the track)
  Row 1       x0 LEN (hold + tap a pad = 1-48 steps) | x1-3 16/64/256 steps | x4-7 direction | x8-15 position
  Row 14/15   triangles: x0 octave | x1 key | x2 scale (each value has its own colour)
  Row 14      x7 mono legato | x8 +STEP | x9 next note legato | x10 step back | x12 MIX | x13 SND | x14 PRE
  Row 15      x5-11 grid (1/2 1/4 1/8 1/16 1/32 1/8T 1/16T) | x12 rec | x13 X erase/clear | x14 stop | x15 play
  REC         playing + empty track: armed, the loop starts on the first note; tap again = close the loop (restarts on the
              next step); on an existing loop = overdub. Stopped: step entry (+STEP = stretch held notes / add a rest).
  PRE         system sound picker on the round pads (instant preview, play = load, X = cancel, rec = alternative SD browser)
  Right side button down: SONG save/load (loops, settings, the 4 sounds and the mix).
*/
#define PANEL_PAD_COLOR YELLOW // colour of QU4TTRO in the panel picker
// LED convention used everywhere:
//   BRIGHTEST(c) = active / selected / sounding
//   c            = available (a button you can press, a pad you can play)
//   DIMMER(c)    = off / not selected, but still visible

static const scale_t k_scales[8] = {
    SCALE_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_PENTATONIC_MINOR,
    SCALE_MAJOR, SCALE_LYDIAN, SCALE_HIRAJOSHI, SCALE_CHROMATIC,
};
static const char *k_scale_names[8] = {"minor", "dorian", "phrygian", "pent. minor",
                                       "major", "lydian", "hirajoshi", "chromatic"};
static const uint32_t k_track_col[4] = {PINK, TEAL, ORANGE, GREEN};
// colour codes (they stay on, so you can read the setting at a glance)
// key: naturals C red, D orange, E yellow, F green, G cyan, A blue, B purple; sharps = the natural below, dimmer
static const uint32_t k_key_col[12] = {RED, DIMMER(RED), ORANGE, DIMMER(ORANGE), YELLOW, GREEN,
                                       DIMMER(GREEN), CYAN, DIMMER(CYAN), BLUE, DIMMER(BLUE), PURPLE};
// scales: minor green, dorian cyan, phrygian purple, minor pentatonic teal, major blue, lydian yellow, hirajoshi pink, chromatic white
static const uint32_t k_scale_col[8] = {GREEN, CYAN, PURPLE, TEAL, BLUE, YELLOW, PINK, WHITE};
static const int8_t k_def_oct[4] = {0, 0, -12, 0}; // default octave of each track (= the "0" colour)
static const playorder_t k_orders[4] = {FORWARD, REVERSE, PINGPONG, RANDOM};
// SOUND BROWSER (PRE): reads the folders of /PLINKY/BANKS on the SD and loads presets directly,
// bypassing the system preset picker. Row 10 = categories (folders in BANKS), rows 1-8 = banks, columns = presets.
static constexpr int MAXCATS = 16, MAXBANKS = 64, BROWSER_ROWS = 8;
// fallback if the SD cannot be listed: USER1/USER2 with BANK1-BANK8
// one fixed colour per category (fixed named colours: they look the same on the real LEDs and in the emulator)
static const uint32_t k_cat_col[8] = {PINK, CYAN, ORANGE, GREEN, YELLOW, BLUE, PURPLE, TEAL};
static const char *k_fallback_cat[2] = {"USER1", "USER2"};
static const char *k_fallback_bank[8] = {"BANK1", "BANK2", "BANK3", "BANK4", "BANK5", "BANK6", "BANK7", "BANK8"};
// grid: steps per quarter note = num/den. 1/2, 1/4, 1/8, 1/16, 1/32, 1/8 triplet, 1/16 triplet
static constexpr int NRATES = 7;
static const uint8_t k_rate_num[NRATES] = {1, 1, 2, 4, 8, 3, 6};
static const uint8_t k_rate_den[NRATES] = {2, 1, 1, 1, 1, 1, 1};
static const char *k_rate_names[NRATES] = {"grid 1/2", "grid 1/4", "grid 1/8", "grid 1/16", "grid 1/32",
                                           "grid 1/8 triplet", "grid 1/16 triplet"};

struct qu4ttro : panel_t {
  static constexpr int T = 4, W = 8, H = 6, NPADS = W * H, MAXSTEPS = 256, NVOICES = DEFAULT_VOICE_ALLOCATOR_VOICES;

  // --- song state ---
  uint64_t seq[T][MAXSTEPS]; // per step: mask of the 48 pads that are sounding
  uint16_t len_steps[T];     // loop length in grid steps, 1..256 (independent per track = polyrhythms)
  uint8_t order_idx[T];
  int8_t octave[T];
  bool muted[T];
  uint8_t sel, scale_idx, root_pc; // selected track, scale, key (0=C)

  // --- runtime ---
  uint64_t held[T];             // pads held live (written in on_ui under lock)
  uint8_t held_vel[T][NPADS];
  uint64_t prev_active[T];
  uint64_t attack[T][MAXSTEPS]; // per step: pads that START on that step (separates repeated notes from ties)
  uint8_t vel[T][MAXSTEPS][NPADS]; // recorded pressure per step and pad (velocity + pressure modulation on playback)
  // slides (glide): each sounding pad has a "source" pad; a finger sliding without lifting keeps the same source = same voice
  uint8_t src_cur[T][NPADS];
  uint64_t slid[T];             // pads entered by sliding from another pad (written in on_ui)
  uint8_t slide_from[T][NPADS]; // which pad the slide came from
  uint64_t prev_src[T];         // sources sounding in the previous frame
  uint64_t vel_seen[T];         // live rec: pads whose pressure was already captured in the current step
  // mixer page and song saving
  slider_t mix_sliders[T][4];
  panel_page_t song_page;
  enum { MODE_PLAY, MODE_MIX, MODE_SOUND, MODE_PRESET };
  bool factory_picker;          // PRESET: false = MY BANKS grid, true = system picker (factory sounds)
  char cat_names[MAXCATS][17];  // category folders found in /PLINKY/BANKS
  int8_t ncat;                  // -1 = not scanned yet, 0 = listing not possible (fallback)
  char bank_names[MAXBANKS][17];// bank folders of the current category
  int8_t nbank;
  uint8_t cur_cat;              // shown category
  int8_t cur_bank;              // selected bank in the category (-1 = none)
  uint64_t preset_mask;         // presets present in the selected bank (file XY.json = row X, column Y)
  uint64_t preset_bad;          // presets that failed to load
  char tr_cat[T][17], tr_bank[T][17]; // what each track has loaded from the browser
  int8_t tr_preset[T];
  int8_t cand_row, cand_col;   // auditioned preset waiting for LOAD (-1 = none)
  volatile int8_t preview_vel;  // >0 while a MY BANKS pad is held: the preview note plays on a spare voice
  bool preview_on;
  uint8_t mode;  // what page 0 is showing
  uint64_t last_held[T];
  int16_t last_pos[T];
  int8_t last_mono[T];
  bool legato[T];               // track in mono-legato mode (uses the preset's glide)
  // step entry (rec on + transport stopped), plonky-style
  uint16_t wpos[T], rec_len[T];  // write position and length of the recorded sequence
  uint64_t srec_prev;           // pads held in the previous UI frame (step entry)
  bool srec_open;               // a step is being written (notes still held)
  bool next_tied;               // next note/chord without an attack (legato)
  // live recording on an empty track: the loop stays open until rec stops, the last step decides the length
  bool rec_free[T], was_rec[T];
  bool pend_restart[T];   // free recording just ended: restart the loop from step 1 on the next grid step
  volatile bool armwait;  // rec held while playing: recording starts on the first note of the selected track
  volatile bool erasing;
  clock_divider_t div[T];      // one clock per track: each track has its own grid
  uint8_t rate_idx[T];         // grid / step length: see k_rates
  playhead_t ph[T];
  voice_allocator_t va;
  preset_pages_t pp;
  hold_timer_t clear_timer;

  static int sector_x(int t) { return (t & 1) * 8; }
  static int sector_y(int t) { return 2 + (t >> 1) * 6; }
  int steps(int t) const { return clampi(len_steps[t], 1, MAXSTEPS); }
  uint32_t flash_us[3]; // octave / key / scale: time of the last change (white flash)
  bool len_held; // LEN held: the sector of the selected track becomes a length selector
  // independent per-track transport
  bool running[T];            // track on/off (plays when the global transport is on)
  volatile bool start_req[T]; // the track has just been switched on: position it according to its start mode
  uint8_t start_mode[T];      // 0 = from step 1, 1 = resume where it stopped, 2 = from the CUE step
  uint16_t cue[T];
  bool cue_held;

  int note_for(int t, int pad) const {
    int col = pad / H, pos = pad % H;
    int degree = col * 4 + pos; // 4 degrees = a fifth in a 7-note scale
    int base = 48 + root_pc + octave[t];
    int n = base + degree_to_note(degree, k_scales[scale_idx]);
    while (n > 108) n -= 12; // above C8 the engine stays silent: fold back down an octave
    while (n < 12) n += 12;
    return n;
  }

  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("QU4TTRO by VRCVS v1.0\n");
    memset(seq, 0, sizeof(seq));
    memset(attack, 0, sizeof(attack));
    memset(vel, 0, sizeof(vel));
    memset(slid, 0, sizeof(slid));
    memset(prev_src, 0, sizeof(prev_src));
    erasing = false;
    static const int8_t def_oct[T] = {0, 0, -12, 0};
    for (int t = 0; t < T; ++t) {
      len_steps[t] = 16;
      rate_idx[t] = 3; // 1/16
      running[t] = true;
      start_req[t] = false;
      start_mode[t] = 0;
      cue[t] = 0;
      order_idx[t] = 0;
      octave[t] = def_oct[t];
      muted[t] = false;
      held[t] = prev_active[t] = last_held[t] = 0;
      last_pos[t] = -1;
      last_mono[t] = -1;
      legato[t] = false;
      wpos[t] = 0;
      rec_len[t] = 0;
      rec_free[t] = was_rec[t] = pend_restart[t] = false;
      ph[t].reset(FORWARD, 0);
    }
    sel = 0;
    for (int t = 0; t < T; ++t) tr_preset[t] = -1;
    ncat = -1;
    cand_row = -1;
    scale_idx = 0;
    root_pc = 2; // D
  }

  int get_num_pages(void) override { return 2; } // 0 instrument (play / mixer / sound / preset), 1 song save/load
  int get_num_tracks(void) override { return T; }
  const char *get_track_name(int i, char buf[16]) override {
    if (!buf) return NULL;
    snprintf(buf, 16, "T%d", (i & 3) + 1);
    return buf;
  }

  // ---------------- MUSIC (timing-critical) ----------------
  void on_sequence(int delta_time_us) override {
    bool playing = is_transport_playing();
    bool restart = has_transport_just_started();
    bool can_adv = sequencer_should_advance_playhead();
    for (int t = 0; t < T; ++t) {
      int edges = div[t].update(-1, k_rate_num[rate_idx[t] % NRATES], k_rate_den[rate_idx[t] % NRATES], UPDATE_DIV_ON_HALF_NOTE); // a grid change takes effect on the next half note (also works for 1/2)
      int adv = can_adv ? edges : 0;
      bool late = div[t].phase_q16() >= 32768; // second half of the step: a new note belongs to the next step
      ph[t].from = 0;
      ph[t].to = steps(t) - 1;
      // while the sound of this track is being chosen (PRE), the track stays silent: the system can replace
      // its preset (and copy the samples) without voices still using it
      bool loading_this = (mode == MODE_PRESET && t == sel);
      bool run = playing && running[t] && !loading_this;
      uint64_t newly = held[t] & ~last_held[t]; // pads just pressed
      last_held[t] = held[t];
      bool first_note_start = false;
      if (armwait && run && t == sel && newly) { // armed by holding rec: start on the first note
        armwait = false;
        start_recording();
        first_note_start = true;
      }
      bool rec_now = run && is_recording() && t == sel;
      if (rec_now && !was_rec[t]) { // recording starts: on an empty track the loop opens from step 1
        bool empty = true;
        for (int i = 0; i < MAXSTEPS; ++i) if (seq[t][i]) { empty = false; break; }
        if (empty) { rec_free[t] = true; ph[t].reset(k_orders[order_idx[t]], 0); last_pos[t] = -1; }
      }
      if (!rec_now && was_rec[t] && rec_free[t]) { // recording ends: the loop is closed here
        // like a guitar looper: the loop lasts from the start of recording to the step where you disarm
        // (that step included); it restarts from step 1 at the end of the current step.
        bool any = false;
        for (int i = 0; i < MAXSTEPS; ++i) if (seq[t][i]) { any = true; break; }
        if (any) { len_steps[t] = (uint16_t)((ph[t].position % MAXSTEPS) + 1); pend_restart[t] = true; }
        rec_free[t] = false;
      }
      was_rec[t] = rec_now;
      int loop_len = rec_free[t] ? MAXSTEPS : steps(t);
      ph[t].looping = true;
      ph[t].to = loop_len - 1;
      if ((restart || start_req[t]) && running[t]) {
        start_req[t] = false;
        if (start_mode[t] == 0) ph[t].reset(k_orders[order_idx[t]], 0);                       // from step 1
        else if (start_mode[t] == 2) ph[t].reset(k_orders[order_idx[t]], cue[t] % steps(t));  // from the CUE
        // start_mode 1: resume where it stopped
        last_pos[t] = -1; // play the starting step right away
      }
      if (pend_restart[t] && adv > 0) { // quantised: the next grid step is step 1 of the new loop
        ph[t].reset(k_orders[order_idx[t]], 0);
        last_pos[t] = -1;
        pend_restart[t] = false;
        adv = 0;
      }
      ph[t].update(run ? adv : 0, k_orders[order_idx[t]]);
      int pos = ph[t].position % MAXSTEPS;
      bool new_step = pos != last_pos[t];
      last_pos[t] = pos;


      if (run && t == sel) {
        if (erasing) { seq[t][pos] = 0; attack[t][pos] = 0; }          // X held: erase under the playhead
        else if (is_recording()) {
          // quantise to the NEAREST 16th: a late press goes onto the next step (in forward order)
          int npos = (late && !first_note_start && k_orders[order_idx[t]] == FORWARD) ? (pos + 1) % loop_len : pos;
          uint64_t att = newly & ~slid[t]; // a slide is not a new attack: it plays back as a glide
          seq[t][npos] |= newly; attack[t][npos] |= att;
          seq[t][pos] |= held[t] & ~newly; // held notes = ties
          if (npos != pos) seq[t][npos] |= held[t] & ~newly;
          if (new_step) vel_seen[t] = 0;
          for (int pad = 0; pad < NPADS; ++pad) { // pressure of every held note: the PEAK within each step
            uint64_t bit = (uint64_t)1 << pad;
            if (!(held[t] & bit)) continue;
            uint8_t v = held_vel[t][pad];
            int wp = (newly & bit) ? npos : pos;
            if (!(vel_seen[t] & bit) || (newly & bit)) { vel[t][wp][pad] = v; vel_seen[t] |= bit; }
            else if (v > vel[t][wp][pad]) vel[t][wp][pad] = v;
          }
        }
      }

      uint64_t from_seq = (run && !muted[t]) ? seq[t][pos] : 0;
      uint64_t step_attack = (run && new_step) ? attack[t][pos] : 0;
      uint64_t active = loading_this ? 0 : (held[t] | from_seq);

      if (legato[t]) {
        // mono: a single voice per track, the highest pad wins; moving between notes = glide (preset GLIDE)
        int pad = active ? 63 - __builtin_clzll(active) : -1;
        uint32_t id = ((uint32_t)t << 8) | 0xFF;
        if (pad >= 0) {
          uint64_t bit = (uint64_t)1 << pad;
          int old_v = va.find_voice(id, 0, NVOICES);
          int v = va.voice_allocate(id, 2, 0, NVOICES, t);
          if (v != old_v) synth_note_up(old_v);
          if (v >= 0) {
            bool live = (held[t] & bit) != 0;
            bool retrig = !prev_active[t] || v != old_v || ((step_attack & bit) && !live);
            int pv = live ? held_vel[t][pad] : (vel[t][pos][pad] ? vel[t][pos][pad] : 100);
            play_synth(v, t, pv, note_for(t, pad) << 8, retrig);
          }
          last_mono[t] = pad;
        }
      } else {
        // poly: every sounding pad belongs to a "source". A pad entered by sliding (live), or a recorded note that
        // starts without an attack, inherits the source of the pad it came from: same voice, new pitch = glide.
        uint64_t ended = prev_active[t] & ~active;
        uint64_t src_used = 0;
        int8_t best_pad[NPADS]; // small: on_sequence runs with little stack
        for (int i = 0; i < NPADS; ++i) best_pad[i] = -1;
        for (int pad = 0; pad < NPADS; ++pad) {
          uint64_t bit = (uint64_t)1 << pad;
          if (!(active & bit)) continue;
          bool live = (held[t] & bit) != 0;
          if (!(prev_active[t] & bit)) { // newly sounding: choose its source
            int src = pad;
            if (live && (slid[t] & bit)) {
              src = src_cur[t][slide_from[t][pad] % NPADS];
            } else if (!live && !(step_attack & bit) && ended) {
              int e = __builtin_ctzll(ended); // a recorded slide/legato: take over a note that just ended
              ended &= ended - 1;
              src = src_cur[t][e];
            }
            src_cur[t][pad] = (uint8_t)(src % NPADS);
          }
          int src = src_cur[t][pad];
          int cur = best_pad[src];
          int pv = live ? held_vel[t][pad] : vel[t][pos][pad];
          int cv = cur < 0 ? -1 : ((held[t] >> cur) & 1 ? held_vel[t][cur] : vel[t][pos][cur]);
          if (cur < 0 || pv >= cv) best_pad[src] = (int8_t)pad; // two pads on one source (finger between pads): the stronger wins
        }
        for (int src = 0; src < NPADS; ++src) {
          int pad = best_pad[src];
          if (pad < 0) continue;
          uint64_t bit = (uint64_t)1 << pad;
          src_used |= (uint64_t)1 << src;
          bool live = (held[t] & bit) != 0;
          uint32_t id = ((uint32_t)t << 8) | (uint32_t)src;
          int old_v = va.find_voice(id, 0, NVOICES);
          int v = va.voice_allocate(id, live ? 2 : 1, 0, NVOICES, t);
          if (v != old_v) synth_note_up(old_v);
          if (v < 0) continue;
          // a new source, or a recorded note with its own attack: retrigger. otherwise tie or glide.
          bool retrig = !(prev_src[t] & ((uint64_t)1 << src)) || v != old_v || (!live && (step_attack & bit));
          int pv = live ? held_vel[t][pad] : (vel[t][pos][pad] ? vel[t][pos][pad] : 100);
          play_synth(v, t, pv, note_for(t, pad) << 8, retrig);
        }
        prev_src[t] = src_used;
      }
      prev_active[t] = active;
    }
    // MY BANKS preview: voice 11 is outside the allocator (which uses voices 0-7)
    if (preview_vel > 0) { play_synth(11, 11, preview_vel, 60 << 8, !preview_on); preview_on = true; } // voice 11, audition slot 11
    else if (preview_on) { synth_note_up(11); preview_on = false; }
    uint16_t stale = va.garbage_collect_voices(1, 0, 0, 0, NVOICES);
    while (stale) {
      int v = __builtin_ctz((unsigned)stale);
      synth_note_up(v);
      stale &= (uint16_t)(stale - 1);
    }
  }

  // ---------------- UI ----------------
  // sector as a step selector, reading order left to right, top to bottom = steps 1..48
  void draw_length_selector(int t) {
    int x0 = sector_x(t), y0 = sector_y(t);
    for (int i = 0; i < NPADS; ++i) {
      int x = x0 + (i % W), y = y0 + (i / W);
      if (cue_held) { // CUE: shows the start step (and, dimmer, the track's length)
        uint32_t c = (i == cue[t]) ? BRIGHTEST(CYAN) : (i < steps(t) ? DIMMER(k_track_col[t]) : DIMMER(WHITE));
        set_led(x, y, c);
        if (get_touch_pressed(x, y) && i < steps(t)) { cue[t] = i; start_mode[t] = 2; }
      } else {
        bool on = i < steps(t);
        set_led(x, y, on ? (((i / 4) & 1) ? k_track_col[t] : WHITE) : DIMMER(WHITE));
        if (get_touch_pressed(x, y)) len_steps[t] = i + 1;
      }
    }
    on_sequence_lock_guard_t lock;
    held[t] = 0;
  }

  void draw_and_read_sector(int t) {
    if ((len_held || cue_held) && t == sel) { draw_length_selector(t); return; }
    int x0 = sector_x(t), y0 = sector_y(t);
    uint32_t col = k_track_col[t];
    uint64_t h = 0;
    uint64_t slid_ui = slid[t] & 0; // rebuilt each frame from new presses
    uint64_t sounding = prev_active[t];
    for (int pad = 0; pad < NPADS; ++pad) {
      int c = pad / H, p = pad % H;
      int x = x0 + c, y = y0 + (H - 1 - p);
      int pr = get_touch_pressure_xy(x, y);
      if (get_touch_down(x, y)) {
        h |= (uint64_t)1 << pad;
        if (get_touch_pressed(x, y)) { // did this finger slide in from another pad of the same sector?
          int ox = get_touch_origin_x(x, y) - x0, oy = get_touch_origin_y(x, y) - y0;
          if ((ox != c || oy != H - 1 - p) && ox >= 0 && ox < W && oy >= 0 && oy < H) {
            slid_ui |= (uint64_t)1 << pad;
            slide_from[t][pad] = (uint8_t)(ox * H + (H - 1 - oy));
          }
        }
        held_vel[t][pad] = (uint8_t)clampi(touch_pressure_curve_q7(pr), 1, 127);
      }
      bool is_root = (note_for(t, pad) % 12) == root_pc;
      uint32_t c_led = is_root ? col : DIMMER(col);
      if (muted[t]) c_led = DIMMER(col); // muted: whole sector low
      if (sounding & ((uint64_t)1 << pad)) c_led = BRIGHTEST(col);
      set_led(x, y, c_led);
    }
    {
      on_sequence_lock_guard_t lock;
      held[t] = h;
      slid[t] = (slid[t] | slid_ui) & h; // keep the slide flag while the finger stays down
    }
  }

  // step entry: the last written step decides the loop length
  void mark_written(int w) {
    if (w + 1 > rec_len[sel]) rec_len[sel] = w + 1;
    len_steps[sel] = rec_len[sel] < 1 ? 1 : rec_len[sel];
  }
  void close_step(void) {
    wpos[sel] = (wpos[sel] + 1) % MAXSTEPS;
    srec_open = false;
  }

  bool track_empty(int t) const {
    for (int i = 0; i < MAXSTEPS; ++i) if (seq[t][i]) return false;
    return true;
  }

  void draw_page0(void) {
    for (int t = 0; t < T; ++t) draw_and_read_sector(t);

    // row 0: track select + mute
    for (int t = 0; t < T; ++t) {
      uint32_t col = k_track_col[t];
      if (button(t, 0, t == sel ? BRIGHTEST(col) : DIMMER(col), ISOLATED, "select track")) sel = t;
      if (button(4 + t, 0, muted[t] ? DIMMER(col) : col, ISOLATED, "mute")) muted[t] = !muted[t];
      // per-track play/stop: blinks on the beat while it is running
      uint32_t rc = running[t] ? ((is_transport_playing() && (div[t].phase_q16() < 16384)) ? BRIGHTEST(col) : col) : DIMMER(col);
      if (button(8 + t, 0, rc, ISOLATED, "track play / stop")) {
        on_sequence_lock_guard_t lock;
        running[t] = !running[t];
        if (running[t]) start_req[t] = true;
      }
    }
    // start mode of the selected track: white = from step 1, yellow = resume, cyan = from the CUE
    static const uint32_t mode_col[3] = {WHITE, YELLOW, CYAN};
    if (button(12, 0, BRIGHTEST(mode_col[start_mode[sel]]), ISOLATED, "start: from step 1 / resume / from the CUE"))
      start_mode[sel] = (start_mode[sel] + 1) % 3;
    cue_held = shift_button(13, 0, CYAN, ISOLATED, "hold CUE + tap a pad of the track: start step");
    // row 1: length, direction, playhead
    uint32_t sc = k_track_col[sel];
    // LEN: hold it and tap a pad of the selected track = length 1..48 steps. Then 16 / 32 / 64 quick buttons.
    len_held = shift_button(0, 1, BRIGHTEST(WHITE), ISOLATED, "hold LEN + tap a pad of the track: length 1-48 steps");
    static const uint16_t quick[3] = {16, 64, 256};
    for (int i = 0; i < 3; ++i)
      if (button(1 + i, 1, steps(sel) == quick[i] ? BRIGHTEST(WHITE) : DIMMER(WHITE), ISOLATED, "length: 16 / 64 / 256 steps"))
        len_steps[sel] = quick[i];
    for (int i = 0; i < 4; ++i)
      if (button(4 + i, 1, order_idx[sel] == i ? BRIGHTEST(YELLOW) : DIMMER(YELLOW), ISOLATED,
                 "direction: forward / reverse / pingpong / random"))
        order_idx[sel] = i;
    bool step_rec = is_recording() && !is_transport_playing();
    if (step_rec) { // show the write position: lit pad = step within the group of 8, colour brightens by bar
      int w = wpos[sel];
      for (int i = 0; i < 8; ++i) set_led(8 + i, 1, i == (w & 7) ? ((w >> 3) & 1 ? WHITE : RED) : DIMMER(RED));
    } else {
      int px = (ph[sel].position % steps(sel)) * 8 / steps(sel);
      for (int i = 0; i < 8; ++i) set_led(8 + i, 1, i == px ? sc : DIMMER(sc));
    }
    // --- step entry ---
    uint64_t h = held[sel];
    if (step_rec) {
      uint64_t newly = h & ~srec_prev;
      srec_prev = h;
      if (newly) { // a press opens the step; other pads pressed while it is held = same chord
        on_sequence_lock_guard_t lock;
        int w = wpos[sel];
        if (!srec_open) { srec_open = true; seq[sel][w] = 0; attack[sel][w] = 0; }
        seq[sel][w] |= h;
        if (!next_tied) attack[sel][w] |= newly;
        for (int pad = 0; pad < NPADS; ++pad) if (h & ((uint64_t)1 << pad)) vel[sel][w][pad] = held_vel[sel][pad];
        mark_written(w);
      }
      if (srec_open && h) { // keep the PEAK pressure while the notes are held (a press starts light)
        int w = wpos[sel];
        for (int pad = 0; pad < NPADS; ++pad)
          if ((h & ((uint64_t)1 << pad)) && held_vel[sel][pad] > vel[sel][w][pad]) vel[sel][w][pad] = held_vel[sel][pad];
      }
      if (srec_open && !h) { close_step(); next_tied = false; } // everything released: move to the next step
    } else {
      srec_prev = 0;
      srec_open = false;
    }
    // +STEP: with notes held = stretch them by one step (tie); with nothing held = one step of rest
    if (button(8, 14, step_rec ? YELLOW : DIMMER(YELLOW), ISOLATED, "+STEP: notes held = stretch / nothing held = rest"))
      if (step_rec) {
        on_sequence_lock_guard_t lock;
        if (srec_open && h) {
          close_step();
          int w = wpos[sel];
          seq[sel][w] = h; attack[sel][w] = 0; srec_open = true;
          for (int pad = 0; pad < NPADS; ++pad) if (h & ((uint64_t)1 << pad)) vel[sel][w][pad] = held_vel[sel][pad];
          mark_written(w);
        } else if (!h) {
          int w = wpos[sel];
          seq[sel][w] = 0; attack[sel][w] = 0;
          mark_written(w);
          close_step();
        }
      }
    if (button(9, 14, next_tied ? BRIGHTEST(CYAN) : DIMMER(CYAN), ISOLATED, "step rec: next note legato"))
      next_tied = !next_tied;
    if (button(10, 14, step_rec ? RED : DIMMER(RED), ISOLATED, "step rec: back/delete")) if (step_rec && !h) {
      on_sequence_lock_guard_t lock;
      int w = (wpos[sel] + MAXSTEPS - 1) % MAXSTEPS;
      seq[sel][w] = attack[sel][w] = 0;
      wpos[sel] = w;
      if (rec_len[sel] == w + 1) { rec_len[sel] = w; len_steps[sel] = w ? w : 1; }
    }

    // rows 14-15: octave, key, scale (up/down)
    // octave / key / scale: each value has its own colour, which stays on (and flashes white for a moment on a change)
    uint32_t now = get_on_ui_time();
    // octave of the selected track: -3 magenta, -2 red, -1 orange, 0 white, +1 cyan, +2 green, +3 yellow
    static const uint32_t oct_col[7] = {PURPLE, RED, ORANGE, WHITE, CYAN, GREEN, YELLOW};
    uint32_t oc = oct_col[clampi((octave[sel] - k_def_oct[sel]) / 12 + 3, 0, 6)];
    uint32_t kc = k_key_col[root_pc % 12];
    uint32_t scc = k_scale_col[scale_idx & 7];
    if (now - flash_us[0] < 150000) oc = BRIGHTEST(WHITE);
    if (now - flash_us[1] < 150000) kc = BRIGHTEST(WHITE);
    if (now - flash_us[2] < 150000) scc = BRIGHTEST(WHITE);
    if (button(0, 14, oc, ISOLATED, "octave +")) { octave[sel] = clampi(octave[sel] + 12, -36, 36); flash_us[0] = now; }
    if (button(0, 15, oc, ISOLATED, "octave -")) { octave[sel] = clampi(octave[sel] - 12, -36, 36); flash_us[0] = now; }
    if (button(1, 14, kc, ISOLATED, "key +")) { root_pc = (root_pc + 1) % 12; flash_us[1] = now; }
    if (button(1, 15, kc, ISOLATED, "key -")) { root_pc = (root_pc + 11) % 12; flash_us[1] = now; }
    if (button(2, 14, scc, ISOLATED, k_scale_names[(scale_idx + 1) & 7])) { scale_idx = (scale_idx + 1) & 7; flash_us[2] = now; }
    if (button(2, 15, scc, ISOLATED, k_scale_names[(scale_idx + 7) & 7])) { scale_idx = (scale_idx + 7) & 7; flash_us[2] = now; }

    // transport
    // REC, like a looper. Playing + empty track: tap = armed (blinks), the loop starts on the first note you play.
    // Playing + track with a loop: tap = overdub on/off. Stopped: tap = step entry on/off.
    {
      bool blink = (get_on_ui_time() >> 17) & 1;
      uint32_t rc = is_recording() ? BRIGHTEST(RED) : (armwait ? (blink ? BRIGHTEST(RED) : DIMMER(RED)) : RED);
      if (button(12, 15, rc, ISOLATED, "rec: empty track = starts on the first note | loop = overdub | stopped = step entry")) {
        if (armwait) armwait = false;                       // cancel the wait
        else if (is_recording()) stop_recording();          // closes the loop / ends the overdub
        else if (is_transport_playing() && track_empty(sel)) armwait = true;
        else start_recording();
      }
      if (!is_transport_playing()) armwait = false;
    }
    // X: while playing, held = erase under the playhead; while stopped, hold 1s = clear the whole track
    bool x_held = shift_button(13, 15, RED, ISOLATED, "hold while playing: erase | stopped, 1s: clear track");
    erasing = x_held && is_transport_playing();
    if (clear_timer.is_last_widget_held_for(1000000) && !is_transport_playing()) {
      on_sequence_lock_guard_t lock;
      memset(seq[sel], 0, sizeof(seq[sel]));
      memset(attack[sel], 0, sizeof(attack[sel]));
      wpos[sel] = 0;
      rec_len[sel] = 0;
    }
    // grid of the selected track (row 15, x5-11): 1/2, 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T
    for (int i = 0; i < NRATES; ++i)
      if (button(5 + i, 15, rate_idx[sel] == i ? BRIGHTEST(ORANGE) : DIMMER(ORANGE), ISOLATED, k_rate_names[i]))
        rate_idx[sel] = i;
    // modes: MIXER / SOUND / PRESET of the selected track (row 14, above rec / X / stop)
    // they open when you LIFT the finger: otherwise the same touch would land on the new page (e.g. on a bank)
    button(12, 14, GREEN, ISOLATED, "MIXER: volume, pan, delay, reverb");
    if (is_last_widget_released()) mode = MODE_MIX;
    button(13, 14, PINK, ISOLATED, "SOUND of the selected track");
    if (is_last_widget_released()) mode = MODE_SOUND;
    button(14, 14, BLUE, ISOLATED, "PRESET: load a sound on the selected track");
    if (is_last_widget_released()) { pp.picker.reset_which_slot_is_selected(); mode = MODE_PRESET; factory_picker = true; ncat = -1; cand_row = -1; }
    // legato toggle for the selected track (first rectangle in the bottom row)
    if (button(7, 14, legato[sel] ? BRIGHTEST(CYAN) : DIMMER(CYAN), ISOLATED, "legato mono (glide)"))
      legato[sel] = !legato[sel];
    stop_button(14, 15, RED);
    play_button(15, 15, GREEN);
  }

  // MIXER page: 4 columns per track = volume, pan/stereo, delay send, reverb send. Bottom rows: select + mute.
  void draw_mixer(int y) {
    static const int mix_params[4] = {VOICE_PARAM_VOLUME, VOICE_PARAM_STEREO, VOICE_PARAM_DELAY_SEND, VOICE_PARAM_REVERB_SEND};
    for (int t = 0; t < T; ++t) {
      synth_param_sliders_block(mix_sliders[t], t * 4, y, 4, 14, t, mix_params);
      uint32_t col = k_track_col[t];
      for (int i = 0; i < 4; ++i) {
        if (i < 2) {
          if (button(t * 4 + i, y + 14, t == sel ? BRIGHTEST(col) : DIMMER(col), ISOLATED, "select track")) sel = t;
        } else {
          if (button(t * 4 + i, y + 14, muted[t] ? DIMMER(col) : col, ISOLATED, "mute")) muted[t] = !muted[t];
        }
        set_led(t * 4 + i, y + 15, i == 0 ? col : DIMMER(col));
      }
    }
  }

  // SAVE: song = loops + per-track settings + the 4 sounds + mix (effects)
  bool on_serialise(serialiser_t &s, int version) override {
    (void)version;
    qu4ttro &o = *this;
    int n_seq = (int)sizeof(seq), n_att = (int)sizeof(attack), n_vel = (int)sizeof(vel);
    int n_len = T, n_rate = T, n_ord = T, n_oct = T, n_mut = T, n_leg = T, n_run = T, n_sm = T, n_cue = T;
    OBJECT_BEGIN(s);
    FIELD_SYNTH_PRESET("sound1", 0);
    FIELD_SYNTH_PRESET("sound2", 1);
    FIELD_SYNTH_PRESET("sound3", 2);
    FIELD_SYNTH_PRESET("sound4", 3);
    FIELD_MIX_PRESET("mix");
    FIELD("scale", scale_idx, 0u, 7u);
    FIELD("key", root_pc, 0u, 11u);
    FIELD_BASE64("len", len_steps, n_len, (int)sizeof(len_steps));
    FIELD_BASE64("grid", rate_idx, n_rate, (int)sizeof(rate_idx));
    FIELD_BASE64("order", order_idx, n_ord, (int)sizeof(order_idx));
    FIELD_BASE64("oct", octave, n_oct, (int)sizeof(octave));
    FIELD_BASE64("mute", muted, n_mut, (int)sizeof(muted));
    FIELD_BASE64("legato", legato, n_leg, (int)sizeof(legato));
    FIELD_BASE64("run", running, n_run, (int)sizeof(running));
    FIELD_BASE64("start", start_mode, n_sm, (int)sizeof(start_mode));
    FIELD_BASE64("cue", cue, n_cue, (int)sizeof(cue));
    FIELD_BASE64("seq", seq, n_seq, (int)sizeof(seq));
    FIELD_BASE64("attack", attack, n_att, (int)sizeof(attack));
    FIELD_BASE64("vel", vel, n_vel, (int)sizeof(vel));
    OBJECT_END(s);
    if (s.reading) { // rebuild runtime state after loading
      for (int t = 0; t < T; ++t) {
        len_steps[t] = (uint16_t)clampi(len_steps[t], 1, MAXSTEPS);
        rate_idx[t] %= NRATES;
        order_idx[t] &= 3;
        start_mode[t] %= 3;
        held[t] = prev_active[t] = last_held[t] = slid[t] = prev_src[t] = 0;
        last_pos[t] = -1;
        rec_free[t] = was_rec[t] = pend_restart[t] = false;
        rec_len[t] = len_steps[t];
        wpos[t] = 0;
      }
    }
    return true;
  }

  // like the factory panels: scroll page 0 = the instrument (with modes), scroll page 1 = song save/load.
  void clear_held(void) {
    on_sequence_lock_guard_t lock;
    for (int t = 0; t < T; ++t) held[t] = 0;
  }
  bool back_button(void) { button(15, 15, BRIGHTEST(WHITE), ISOLATED, "back to play"); return is_last_widget_released(); }

  // MY BANKS: rows 1-8 = BANK1-BANK8 of USER1 or USER2, columns 1-16 = presets of the bank.
  // Touch = load onto the selected track and play a preview while held (pressure = dynamics).
  // list the sub-folders of path, alphabetically, into names[max][17]
  int list_dirs(const char *path, char (*names)[17], int maxn) {
    DIR dir;
    FILINFO fi;
    if (f_opendir(&dir, path) != FR_OK) { printf("BROWSER: cannot open %s\n", path); return -1; }
    int n = 0;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
      if (!(fi.fattrib & AM_DIR) || fi.fname[0] == '.' || strlen(fi.fname) > 16) continue;
      if (n >= maxn) break;
      int i = n;
      while (i > 0 && strcmp(names[i - 1], fi.fname) > 0) { memcpy(names[i], names[i - 1], 17); --i; }
      strncpy(names[i], fi.fname, 16);
      names[i][16] = 0;
      ++n;
    }
    f_closedir(&dir);
    return n;
  }
  // a bank folder holds XY.json files: X = row 0-7, Y = column 0-7 of the 8x8 preset grid (index X*8+Y)
  void select_bank(int b) {
    cur_bank = (int8_t)b;
    preset_mask = preset_bad = 0;
    cand_row = -1;
    if (b < 0 || b >= nbank) return;
    char path[60];
    snprintf(path, sizeof(path), "/PLINKY/BANKS/%s/%s", cat_names[cur_cat], bank_names[b]);
    DIR dir;
    FILINFO fi;
    if (f_opendir(&dir, path) != FR_OK) { preset_mask = ~0ull; printf("BROWSER: cannot open %s\n", path); return; } // cannot list: allow every slot
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
      const char *n = fi.fname;
      // "XY.json", or its short 8.3 form "XY~1.JSO" (some SD setups only return short names)
      if (n[0] >= '0' && n[0] <= '7' && n[1] >= '0' && n[1] <= '7' && (n[2] == '.' || n[2] == '~'))
        preset_mask |= 1ull << ((n[0] - '0') * 8 + (n[1] - '0'));
    }
    f_closedir(&dir);
    if (!preset_mask) preset_mask = ~0ull; // nothing recognised: show every slot, touching tells which ones exist
  }
  void select_category(int c) {
    cur_cat = (uint8_t)c;
    if (ncat > 0) {
      char path[40];
      snprintf(path, sizeof(path), "/PLINKY/BANKS/%s", cat_names[c]);
      int n = list_dirs(path, bank_names, MAXBANKS);
      nbank = (int8_t)(n < 0 ? 0 : n);
    } else {
      for (int i = 0; i < 8; ++i) { strncpy(bank_names[i], k_fallback_bank[i], 16); bank_names[i][16] = 0; }
      nbank = 8;
    }
    select_bank(nbank > 0 ? 0 : -1);
  }
  void scan_categories(void) {
    int n = list_dirs("/PLINKY/BANKS", cat_names, MAXCATS);
    if (n <= 0) { // listing not possible: USER1 / USER2 with fixed bank names
      ncat = 0;
      for (int i = 0; i < 2; ++i) { strncpy(cat_names[i], k_fallback_cat[i], 16); cat_names[i][16] = 0; }
    } else ncat = (int8_t)n;
    int ncats = ncat > 0 ? ncat : 2, start = 0;
    for (int i = 0; i < ncats; ++i) if (!strcmp(cat_names[i], "USER1")) start = i; // open on USER1 if it exists
    select_category(start);
  }

  // SOUND BROWSER layout (round pads only: rows 3-14 of the Chords panel):
  //   row 3                categories (folders in PLINKY/BANKS), one colour each
  //   rows 5-12, cols 1-8  banks of the category (8x8, reading order)
  //   rows 5-12, cols 9-16 presets of the selected bank (8x8, same grid as the XY.json files)
  //   row 14               track select (first 4 pads)
  //   bottom right:  ● = system picker   ✖ = cancel   ▶ = LOAD on the selected track and back to playing
  // Touch a preset = AUDITION only (spare slot, plays while held). The track changes only with ▶.
  static constexpr int AUD = 11, ROWC = 2, ROW0 = 4;
  void draw_my_banks(void) {
    if (ncat < 0) scan_categories();
    int ncats = ncat > 0 ? ncat : 2;
    int pv = 0;
    uint32_t base = BRIGHTEST(k_cat_col[cur_cat & 7]); // presets present: very bright
    uint32_t base_lo = k_cat_col[cur_cat & 7];         // banks: normal colour of the category
    for (int i = 0; i < ncats && i < 16; ++i)
      if (button(i, ROWC, i == cur_cat ? BRIGHTEST(WHITE) : k_cat_col[i & 7], ISOLATED, cat_names[i]))
        select_category(i);
    for (int b = 0; b < 64 && b < nbank; ++b) {
      int x = b & 7, y = ROW0 + (b >> 3);
      bool cur = !strcmp(tr_cat[sel], cat_names[cur_cat]) && !strcmp(tr_bank[sel], bank_names[b]);
      if (button(x, y, b == cur_bank ? BRIGHTEST(WHITE) : cur ? WHITE : base_lo, ISOLATED, bank_names[b])) select_bank(b);
    }
    if (cur_bank >= 0) {
      const char *bname = bank_names[cur_bank];
      bool this_bank = !strcmp(tr_cat[sel], cat_names[cur_cat]) && !strcmp(tr_bank[sel], bname);
      for (int i = 0; i < 64; ++i) {
        int x = 8 + (i & 7), y = ROW0 + (i >> 3);
        uint64_t bit = 1ull << i;
        if (!(preset_mask & bit)) continue; // empty slot: stays dark
        bool cand = cand_row == (i >> 3) && cand_col == (i & 7);
        bool cur = this_bank && tr_preset[sel] == i;
        set_led(x, y, cand ? BRIGHTEST(GREEN) : cur ? BRIGHTEST(WHITE) : (preset_bad & bit) ? RED : base);
        if (get_touch_pressed(x, y)) {
          bool ok = load_preset_from_filename_with_category(AUD, cat_names[cur_cat], bname, i); // audition slot
          if (ok) { preset_bad &= ~bit; cand_row = (int8_t)(i >> 3); cand_col = (int8_t)(i & 7); }
          else { preset_bad |= bit; if (cand) cand_row = -1; printf("BROWSER: cannot load %s/%s %d%d\n", cat_names[cur_cat], bname, i >> 3, i & 7); }
        }
        if (get_touch_down(x, y) && cand_row == (i >> 3) && cand_col == (i & 7))
          pv = clampi(touch_pressure_curve_q7(get_touch_pressure_xy(x, y)), 1, 127);
      }
    }
    preview_vel = (int8_t)pv;
    for (int t = 0; t < T; ++t)
      if (button(t, 13, t == sel ? BRIGHTEST(k_track_col[t]) : DIMMER(k_track_col[t]), ISOLATED, "select track")) sel = t;
    // ● system picker
    button(12, 15, BLUE, ISOLATED, "back to the system picker");
    if (is_last_widget_released()) { pp.picker.reset_which_slot_is_selected(); factory_picker = true; preview_vel = 0; }
    // ✖ cancel: back to playing, nothing changes
    button(13, 15, RED, ISOLATED, "cancel");
    if (is_last_widget_released()) { mode = MODE_PLAY; preview_vel = 0; cand_row = -1; }
    // ▶ LOAD the auditioned sound on the selected track, then back to playing
    bool have = cand_row >= 0 && cur_bank >= 0;
    button(15, 15, have ? BRIGHTEST(GREEN) : DIMMER(GREEN), ISOLATED, "LOAD on the selected track");
    if (is_last_widget_released() && have) {
      int idx = cand_row * 8 + cand_col;
      const char *bname = bank_names[cur_bank];
      // the auditioned sound is already loaded (samples included) in the audition slot:
      // copy it onto the track instead of loading it from the SD a second time (instant)
      on_sequence_lock_guard_t lock;
      synth_presets[sel] = synth_presets[AUD];
      bool ok = true;
      if (ok) {
        strncpy(tr_cat[sel], cat_names[cur_cat], 16); tr_cat[sel][16] = 0;
        strncpy(tr_bank[sel], bname, 16); tr_bank[sel][16] = 0;
        tr_preset[sel] = (int8_t)idx;
        cand_row = -1;
        mode = MODE_PLAY;
        preview_vel = 0;
      }
    }
  }

  void on_ui(int delta_time_us) override {
    int sp = get_scroll_page();
    if (sp < 0) return;
    leds_clear();
    if (sp == 1) { clear_held(); song_page.saveload(16, true, FLAG_PICKER_ENABLE_DELETE); return; }
    if (sp > 1) { scroll_to_page(1); return; }
    if (mode == MODE_MIX) {
      clear_held();
      draw_mixer(0);
      if (back_button()) mode = MODE_PLAY;
      return;
    }
    if (mode == MODE_SOUND) {
      clear_held();
      pp.edit(sel, 0);
      pp.xy_pad(sel, 8, 10);
      if (back_button()) mode = MODE_PLAY;
      return;
    }
    if (mode == MODE_PRESET) {
      clear_held();
      if (factory_picker) {
        // system picker moved down onto the round pads (rows 3-14): banks from row 3, categories from row 11.
        // ✖ = cancel (back to the browser), ▶ = load (tap) and back to playing.
        // PRE main view: the system picker (instant preview, like the factory panels), on the round pads.
        // ✖ = cancel (back to playing), ▶ = load, ● = alternative SD browser, row 14 = track select.
        pp.picker.preset_picker(sel, 2, 10, FLAG_PICKER_ENABLE_DELETE);
        for (int t = 0; t < T; ++t)
          if (button(t, 13, t == sel ? BRIGHTEST(k_track_col[t]) : DIMMER(k_track_col[t]), ISOLATED, "select track")) {
            sel = t;
            pp.picker.reset_which_slot_is_selected();
          }
        button(12, 15, BLUE, ISOLATED, "alternative SD browser");
        if (is_last_widget_released()) { factory_picker = false; ncat = -1; cand_row = -1; return; }
        if (pp.picker.cancel_button(13, 15)) { mode = MODE_PLAY; return; }
        int action = pp.picker.preset_ok_button(sel, false, 15, 15); // no "save on long press": a long load must not turn into a save
        if (action > 0) mode = MODE_PLAY;
        return;
      }
      draw_my_banks();
      return;
    }
    draw_page0();
  }
};

static_assert(sizeof(qu4ttro) <= 128 * 1024, "QU4TTRO is larger than the 128 KB panel arena");
