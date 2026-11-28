/*
@Name: DER1VA
@Author: VRCVS (Filippo Rieder)
@Description: Four-track step sequencer for the Chords faceplate, freely inspired by Kria (monome). Every parameter
  (rhythm, notes, octaves, lengths, repeats) has its own loop and speed, so the tracks drift against each other like
  tides. Each track plays single notes, ready-made chords or free chords; a chord progression can steer them, a gentle
  drift keeps melodies alive, and a keyboard in scale lets you play on top.
@Documentation: https://github.com/vrcvs/plinky12_der1va
@Discussion: https://github.com/vrcvs/plinky12_der1va/issues
@Tags: sequencer, polymetric, harmony, chords, kria
@Preferred Panels: chords
@Category: sequencer

Version 0.5.2 (beta)
DER1VA by VRCVS - https://www.instagram.com/vrcvs/

CREDITS
  Freely inspired by Kria, the sequencer by monome for ansible and grid (https://monome.org).
  No Kria code is used: DER1VA is a new implementation for Plinky 12.
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

QUICK REFERENCE (Chords panel; rows/columns counted from 0, top-left; row 0 has 12 pads, the last 4 are the logo)
  Three gestures everywhere: TAP = choose / switch on-off.  HOLD A MODIFIER + TAP = change it.  HOLD A TRACK = its settings.
  Row 0       x0-3 tracks T1-T4 (tap = select, hold = settings) | x4 NOTE x5 OCT x6 LEN x7 REP (pages, blue)
              x8 LOOP x9 SPEED x10 PROB (modifiers, white: hold + tap) | x11 ARM (harmony page, green)
  Rows 1-4    rhythm of T1-T4, 16 steps each, always visible. Tap = hit on/off.
              LOOP + tap = the loop runs from step 1 to there (two pads pressed together = from one to the other). SPEED + tap n = the track moves every n 16ths.
              PROB + tap a hit = 100 / 75 / 50 / 25 %.  SKIP (row 15) + tap = the track jumps there.
  Rows 5-11   the page of the selected track, one column per step:
              NOTE  rows = scale degrees (1 at the bottom). MONO/CHORD: one per step. POLY: light several = they play together
              OCT   middle row = no shift, up/down = octaves     LEN  higher = longer note     REP  higher = more repeats
              LOOP / SPEED work here too, on that parameter only: every parameter has its own loop and speed (Kria).
  ARM page    x0-7 progression (one chord per column, row = degree, I at the bottom; LOOP sets its length)
              right side: key (rows 5-6), scale (rows 8-9), bars per chord 1/2/4/8 (row 11)
  Track settings (hold T1-T4): MODE mono / chord / poly | CHORD triad / 7th / sus / fifth | VOICING close..wide |
              FOLLOW the progression | GLIDE (mono) | REGISTER -2..+2 octaves | MIDI out (channel = track number)
  Rows 12-14  keyboard in scale for the selected track (its sound; in CHORD mode each pad is a chord). Lowest row = low.
  Row 15      x0 DRIFT (selected track: notes move a degree now and then; off = back to what you wrote) | x1-4 mute
              x5 FILL (hold) | x6 SKIP (hold + tap a step) | x7-12 patterns 1-6 (change at the bar; hold one + tap another
              = copy; hold 2 s and release = clear) | x13 CHAIN (tap = on/off, hold + tap patterns = order, 4 bars each) | x14 PRE (sound) | x15 PLAY
  Right side button down: song save/load.
*/
#define PANEL_PAD_COLOR CYAN // colour of DER1VA in the panel picker

// ---------------- colour code: one family per kind of control ----------------
#define DV_T1 ORANGE
#define DV_T2 YELLOW
#define DV_T3 TEAL
#define DV_T4 PURPLE
#define DV_PAGE BLUE           // pages and their content
#define DV_MOD WHITE           // modifiers to hold
#define DV_ARM GREEN           // harmony
#define DV_LIVE RED            // live actions
#define DV_PAT LED_RGB(31, 0, 12) // patterns and chain
static const uint32_t k_track_col[4] = {DV_T1, DV_T2, DV_T3, DV_T4};
// light levels (strong contrast): MAX = playing now / held / chosen, ON = lit, MID = secondary, DIM = available
#define L_MAX(c) ((c) << 3)
#define L_ON(c) BRIGHTEST(c)
#define L_MID(c) (c)
#define L_DIM(c) DIMMEST(c)

static constexpr int NSCALES = 12;
static const scale_t k_scales[NSCALES] = {
    SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_LYDIAN, SCALE_MIXOLYDIAN,
    SCALE_LOCRIAN, SCALE_HARMONIC_MINOR, SCALE_PENTATONIC_MAJOR, SCALE_PENTATONIC_MINOR, SCALE_HIRAJOSHI, SCALE_INSEN,
};
static const char *k_scale_names[NSCALES] = {"scale: major", "scale: minor", "scale: dorian", "scale: phrygian",
                                             "scale: lydian", "scale: mixolydian", "scale: locrian",
                                             "scale: harmonic minor", "scale: pentatonic major",
                                             "scale: pentatonic minor", "scale: hirajoshi", "scale: in (insen)"};
static const char *k_key_names[12] = {"key: C", "key: C#", "key: D", "key: D#", "key: E", "key: F",
                                      "key: F#", "key: G", "key: G#", "key: A", "key: A#", "key: B"};
static const uint8_t k_bars[4] = {1, 2, 4, 8};
static const uint8_t k_prob_pct[4] = {100, 75, 50, 25};
// note length (row 0..6) as a fraction of the step, x256: very short .. 4 steps
static const uint16_t k_len_q8[7] = {26, 64, 128, 192, 243, 512, 1024};

static inline int od_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int od_mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }

struct der1va : panel_t {
  enum { T = 4, S = 16, NP = 6, NPAR = 5, MAXEV = 40, KROWS = 3, KY0 = 12 };
  enum { P_TRG = 0, P_NOTE = 1, P_OCT = 2, P_LEN = 3, P_REP = 4 };
  enum { PG_NOTE = 0, PG_OCT = 1, PG_LEN = 2, PG_REP = 3, PG_ARM = 4 };
  enum { M_MONO = 0, M_CHORD = 1, M_POLY = 2 };
  enum { MODE_PLAY, MODE_PRESET, MODE_MIX, MODE_SND }; // the last three = the sound area (tabs PRE / MIX / SND)

  // ---------------- song: patterns ----------------
  uint16_t trig[NP][T];          // hits, one bit per step
  uint8_t prob[NP][T][S];        // 0..3 = 100 / 75 / 50 / 25 %
  uint8_t nmask[NP][T][S];       // notes: one bit per degree (bit 0 = degree 1)
  uint8_t octv[NP][T][S];        // 0..6, 3 = no shift
  uint8_t vrand[NP][T][4][S];    // live variation per page (NOTE OCT LEN REP): 0 = off, 1..3 = +/- up to n around the value
  uint8_t lenv[NP][T][S];        // 0..6
  uint8_t repv[NP][T][S];        // 0..6 = 1..7 hits
  uint8_t lp_s[NP][T][NPAR], lp_e[NP][T][NPAR], spd[NP][T][NPAR]; // loop start/end, speed (16ths per step, 1..16)
  int8_t drift_off[NP][T][S];    // drift: offset in degrees, cleared when drift is switched off

  // ---------------- song: tracks and harmony ----------------
  // track settings and progression belong to each pattern (MIDI and mute are global)
  uint8_t tmode[NP][T], ctype[NP][T], voicing[NP][T], follow[NP][T], glide[NP][T], reg[NP][T], midi_on[T], muted[T], drift_on[NP][T]; // reg: 2 = 0
  uint8_t prog[NP][8], prog_s[NP], prog_e[NP], bars_idx, root_pc, scale_idx;
  // songs saved by 0.3.x have one set of settings for the whole song: read into these, then copied to every pattern
  uint8_t lg_tmode[T], lg_ctype[T], lg_voicing[T], lg_follow[T], lg_glide[T], lg_reg[T], lg_drift[T], lg_prog[8], lg_prog_s, lg_prog_e;
  uint8_t cur_pat, chain[8], chain_n, chain_on;

  // ---------------- runtime ----------------
  uint8_t pos[T][NPAR], sub[T][NPAR];
  uint8_t prog_pos;
  int32_t tick16;
  volatile int8_t pend_pat;
  uint8_t chain_i;
  uint32_t t_us, last_tick_us, tick_us;
  clock_divider_t clk;
  struct ev_t { uint32_t id, start_us, end_us; uint8_t note, vel, track, prio; bool active, started, retrig; };
  ev_t ev[MAXEV];
  uint32_t ev_seq;
  uint8_t key_vel[KROWS * 16];   // live keyboard, written in on_ui (0 = not held)
  uint8_t key_prev[KROWS * 16];
  volatile bool fill_held;
  voice_allocator_t va;
  preset_pages_t pp;
  panel_page_t song_page;
  slider_t mix_sliders[T][4];
  bool pre_used; // PRE held for a shortcut: releasing it does not open the picker

  // ---------------- UI ----------------
  uint8_t mode, sel, page;
  int8_t chain_edit;             // CHAIN held: building a new order
  int8_t copy_src;               // a pattern pad held: tapping another copies into it
  uint32_t pat_hold_us[NP];      // when each pattern pad went down (long press = clear)
  bool pat_used[NP];             // the held pad was used to copy: no clearing on release
  bool song_folder_ok;

  // =========================================================
  // SCALE AND NOTES
  // =========================================================
  scale_t cur_scale(void) const { return k_scales[scale_idx % NSCALES]; }
  int per_oct(void) const { int n = __builtin_popcount((unsigned)(cur_scale() & 0xfff)); return n < 2 ? 7 : n; }
  // note for a degree (0 = root), any sign, plus octaves
  int deg_note(int deg, int base) const {
    int n = per_oct(), o = 0;
    while (deg < 0) { deg += n; o -= 12; }
    int note = base + o + degree_to_note(deg, cur_scale());
    while (note > 108) note -= 12;
    while (note < 24) note += 12;
    return note;
  }
  int track_base(int t) const { return 48 + root_pc + 12 * ((int)reg[cur_pat][t] - 2); }
  // following the progression moves the notes by the chord's degree, the short way (-3..+3), so the track keeps its register
  int chord_shift(int t) const {
    if (!follow[cur_pat][t]) return 0;
    int d = prog[cur_pat][prog_pos & 7], n = per_oct();
    return d > n / 2 ? d - n : d;
  }
  // degrees of a chord built on 'd' (type and voicing of the track), as degree offsets + octave shifts
  int chord_notes(int t, int d, int base, int *out) const {
    int n = per_oct();
    int third = (2 * n + 3) / 7, fifth = (4 * n + 3) / 7, seventh = (6 * n + 3) / 7, fourth = (3 * n + 3) / 7;
    int degs[4], k = 0;
    switch (ctype[cur_pat][t] & 3) {
      case 0: degs[0] = 0; degs[1] = third; degs[2] = fifth; k = 3; break;            // triad
      case 1: degs[0] = 0; degs[1] = third; degs[2] = fifth; degs[3] = seventh; k = 4; break; // seventh
      case 2: degs[0] = 0; degs[1] = fourth; degs[2] = fifth; k = 3; break;           // sus4
      default: degs[0] = 0; degs[1] = fifth; degs[2] = n; k = 3; break;               // open fifth
    }
    int notes[4];
    for (int i = 0; i < k; ++i) notes[i] = deg_note(d + degs[i], base);
    switch (voicing[cur_pat][t] & 3) {
      case 1: notes[0] -= 12; break;                                  // medium: root down
      case 2: if (k > 1) notes[1] += 12; break;                       // open: third up
      case 3: notes[0] -= 12; if (k > 1) notes[1] += 12; if (k > 3) notes[3] += 12; break; // wide
      default: break;                                                 // close
    }
    for (int i = 0; i < k; ++i) { int v = notes[i]; while (v < 24) v += 12; while (v > 108) v -= 12; out[i] = v; }
    return k;
  }

  // =========================================================
  // DEFAULT SONG
  // =========================================================
  void clear_pattern(int p) {
    for (int t = 0; t < T; ++t) {
      trig[p][t] = 0;
      for (int s = 0; s < S; ++s) {
        prob[p][t][s] = 0; nmask[p][t][s] = 1; octv[p][t][s] = 3; for (int g = 0; g < 4; ++g) vrand[p][t][g][s] = 0; lenv[p][t][s] = 3; repv[p][t][s] = 0;
        drift_off[p][t][s] = 0;
      }
      for (int q = 0; q < NPAR; ++q) { lp_s[p][t][q] = 0; lp_e[p][t][q] = S - 1; spd[p][t][q] = 1; }
      // track settings back to the default: MONO, one note, no glide, register 0, no drift
      tmode[p][t] = M_MONO; ctype[p][t] = 0; voicing[p][t] = 1; follow[p][t] = 0; glide[p][t] = 0; reg[p][t] = 2; drift_on[p][t] = 0;
    }
    memset(prog[p], 0, sizeof(prog[p])); prog_s[p] = 0; prog_e[p] = 0; // progression: the I chord only
  }
  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("DER1VA by VRCVS v0.5.2\n");
    for (int p = 0; p < NP; ++p) clear_pattern(p);
    // Demo song (pattern 1; patterns 2-6 are empty). T1 chords and T3 bass FOLLOW the progression (their notes move with
    // the chord), T2 free chords (empty), T4 a free melody written on the scale. Rhythm and notes have the same length.
    static const uint8_t m[T] = {M_CHORD, M_POLY, M_MONO, M_MONO}, r[T] = {2, 2, 1, 3}, f[T] = {1, 0, 1, 0};
    for (int t = 0; t < T; ++t) {
      tmode[0][t] = m[t]; follow[0][t] = f[t]; reg[0][t] = r[t]; // pattern 1 only; the others stay at the default
      midi_on[t] = 0; muted[t] = 0;
    }
    trig[0][0] = (1 << 0) | (1 << 8);                       // T1: a chord every half bar, long
    for (int s = 0; s < S; ++s) lenv[0][0][s] = 5;
    trig[0][2] = (1 << 0) | (1 << 6) | (1 << 10);           // T3: bass on the root of each chord
    trig[0][3] = (1 << 2) | (1 << 5) | (1 << 7) | (1 << 11) | (1 << 13); // T4: melody, A minor: E C D A E
    static const uint8_t mel[5][2] = {{2, 4}, {5, 2}, {7, 3}, {11, 0}, {13, 4}};
    for (int i = 0; i < 5; ++i) nmask[0][3][mel[i][0]] = (uint8_t)(1 << mel[i][1]);
    prob[0][3][13] = 2;
    static const uint8_t pr[8] = {0, 5, 3, 4, 0, 0, 0, 0}; // I VI IV V
    memcpy(prog[0], pr, sizeof(prog[0]));
    prog_s[0] = 0; prog_e[0] = 3; bars_idx = 1; root_pc = 9; scale_idx = 1; // A minor, 2 bars per chord
    cur_pat = 0; chain_n = 0; chain_on = 0;
    for (int i = 0; i < 8; ++i) chain[i] = 0;
    reset_runtime();
  }
  // Loading a saved song builds a fresh panel without the default-song hook: set the transient state on both paths.
  void reset_runtime(void) {
    mode = MODE_PLAY; sel = 0; page = PG_NOTE; pre_used = false;
    memset(ev, 0, sizeof(ev));
    memset(key_vel, 0, sizeof(key_vel));
    memset(key_prev, 0, sizeof(key_prev));
    for (int i = 0; i < NP; ++i) { pat_hold_us[i] = 0; pat_used[i] = false; }
    chain_edit = -1; copy_src = -1; pend_pat = -1; chain_i = 0;
    fill_held = false; ev_seq = 0; t_us = 1; last_tick_us = 0; tick_us = 125000; tick16 = 0;
    song_folder_ok = false;
    restart_positions();
  }
  void restart_positions(void) {
    for (int t = 0; t < T; ++t)
      for (int q = 0; q < NPAR; ++q) { pos[t][q] = lp_s[cur_pat][t][q]; sub[t][q] = 0; }
    prog_pos = prog_s[cur_pat]; tick16 = 0;
  }
  void sanitise(void) {
    for (int p = 0; p < NP; ++p)
      for (int t = 0; t < T; ++t) {
        for (int s = 0; s < S; ++s) {
          prob[p][t][s] &= 3; nmask[p][t][s] &= 0x7f; octv[p][t][s] = (uint8_t)od_clamp(octv[p][t][s], 0, 6); for (int g = 0; g < 4; ++g) vrand[p][t][g][s] &= 3;
          lenv[p][t][s] = (uint8_t)od_clamp(lenv[p][t][s], 0, 6); repv[p][t][s] = (uint8_t)od_clamp(repv[p][t][s], 0, 6);
          drift_off[p][t][s] = (int8_t)od_clamp(drift_off[p][t][s], -2, 2);
        }
        for (int q = 0; q < NPAR; ++q) {
          lp_s[p][t][q] &= 15; lp_e[p][t][q] &= 15;
          if (lp_e[p][t][q] < lp_s[p][t][q]) lp_e[p][t][q] = lp_s[p][t][q];
          spd[p][t][q] = (uint8_t)od_clamp(spd[p][t][q], 1, 16);
        }
      }
    for (int p = 0; p < NP; ++p) {
      for (int t = 0; t < T; ++t) {
        tmode[p][t] %= 3; ctype[p][t] &= 3; voicing[p][t] &= 3; follow[p][t] &= 1; glide[p][t] &= 1;
        reg[p][t] = (uint8_t)od_clamp(reg[p][t], 0, 4); drift_on[p][t] &= 1;
      }
      for (int i = 0; i < 8; ++i) prog[p][i] = (uint8_t)od_clamp(prog[p][i], 0, 6);
      prog_s[p] &= 7; prog_e[p] &= 7; if (prog_e[p] < prog_s[p]) prog_e[p] = prog_s[p];
    }
    for (int t = 0; t < T; ++t) { midi_on[t] &= 1; muted[t] &= 1; }
    for (int i = 0; i < 8; ++i) chain[i] %= NP;
    bars_idx &= 3; root_pc %= 12; scale_idx %= NSCALES; cur_pat %= NP; chain_n = (uint8_t)od_clamp(chain_n, 0, 8); chain_on &= 1;
  }
  // a song saved by 0.3.x: the same track settings and progression in every pattern
  void load_legacy(void) {
    for (int p = 0; p < NP; ++p) {
      for (int t = 0; t < T; ++t) {
        if (lg_tmode[t] != 0xff) tmode[p][t] = lg_tmode[t];
        if (lg_ctype[t] != 0xff) ctype[p][t] = lg_ctype[t];
        if (lg_voicing[t] != 0xff) voicing[p][t] = lg_voicing[t];
        if (lg_follow[t] != 0xff) follow[p][t] = lg_follow[t];
        if (lg_glide[t] != 0xff) glide[p][t] = lg_glide[t];
        if (lg_reg[t] != 0xff) reg[p][t] = lg_reg[t];
        if (lg_drift[t] != 0xff) drift_on[p][t] = lg_drift[t];
      }
      if (lg_prog[0] != 0xff) memcpy(prog[p], lg_prog, 8);
      if (lg_prog_s != 0xff) prog_s[p] = lg_prog_s;
      if (lg_prog_e != 0xff) prog_e[p] = lg_prog_e;
    }
  }
  void on_load_finished() override { reset_runtime(); }

  int get_num_pages(void) override { return 2; } // 0 = instrument, 1 = song save/load
  int get_num_tracks(void) override { return T; }
  const char *get_track_name(int i, char buf[16]) override {
    if (!buf) return NULL;
    snprintf(buf, 16, "T%d", (i & 3) + 1);
    return buf;
  }

  // =========================================================
  // SEQUENCER
  // =========================================================
  void advance(int t, int q) {
    int p = cur_pat;
    if (++sub[t][q] < spd[p][t][q]) return;
    sub[t][q] = 0;
    int s = lp_s[p][t][q], e = lp_e[p][t][q];
    int x = pos[t][q] + 1;
    bool wrapped = false;
    if (x > e || x < s) { x = s; wrapped = true; }
    pos[t][q] = (uint8_t)x;
    if (q == P_NOTE && wrapped && drift_on[cur_pat][t]) drift_step(t);
  }
  // drift: at the end of each loop of the notes, one step may move by a degree (never more than 2 from what you wrote)
  void drift_step(int t) {
    if ((rand() & 255) > 90) return;
    int p = cur_pat, s = lp_s[p][t][P_NOTE], e = lp_e[p][t][P_NOTE];
    int st = s + rand() % (e - s + 1);
    int d = drift_off[p][t][st] + ((rand() & 1) ? 1 : -1);
    if (d > 2) d = 1;
    if (d < -2) d = -1;
    drift_off[p][t][st] = (int8_t)d;
  }
  void add_event(int t, int note, int vel, uint32_t start, uint32_t len, uint32_t id, bool retrig, int prio) {
    int slot = -1;
    uint32_t oldest = 0xffffffffu;
    for (int i = 0; i < MAXEV; ++i) {
      if (!ev[i].active) { slot = i; break; }
      if (ev[i].end_us < oldest) { oldest = ev[i].end_us; slot = i; }
    }
    ev_t &g = ev[slot];
    g.id = id; g.note = (uint8_t)note; g.vel = (uint8_t)od_clamp(vel, 1, 127); g.track = (uint8_t)t; g.prio = (uint8_t)prio;
    g.start_us = start; g.end_us = start + (len < 20000 ? 20000 : len);
    g.active = true; g.started = false; g.retrig = retrig;
  }
  void end_track_events(int t) { // mono: a new note ends the previous one
    for (int i = 0; i < MAXEV; ++i) if (ev[i].active && ev[i].track == t) ev[i].end_us = t_us;
  }
  // live variation: a random offset in -n..+n (n = 0..3) for this page and step
  int vary(int p, int t, int g, int s) const { int n = vrand[p][t][g][s] & 3; return n ? (int)(rand() % (2 * n + 1)) - n : 0; }
  void fire(int t) {
    int p = cur_pat;
    int s = pos[t][P_TRG];
    bool hit = fill_held || ((trig[p][t] >> s) & 1);
    if (!hit || muted[t]) return;
    if (!fill_held && prob[p][t][s] && (rand() % 100) >= k_prob_pct[prob[p][t][s] & 3]) return;
    int ns = pos[t][P_NOTE];
    uint8_t mask = nmask[p][t][ns];
    if (!mask) return; // no note on this step: a rest
    int os = pos[t][P_OCT];
    int ov = od_clamp((int)octv[p][t][os] - 3 + vary(p, t, PG_OCT, os), -3, 3);         // octave, at most +/- 3
    int base = track_base(t) + 12 * ov;
    int shift = chord_shift(t) + (drift_on[cur_pat][t] ? drift_off[p][t][ns] : 0) + vary(p, t, PG_NOTE, ns); // degrees: stays in scale
    int notes[7], k = 0;
    if (tmode[cur_pat][t] == M_POLY) {
      for (int d = 0; d < 7 && k < 3; ++d) if ((mask >> d) & 1) notes[k++] = deg_note(d + shift, base); // max 3: 8 voices in all
    } else {
      int d = __builtin_ctz((unsigned)mask);
      if (tmode[cur_pat][t] == M_CHORD) k = chord_notes(t, d + shift, base, notes);
      else notes[k++] = deg_note(d + shift, base);
    }
    uint32_t step_us = tick_us * spd[p][t][P_TRG];
    int rs = pos[t][P_REP], ls = pos[t][P_LEN];
    int reps = 1 + od_clamp((int)repv[p][t][rs] + vary(p, t, PG_REP, rs), 0, 6);
    uint32_t slice = step_us / reps;
    uint32_t len = (uint32_t)(((uint64_t)step_us * k_len_q8[od_clamp((int)lenv[p][t][ls] + vary(p, t, PG_LEN, ls), 0, 6)]) >> 8);
    if (reps > 1 && len > slice * 6 / 10) len = slice * 6 / 10;
    bool mono = tmode[cur_pat][t] == M_MONO;
    if (mono) end_track_events(t);
    static const uint8_t prio_of_mode[3] = {3, 1, 2}; // mono (bass, melody) is never cut first; chords give way
    for (int r = 0; r < reps; ++r)
      for (int i = 0; i < k; ++i) {
        uint32_t id;
        bool retrig = true;
        if (mono && glide[cur_pat][t]) { id = 0x100u | (uint32_t)t; retrig = false; } // same voice: the preset's glide
        else id = 0x10000000u | (ev_seq++ & 0x0fffffffu);
        int vel = (tmode[cur_pat][t] == M_CHORD ? 80 : 100) - r * 6;
        add_event(t, notes[i], vel, t_us + slice * r, len, id, retrig, prio_of_mode[tmode[cur_pat][t] % 3]);
      }
  }
  void on_tick(void) {
    ++tick16;
    for (int t = 0; t < T; ++t) {
      for (int q = 0; q < NPAR; ++q) advance(t, q);
      if (sub[t][P_TRG] == 0) fire(t); // the rhythm moved to a new step
    }
    if (tick16 % 16 == 0) on_bar();
    if (tick16 % (16 * k_bars[bars_idx & 3]) == 0) {
      int x = prog_pos + 1;
      prog_pos = (uint8_t)((x > prog_e[cur_pat] || x < prog_s[cur_pat]) ? prog_s[cur_pat] : x);
    }
  }
  void on_bar(void) {
    if (chain_on && chain_n > 0 && tick16 % 64 == 0) { // chain: 4 bars per pattern
      chain_i = (uint8_t)((chain_i + 1) % chain_n);
      pend_pat = (int8_t)chain[chain_i];
    }
    if (pend_pat >= 0) { cur_pat = (uint8_t)pend_pat; pend_pat = -1; }
  }
  void start_fire(void) { // transport start: play the first step of every track right away
    restart_positions();
    for (int t = 0; t < T; ++t) fire(t);
  }

  void on_sequence(int delta_time_us) override {
    t_us += (uint32_t)delta_time_us;
    if (!t_us) t_us = 1;
    bool playing = is_transport_playing();
    int edges = clk.update(-1, 4, 1, UPDATE_DIV_NOW);
    if (has_transport_just_started()) {
      start_fire();
      last_tick_us = t_us;
    } else if (sequencer_should_advance_playhead()) {
      if (edges > 4) edges = 4;
      for (int e = 0; e < edges; ++e) {
        uint32_t d = t_us - last_tick_us;
        if (last_tick_us && d > 20000 && d < 1000000) tick_us = (tick_us * 3 + d) / 4; // follow the tempo
        last_tick_us = t_us;
        on_tick();
      }
    }
    if (!playing) for (int i = 0; i < MAXEV; ++i) ev[i].active = false;

    // ---- voices: sequenced notes first (new ones may take the oldest voice), then the ones already sounding ----
    bool loading = mode == MODE_PRESET;
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 0; i < MAXEV; ++i) {
        ev_t &g = ev[i];
        if (!g.active || g.started != (pass == 1)) continue;
        if ((int32_t)(t_us - g.start_us) < 0) continue;       // a repeat still waiting
        if ((int32_t)(g.end_us - t_us) <= 0) { g.active = false; continue; }
        bool first = !g.started;
        g.started = true;
        if (loading && g.track == sel) continue;
        int old_v = va.find_voice(g.id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
        int v = va.voice_allocate(g.id, g.prio, 0, DEFAULT_VOICE_ALLOCATOR_VOICES, g.track);
        if (v < 0) { g.active = false; continue; }
        if (v != old_v && old_v >= 0) synth_note_up(old_v);
        play_synth(v, g.track, g.vel, g.note << 8, (first && g.retrig) || v != old_v);
      }
    // ---- live keyboard: selected track's sound, in scale ----
    for (int k = 0; k < KROWS * 16; ++k) {
      uint32_t id = 0x20000000u | (uint32_t)k;
      if (!key_vel[k] || (loading)) { key_prev[k] = 0; continue; }
      int notes[4], n = key_notes(k, notes);
      for (int i = 0; i < n; ++i) {
        uint32_t sid = id | ((uint32_t)i << 8);
        if (midi_on[sel]) declare_midi_note((uint8_t)(sel & 15), (uint8_t)notes[i], key_vel[k], MIDI_PORT_1);
        int old_v = va.find_voice(sid, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
        int v = va.voice_allocate(sid, 4, 0, DEFAULT_VOICE_ALLOCATOR_VOICES, sel);
        if (v < 0) continue;
        if (v != old_v && old_v >= 0) synth_note_up(old_v);
        play_synth(v, sel, key_vel[k], notes[i] << 8, !key_prev[k] || v != old_v);
      }
      key_prev[k] = 1;
    }
    // ---- MIDI out: channel = track number ----
    for (int i = 0; i < MAXEV; ++i) {
      const ev_t &g = ev[i];
      if (g.active && g.started && midi_on[g.track & 3]) declare_midi_note(g.track & 3, g.note, g.vel, MIDI_PORT_1);
    }
    send_declared_midi_notes(-1);
    uint16_t stale = va.garbage_collect_voices(1, 0, 0, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
    while (stale) {
      int v = __builtin_ctz((unsigned)stale);
      synth_note_up(v);
      stale &= (uint16_t)(stale - 1);
    }
  }
  // keyboard pad -> notes (rows 12-14, lowest row at the bottom, each row one octave up)
  int key_degree(int k) const { int x = k % 16, row = KROWS - 1 - k / 16; return x + row * per_oct(); }
  int key_notes(int k, int *out) const {
    int d = key_degree(k), base = track_base(sel);
    if (tmode[cur_pat][sel] == M_CHORD) return chord_notes(sel, d, base, out);
    out[0] = deg_note(d, base);
    return 1;
  }

  // =========================================================
  // UI
  // =========================================================
  uint32_t tcol(int t) const { return k_track_col[t & 3]; }
  int param_of_page(void) const { return page == PG_OCT ? P_OCT : page == PG_LEN ? P_LEN : page == PG_REP ? P_REP : P_NOTE; }

  // LOOP held: one tap = the loop runs from step 1 to the tapped step; two pads pressed together = from one to the other.
  // Returns the column of another pad held down in rows y0..y1, columns 0..x1 (or -1).
  int other_held(int y0, int y1, int x1, int x) const {
    for (int y = y0; y <= y1; ++y)
      for (int c = 0; c <= x1; ++c)
        if (c != x && get_touch_down(c, y)) return c;
    return -1;
  }
  void loop_range(int x, int other, uint8_t &s0, uint8_t &e0) {
    if (other < 0) { s0 = 0; e0 = (uint8_t)x; return; }
    s0 = (uint8_t)(other < x ? other : x);
    e0 = (uint8_t)(other < x ? x : other);
  }
  void set_loop(int t, int q, int x, int other) {
    on_sequence_lock_guard_t lock;
    int p = cur_pat;
    loop_range(x, other, lp_s[p][t][q], lp_e[p][t][q]);
    if (pos[t][q] < lp_s[p][t][q] || pos[t][q] > lp_e[p][t][q]) pos[t][q] = lp_s[p][t][q];
  }

  // rows 1-4: rhythm of the four tracks
  void draw_rhythm(bool loop_h, bool speed_h, bool prob_h, bool skip_h) {
    int p = cur_pat;
    for (int t = 0; t < T; ++t) {
      int y = 1 + t;
      uint32_t c = tcol(t);
      int s0 = lp_s[p][t][P_TRG], e0 = lp_e[p][t][P_TRG];
      for (int x = 0; x < S; ++x) {
        bool in = x >= s0 && x <= e0, on = (trig[p][t] >> x) & 1;
        uint32_t col;
        if (speed_h) col = x < spd[p][t][P_TRG] ? (x == spd[p][t][P_TRG] - 1 ? L_MAX(c) : L_MID(c)) : L_DIM(c);
        else if (prob_h && on) col = prob[p][t][x] == 0 ? L_ON(c) : prob[p][t][x] == 1 ? ((c) << 1) : prob[p][t][x] == 2 ? L_MID(c) : DIMMER(c);
        else {
          col = on ? (in ? L_ON(c) : L_MID(c)) : (in ? (loop_h ? L_MID(c) : L_DIM(c)) : 0);
          if (x == pos[t][P_TRG] && is_transport_playing()) col = on ? L_MAX(c) : WHITE;
          if (muted[t]) col = on ? L_DIM(c) : (in ? DIMMESTEST(c) : 0);
        }
        set_led(x, y, col);
        if (!get_touch_pressed(x, y)) continue;
        if (loop_h) set_loop(t, P_TRG, x, other_held(y, y, S - 1, x));
        else if (speed_h) { on_sequence_lock_guard_t l; spd[p][t][P_TRG] = (uint8_t)(x + 1); }
        else if (prob_h) { if (on) { on_sequence_lock_guard_t l; prob[p][t][x] = (uint8_t)((prob[p][t][x] + 1) & 3); } }
        else if (skip_h) { on_sequence_lock_guard_t l; pos[t][P_TRG] = (uint8_t)x; sub[t][P_TRG] = 0; }
        else { on_sequence_lock_guard_t l; trig[p][t] ^= (uint16_t)(1 << x); }
      }
    }
  }

  // rows 5-11: page of the selected track
  // is row r within n of the step's value? (NOTE: of any lit note; the others: of the top of the bar)
  bool near_value(int p, int t, int x, int r, int n) const {
    int v;
    switch (page) {
      case PG_NOTE: for (int d = 0; d < 7; ++d) if (((nmask[p][t][x] >> d) & 1) && r >= d - n && r <= d + n) return true; return false;
      case PG_OCT: v = octv[p][t][x]; break;
      case PG_LEN: v = lenv[p][t][x]; break;
      default: v = repv[p][t][x]; break;
    }
    return r >= v - n && r <= v + n;
  }
  void draw_page(bool loop_h, bool speed_h, bool prob_h) {
    int p = cur_pat, t = sel, q = param_of_page();
    uint32_t c = DV_PAGE;
    int s0 = lp_s[p][t][q], e0 = lp_e[p][t][q];
    uint32_t now_ui = get_on_ui_time();
    for (int x = 0; x < S; ++x) {
      bool in = x >= s0 && x <= e0, now = x == pos[t][q] && is_transport_playing();
      for (int r = 0; r < 7; ++r) {
        int y = 11 - r;
        bool lit;
        switch (page) {
          case PG_NOTE: lit = (nmask[p][t][x] >> r) & 1; break;
          case PG_OCT: { int v = octv[p][t][x]; lit = (r == v) || (v > 3 ? (r >= 3 && r < v) : (r <= 3 && r > v)); break; }
          case PG_LEN: lit = r <= lenv[p][t][x]; break;
          default: lit = r <= repv[p][t][x]; break;
        }
        bool top = (page == PG_NOTE) || (page == PG_OCT && r == octv[p][t][x]) || (page == PG_LEN && r == lenv[p][t][x]) ||
                   (page == PG_REP && r == repv[p][t][x]);
        uint32_t col;
        if (speed_h) col = (r == 0 && x < spd[p][t][q]) ? (x == spd[p][t][q] - 1 ? L_MAX(c) : L_ON(c)) : (in ? L_DIM(c) : 0);
        else if (lit) col = !in ? L_MID(c) : (top ? (now ? L_MAX(c) : L_ON(c)) : L_MID(c));
        else col = in ? (now ? L_MID(c) : (loop_h ? L_MID(c) : L_DIM(c))) : 0;
        if (page == PG_OCT && r == 3 && !lit && in) col = DIMMER(WHITE); // the "no shift" line
        if (!speed_h) {
          int n = vrand[p][t][page][x] & 3, d = r > 3 ? r - 3 : 3 - r;
          if (prob_h) col = (n && d <= n) ? (d == n ? L_ON(c) : L_MID(c)) : (r == 3 ? DIMMER(WHITE) : (in ? L_DIM(c) : 0)); // PROB held: the range, around the middle row
          else if (n && !lit && near_value(p, t, x, r, n)) // live variation: the pads it can reach flicker
            col = ((((now_ui >> 17) + (uint32_t)(x * 5 + r * 3)) % 3) == 0) ? L_ON(c) : L_MID(c);
        }
        set_led(x, y, col);
        if (!get_touch_pressed(x, y)) continue;
        if (loop_h) { set_loop(t, q, x, other_held(5, 11, S - 1, x)); continue; }
        on_sequence_lock_guard_t l;
        if (speed_h) { spd[p][t][q] = (uint8_t)(x + 1); continue; }
        if (prob_h) { // PROB + tap: live variation +/- the distance from the middle row (middle row = off)
          int n = r > 3 ? r - 3 : 3 - r;
          vrand[p][t][page][x] = (uint8_t)(vrand[p][t][page][x] == n ? 0 : n);
          continue;
        }
        switch (page) {
          case PG_NOTE:
            if (tmode[cur_pat][t] == M_POLY) {
              nmask[p][t][x] ^= (uint8_t)(1 << r);
            } else nmask[p][t][x] = (nmask[p][t][x] == (1 << r)) ? 0 : (uint8_t)(1 << r); // tap the lit note again = rest
            drift_off[p][t][x] = 0;
            break;
          case PG_OCT: octv[p][t][x] = (uint8_t)r; break;
          case PG_LEN: lenv[p][t][x] = (uint8_t)r; break;
          default: repv[p][t][x] = (uint8_t)r; break;
        }
      }
    }
  }

  // ARM page: progression on the left, key / scale / bars on the right
  void draw_arm(bool loop_h) {
    uint32_t c = DV_ARM;
    for (int x = 0; x < 8; ++x) {
      bool in = x >= prog_s[cur_pat] && x <= prog_e[cur_pat], now = x == prog_pos && is_transport_playing();
      for (int r = 0; r < 7; ++r) {
        int y = 11 - r;
        bool lit = prog[cur_pat][x] == r;
        set_led(x, y, lit ? (in ? (now ? L_MAX(c) : L_ON(c)) : L_MID(c)) : (in ? (loop_h ? L_MID(c) : L_DIM(c)) : 0));
        if (!get_touch_pressed(x, y)) continue;
        if (loop_h) {
          on_sequence_lock_guard_t l;
          loop_range(x, other_held(5, 11, 7, x), prog_s[cur_pat], prog_e[cur_pat]);
          if (prog_pos < prog_s[cur_pat] || prog_pos > prog_e[cur_pat]) prog_pos = prog_s[cur_pat];
        } else { on_sequence_lock_guard_t l; prog[cur_pat][x] = (uint8_t)r; }
      }
    }
    for (int i = 0; i < 12; ++i)
      if (button(9 + i % 6, 5 + i / 6, root_pc == i ? L_MAX(c) : L_DIM(c), ISOLATED, k_key_names[i])) {
        on_sequence_lock_guard_t l; root_pc = (uint8_t)i;
      }
    for (int i = 0; i < NSCALES; ++i)
      if (button(9 + i % 6, 8 + i / 6, scale_idx == i ? L_MAX(c) : L_DIM(c), ISOLATED, k_scale_names[i])) {
        on_sequence_lock_guard_t l; scale_idx = (uint8_t)i;
      }
    static const char *bar_help[4] = {"1 bar per chord", "2 bars per chord", "4 bars per chord", "8 bars per chord"};
    for (int i = 0; i < 4; ++i)
      if (button(9 + i, 11, bars_idx == i ? L_MAX(c) : L_DIM(c), ISOLATED, bar_help[i])) bars_idx = (uint8_t)i;
  }

  // hold a track pad: its settings on rows 5-11 (x0 is the label column, left dark)
  void draw_settings(int t) {
    uint32_t c = tcol(t);
    static const uint8_t nopt[7] = {3, 4, 4, 2, 2, 5, 2};
    static const char *help[7][5] = {
        {"mode: single notes", "mode: ready-made chords", "mode: free chords (light several notes)", 0, 0},
        {"chord: triad", "chord: seventh", "chord: sus4", "chord: open fifth", 0},
        {"voicing: close", "voicing: medium", "voicing: open", "voicing: wide", 0},
        {"progression: ignore", "progression: follow the chords", 0, 0, 0},
        {"glide: off", "glide: on (single notes, uses the sound's GLIDE)", 0, 0, 0},
        {"register: -2 octaves", "register: -1 octave", "register: normal", "register: +1 octave", "register: +2 octaves"},
        {"MIDI out: off", "MIDI out: on (channel = track number)", 0, 0, 0}};
    uint8_t *val[7] = {&tmode[cur_pat][t], &ctype[cur_pat][t], &voicing[cur_pat][t], &follow[cur_pat][t], &glide[cur_pat][t], &reg[cur_pat][t], &midi_on[t]};
    for (int row = 0; row < 7; ++row) {
      bool relevant = !((row == 1 || row == 2) && tmode[cur_pat][t] != M_CHORD) && !(row == 4 && tmode[cur_pat][t] != M_MONO);
      for (int i = 0; i < nopt[row]; ++i) {
        uint32_t col = *val[row] == i ? L_MAX(c) : L_DIM(c);
        if (!relevant) col = *val[row] == i ? L_MID(c) : DIMMESTEST(c);
        if (button(1 + i, 5 + row, col, NOT_ISOLATED, help[row][i])) {
          on_sequence_lock_guard_t l;
          *val[row] = (uint8_t)i;
          if (row == 0 && i != M_POLY) // leaving poly: keep only the lowest note of each step
            for (int p = 0; p < NP; ++p)
              for (int s = 0; s < S; ++s) { uint8_t m = nmask[p][t][s]; if (m) nmask[p][t][s] = (uint8_t)(m & (uint8_t)(-(int8_t)m)); }
        }
      }
    }
  }

  void draw_keyboard(void) {
    uint32_t c = tcol(sel);
    uint8_t vel[KROWS * 16];
    for (int k = 0; k < KROWS * 16; ++k) {
      int x = k % 16, y = KY0 + k / 16;
      vel[k] = 0;
      if (get_touch_down(x, y)) vel[k] = (uint8_t)od_clamp(touch_pressure_curve_q7(get_touch_pressure_xy(x, y)), 1, 127);
      bool tonic = od_mod(key_degree(k), per_oct()) == 0;
      set_led(x, y, vel[k] ? L_MAX(c) : (tonic ? ((c) << 1) : L_DIM(c)));
    }
    on_sequence_lock_guard_t lock;
    memcpy(key_vel, vel, sizeof(vel));
  }
  void clear_keyboard(void) { on_sequence_lock_guard_t lock; memset(key_vel, 0, sizeof(key_vel)); fill_held = false; }

  void draw_bottom(bool &skip_h) {
    if (button(0, 15, drift_on[cur_pat][sel] ? L_MAX(DV_MOD) : L_DIM(DV_MOD), ISOLATED,
               "DRIFT of the selected track: its notes move a degree now and then (off = back to what you wrote)")) {
      on_sequence_lock_guard_t l;
      drift_on[cur_pat][sel] ^= 1;
      if (!drift_on[cur_pat][sel]) memset(drift_off[cur_pat][sel], 0, S);
    }
    for (int t = 0; t < T; ++t)
      if (button(1 + t, 15, muted[t] ? L_DIM(tcol(t)) : L_ON(tcol(t)), ISOLATED, "mute")) muted[t] ^= 1;
    fill_held = shift_button(5, 15, get_touch_down(5, 15) ? L_MAX(DV_LIVE) : L_MID(DV_LIVE), NOT_ISOLATED, "FILL: hold, every track plays on every step");
    skip_h = shift_button(6, 15, get_touch_down(6, 15) ? L_MAX(DV_LIVE) : L_MID(DV_LIVE), NOT_ISOLATED, "SKIP: hold and tap a step of the rhythm, the track jumps there");
    bool chain_h = shift_button(13, 15, chain_on ? L_MAX(DV_PAT) : L_DIM(DV_PAT), NOT_ISOLATED,
                                "CHAIN: tap = on/off, hold + tap patterns = order (4 bars each)");
    if (is_last_widget_pressed()) chain_edit = 0;
    if (is_last_widget_released()) {
      if (chain_edit > 0) { chain_n = (uint8_t)chain_edit; chain_on = 1; }
      else chain_on ^= 1;
      if (chain_on && chain_n > 0) { // start from the first pattern of the chain
        chain_i = 0;
        if (is_transport_playing()) pend_pat = (int8_t)chain[0];
        else { on_sequence_lock_guard_t l; cur_pat = chain[0]; restart_positions(); }
      }
      chain_edit = -1;
    }
    uint32_t now = get_on_ui_time();
    bool blink = (now >> 17) & 1;
    int held_pat = -1;
    for (int i = 0; i < NP; ++i) {
      uint32_t col = cur_pat == i ? L_MAX(DV_PAT) : L_DIM(DV_PAT);
      if (pend_pat == i && blink) col = L_ON(DV_PAT);
      if (chain_h && chain_edit > 0) for (int j = 0; j < chain_edit; ++j) if (chain[j] == i) col = L_ON(DV_PAT);
      // long press (2 s, nothing copied): blinks red, release = the pattern is cleared
      bool armed = pat_hold_us[i] && !pat_used[i] && !chain_h && (uint32_t)(now - pat_hold_us[i]) >= CLEAR_HOLD_US;
      if (armed) col = ((now >> 16) & 1) ? L_MAX(DV_LIVE) : L_DIM(DV_LIVE);
      bool pressed = button(7 + i, 15, col, NOT_ISOLATED,
                            "pattern: tap = play it at the end of the bar | hold one + tap another = copy | hold 2 s and release = clear");
      if (is_last_widget_held()) { if (held_pat < 0) held_pat = i; }
      if (is_last_widget_released()) {
        if (armed) clear_pattern_now(i);
        pat_hold_us[i] = 0;
      }
      if (!pressed) continue;
      pat_hold_us[i] = now ? now : 1; pat_used[i] = false;
      if (chain_h && chain_edit >= 0) { if (chain_edit < 8) chain[chain_edit++] = (uint8_t)i; continue; }
      if (copy_src >= 0 && copy_src != i) { pat_used[copy_src] = true; pat_hold_us[i] = 0; copy_pattern(copy_src, i); continue; }
      if (is_transport_playing()) pend_pat = (int8_t)i;
      else { on_sequence_lock_guard_t l; cur_pat = (uint8_t)i; restart_positions(); }
    }
    copy_src = (int8_t)held_pat;
    shift_button(14, 15, get_touch_down(14, 15) ? L_MAX(DV_PAGE) : L_MID(DV_PAGE), NOT_ISOLATED,
                 "PRE: tap = sounds (then PRE / MIX / SND) | hold + T1-T4 = mixer | hold + NOTE = sound editor");
    if (is_last_widget_pressed()) pre_used = false;
    if (is_last_widget_released() && !pre_used) open_sound_area(MODE_PRESET);
    uint32_t pc = is_transport_playing() ? (((get_on_ui_time() >> 18) & 1) ? L_MAX(WHITE) : L_MID(WHITE)) : L_MID(WHITE);
    if (button(15, 15, pc, ISOLATED, "PLAY: start / stop")) {
      if (is_transport_playing()) stop_transport();
      else start_transport();
    }
  }
  static constexpr uint32_t CLEAR_HOLD_US = 2000000;
  void clear_pattern_now(int p) {
    on_sequence_lock_guard_t l;
    clear_pattern(p);
    if (p == cur_pat) prog_pos = 0;
    if (p == cur_pat) {
      for (int i = 0; i < MAXEV; ++i) ev[i].end_us = t_us; // stop what was sounding
      for (int t = 0; t < T; ++t)
        for (int q = 0; q < NPAR; ++q) { pos[t][q] = 0; sub[t][q] = 0; }
    }
  }
  void copy_pattern(int a, int b) {
    on_sequence_lock_guard_t l;
    for (int t = 0; t < T; ++t) {
      trig[b][t] = trig[a][t];
      memcpy(prob[b][t], prob[a][t], S); memcpy(nmask[b][t], nmask[a][t], S); memcpy(octv[b][t], octv[a][t], S); memcpy(vrand[b][t], vrand[a][t], sizeof(vrand[b][t]));
      memcpy(lenv[b][t], lenv[a][t], S); memcpy(repv[b][t], repv[a][t], S); memcpy(drift_off[b][t], drift_off[a][t], S);
      memcpy(lp_s[b][t], lp_s[a][t], NPAR); memcpy(lp_e[b][t], lp_e[a][t], NPAR); memcpy(spd[b][t], spd[a][t], NPAR);
      tmode[b][t] = tmode[a][t]; ctype[b][t] = ctype[a][t]; voicing[b][t] = voicing[a][t]; follow[b][t] = follow[a][t];
      glide[b][t] = glide[a][t]; reg[b][t] = reg[a][t]; drift_on[b][t] = drift_on[a][t];
    }
  }

  // GENERATE (PROB + a button): new values, in scale; with FOL = YES the notes then follow the chords
  // degree rows 0..6: chord tones (1, 3, 5) come up three times as often as the others
  uint8_t rnd_degree(void) { static const uint8_t w[13] = {0, 0, 0, 2, 2, 2, 4, 4, 4, 1, 3, 5, 6}; return w[rand() % 13]; }
  void gen_page(int t, int g) {
    on_sequence_lock_guard_t l;
    int p = cur_pat;
    for (int s = 0; s < S; ++s) {
      int r = rand() % 100;
      switch (g) {
        case PG_NOTE:
          nmask[p][t][s] = (uint8_t)(1 << rnd_degree());
          if (tmode[p][t] == M_POLY && r < 30) nmask[p][t][s] |= (uint8_t)(1 << rnd_degree()); // sometimes two notes
          drift_off[p][t][s] = 0;
          break;
        case PG_OCT: octv[p][t][s] = (uint8_t)(r < 70 ? 3 : r < 85 ? 4 : 2); break;          // mostly 0, sometimes +/- 1
        case PG_LEN: lenv[p][t][s] = (uint8_t)(1 + rand() % 5); break;                      // from short to long
        default: repv[p][t][s] = (uint8_t)(r < 75 ? 0 : r < 90 ? 1 : r < 97 ? 2 : 3); break; // mostly single hits
      }
    }
  }
  void gen_rhythm(int t) {
    on_sequence_lock_guard_t l;
    int p = cur_pat;
    static const uint8_t mono_pct[4] = {65, 20, 40, 25}, chord_pct[4] = {55, 5, 20, 5}; // by position in the beat
    const uint8_t *pct = tmode[p][t] == M_CHORD ? chord_pct : mono_pct;
    uint16_t m = 0;
    for (int tries = 0; tries < 8 && __builtin_popcount(m) < 2; ++tries) {
      m = 0;
      for (int s = 0; s < S; ++s) if ((int)(rand() % 100) < pct[s & 3]) m |= (uint16_t)(1u << s);
    }
    trig[p][t] = m;
    for (int s = 0; s < S; ++s) prob[p][t][s] = 0;
  }
  void gen_track(int t) { // a whole new sequence: rhythm, notes, octaves, lengths, repeats
    gen_rhythm(t);
    for (int g = PG_NOTE; g <= PG_REP; ++g) gen_page(t, g);
    on_sequence_lock_guard_t l;
    for (int g = 0; g < 4; ++g) memset(vrand[cur_pat][t][g], 0, S);
  }

  void draw_play(void) {
    // row 0: tracks (tap = select, hold = settings), pages, modifiers, ARM
    int held_track = -1;
    bool prob_down = get_touch_down(10, 0); // PROB held: track and page buttons generate
    bool pre_down = get_touch_down(14, 15);  // PRE held: T1-T4 open the mixer, NOTE the sound editor
    for (int t = 0; t < T; ++t) {
      bool h = shift_button(t, 0, sel == t ? L_MAX(tcol(t)) : L_DIM(tcol(t)), NOT_ISOLATED,
                            "track: tap = select, hold = its settings | PROB + tap = generate a new sequence");
      if (is_last_widget_pressed()) {
        sel = (uint8_t)t;
        if (pre_down) { pre_used = true; open_sound_area(MODE_MIX); return; }
        if (prob_down) gen_track(t);
      }
      if (h && !prob_down && !pre_down) held_track = t;
    }
    static const char *page_help[4] = {"NOTE page", "OCTAVE page", "LENGTH page", "REPEAT page (ratchet)"};
    for (int i = 0; i < 4; ++i)
      if (button(4 + i, 0, page == i ? L_MAX(DV_PAGE) : L_DIM(DV_PAGE), NOT_ISOLATED, page_help[i])) {
        if (pre_down && i == PG_NOTE) { pre_used = true; open_sound_area(MODE_SND); return; }
        page = (uint8_t)i;
        if (prob_down) gen_page(sel, i); // PROB + page = new values for that page
      }
    bool loop_h = shift_button(8, 0, get_touch_down(8, 0) ? L_MAX(DV_MOD) : L_MID(DV_MOD), NOT_ISOLATED, "LOOP: hold + tap a step = loop from step 1 to there (press two steps together = from one to the other)");
    bool speed_h = shift_button(9, 0, get_touch_down(9, 0) ? L_MAX(DV_MOD) : L_MID(DV_MOD), NOT_ISOLATED, "SPEED: hold + tap n = move every n sixteenths");
    bool prob_h = shift_button(10, 0, get_touch_down(10, 0) ? L_MAX(DV_MOD) : L_MID(DV_MOD), NOT_ISOLATED, "PROB: hold + tap a hit = 100 / 75 / 50 / 25 % | on a page: + tap = live variation +/- 1, 2, 3 | + page = generate that page | + track = generate the track");
    if (button(11, 0, page == PG_ARM ? L_MAX(DV_ARM) : L_DIM(DV_ARM), ISOLATED, "ARM: chord progression, key, scale"))
      page = PG_ARM;

    bool skip_h = false;
    draw_bottom(skip_h);
    draw_rhythm(loop_h, speed_h, prob_h, skip_h);
    if (held_track >= 0) draw_settings(held_track);
    else if (page == PG_ARM) draw_arm(loop_h);
    else draw_page(loop_h, speed_h, prob_h);
    draw_keyboard();
  }

  // =========================================================
  // SAVE / LOAD
  // =========================================================
  bool on_serialise(serialiser_t &s, int version) override {
    (void)version;
    der1va &o = *this;
    (void)o;
    memset(lg_tmode, 0xff, sizeof(lg_tmode)); memset(lg_ctype, 0xff, sizeof(lg_ctype)); memset(lg_voicing, 0xff, sizeof(lg_voicing));
    memset(lg_follow, 0xff, sizeof(lg_follow)); memset(lg_glide, 0xff, sizeof(lg_glide)); memset(lg_reg, 0xff, sizeof(lg_reg));
    memset(lg_drift, 0xff, sizeof(lg_drift)); memset(lg_prog, 0xff, sizeof(lg_prog)); lg_prog_s = 0xff; lg_prog_e = 0xff;
    OBJECT_BEGIN(s);
    FIELD_SYNTH_PRESET("sound1", 0);
    FIELD_SYNTH_PRESET("sound2", 1);
    FIELD_SYNTH_PRESET("sound3", 2);
    FIELD_SYNTH_PRESET("sound4", 3);
    FIELD_MIX_PRESET("mix");
    FIELD("scale", scale_idx, 0u, 11u);
    FIELD("key", root_pc, 0u, 11u);
    FIELD("bars", bars_idx, 0u, 3u);
    FIELD("prog_s", lg_prog_s, 0u, 255u);
    FIELD("prog_e", lg_prog_e, 0u, 255u);
    FIELD("pattern", cur_pat, 0u, 5u);
    FIELD("chain_n", chain_n, 0u, 8u);
    FIELD("chain_on", chain_on, 0u, 1u);
    FIELD_BASE64_ARRAY("prog", lg_prog);
    FIELD_BASE64_ARRAY("p_prog", prog);
    FIELD_BASE64_ARRAY("p_prog_s", prog_s);
    FIELD_BASE64_ARRAY("p_prog_e", prog_e);
    FIELD_BASE64_ARRAY("chain", chain);
    FIELD_BASE64_ARRAY("tmode", lg_tmode);
    FIELD_BASE64_ARRAY("p_tmode", tmode);
    FIELD_BASE64_ARRAY("ctype", lg_ctype);
    FIELD_BASE64_ARRAY("p_ctype", ctype);
    FIELD_BASE64_ARRAY("voicing", lg_voicing);
    FIELD_BASE64_ARRAY("p_voicing", voicing);
    FIELD_BASE64_ARRAY("follow", lg_follow);
    FIELD_BASE64_ARRAY("p_follow", follow);
    FIELD_BASE64_ARRAY("glide", lg_glide);
    FIELD_BASE64_ARRAY("p_glide", glide);
    FIELD_BASE64_ARRAY("reg", lg_reg);
    FIELD_BASE64_ARRAY("p_reg", reg);
    FIELD_BASE64_ARRAY("midi", midi_on);
    FIELD_BASE64_ARRAY("mute", muted);
    FIELD_BASE64_ARRAY("drift", lg_drift);
    FIELD_BASE64_ARRAY("p_drift", drift_on);
    FIELD_BASE64_ARRAY("trig", trig);
    FIELD_BASE64_ARRAY("prob", prob);
    FIELD_BASE64_ARRAY("notes", nmask);
    FIELD_BASE64_ARRAY("oct", octv);
    FIELD_BASE64_ARRAY("rand", vrand);
    FIELD_BASE64_ARRAY("len", lenv);
    FIELD_BASE64_ARRAY("rep", repv);
    FIELD_BASE64_ARRAY("loop_s", lp_s);
    FIELD_BASE64_ARRAY("loop_e", lp_e);
    FIELD_BASE64_ARRAY("speed", spd);
    FIELD_BASE64_ARRAY("drift_off", drift_off);
    OBJECT_END(s);
    if (s.reading) { load_legacy(); sanitise(); restart_positions(); }
    return true;
  }

  // The song page lists /PLINKY/<panel>/<bank>/. On a card where this panel has never saved, create the folders first.
  void ensure_song_folder(void) {
    if (song_folder_ok) return;
    const char *name = get_current_panel_name();
    if (!name || !name[0]) return;
    char path[48];
    snprintf(path, sizeof(path), "/PLINKY/%s", name);
    FRESULT a = f_mkdir(path);
    snprintf(path, sizeof(path), "/PLINKY/%s/00", name);
    FRESULT b = f_mkdir(path);
    song_folder_ok = (a == FR_OK || a == FR_EXIST) && (b == FR_OK || b == FR_EXIST);
  }

  // =========================================================
  // SOUND AREA: PRE (sounds) / MIX (mixer) / SND (sound editor + FX). Row 1 = track select, row 16 = tabs and back.
  // =========================================================
  void open_sound_area(int m) {
    clear_keyboard();
    if (m == MODE_PRESET) pp.picker.reset_which_slot_is_selected();
    mode = (uint8_t)m;
  }
  void draw_sound_area(void) {
    clear_keyboard();
    // row 1: track select (same place as on the main screen)
    for (int t = 0; t < T; ++t)
      if (button(t, 0, t == sel ? L_MAX(tcol(t)) : L_DIM(tcol(t)), ISOLATED, "select track")) {
        sel = (uint8_t)t;
        if (mode == MODE_PRESET) pp.picker.reset_which_slot_is_selected();
      }
    // row 16: tabs. Leaving PRE without OK is like cancel: the track keeps its sound
    static const uint8_t tab_mode[3] = {MODE_PRESET, MODE_MIX, MODE_SND};
    static const char *tab_help[3] = {"PRE: choose a sound", "MIX: volume, pan, delay, reverb of the 4 tracks",
                                      "SND: sound editor (envelope, filter...) and delay / reverb / EQ"};
    for (int i = 0; i < 3; ++i)
      if (button(i, 15, mode == tab_mode[i] ? L_MAX(DV_PAGE) : L_MID(DV_PAGE), ISOLATED, tab_help[i]) && mode != tab_mode[i]) {
        if (mode == MODE_PRESET) pp.picker.on_done();
        open_sound_area(tab_mode[i]);
        return;
      }
    if (mode == MODE_PRESET) {
      // the system picker (instant preview on the track): factory sounds, saved presets and your own banks on the SD.
      // X = cancel (the track keeps its sound), play = load. Both go back to playing.
      pp.picker.preset_picker(sel, 2, 10, FLAG_PICKER_ENABLE_DELETE);
      if (pp.picker.cancel_button(13, 15)) { mode = MODE_PLAY; return; }
      int action = pp.picker.preset_ok_button(sel, false, 15, 15);
      if (action > 0) mode = MODE_PLAY;
      return;
    }
    if (mode == MODE_MIX) {
      // 4 columns per track (rows 2-14): volume, pan / stereo, delay send, reverb send. Row 15: mute.
      static const int mix_params[4] = {VOICE_PARAM_VOLUME, VOICE_PARAM_STEREO, VOICE_PARAM_DELAY_SEND, VOICE_PARAM_REVERB_SEND};
      for (int t = 0; t < T; ++t) {
        synth_param_sliders_block(mix_sliders[t], t * 4, 1, 4, 13, t, mix_params);
        for (int i = 0; i < 4; ++i)
          if (button(t * 4 + i, 14, muted[t] ? L_DIM(tcol(t)) : L_ON(tcol(t)), ISOLATED, "mute")) muted[t] ^= 1;
      }
    } else {
      // SND: the Plinky sound editor for the selected track. Rows 2-6: sends, audio in, delay, reverb, EQ, output
      // (delay / reverb / EQ / output are for the whole song); rows 7-11: pitch, sample, chorus, sub, fold, filters, ADSR;
      // row 12: sound flags; XY pad with LFO / envelope on the right, rows 12-15.
      pp.edit(sel, 1);
      synth_xy_block(&pp.the_xy_pad, sel, 8, 11, 7, 4);
    }
    if (button(15, 15, L_MID(WHITE), ISOLATED, "back to playing")) mode = MODE_PLAY;
  }

  void on_ui(int delta_time_us) override {
    (void)delta_time_us;
    int sp = get_scroll_page();
    if (sp < 0) return;
    leds_clear();
    if (sp == 1) { clear_keyboard(); ensure_song_folder(); song_page.saveload(16, true, FLAG_PICKER_ENABLE_DELETE); return; }
    if (sp > 1) { scroll_to_page(1); return; }
    if (mode != MODE_PLAY) { draw_sound_area(); return; }
    draw_play();
  }
};

static_assert(sizeof(der1va) <= 128 * 1024, "DER1VA is larger than the 128 KB panel arena");
