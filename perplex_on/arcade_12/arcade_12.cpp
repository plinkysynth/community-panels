/*
@Name: ARCADE12
@Author: PERPLEX ON
@Firmware: beta
@Preferred Panels: blocks
@Tags: game, stack, breakout, generative, sequencer, sonification, midi
@Description: Two games that write their own soundtrack, one cartridge slot.
  SELECTOR: both games run as silent demos, SELECT GAME rolls between them.
  Tap the left demo for KLOTZ, the right one for PRALL (an arcade start
  jingle plays). The lettering is not a button. Bottom-right pad in a game:
  tap = play / pause, hold 0.7 s = back to the selector. A run in progress
  waits there, paused.
  KLOTZ (falling blocks): swipe left / right moves the stone, swipe down drops
  it on the next 16th, tap rotates. The stack is a 10-step sequencer against
  the 16-step bar, landing strums a chord, cleared lines move the harmony,
  four lines at once modulate up a fourth. One extra layer per level.
  PRALL (breakout on the clock): drag moves the paddle, tap or swipe up
  launches. Every bounce is a note: brick row = height in the chord, brick
  column = which chord tone. Cleared rows move the chords, every level is a
  song. Power-ups: HALF, DOUBLE, MULTI, WIDE.
  Each game keeps its own voices, mix, MIDI channels, root, scale, settings.
  HIGH SCORES: top 5 per game, stored with the panel settings. After game
  over the score scrolls across the grid; a new best plays a fanfare.
  Pages: Play / Sound (current game, bottom row selects + auditions) /
  Save-Load (both sound sets). Settings (right-up): the current game's.
*/

// Code map
//   klotz_game  KLOTZ: game, soundtrack, gesture engine, drawing, settings
//   prall_game  PRALL: game, soundtrack, gesture engine, drawing, settings
//   arcade_12   the console: selector, sound-set swap, high scores, play pad,
//               pages and serialisation. It owns both games and forwards
//               on_sequence / on_ui to the one that is running.
// Each game talks to the console through the same small interface:
//   setup_default(bank)  default sounds into its own preset store
//   enter()              start a new run or resume a paused one
//   on_sequence(dt)      timing-critical game step (only while it is running)
//   on_menu_sequence(dt) selector on screen: only sound previews
//   ui_play()            gestures + drawing; leaves the pad colour in pad_col
//   settings_page(page)  draws one settings page, returns true on change

#define PANEL_PAD_COLOR LED_RGB(31, 6, 2)

// =====================================================================
// GAME 1 - KLOTZ: falling blocks. The settled stack is the sequencer.
// =====================================================================
struct klotz_hit_t {
  uint32_t src;      // voice allocator source id
  int32_t delay_us;  // > 0: waiting to start
  int32_t left_us;   // gate time remaining once started
  uint8_t preset;
  uint8_t note;      // synth note
  uint8_t midi_note;
  uint8_t midi_ch;   // zero-based wire channel
  uint8_t vel;
  uint8_t active;    // 0 free, 1 pending, 2 sounding
  int8_t voice;
};

struct klotz_game {
  enum { WW = 10, HH = 16, NT = 7, P_BASS = 7, P_PERC = 8, NUM_VOICES_EDIT = 9, NUM_HITS = 48 };
  enum { ST_IDLE, ST_PLAY, ST_OVER };
  enum { C_WHITE = 7, C_SIGNAL = 8, C_WALL = 9, C_PERC = 10 };

  // ---- game state ----
  uint8_t well[HH][WW]; // 0 empty, 1..7 stone type + 1
  uint32_t born_us[HH][WW];
  uint32_t seqhit_us[HH][WW];
  uint8_t cur_t, cur_rot, nxt;
  int8_t cur_x, cur_y;
  bool has_cur;
  uint8_t bag[NT];
  uint8_t bag_n;
  uint8_t state;
  uint16_t lines;
  uint8_t level, lines_lv;
  uint32_t score;
  uint16_t clear_mask;
  uint32_t clear_until;
  uint32_t clear_t0;
  uint32_t step;
  uint8_t grav_count;
  uint8_t chord_i;
  uint8_t key_mod;
  bool drop_pending, snd_move, snd_rot;

  // ---- timing / voices ----
  clock_divider_t clk;
  voice_allocator_t voice_allocator;
  klotz_hit_t hits[NUM_HITS];
  uint32_t hit_counter;

  // ---- requests from on_ui ----
  int8_t req_dx;
  bool req_rot, req_drop, req_start, req_toggle;
  bool paused;
  int8_t req_audition; // preset + 1

  // ---- visuals ----
  uint32_t beat_us, danger_us, over_us;
  uint8_t cur_beat;
  uint8_t act[4];

  // ---- gesture ----
  bool g_down, g_ignore;
  uint8_t g_axis;
  int g_x0_q8, g_y0_q8, g_fx, g_fy, g_applied, g_max_q8;
  uint32_t g_t0, g_up_t, g_move_t;

  // ---- settings ----
  uint8_t root = 2; // D
  uint8_t prog = 0; // 0 minor, 1 major, 2 modal
  uint8_t scl = 0;  // 0 classic voicings, 1..11 Plinky scales (stacked scale thirds), 12 chrom = classic
  uint8_t midi_on = 1;

  uint8_t sound_sel;
  uint32_t pad_col; // colour of the play / pause pad, drawn by the panel

  const char *get_track_name(int i, char buf[16]) {
    static const char *const names[NUM_VOICES_EDIT] = {"I Pad", "O Bass", "T Pluck", "S Bell", "Z Glass",
                                                       "J Keys", "L Reed", "Sub", "Perc"};
    if (!buf) return NULL;
    snprintf(buf, 16, "%s", names[clampi(i, 0, NUM_VOICES_EDIT - 1)]);
    return buf;
  }

  // =====================================================================
  // colours (direct LED_RGB triplets, 5-bit full scale = 31)
  // =====================================================================
  static void col_rgb(int c, int *r, int *g, int *b) {
    static const uint8_t COL[11][3] = {
        {0, 28, 31},  // I cyan
        {31, 26, 0},  // O yellow
        {22, 0, 31},  // T purple
        {4, 31, 6},   // S green
        {31, 2, 3},   // Z red
        {3, 8, 31},   // J blue
        {31, 11, 0},  // L orange
        {24, 24, 22}, // white
        {31, 6, 2},   // signal red
        {20, 20, 18}, // wall grey
        {14, 14, 14}, // perc grey
    };
    c = clampi(c, 0, 10);
    *r = COL[c][0];
    *g = COL[c][1];
    *b = COL[c][2];
  }
  // linear brightness 0..256, gamma (square) right before quantising; white_q8 mixes toward white
  static uint32_t lc(int c, int b, int white_q8 = 0) {
    if (b <= 0) return 0;
    b = mini(b, 256);
    int r, g, bl;
    col_rgb(c, &r, &g, &bl);
    if (white_q8 > 0) {
      r += ((24 - r) * white_q8) >> 8;
      g += ((24 - g) * white_q8) >> 8;
      bl += ((24 - bl) * white_q8) >> 8;
    }
    int gm = (b * b) >> 8;
    int rr = (r * gm) >> 8, gg = (g * gm) >> 8, bb = (bl * gm) >> 8;
    if (rr == 0 && r > 8) rr = 1;
    if (gg == 0 && g > 8) gg = 1;
    if (bb == 0 && bl > 8) bb = 1;
    return LED_RGB(rr, gg, bb);
  }

  // =====================================================================
  // stones
  // =====================================================================
  static void cells_of(int t, int rot, int x, int y, int8_t *cx, int8_t *cy) {
    static const int8_t S[7][4][2] = {
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, // I
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, // O
        {{1, 0}, {0, 1}, {1, 1}, {2, 1}}, // T
        {{1, 0}, {2, 0}, {0, 1}, {1, 1}}, // S
        {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, // Z
        {{0, 0}, {0, 1}, {1, 1}, {2, 1}}, // J
        {{2, 0}, {0, 1}, {1, 1}, {2, 1}}, // L
    };
    static const int8_t N[7] = {4, 2, 3, 3, 3, 3, 3};
    t = clampi(t, 0, 6);
    for (int i = 0; i < 4; ++i) {
      int a = S[t][i][0], b = S[t][i][1];
      for (int r = 0; r < (rot & 3); ++r) {
        int na = N[t] - 1 - b;
        b = a;
        a = na;
      }
      cx[i] = (int8_t)(x + a);
      cy[i] = (int8_t)(y + b);
    }
  }
  static int stone_box(int t) { return t == 0 ? 4 : (t == 1 ? 2 : 3); }

  bool collides(int t, int rot, int x, int y) const {
    int8_t cx[4], cy[4];
    cells_of(t, rot, x, y, cx, cy);
    for (int i = 0; i < 4; ++i) {
      if (cx[i] < 0 || cx[i] >= WW || cy[i] >= HH) return true;
      if (cy[i] >= 0 && well[cy[i]][cx[i]]) return true;
    }
    return false;
  }

  uint8_t draw_bag() {
    if (bag_n == 0) {
      for (int i = 0; i < NT; ++i) bag[i] = (uint8_t)i;
      for (int i = NT - 1; i > 0; --i) {
        int j = randi(0, i);
        uint8_t tmp = bag[i];
        bag[i] = bag[j];
        bag[j] = tmp;
      }
      bag_n = NT;
    }
    return bag[--bag_n];
  }

  int stack_top() const {
    for (int y = 0; y < HH; ++y)
      for (int x = 0; x < WW; ++x)
        if (well[y][x]) return HH - y;
    return 0;
  }

  int ghost_y() const {
    int y = cur_y;
    while (!collides(cur_t, cur_rot, cur_x, y + 1) && y < HH) y++;
    return y;
  }

  int grav_interval() const {
    static const uint8_t G[8] = {4, 4, 3, 3, 2, 2, 2, 1};
    return G[mini(level, 7)];
  }

  // =====================================================================
  // harmony
  // =====================================================================
  int chord_tone(int i) const {
    static const int8_t ROOTS[3][4] = {{0, 8, 3, 10}, {0, 7, 9, 5}, {0, 5, 10, 3}};
    static const int8_t TONES[3][4][5] = {
        {{0, 3, 7, 10, 14}, {0, 4, 7, 11, 14}, {0, 4, 7, 9, 14}, {0, 4, 7, 9, 14}},   // Dm Bb F C
        {{0, 4, 7, 11, 14}, {0, 4, 7, 10, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 11, 14}}, // I V vi IV
        {{0, 3, 7, 10, 14}, {0, 4, 7, 10, 14}, {0, 4, 7, 11, 14}, {0, 4, 7, 11, 14}}, // dorian i IV bVII bIII
    };
    int p = prog < 3 ? prog : 0;
    int c = chord_i & 3;
    if (i < 0) i = 0;
    int k = 48 + ((root + key_mod) % 12);
    if (scl >= 1 && scl <= 11) {
      // Plinky scales: chords are stacked scale thirds (root 3rd 5th 7th 9th), progression = scale degrees
      static const uint8_t SCN[11] = {7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 6};
      static const uint8_t SCV[11][7] = {
          {0, 2, 4, 5, 7, 9, 11}, // major
          {0, 2, 3, 5, 7, 8, 10}, // minor
          {0, 2, 3, 5, 7, 9, 10}, // dorian
          {0, 1, 3, 5, 7, 8, 10}, // phrygian
          {0, 2, 4, 6, 7, 9, 11}, // lydian
          {0, 2, 4, 5, 7, 9, 10}, // mixolydian
          {0, 2, 3, 5, 7, 8, 11}, // harmonic minor
          {0, 2, 4, 7, 9, 0, 0},  // pentatonic major
          {0, 3, 5, 7, 10, 0, 0}, // pentatonic minor
          {0, 2, 3, 7, 8, 0, 0},  // hirajoshi
          {0, 2, 4, 6, 8, 10, 0}, // whole tone
      };
      static const uint8_t DEG[3][4] = {{0, 5, 2, 6}, {0, 4, 5, 3}, {0, 3, 6, 2}}; // i VI III VII | I V vi IV | i IV bVII bIII
      int si = scl - 1, n = SCN[si];
      int dg = DEG[p][c] + 2 * (i % 5);
      return k + SCV[si][dg % n] + 12 * (dg / n) + 12 * (i / 5);
    }
    return k + ROOTS[p][c] + TONES[p][c][i % 5] + 12 * (i / 5);
  }
  static int clampn(int n) {
    while (n > 94) n -= 12;
    while (n < 30) n += 12;
    return n;
  }

  // =====================================================================
  // note pool: hard-gated one-shots, synth + level-triggered MIDI
  // =====================================================================
  void fire(int preset, int note, int vel, int midi_ch, int midi_note, int delay_us, int gate_ms = 0) {
    static const uint16_t GATE_MS[NUM_VOICES_EDIT] = {900, 220, 160, 320, 260, 200, 260, 300, 60};
    int slot = -1;
    for (int i = 0; i < NUM_HITS; ++i)
      if (!hits[i].active) {
        slot = i;
        break;
      }
    if (slot < 0) return; // pool full: drop
    klotz_hit_t *h = &hits[slot];
    h->src = 0x4b000000u | (++hit_counter & 0x00ffffffu);
    h->delay_us = maxi(0, delay_us);
    h->left_us = (gate_ms > 0 ? gate_ms : GATE_MS[clampi(preset, 0, NUM_VOICES_EDIT - 1)]) * 1000;
    h->preset = (uint8_t)preset;
    h->note = (uint8_t)clampi(note, 0, 127);
    h->midi_note = (uint8_t)clampi(midi_note, 0, 127);
    h->midi_ch = (uint8_t)(midi_ch >= 0 ? midi_ch : get_midi_channel_for_preset_idx(preset, true));
    h->vel = (uint8_t)clampi(vel, 1, 127);
    h->voice = -1;
    h->active = 1;
  }

  void start_hit(klotz_hit_t *h) {
    h->active = 2;
    int v = voice_allocator.voice_allocate(h->src, 8, 0, DEFAULT_VOICE_ALLOCATOR_VOICES, h->preset);
    if (v >= 0) {
      for (int i = 0; i < NUM_HITS; ++i)
        if (&hits[i] != h && hits[i].active == 2 && hits[i].voice == v) hits[i].voice = -1; // stolen
      h->voice = (int8_t)v;
      play_synth(v, h->preset, h->vel, (int)h->note << 8, true);
    }
    int a = (h->preset == P_PERC) ? (h->midi_note == 42 ? 0 : 1)
            : (h->preset == 1 || h->preset == P_BASS) ? 1
            : (h->preset == 0 || h->preset == 5) ? 2
                                                    : 3;
    act[a] = (uint8_t)maxi(act[a], mini(255, h->vel * 2));
  }

  void release_hit(klotz_hit_t *h) {
    if (h->voice >= 0 && voice_allocator.find_voice(h->src, 0, DEFAULT_VOICE_ALLOCATOR_VOICES) == h->voice) {
      synth_note_up(h->voice);
      voice_allocator.voice_deallocate(h->src, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
    }
    h->active = 0;
    h->voice = -1;
  }

  void update_hits(int dt) {
    for (int i = 0; i < NUM_HITS; ++i) {
      klotz_hit_t *h = &hits[i];
      if (!h->active) continue;
      if (h->active == 1) {
        h->delay_us -= dt;
        if (h->delay_us > 0) continue;
        start_hit(h);
      }
      h->left_us -= dt;
      if (h->left_us <= 0) {
        release_hit(h);
        continue;
      }
      if (midi_on) declare_midi_note(h->midi_ch, h->midi_note, h->vel, MIDI_PORT_1);
    }
    send_declared_midi_notes();
  }

  void release_all_hits() {
    for (int i = 0; i < NUM_HITS; ++i)
      if (hits[i].active) release_hit(&hits[i]);
  }

  // musical shortcuts
  void play_stone(int t, int note, int vel, int delay_us, int gate_ms = 0) {
    static const int8_t OCT[NT] = {0, -12, 12, 12, 12, 0, 12};
    int n = clampn(note + OCT[clampi(t, 0, NT - 1)]);
    fire(t, n, vel, -1, n, delay_us, gate_ms);
  }
  void kick(int vel, int d = 0) { fire(P_PERC, 36, vel, 9, 36, d); }
  void snare(int vel, int d = 0) { fire(P_PERC, 60, vel, 9, 38, d, 90); }
  void hat(int vel, int d = 0) { fire(P_PERC, 84, vel, 9, 42, d, 30); }
  void clap(int vel, int d = 0) { fire(P_PERC, 72, vel, 9, 39, d, 120); }

  // =====================================================================
  // game flow
  // =====================================================================
  void spawn() {
    cur_t = nxt;
    nxt = draw_bag();
    cur_rot = 0;
    int8_t cx[4], cy[4];
    cells_of(cur_t, 0, 0, 0, cx, cy);
    int miny = 9;
    for (int i = 0; i < 4; ++i) miny = mini(miny, cy[i]);
    cur_x = (int8_t)((WW - stone_box(cur_t)) / 2);
    cur_y = (int8_t)(-miny);
    grav_count = 0;
    has_cur = true;
    if (collides(cur_t, cur_rot, cur_x, cur_y)) game_over();
  }

  void reset_game() {
    memset(well, 0, sizeof(well));
    memset(born_us, 0, sizeof(born_us));
    memset(seqhit_us, 0, sizeof(seqhit_us));
    score = 0;
    lines = 0;
    level = 0;
    lines_lv = 0;
    chord_i = 0;
    key_mod = 0;
    clear_mask = 0;
    drop_pending = snd_move = snd_rot = false;
    bag_n = 0;
    nxt = draw_bag();
    state = ST_PLAY;
    paused = false;
    spawn();
  }

  void game_over() {
    state = ST_OVER;
    has_cur = false;
    over_us = time_us();
    int sp = qn_period_us / 4;
    static const int8_t DESC[5] = {7, 5, 3, 1, 0};
    for (int k = 0; k < 5; ++k) play_stone(0, chord_tone(DESC[k]) + 12, 80, k * sp, 700);
  }

  bool try_move(int dx) {
    if (collides(cur_t, cur_rot, cur_x + dx, cur_y)) return false;
    cur_x = (int8_t)(cur_x + dx);
    snd_move = true;
    return true;
  }

  void rotate() {
    int r = (cur_rot + 1) & 3;
    static const int8_t KICKS[5] = {0, -1, 1, -2, 2};
    for (int k = 0; k < 5; ++k)
      if (!collides(cur_t, r, cur_x + KICKS[k], cur_y)) {
        cur_rot = (uint8_t)r;
        cur_x = (int8_t)(cur_x + KICKS[k]);
        snd_rot = true;
        return;
      }
  }

  void lock(bool hard, int dist) {
    int8_t cx[4], cy[4];
    cells_of(cur_t, cur_rot, cur_x, cur_y, cx, cy);
    for (int i = 0; i < 4; ++i)
      if (cy[i] < 0) {
        game_over();
        return;
      }
    uint32_t now = time_us();
    int ysum = 0;
    for (int i = 0; i < 4; ++i) {
      well[cy[i]][cx[i]] = (uint8_t)(cur_t + 1);
      born_us[cy[i]][cx[i]] = now;
      ysum += cy[i];
    }
    int avg_y = ysum / 4;
    int sp32 = qn_period_us / 8;
    int vel = hard ? mini(127, 92 + dist * 2) : 76;
    // landing strum: one note per occupied column, low to high
    int k = 0;
    for (int x = 0; x < WW; ++x) {
      bool used = false;
      for (int i = 0; i < 4; ++i) used |= (cx[i] == x);
      if (!used) continue;
      play_stone(cur_t, chord_tone((x * 4) / 5 + ((HH - 1 - avg_y) / 4) * 2), vel, k * sp32);
      k++;
    }
    kick(hard ? 112 : 84);
    if (hard) score += (uint32_t)(dist * 2);

    uint16_t full = 0;
    int n = 0;
    for (int y = 0; y < HH; ++y) {
      bool f = true;
      for (int x = 0; x < WW; ++x) f &= (well[y][x] != 0);
      if (f) {
        full |= (uint16_t)(1u << y);
        n++;
      }
    }
    has_cur = false;
    if (full) {
      clear_mask = full;
      clear_until = step + 2;
      clear_t0 = now;
      int ri = 0;
      for (int y = 0; y < HH; ++y) {
        if (!(full & (1u << y))) continue;
        for (int x = 0; x < WW; ++x)
          play_stone(well[y][x] - 1, chord_tone(x + 5), 70, ((x + ri * 2) * sp32) / 2 + sp32, 140);
        ri++;
      }
      if (n == 4)
        clap(127);
      else
        snare(110, 2 * sp32);
    } else {
      spawn();
    }
  }

  void finish_clear() {
    int n = 0, w = HH - 1;
    for (int y = HH - 1; y >= 0; --y) {
      if (clear_mask & (1u << y)) {
        n++;
        continue;
      }
      if (w != y)
        for (int x = 0; x < WW; ++x) {
          well[w][x] = well[y][x];
          born_us[w][x] = born_us[y][x];
        }
      w--;
    }
    for (; w >= 0; --w)
      for (int x = 0; x < WW; ++x) {
        well[w][x] = 0;
        born_us[w][x] = 0;
      }
    memset(seqhit_us, 0, sizeof(seqhit_us));
    static const uint16_t PTS[5] = {0, 100, 300, 500, 800};
    lines = (uint16_t)(lines + n);
    lines_lv = (uint8_t)(lines_lv + n);
    score += PTS[mini(n, 4)] * (uint32_t)(level + 1);
    chord_i = (uint8_t)((chord_i + n) & 3);
    if (n == 4) {
      key_mod = (uint8_t)((key_mod + 5) % 12);
      for (int i = 0; i < 4; ++i) play_stone(0, chord_tone(i), 110, 0, 1400);
    }
    while (lines_lv >= 6) {
      lines_lv = (uint8_t)(lines_lv - 6);
      if (level < 15) level++;
    }
    clear_mask = 0;
    spawn();
  }

  // one 16th-note step of the main clock
  void on_step() {
    uint32_t now = time_us();
    uint32_t i = step++;
    int s16 = (int)(i & 15);
    cur_beat = (uint8_t)(s16 >> 2);
    if ((s16 & 3) == 0) beat_us = now;
    int top = stack_top();

    // foundation: sub on 1 and 3 follows the chord root
    if (s16 == 0 || s16 == 8) {
      int r = clampn(chord_tone(0) - 12);
      fire(P_BASS, r, 92, -1, r, 0);
    }
    if (state == ST_PLAY && !paused) {
      // the arrangement grows with the game: one layer per level
      //  L1 kick on 1 | L2 offbeat hats | L3 kick on 1+3 | L4 backbeat clap
      //  L5 four-on-the-floor + 16th ghost hats | L6 sub on the offbeats
      //  L7+ pad swell every bar  | stack >= 12: heartbeat override
      int lv = level + (top >= 8 ? 1 : 0); // a tall stack adds pressure
      bool danger = top >= 12;
      if (danger) {
        if (s16 == 0 || s16 == 3) {
          kick(96);
          danger_us = now;
        }
      } else {
        if (lv >= 1 && s16 == 0) kick(74);
        if (lv >= 3 && s16 == 8) kick(68);
        if (lv >= 5 && (s16 == 4 || s16 == 12)) kick(62);
      }
      if (lv >= 2 && (s16 & 3) == 2) hat(52 + mini(lv, 7) * 3);
      if (lv >= 5 && (s16 & 1)) hat(26);
      if (lv >= 4 && (s16 == 4 || s16 == 12)) clap(lv >= 6 ? 74 : 60);
      if (lv >= 6 && (s16 & 7) == 6) {
        int r = clampn(chord_tone(0) - 12);
        fire(P_BASS, r + 12, 70, -1, r + 12, 0, 120);
      }
      if (lv >= 7 && s16 == 0)
        for (int k = 0; k < 3; ++k) play_stone(0, chord_tone(k * 2), 48, 0, 1600);
    }

    // the stack is the score: 10 steps against 16
    int col = (int)(i % WW);
    for (int y = 0; y < HH; ++y) {
      uint8_t c = well[y][col];
      if (!c) continue;
      if (clear_mask & (1u << y)) break;
      play_stone(c - 1, chord_tone(HH - 1 - y), state == ST_OVER ? 58 : 44 + mini(level, 7) * 3, 0);
      seqhit_us[y][col] = now;
      break;
    }

    if (state != ST_PLAY) return;
    if (clear_mask) {
      if (step >= clear_until) finish_clear();
      return;
    }
    if (!has_cur) return;

    if (snd_move) {
      snd_move = false;
      play_stone(cur_t, chord_tone(cur_x + 2) + 12, 34, 0, 70);
      hat(56);
    }
    if (snd_rot) {
      snd_rot = false;
      play_stone(cur_t, chord_tone(cur_rot + 3) + 12, 46, 0, 90);
    }
    if (drop_pending) {
      drop_pending = false;
      int d = 0;
      while (!collides(cur_t, cur_rot, cur_x, cur_y + 1)) {
        cur_y++;
        d++;
      }
      lock(true, d);
      return;
    }
    if (++grav_count >= grav_interval()) {
      grav_count = 0;
      if (collides(cur_t, cur_rot, cur_x, cur_y + 1)) {
        lock(false, 0);
      } else {
        cur_y++;
        hat(mini(110, 50 + level * 8));
      }
    }
  }

  // =====================================================================
  // lifecycle
  // =====================================================================
  void setup_voice(synth_preset_t *bank, int p, int a, int d, int s, int r, int lp) {
    synth_preset_t *sp = &bank[p];
    set_lo_hi_same_for_all_corners(VOICE_PARAM_ATTACK, a, a, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_DECAY, d, d, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_SUSTAIN, s, s, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_RELEASE, r, r, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_CUTOFF_LP, lp, lp, sp);
  }
  void set_p(synth_preset_t *bank, int p, int param, int v) { set_lo_hi_same_for_all_corners(param, v, v, &bank[p]); }

  void clear_runtime() {
    memset(&voice_allocator, 0, sizeof(voice_allocator));
    memset(hits, 0, sizeof(hits));
    for (int i = 0; i < NUM_HITS; ++i) hits[i].voice = -1;
    req_dx = 0;
    req_rot = req_drop = req_start = req_toggle = false;
    paused = false;
    req_audition = 0;
    g_down = false;
    g_up_t = g_move_t = 0;
  }

  void setup_default(synth_preset_t *bank) {
    clear_runtime();
    // I pad
    setup_voice(bank, 0, 40, 90, 80, 70, 70);
    set_p(bank, 0, VOICE_PARAM_REVERB_SEND, 90);
    set_p(bank, 0, VOICE_PARAM_DELAY_SEND, 30);
    // O bass
    setup_voice(bank, 1, 0, 40, 30, 20, 50);
    set_p(bank, 1, VOICE_PARAM_SUBOSC, 90);
    // T pluck
    setup_voice(bank, 2, 0, 35, 0, 30, 90);
    set_p(bank, 2, VOICE_PARAM_RESONANCE, 40);
    set_p(bank, 2, VOICE_PARAM_DELAY_SEND, 60);
    // S bell
    setup_voice(bank, 3, 0, 60, 0, 60, 110);
    set_p(bank, 3, VOICE_PARAM_WAVEFOLD, 90);
    set_p(bank, 3, VOICE_PARAM_REVERB_SEND, 70);
    // Z glass
    setup_voice(bank, 4, 0, 50, 0, 50, 100);
    set_p(bank, 4, VOICE_PARAM_WAVEFOLD, 100);
    set_p(bank, 4, VOICE_PARAM_CHORUS, 90);
    set_p(bank, 4, VOICE_PARAM_DELAY_SEND, 60);
    // J keys
    setup_voice(bank, 5, 2, 45, 20, 35, 75);
    // L reed
    setup_voice(bank, 6, 10, 55, 40, 40, 80);
    set_p(bank, 6, VOICE_PARAM_CHORUS, 80);
    // sub
    setup_voice(bank, P_BASS, 0, 50, 40, 25, 40);
    set_p(bank, P_BASS, VOICE_PARAM_SUBOSC, 110);
    // perc: noise below centre on subosc, very short
    setup_voice(bank, P_PERC, 0, 18, 0, 10, 120);
    set_p(bank, P_PERC, VOICE_PARAM_SUBOSC, 10);

    memset(well, 0, sizeof(well));
    bag_n = 0;
    nxt = draw_bag();
    has_cur = false;
    state = ST_IDLE;
    sound_sel = 0;
  }

  void on_load_finished(void) {
    clear_runtime();
    memset(well, 0, sizeof(well));
    has_cur = false;
    clear_mask = 0;
    state = ST_IDLE;
  }

  // =====================================================================
  // sequence (timing-critical)
  // =====================================================================
  void handle_audition() {
    if (req_audition) {
      int p = req_audition - 1;
      req_audition = 0;
      // preview phrase on fixed timing (120 bpm 8ths), works with the clock stopped
      release_all_hits();
      const int e = 250000;
      if (p == P_PERC) {
        kick(100, 0);
        hat(70, e);
        snare(90, 2 * e);
        hat(70, 3 * e);
        clap(90, 4 * e);
      } else if (p == P_BASS) {
        int r = clampn(chord_tone(0) - 12);
        fire(P_BASS, r, 96, -1, r, 0, 400);
        fire(P_BASS, r + 7, 90, -1, r + 7, 2 * e, 200);
        fire(P_BASS, r + 12, 90, -1, r + 12, 3 * e, 400);
      } else {
        static const uint8_t PH[4] = {0, 2, 4, 5};
        for (int k = 0; k < 4; ++k) play_stone(p, chord_tone(PH[k]), 96, k * e);
        play_stone(p, chord_tone(0), 80, 5 * e, p == 0 ? 1400 : 0); // ring-out
      }
    }
  }

  // the selector is on screen: only sound previews run, the game is frozen
  void on_menu_sequence(int delta_time_us) {
    handle_audition();
    update_hits(delta_time_us);
  }

  // called from on_sequence when the selector starts or resumes this game
  void enter() {
    req_dx = 0;
    req_rot = req_drop = req_start = req_toggle = false;
    g_down = false;
    g_up_t = g_move_t = 0;
    clk.update(-1, 4, 1, UPDATE_DIV_ON_QUARTER_NOTE, true); // drop edges collected while away
    if (state == ST_PLAY)
      paused = true; // a run in progress waits on the pad
    else
      reset_game();
  }

  void on_sequence(int delta_time_us) {
    // consume UI requests
    if (req_start) {
      req_start = false;
      if (state != ST_PLAY) reset_game();
    }
    if (req_toggle) { // play / pause button (bottom right)
      req_toggle = false;
      if (state == ST_PLAY) paused = !paused;
      else reset_game();
    }
    handle_audition();

    if (paused) {
      // frozen: eat all input, keep the clock phase, let ringing notes release
      req_dx = 0;
      req_rot = req_drop = false;
      clk.update(-1, 4, 1, UPDATE_DIV_ON_QUARTER_NOTE, true);
      update_hits(delta_time_us);
      return;
    }
    if (state == ST_PLAY && has_cur && !clear_mask) {
      while (req_dx > 0) {
        if (!try_move(1)) break;
        req_dx--;
      }
      while (req_dx < 0) {
        if (!try_move(-1)) break;
        req_dx++;
      }
      if (req_rot) rotate();
      if (req_drop) drop_pending = true;
    }
    req_dx = 0;
    req_rot = false;
    req_drop = false;
    // freerunning 16th grid, phase-locked to transport when it plays
    int edges = clk.update(-1, 4, 1, UPDATE_DIV_ON_QUARTER_NOTE, true);
    if (edges > 4) edges = 4;
    while (edges-- > 0) on_step();

    update_hits(delta_time_us);
  }

  // =====================================================================
  // UI
  // =====================================================================
  // Gesture engine: one touch = exactly ONE gesture class, decided once, never mixed.
  //  1. settle   : first 30 ms after contact only track the origin (pressure ramp moves the centroid)
  //  2. undecided: smoothed peak-centroid travels; nothing fires yet
  //  3. lock     : travel > 0.7 cell -> axis = dominant one (needs 1.6x the other)
  //                 H -> stone follows finger cell by cell (hysteresis), vertical ignored
  //                 V -> only downward > 2 cells drops, sideways ignored; upward = nothing
  //  4. release  : 70 ms grace for pad gaps; tap = never locked, travel < 0.45 cell, 30..250 ms
  //  5. refractory: 120 ms after any swipe no tap counts
  enum { AX_NONE, AX_H, AX_V, AX_DEAD };
  void handle_gesture() {
    // peak-centred centroid: strongest pad + its 3x3 neighbourhood (ignores palm / second finger)
    int pk = 0, px = 0, py = 0;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < WW; ++x) { // well only: side strip + play button stay out of the gesture
        int p = get_touch_pressure_xy(x, y);
        if (p > pk) {
          pk = p;
          px = x;
          py = y;
        }
      }
    uint32_t now = time_us();
    bool touching = g_down ? (pk > 14) : (pk > 32);
    if (touching) {
      int sp = 0, sx = 0, sy = 0;
      for (int y = maxi(0, py - 1); y <= mini(15, py + 1); ++y)
        for (int x = maxi(0, px - 1); x <= mini(WW - 1, px + 1); ++x) {
          int p = get_touch_pressure_xy(x, y);
          if (p < 10) continue;
          sp += p;
          sx += p * x;
          sy += p * y;
        }
      if (sp <= 0) return;
      int cx = (sx * 256) / sp + 128, cy = (sy * 256) / sp + 128;
      g_up_t = 0;
      if (!g_down) {
        g_down = true;
        g_fx = g_x0_q8 = cx;
        g_fy = g_y0_q8 = cy;
        g_applied = 0;
        g_max_q8 = 0;
        g_axis = AX_NONE;
        g_t0 = now;
        g_ignore = (state != ST_PLAY) || paused;
        if (state == ST_OVER) {
          on_sequence_lock_guard_t guard;
          req_start = true;
        }
        return;
      }
      // light smoothing against pad-to-pad jumps
      g_fx += (cx - g_fx) >> 1;
      g_fy += (cy - g_fy) >> 1;
      if ((uint32_t)(now - g_t0) < 30000u) { // settle: origin follows the finger
        g_x0_q8 = g_fx;
        g_y0_q8 = g_fy;
        return;
      }
      if (g_ignore) return;
      int dx = g_fx - g_x0_q8, dy = g_fy - g_y0_q8;
      int adx = abs(dx), ady = abs(dy);
      g_max_q8 = maxi(g_max_q8, maxi(adx, ady));
      if (g_axis == AX_NONE && maxi(adx, ady) > 180) { // 0.7 cell: decide the axis once
        if (adx * 10 > ady * 16) g_axis = AX_H;
        else if (ady * 10 > adx * 16) g_axis = AX_V;
        else g_axis = AX_DEAD; // diagonal: do nothing for this touch
      }
      if (g_axis == AX_H) {
        int target = g_applied;
        while (dx > (target << 8) + 128 + 64) target++;
        while (dx < (target << 8) - 128 - 64) target--;
        int delta = target - g_applied;
        if (delta) {
          on_sequence_lock_guard_t guard;
          req_dx = (int8_t)clampi(req_dx + delta, -9, 9);
          g_applied = target;
          g_move_t = now;
        }
      } else if (g_axis == AX_V && dy > 512) { // 2 cells down: drop, then this touch is spent
        g_axis = AX_DEAD;
        g_move_t = now;
        on_sequence_lock_guard_t guard;
        req_drop = true;
      }
    } else if (g_down) {
      if (!g_up_t) g_up_t = now | 1;
      if ((uint32_t)(now - g_up_t) < 70000u) return; // grace window: maybe a pad gap
      g_down = false;
      uint32_t dur = g_up_t - g_t0;
      bool refractory = g_move_t && (uint32_t)(now - g_move_t) < 120000u + 70000u;
      if (!g_ignore && g_axis == AX_NONE && !refractory && g_max_q8 < 115 && dur > 30000u && dur < 250000u) {
        on_sequence_lock_guard_t guard;
        req_rot = true;
      }
      g_up_t = 0;
    }
  }

  void draw_game() {
    uint32_t now = time_us();
    int top = stack_top();
    bool danger = top >= 12 && state == ST_PLAY;

    // wall
    uint32_t sb = now - beat_us;
    for (int y = 0; y < 16; ++y) {
      if (danger) {
        uint32_t dd = now - danger_us;
        int pulse = dd < 260000u ? 256 - (int)(dd * 256 / 260000u) : 0;
        set_led(WW, y, lc(C_SIGNAL, 70 + (pulse * 186 >> 8)));
      } else {
        set_led(WW, y, lc(C_WALL, sb < 90000u ? 110 : 72));
      }
    }

    // stack
    for (int y = 0; y < HH; ++y)
      for (int x = 0; x < WW; ++x) {
        uint8_t c = well[y][x];
        if (!c) continue;
        int b = 150, w = 0;
        if (state == ST_OVER) {
          int ph = (int)((now / 3000u + (uint32_t)(x * 40 + y * 30)) & 511);
          int tri = ph < 256 ? ph : 511 - ph;
          b = 100 + (tri * 70 >> 8);
        }
        uint32_t h = now - seqhit_us[y][x];
        if (seqhit_us[y][x] && h < 220000u) {
          b = maxi(b, 256 - (int)(h * 110 / 220000u));
          if (h < 90000u) w = 140;
        }
        uint32_t ag = now - born_us[y][x];
        if (born_us[y][x] && ag < 160000u) {
          b = 256;
          w = 160;
        }
        set_led(x, y, lc(c - 1, b, w));
      }

    // clearing flash
    if (clear_mask) {
      uint32_t k = now - clear_t0;
      int b = 256 - mini(180, (int)(k / 1500u));
      for (int y = 0; y < HH; ++y)
        if (clear_mask & (1u << y))
          for (int x = 0; x < WW; ++x) set_led(x, y, lc(C_WHITE, b, 256));
    }

    // ghost + active stone
    if (state == ST_PLAY && has_cur) {
      int8_t cx[4], cy[4];
      int gy = ghost_y();
      cells_of(cur_t, cur_rot, cur_x, gy, cx, cy);
      for (int i = 0; i < 4; ++i)
        if (cy[i] >= 0 && cy[i] < HH && !well[cy[i]][cx[i]]) set_led(cx[i], cy[i], lc(cur_t, drop_pending ? 130 : 78));
      cells_of(cur_t, cur_rot, cur_x, cur_y, cx, cy);
      for (int i = 0; i < 4; ++i)
        if (cy[i] >= 0 && cy[i] < HH) set_led(cx[i], cy[i], lc(cur_t, 256));
    }

    // side: next stone
    {
      int8_t cx[4], cy[4];
      cells_of(nxt, 0, 0, 0, cx, cy);
      int miny = 9;
      for (int i = 0; i < 4; ++i) miny = mini(miny, cy[i]);
      for (int i = 0; i < 4; ++i) set_led(11 + cx[i], 1 + cy[i] - miny, lc(nxt, 210));
    }
    // beat dots
    for (int i = 0; i < 4; ++i) set_led(11 + i, 4, lc(C_WHITE, i == cur_beat ? 240 : 60));
    // chord position
    for (int i = 0; i < 4; ++i) set_led(11 + i, 6, lc(C_SIGNAL, i == (chord_i & 3) ? 240 : 50));
    // activity meters: hat / low / pad+keys / bright voices
    static const uint8_t MC[4] = {C_WHITE, 1, 0, 2};
    for (int i = 0; i < 4; ++i) {
      int n = (act[i] * 7 + 128) >> 8;
      for (int k = 0; k < 7; ++k)
        if (k < n) set_led(11 + i, 15 - k, lc(MC[i], 230 - k * 18));
    }
    if ((frame_count & 1) == 0)
      for (int i = 0; i < 4; ++i) act[i] = (uint8_t)(act[i] - ((act[i] >> 4) + (act[i] ? 1 : 0)));
    // level progress (bottom up) + level count (top down)
    int prog_n = (lines_lv * 8) / 6;
    for (int k = 0; k < 8; ++k) set_led(15, 14 - k, lc(C_WHITE, k < prog_n ? 200 : 50));
    for (int k = 0; k < mini(level, 7); ++k) set_led(15, k, lc(C_SIGNAL, 220));
  }

  void draw_sound_page(preset_pages_t &preset_pages, int y) {
    preset_pages.edit(sound_sel, y, 0, true);
    preset_pages.xy_pad(sound_sel, 8, y + 10);
    for (int x = 0; x < NUM_VOICES_EDIT; ++x) {
      int c = x < NT ? x : (x == P_BASS ? C_WHITE : C_PERC);
      if (button(x, y + 15, lc(c, x == sound_sel ? 256 : 110), ISOLATED, "Select voice")) {
        sound_sel = (uint8_t)x;
        on_sequence_lock_guard_t guard;
        req_audition = (int8_t)(x + 1);
      }
    }
    static const char *const names[NUM_VOICES_EDIT] = {"I pad", "O bass", "T pluck", "S bell", "Z glass",
                                                       "J keys", "L reed", "sub", "perc"};
    set_help_text("Sound: #fc2#*%s#.  tap bottom row = select + preview (works without clock)", names[sound_sel]);
  }

  bool settings_page(int page) {
    bool changed = false;
    if (page == -1) {
      static const char *const keys[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
      int d = draw_system_style_settings_page("root", keys[root % 12], -1);
      if (d) {
        root = (uint8_t)((root + d + 120) % 12);
        changed = true;
      }
    } else if (page == -2) {
      static const char *const scales[13] = {"classic", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO",
                                             "H.MIN", "P.MAJ", "P.MIN", "HIRAJ", "WHOLE", "CHROM"};
      int d = draw_system_style_enum_settings_page("scale", scl, scales, 13);
      if (d) {
        scl = (uint8_t)clampi(scl + d, 0, 12);
        changed = true;
      }
    } else if (page == -3) {
      static const char *const progs[3] = {"moll", "dur", "modl"};
      int d = draw_system_style_enum_settings_page("prog", prog, progs, 3);
      if (d) {
        prog = (uint8_t)clampi(prog + d, 0, 2);
        changed = true;
      }
    } else if (page == -4) {
      int d = draw_system_style_bool_settings_page("midi", midi_on != 0);
      if (d) {
        midi_on = d > 0 ? 1 : 0;
        changed = true;
      }
    }
    return changed;
  }

  void ui_play() {
    if (!is_scroll_in_progress()) handle_gesture();
    draw_game();
    {
      // play / pause pad, bottom right
      uint32_t now = time_us();
      int ph = (int)((now / 2500u) & 511);
      int tri = ph < 256 ? ph : 511 - ph;
      uint32_t col;
      if (paused) col = lc(C_SIGNAL, 60 + (tri * 196 >> 8));
      else if (state == ST_PLAY) col = lc(C_WHITE, 130);
      else col = lc(C_WHITE, 90 + (tri * 166 >> 8));
      pad_col = col;
    }
    if (paused)
      set_help_text("#fc2#*Pause#.  bottom right = continue, hold = menu   lines %d  lvl %d", (int)lines, (int)level);
    else if (state == ST_OVER)
      set_help_text("Game over  lines #*%d#.  score #*%d#.  tap to restart, hold bottom right = menu", (int)lines, (int)score);
    else
      set_help_text("Swipe #*left/right#. move  swipe #*down#. drop  #*tap#. rotate   lines %d  lvl %d", (int)lines,
                    (int)level);
  }
};

// =====================================================================
// GAME 2 - PRALL: breakout on the clock. Every bounce is a note.
// =====================================================================
struct prall_hit_t {
  uint32_t src;      // voice allocator source id
  int32_t delay_us;  // > 0: waiting to start
  int32_t left_us;   // gate time remaining once started
  uint8_t preset;
  uint8_t note;      // synth note
  uint8_t midi_note;
  uint8_t midi_ch;   // zero-based wire channel
  uint8_t vel;
  uint8_t active;    // 0 free, 1 pending, 2 sounding
  int8_t voice;
};

struct prall_ball_t {
  int8_t x, y, dx, dy, px, py, qx, qy, rx, ry; // cell, sign of vx, row dir, p/q/r = previous cells (tail)
  int16_t xq, vx; // horizontal position and velocity in q8 cells: one row per step, any slope
  int16_t pxq, qxq, rxq; // true horizontal positions at p, q, r (straight tail, smooth head)
  uint8_t alive, held, ch; // ch 1 = main ball, 2 = multi ball
};

struct prall_drop_t {
  int8_t x, y;
  uint8_t type, alive; // type 1..4
};

struct prall_echo_t {
  int8_t x, y;
  uint8_t col, alive;
  uint32_t t0; // tick of the hit
};

struct prall_game {
  enum { N = 16, PR = 14, NUM_HITS = 64, MAX_DROPS = 8, MAX_ECHO = 24 };
  enum { P_LEAD = 0, P_PLUCK = 1, P_PERC = 2, P_BALL2 = 3, P_BASS = 4, P_PAD = 5, P_DRUM = 6, NUM_VOICES_EDIT = 7 };
  enum { ST_IDLE, ST_READY, ST_PLAY, ST_CLEAR, ST_OVER };
  enum { PU_NONE = 0, PU_HALF, PU_DOUBLE, PU_MULTI, PU_WIDE };
  enum { DIV_NORMAL = 0, DIV_HALF, DIV_DOUBLE };
  enum { C_LEAD = 0, C_PLUCK, C_PERC, C_WHITE, C_SIGNAL, C_HALF, C_MULTI, C_WIDE, C_PADDLE, C_BALL, NUM_COLS };
  enum { NUM_SCALES = 13 };

  // ---- game state ----
  uint8_t brick[N][N]; // 0 empty; bits 0-1 voice + 1, bits 2-4 power-up type
  prall_ball_t balls[2];
  prall_drop_t drops[MAX_DROPS];
  prall_echo_t echoes[MAX_ECHO];
  uint8_t state;
  bool paused;
  uint8_t level, lives;
  uint32_t score;
  int8_t paddle_l, paddle_w;
  uint8_t div_mode, div_bars, wide_bars;
  uint32_t tick;
  uint32_t last_tick_us;
  uint32_t flash_tick, move_tick, msg_until;
  int8_t move_dir;
  bool launch_pending;

  // ---- soundtrack state ----
  uint32_t bar0;        // bar index at level start (pattern anchor)
  uint8_t chord_i;      // progression step 0..7, moved by row clears
  uint8_t key_mod;      // semitones, moves with level clears / double rows
  uint16_t row_had;     // rows that still count for a row clear
  uint8_t rows_now;     // rows cleared during the current ball step
  uint32_t last_chord_tick, chord_flash_tick;
  int8_t sweep_y[4], sweep_x[4];
  uint32_t sweep_t[4];
  uint8_t sweep_alive[4];
  uint8_t combo;        // bricks since last paddle contact
  uint8_t arp_i, multi_i;
  uint16_t bricks_total, bricks_now;

  // ---- sub-pixel motion ----
  uint32_t bstep_us, dstep_us; // time of last ball / drop step
  int bstep_per_us;            // duration of that ball step
  int pv_q8, pw_q8;            // paddle left / width, eased (q8 cells)

  // ---- timing / voices ----
  clock_divider_t clk;
  voice_allocator_t voice_allocator;
  prall_hit_t hits[NUM_HITS];
  uint32_t hit_counter;

  // ---- requests from on_ui ----
  int8_t req_dx;
  bool req_launch, req_play;
  int8_t req_audition; // preset + 1

  // ---- render scratch (member, not stack) ----
  uint16_t fb[N * N][3];
  uint8_t tmax[N * N]; // per-cell tail brightness scratch
  uint32_t dither_frame;
  uint8_t nodither[N * N]; // ball pixels: crisp, never brightness-dithered
  uint16_t dacc[2];         // per-ball error accumulator for horizontal position dithering
  char msgbuf[16];

  // ---- gesture ----
  bool g_down, g_launched;
  int g_x0_q8, g_y0_q8, g_applied, g_max_q8;
  uint32_t g_t0, g_last_us;

  // ---- settings ----
  uint8_t root = 0;    // C
  uint8_t scale_i = 8; // pentatonic minor
  uint8_t midi_on = 1;

  uint8_t sound_sel;
  uint32_t pad_col; // colour of the play / pause pad, drawn by the panel

  const char *get_track_name(int i, char buf[16]) {
    static const char *const names[NUM_VOICES_EDIT] = {"Lead", "Pluck", "Perc", "Ball 2", "Bass", "Pad", "Drums"};
    if (!buf) return NULL;
    snprintf(buf, 16, "%s", names[clampi(i, 0, NUM_VOICES_EDIT - 1)]);
    return buf;
  }

  // =====================================================================
  // colours (direct LED_RGB triplets, 5-bit full scale = 31)
  // =====================================================================
  static void col_rgb(int c, int *r, int *g, int *b) {
    static const uint8_t COL[NUM_COLS][3] = {
        {0, 24, 31},  // lead cyan
        {31, 18, 0},  // pluck amber
        {31, 3, 16},  // perc magenta
        {24, 24, 22}, // white
        {31, 6, 2},   // signal red (also DOUBLE)
        {6, 10, 31},  // HALF blue
        {4, 31, 8},   // MULTI green
        {31, 28, 14}, // WIDE warm white
        {20, 18, 15}, // paddle
        {31, 31, 30}, // ball
    };
    c = clampi(c, 0, NUM_COLS - 1);
    *r = COL[c][0];
    *g = COL[c][1];
    *b = COL[c][2];
  }
  static int pu_col(int t) {
    static const uint8_t PC[5] = {C_WHITE, C_HALF, C_SIGNAL, C_MULTI, C_WIDE};
    return PC[clampi(t, 0, 4)];
  }

  // additive framebuffer: linear brightness 0..256, gamma (square) applied per contribution
  void fb_add(int x, int y, int c, int b) {
    if (x < 0 || x >= N || y < 0 || y >= N || b <= 0) return;
    b = mini(b, 256);
    int r, g, bl;
    col_rgb(c, &r, &g, &bl);
    int gm = (b * b) >> 8;
    uint16_t *p = fb[y * N + x];
    p[0] = (uint16_t)mini(65535, p[0] + r * gm);
    p[1] = (uint16_t)mini(65535, p[1] + g * gm);
    p[2] = (uint16_t)mini(65535, p[2] + bl * gm);
  }
  // linear-light add (no gamma): energy 0..256 per full-on LED; used for sub-pixel splats
  void fb_add_lin(int x, int y, int c, int e) {
    if (x < 0 || x >= N || y < 0 || y >= N || e <= 0) return;
    int r, g, bl;
    col_rgb(c, &r, &g, &bl);
    uint16_t *p = fb[y * N + x];
    p[0] = (uint16_t)mini(65535, p[0] + r * e);
    p[1] = (uint16_t)mini(65535, p[1] + g * e);
    p[2] = (uint16_t)mini(65535, p[2] + bl * e);
  }
  static int floor8(int v) { return (v + 8192) / 256 - 32; } // floor(v / 256) for v >= -8192
  // bilinear splat of a point at cell coordinates (q8), energy-conserving
  void splat(int xq, int yq, int c, int e) {
    int ix = floor8(xq), iy = floor8(yq);
    int fx = xq - ix * 256, fy = yq - iy * 256;
    fb_add_lin(ix, iy, c, (e * (256 - fx) >> 8) * (256 - fy) >> 8);
    fb_add_lin(ix + 1, iy, c, (e * fx >> 8) * (256 - fy) >> 8);
    fb_add_lin(ix, iy + 1, c, (e * (256 - fx) >> 8) * fy >> 8);
    fb_add_lin(ix + 1, iy + 1, c, (e * fx >> 8) * fy >> 8);
  }
  // splat that leaves one cell (the ball head) untouched, so the head stays one round LED
  void splat_ex(int xq, int yq, int c, int e, int hx, int hy) {
    int ix = floor8(xq), iy = floor8(yq);
    int fx = xq - ix * 256, fy = yq - iy * 256;
    int w[4] = {(e * (256 - fx) >> 8) * (256 - fy) >> 8, (e * fx >> 8) * (256 - fy) >> 8,
                (e * (256 - fx) >> 8) * fy >> 8, (e * fx >> 8) * fy >> 8};
    for (int k = 0; k < 4; ++k) {
      int cx = ix + (k & 1), cy = iy + (k >> 1);
      if (cx == hx && cy == hy) continue;
      fb_add_lin(cx, cy, c, w[k]);
    }
  }
  // anti-aliased horizontal bar [l, l + w) in q8 cells on row y
  void span(int lq, int wq, int y, int c, int e) {
    int rq = lq + wq;
    for (int i = floor8(lq); i <= floor8(rq - 1); ++i) {
      int a = maxi(lq, i * 256), b = mini(rq, i * 256 + 256);
      if (b > a) fb_add_lin(i, y, c, e * (b - a) >> 8);
    }
  }

  void fb_dim(int q8) {
    for (int i = 0; i < N * N; ++i)
      for (int k = 0; k < 3; ++k) fb[i][k] = (uint16_t)((fb[i][k] * q8) >> 8);
  }
  static int fb_chan(int v) {
    int o = (v + 128) >> 8;
    if (o == 0 && v >= 64) o = 1;
    return mini(31, o);
  }
  // temporal dithering: on_ui runs ~250 fps, so a value between two 5-bit levels alternates between them
  // and the eye sees the in-between brightness. Below level 1 no dithering (would flicker).
  static int fb_chan_d(int v, int thr) {
    if (v < 256) return v >= 64 ? 1 : 0;
    int o = v >> 8;
    if ((v & 255) > thr) o++;
    return mini(31, o);
  }
  void fb_flush() {
    dither_frame++;
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
        uint16_t *p = fb[y * N + x];
        if (x == 15 && y == 15) continue; // play pad is drawn by its button
        int i = y * N + x;
        int thr = nodither[i] ? 128 : (int)((dither_frame * 97u + (uint32_t)i * 157u + (uint32_t)(x ^ y) * 61u) & 255u);
        set_led(x, y, LED_RGB(fb_chan_d(p[0], thr), fb_chan_d(p[1], thr), fb_chan_d(p[2], thr)));
      }
  }
  static uint32_t lc(int c, int b) {
    if (b <= 0) return 0;
    b = mini(b, 256);
    int r, g, bl;
    col_rgb(c, &r, &g, &bl);
    int gm = (b * b) >> 8;
    return LED_RGB(fb_chan(r * gm), fb_chan(g * gm), fb_chan(bl * gm));
  }

  // =====================================================================
  // harmony (own root + scale, KLOTZ-style settings)
  // =====================================================================
  static const int8_t *scale_row(int s) {
    static const int8_t SC[NUM_SCALES][8] = {
        {0, 2, 4, 5, 7, 9, 11, -1},  // MAJ
        {0, 2, 3, 5, 7, 8, 10, -1},  // MIN
        {0, 2, 3, 5, 7, 9, 10, -1},  // DOR
        {0, 1, 3, 5, 7, 8, 10, -1},  // PHRY
        {0, 2, 4, 6, 7, 9, 11, -1},  // LYD
        {0, 2, 4, 5, 7, 9, 10, -1},  // MIXO
        {0, 2, 3, 5, 7, 8, 11, -1},  // HMIN
        {0, 2, 4, 7, 9, -1, -1, -1}, // PMAJ
        {0, 3, 5, 7, 10, -1, -1, -1},// PMIN
        {0, 2, 3, 7, 8, -1, -1, -1}, // HIRA
        {0, 3, 5, 6, 7, 10, -1, -1}, // BLUE
        {0, 2, 4, 6, 8, 10, -1, -1}, // WHOL
        {0, 1, 2, 3, 4, 5, 6, 7},    // CHRO (first 8 steps)
    };
    return SC[clampi(s, 0, NUM_SCALES - 1)];
  }
  int scale_len() const {
    if (scale_i >= NUM_SCALES - 1) return 12;
    const int8_t *r = scale_row(scale_i);
    int len = 0;
    while (len < 8 && r[len] >= 0) len++;
    return maxi(1, len);
  }
  int note_for(int deg, int preset) const {
    int len = scale_len();
    if (deg < 0) deg = 0;
    int step = deg % len, oct = deg / len;
    int iv = (scale_i >= NUM_SCALES - 1) ? step : scale_row(scale_i)[step];
    int base = 60 + ((root + key_mod) % 12);
    if (preset == P_PLUCK || preset == P_BALL2) base += 12;
    if (preset == P_PERC || preset == P_PAD) base -= 12;
    if (preset == P_BASS) base -= 24;
    return clampn(base + iv + 12 * oct);
  }
  // per-level 4-chord progression, in scale degrees
  // per-level 8-chord progression in scale degrees, each ends on the dominant
  int chord_root_deg() const {
    static const int8_t PROG[4][8] = {
        {0, 5, 3, 4, 0, 5, 1, 4}, // i  VI  iv  v  | i  VI  ii  v
        {0, 3, 5, 4, 0, 3, 1, 4},
        {0, 5, 2, 6, 0, 5, 3, 4},
        {0, 4, 5, 3, 0, 4, 1, 4},
    };
    return PROG[(level + 3) & 3][chord_i & 7];
  }
  // chord tone i: 0 root, 1 third, 2 fifth, 3 root + octave ...
  int chord_tone(int i, int preset) const {
    if (i < 0) i = 0;
    return note_for(chord_root_deg() + 2 * (i % 3) + scale_len() * (i / 3), preset);
  }
  // stacked thirds above the chord root: 3 = 7th, 4 = 9th, 5 = 11th, 6 = 13th
  int ext_tone(int i, int preset) const { return note_for(chord_root_deg() + 2 * maxi(0, i), preset); }
  int gate_16th_ms() const { return maxi(30, tick_us() * 2 / 1000); }

  // a harmonic event: next chord, strummed pad + bass, fields flash
  void advance_chord(int steps, bool strum) {
    chord_i = (uint8_t)((chord_i + steps) & 7);
    last_chord_tick = tick;
    chord_flash_tick = tick;
    if (!strum) return;
    int t = tick_us();
    for (int i = 0; i < 3; ++i) voice_note(P_PAD, chord_tone(i, P_PAD), 72, i * t, (int)(qn_period_us * 3 / 1000));
    voice_note(P_BASS, chord_tone(0, P_BASS), 96, 0, (int)(qn_period_us * 2 / 1000));
  }
  static int clampn(int n) {
    while (n > 96) n -= 12;
    while (n < 28) n += 12;
    return n;
  }

  // =====================================================================
  // note pool: hard-gated one-shots, synth + level-triggered MIDI
  // =====================================================================
  void fire(int preset, int note, int vel, int midi_ch, int midi_note, int delay_us, int gate_ms = 0) {
    static const uint16_t GATE_MS[NUM_VOICES_EDIT] = {260, 160, 120, 300, 300, 1500, 60};
    int slot = -1;
    for (int i = 0; i < NUM_HITS; ++i)
      if (!hits[i].active) {
        slot = i;
        break;
      }
    if (slot < 0) return; // pool full: drop
    prall_hit_t *h = &hits[slot];
    h->src = 0x50000000u | (++hit_counter & 0x00ffffffu);
    h->delay_us = maxi(0, delay_us);
    h->left_us = (gate_ms > 0 ? gate_ms : GATE_MS[clampi(preset, 0, NUM_VOICES_EDIT - 1)]) * 1000;
    h->preset = (uint8_t)preset;
    h->note = (uint8_t)clampi(note, 0, 127);
    h->midi_note = (uint8_t)clampi(midi_note, 0, 127);
    h->midi_ch = (uint8_t)(midi_ch >= 0 ? midi_ch : get_midi_channel_for_preset_idx(preset, true));
    h->vel = (uint8_t)clampi(vel, 1, 127);
    h->voice = -1;
    h->active = 1;
  }

  void start_hit(prall_hit_t *h) {
    h->active = 2;
    int v = voice_allocator.voice_allocate(h->src, 8, 0, DEFAULT_VOICE_ALLOCATOR_VOICES, h->preset);
    if (v >= 0) {
      for (int i = 0; i < NUM_HITS; ++i)
        if (&hits[i] != h && hits[i].active == 2 && hits[i].voice == v) hits[i].voice = -1; // stolen
      h->voice = (int8_t)v;
      play_synth(v, h->preset, h->vel, (int)h->note << 8, true);
    }
  }

  void release_hit(prall_hit_t *h) {
    if (h->voice >= 0 && voice_allocator.find_voice(h->src, 0, DEFAULT_VOICE_ALLOCATOR_VOICES) == h->voice) {
      synth_note_up(h->voice);
      voice_allocator.voice_deallocate(h->src, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
    }
    h->active = 0;
    h->voice = -1;
  }

  void update_hits(int dt) {
    for (int i = 0; i < NUM_HITS; ++i) {
      prall_hit_t *h = &hits[i];
      if (!h->active) continue;
      if (h->active == 1) {
        h->delay_us -= dt;
        if (h->delay_us > 0) continue;
        start_hit(h);
      }
      h->left_us -= dt;
      if (h->left_us <= 0) {
        release_hit(h);
        continue;
      }
      if (midi_on) declare_midi_note(h->midi_ch, h->midi_note, h->vel, MIDI_PORT_1);
    }
    send_declared_midi_notes();
  }

  void release_all_hits() {
    for (int i = 0; i < NUM_HITS; ++i)
      if (hits[i].active) release_hit(&hits[i]);
  }

  // musical shortcuts
  int tick_us() const { return maxi(1, (int)qn_period_us / 8); } // base tick = 1/32
  void voice_note(int preset, int note, int vel, int d = 0, int gate_ms = 0) { fire(preset, note, vel, -1, note, d, gate_ms); }
  void kick(int vel, int d = 0) { fire(P_DRUM, 36, vel, 9, 36, d); }
  void hat(int vel, int d = 0) { fire(P_DRUM, 84, vel, 9, 42, d, 30); }
  void snare(int vel, int d = 0) { fire(P_DRUM, 60, vel, 9, 38, d, 90); }

  // =====================================================================
  // level / game flow
  // =====================================================================
  void make_level(int n) {
    memset(brick, 0, sizeof(brick));
    int cnt = 0;
    for (int y = 1; y <= 7; ++y)
      for (int x = 0; x < N; ++x) {
        bool on = false;
        switch ((n - 1) % 5) {
        case 0: on = y >= 2 && y <= 5; break;
        case 1: on = ((x + y) & 1) == 0 && y <= 6; break;
        case 2: {
          int ax = (x * 2 - 15); // distance to centre in half cells
          if (ax < 0) ax = -ax;
          int ay = y - 4;
          if (ay < 0) ay = -ay;
          on = ax * 10 + ay * 32 <= 134; // diamond
        } break;
        case 3: on = ((x >> 1) & 1) == 0; break;
        default: on = randi(0, 99) < mini(75, 45 + 3 * n);
        }
        if (on) {
          int v = (y + n + (n > 2 ? (x >> 2) : 0)) % 3;
          brick[y][x] = (uint8_t)(v + 1);
          cnt++;
        }
      }
    bricks_total = bricks_now = (uint16_t)cnt;
    row_had = 0;
    for (int y = 0; y < PR; ++y)
      for (int x = 0; x < N; ++x)
        if (brick[y][x]) row_had |= (uint16_t)(1u << y);
    // power-up bricks
    int k = mini(cnt, 3 + mini(2, n / 3));
    for (int j = 0; j < k; ++j) {
      for (int tries = 0; tries < 64; ++tries) {
        int x = randi(0, N - 1), y = randi(1, 7);
        if (!brick[y][x] || (brick[y][x] >> 2)) continue;
        int t = 1 + ((j + n + 2) & 3); // cycles MULTI, WIDE, HALF, DOUBLE ...
        brick[y][x] = (uint8_t)(brick[y][x] | (t << 2));
        break;
      }
    }
  }

  void held_ball(int i, int ch) {
    prall_ball_t *b = &balls[i];
    b->x = (int8_t)paddle_center();
    b->y = PR - 1;
    b->dx = 1;
    b->dy = -1;
    b->xq = b->pxq = b->qxq = b->rxq = (int16_t)(b->x * 256);
    b->vx = 128;
    b->px = b->qx = b->rx = b->x;
    b->py = b->qy = b->ry = b->y;
    b->alive = 1;
    b->held = 1;
    b->ch = (uint8_t)ch;
  }
  int paddle_center() const { return paddle_l + (paddle_w - 1) / 2; }
  bool covers(int x) const { return x >= paddle_l && x < paddle_l + paddle_w; }
  bool brick_at(int x, int y) const { return x >= 0 && x < N && y >= 0 && y < PR && brick[y][x]; }
  int balls_alive() const { return (balls[0].alive ? 1 : 0) + (balls[1].alive ? 1 : 0); }
  bool bricks_left() const {
    for (int y = 0; y < PR; ++y)
      for (int x = 0; x < N; ++x)
        if (brick[y][x]) return true;
    return false;
  }

  void set_width(int w) {
    int c2 = paddle_l * 2 + paddle_w; // centre in half cells
    paddle_w = (int8_t)w;
    paddle_l = (int8_t)clampi((c2 - w + 1) / 2, 0, N - w);
  }
  void move_paddle(int d) {
    int l = clampi(paddle_l + d, 0, N - paddle_w);
    if (l == paddle_l) return;
    move_dir = (int8_t)(l > paddle_l ? 1 : -1);
    move_tick = tick;
    paddle_l = (int8_t)l;
  }

  void load_level() {
    make_level(level);
    memset(drops, 0, sizeof(drops));
    memset(echoes, 0, sizeof(echoes));
    memset(balls, 0, sizeof(balls));
    div_mode = DIV_NORMAL;
    div_bars = 0;
    wide_bars = 0;
    paddle_w = 4;
    paddle_l = 6;
    held_ball(0, 1);
    launch_pending = false;
    bar0 = (tick >> 5) + ((tick & 31) ? 1 : 0); // progression starts on the next bar line
    chord_i = 0;
    combo = 0;
    rows_now = 0;
    last_chord_tick = tick;
    memset(sweep_alive, 0, sizeof(sweep_alive));
  }

  void reset_game() {
    level = 1;
    lives = 3;
    score = 0;
    key_mod = 0;
    load_level();
    state = ST_READY;
    paused = false;
  }

  void launch() {
    int dir = (tick - move_tick < 6 && move_dir) ? move_dir : (randi(0, 1) ? 1 : -1);
    for (int i = 0; i < 2; ++i) {
      prall_ball_t *b = &balls[i];
      if (!b->alive || !b->held) continue;
      b->held = 0;
      b->x = b->px = b->qx = b->rx = (int8_t)paddle_center();
      b->y = b->py = b->qy = b->ry = PR - 1;
      b->dx = (int8_t)dir;
      b->dy = -1;
      b->xq = b->pxq = b->qxq = b->rxq = (int16_t)(b->x * 256);
      b->vx = (int16_t)(dir * (96 + randi(0, 64))); // launch slope varies a little
    }
    state = ST_PLAY;
    launch_pending = false;
    combo = 0;
    bstep_us = time_us();
    bstep_per_us = 2 * tick_us();
    // launch: rising chord pick-up
    int t = tick_us();
    for (int i = 0; i < 3; ++i) voice_note(P_PLUCK, chord_tone(i, P_PLUCK), 60 + i * 12, i * t, 90);
  }

  void add_echo(int x, int y, int col) {
    int slot = 0;
    uint32_t oldest = 0;
    for (int i = 0; i < MAX_ECHO; ++i) {
      if (!echoes[i].alive) {
        slot = i;
        break;
      }
      uint32_t age = tick - echoes[i].t0;
      if (age >= oldest) {
        oldest = age;
        slot = i;
      }
    }
    echoes[slot].x = (int8_t)x;
    echoes[slot].y = (int8_t)y;
    echoes[slot].col = (uint8_t)col;
    echoes[slot].t0 = tick;
    echoes[slot].alive = 1;
  }

  void hit_brick(int x, int y, const prall_ball_t *b) {
    uint8_t br = brick[y][x];
    brick[y][x] = 0;
    score += 10u * level;
    if (bricks_now) bricks_now--;
    int v = (br & 3) - 1;
    int pu = br >> 2;
    int preset = (b->ch == 2) ? P_BALL2 : v;
    int h = 7 - y; // 0 low .. 6 high
    int n;
    // row = height in the harmony, column = which chord tone: neighbours in a row sound different
    static const int8_t CO[16] = {0, 2, 1, 3, 2, 0, 3, 1, 1, 3, 0, 2, 3, 1, 2, 0};
    int hc = h + CO[x & 15];
    if (preset == P_LEAD || preset == P_BALL2)
      n = chord_tone(hc, preset); // chord tones, higher rows climb the chord
    else if (preset == P_PLUCK)
      n = ext_tone(3 + (hc & 3), preset); // colour tones: 7th, 9th, 11th, 13th
    else
      n = chord_tone(hc % 3, preset); // perc: root / third / fifth
    int t = tick_us();
    voice_note(preset, n, 96);
    if (combo < 250) combo++;
    if (combo >= 2) { // combo climbs up the chord, one 1/32 later
      voice_note(P_PLUCK, chord_tone(combo + 1, P_PLUCK), mini(110, 50 + combo * 8), t, 90);
      score += 5u * combo;
    }
    if (combo == 6) { // fanfare
      for (int i = 0; i < 4; ++i) voice_note(P_LEAD, chord_tone(i + 3, P_LEAD), 100, 2 * t + i * t, 260);
      chord_flash_tick = tick;
    }
    // row cleared? -> harmonic event after the step
    if ((row_had >> y) & 1) {
      bool empty = true;
      for (int xx = 0; xx < N; ++xx)
        if (brick[y][xx]) empty = false;
      if (empty) {
        row_had &= (uint16_t)~(1u << y);
        rows_now++;
        for (int k = 0; k < 4; ++k)
          if (!sweep_alive[k]) {
            sweep_alive[k] = 1;
            sweep_y[k] = (int8_t)y;
            sweep_x[k] = (int8_t)x;
            sweep_t[k] = tick;
            break;
          }
      }
    }
    voice_note(preset, n, 46, 6 * t, 110);  // dotted-1/8 echo
    voice_note(preset, n, 24, 12 * t, 90);  // second repeat
    add_echo(x, y, b->ch == 2 ? C_MULTI : v);
    if (pu) {
      for (int i = 0; i < MAX_DROPS; ++i)
        if (!drops[i].alive) {
          drops[i].x = (int8_t)x;
          drops[i].y = (int8_t)y;
          drops[i].type = (uint8_t)pu;
          drops[i].alive = 1;
          break;
        }
    }
  }

  static int cell_of(int q) { return (q + 128) >> 8; } // q >= 0
  // reflect a horizontal position at the side walls; returns true on a wall hit
  static bool mirror_x(int *q, int *vx) {
    const int hi = (N - 1) * 256;
    if (*q < 0) {
      *q = -*q;
      *vx = -*vx;
      return true;
    }
    if (*q > hi) {
      *q = 2 * hi - *q;
      *vx = -*vx;
      return true;
    }
    return false;
  }

  // one ball step: exactly one row per step (keeps the clock feel), but the horizontal velocity is
  // fractional, so the ball flies at many slopes from steep to 45 degrees and reaches every column.
  void step_ball(prall_ball_t *b) {
    b->rx = b->qx;
    b->ry = b->qy;
    b->qx = b->px;
    b->qy = b->py;
    b->px = b->x;
    b->py = b->y;
    b->rxq = b->qxq;
    b->qxq = b->pxq;
    b->pxq = b->xq;
    int y = b->y, dy = b->dy, vx = b->vx, xq = b->xq;
    int cx = cell_of(xq);
    int nxq = xq + vx;
    if (mirror_x(&nxq, &vx)) { // wall: hat + metallic chord tone by height
      hat(50);
      voice_note(P_PERC, chord_tone(clampi((PR - y) / 3, 0, 4), P_PERC) + 12, 46, 0, 140);
    }
    if (y + dy < 0) { // ceiling: bright colour-tone stab
      dy = -dy;
      hat(64);
      int t = tick_us();
      for (int i = 0; i < 3; ++i) voice_note(P_PLUCK, ext_tone(3 + i, P_PLUCK), 52 - i * 6, i * t / 2, 110);
    }
    int dcx = cell_of(nxq) - cx;
    if (brick_at(cx, y + dy)) {
      hit_brick(cx, y + dy, b);
      dy = -dy;
    } else if (dcx && brick_at(cx + dcx, y)) {
      hit_brick(cx + dcx, y, b);
      vx = -vx;
      nxq = xq + vx;
      mirror_x(&nxq, &vx);
    } else if (dcx && brick_at(cx + dcx, y + dy)) {
      hit_brick(cx + dcx, y + dy, b);
      vx = -vx;
      dy = -dy;
      nxq = xq + vx;
      mirror_x(&nxq, &vx);
    }
    if (y + dy < 0) dy = -dy;
    int ny = y + dy, ncx = cell_of(nxq);
    if (brick_at(ncx, ny)) { // blocked: stay this step
      nxq = xq;
      ncx = cx;
      ny = y;
    }
    if (dy > 0 && ny == PR) {
      int c = covers(ncx) ? ncx : (covers(cx) ? cx : -1);
      if (c >= 0) {
        int seg = c - paddle_l;
        // landing spot sets the slope: edges 45 degrees, centre steep
        int w1 = maxi(1, paddle_w - 1);
        vx = (2 * seg - w1) * 256 / w1;
        // within the cell: where exactly the ball sits adds a little
        vx += (xq - c * 256) / 2;
        // english: a moving paddle pushes the ball
        if (tick - move_tick < 6 && move_dir) vx += move_dir * 72;
        vx += randi(-10, 10); // breaks repeating loops
        vx = clampi(vx, -256, 256);
        if (vx > -20 && vx < 20) vx = (vx < 0 ? -20 : 20);
        dy = -1;
        nxq = xq + vx; // leave the paddle on this step: contact cell = kick time
        mirror_x(&nxq, &vx);
        ny = y - 1;
        ncx = cell_of(nxq);
        if (brick_at(ncx, ny)) {
          nxq = xq;
          ncx = cx;
          ny = y;
        }
        kick(104);
        // the paddle plays the bass: landing spot picks root, third, fifth or octave
        int k = (seg * 3 + w1 / 2) / w1;
        voice_note(P_BASS, chord_tone(clampi(k, 0, 3), P_BASS), 100, 0, gate_16th_ms() * 2);
        flash_tick = tick;
        combo = 0;
      }
    }
    b->xq = (int16_t)nxq;
    b->vx = (int16_t)vx;
    b->x = (int8_t)ncx;
    b->y = (int8_t)ny;
    b->dx = (int8_t)(vx > 0 ? 1 : (vx < 0 ? -1 : 0));
    b->dy = (int8_t)dy;
    if (ny > PR) b->alive = 0;
  }

  void activate(int type) {
    score += 50;
    if (type == PU_HALF) {
      div_mode = DIV_HALF;
      div_bars = 4;
    } else if (type == PU_DOUBLE) {
      div_mode = DIV_DOUBLE;
      div_bars = 4;
    } else if (type == PU_WIDE) {
      wide_bars = 4;
      set_width(6);
    } else if (type == PU_MULTI && !balls[1].alive) {
      prall_ball_t *b = &balls[1];
      b->x = (int8_t)paddle_center();
      b->y = PR - 1;
      b->vx = (int16_t)(balls[0].alive ? -balls[0].vx : 128);
      if (b->vx == 0) b->vx = 96;
      b->dx = (int8_t)(b->vx > 0 ? 1 : -1);
      b->dy = -1;
      b->xq = b->pxq = b->qxq = b->rxq = (int16_t)(b->x * 256);
      b->px = b->qx = b->rx = b->x;
      b->py = b->qy = b->ry = b->y;
      b->alive = 1;
      b->held = 0;
      b->ch = 2;
    }
    static const int8_t ARP[4] = {0, 2, 4, 7};
    int t = tick_us();
    for (int j = 0; j < 4; ++j) voice_note(P_PLUCK, ext_tone(ARP[j], P_PLUCK), 84, j * t, 90);
  }

  void step_drops() {
    for (int i = 0; i < MAX_DROPS; ++i) {
      prall_drop_t *d = &drops[i];
      if (!d->alive) continue;
      d->y++;
      if (d->y == PR && covers(d->x)) {
        activate(d->type);
        d->alive = 0;
      } else if (d->y > PR) {
        d->alive = 0;
      }
    }
  }

  void lose_life() {
    lives = (uint8_t)(lives > 0 ? lives - 1 : 0);
    int t = tick_us();
    static const int8_t DESC[4] = {4, 3, 1, 0};
    memset(drops, 0, sizeof(drops));
    if (lives == 0) {
      state = ST_OVER;
      for (int k = 0; k < 4; ++k) voice_note(P_LEAD, chord_tone(DESC[k], P_LEAD) - 12, 84, k * 4 * t, 500);
      return;
    }
    for (int k = 0; k < 3; ++k) voice_note(P_LEAD, chord_tone(DESC[k + 1], P_LEAD) - 12, 70, k * 2 * t, 220);
    chord_i = 0; // back home
    last_chord_tick = tick;
    // the lost ball leaves a red mark where it left the field
    for (int i = 0; i < 2; ++i)
      if (balls[i].ch == 1 && balls[i].px >= 0) add_echo(balls[i].px, PR, C_SIGNAL);
    memset(balls, 0, sizeof(balls));
    held_ball(0, 1);
    div_mode = DIV_NORMAL;
    div_bars = 0;
    if (wide_bars) {
      wide_bars = 0;
      set_width(4);
    }
    state = ST_READY;
  }

  // row clear: the KLOTZ moment. chord moves, strum + arpeggio sweep + snare
  void row_clear(int n) {
    score += 150u * n * level;
    if (n >= 2) key_mod = (uint8_t)((key_mod + 5) % 12); // double row: up a fourth
    advance_chord(n >= 2 ? 2 : 1, true);
    int t = tick_us();
    for (int j = 0; j < 8; ++j) voice_note(P_LEAD, chord_tone(j, P_LEAD), 64 + j * 6, t + j * t, 150);
    snare(110, 2 * t);
    if (n >= 2) kick(110);
  }

  void level_clear() {
    score += 100u * level;
    int t = tick_us();
    // dominant of the old key ...
    int vdeg = 4;
    for (int i = 0; i < 3; ++i) voice_note(P_PAD, note_for(vdeg + 2 * i, P_PAD), 84, i * t, (int)(qn_period_us * 2 / 1000));
    voice_note(P_BASS, note_for(vdeg, P_BASS), 104, 0, (int)(qn_period_us * 2 / 1000));
    // ... becomes the tonic of the new key: up a fifth
    key_mod = (uint8_t)((key_mod + 7) % 12);
    chord_i = 0;
    for (int j = 0; j < 8; ++j) voice_note(P_LEAD, chord_tone(j, P_LEAD), 90, 16 * t + j * 2 * t, 220);
    kick(110, 16 * t);
    if (level < 99) level++;
    load_level();
    state = ST_CLEAR;
    msg_until = tick + 64; // two bars of "LEVEL n"
  }

  // dynamic soundtrack: progression, level layers, 4-bar variation, tension, power-ups
  void backing(int bt) {
    int rel = (int)((tick >> 5) - bar0); // bars since level start
    if (rel < 0) rel = 0;
    // harmony drifts on if no row was cleared for 8 bars
    if (state == ST_PLAY && bt == 0 && tick - last_chord_tick >= 256) advance_chord(1, true);
    int block = rel >> 2;                // 4-bar blocks
    bool last_bar = (rel & 3) == 3;
    int s16 = bt >> 1;
    bool play = state == ST_PLAY;
    bool sparse = play && lives == 1; // last life: pulse + bass only
    int left_pct = bricks_total ? (int)bricks_now * 100 / bricks_total : 100;
    int pat = (block + level) & 3;

    if (state == ST_READY && (bt & 7) == 0) hat(30); // count-in pulse while waiting
    if (bt & 1) { // odd 1/32: DOUBLE hat roll on beat 4
      if (play && !sparse && div_mode == DIV_DOUBLE && s16 >= 12) hat(34);
      return;
    }
    // bass follows the chord
    if (level >= 2) {
      int r = chord_tone(0, P_BASS);
      if (s16 == 0) voice_note(P_BASS, r, 84, 0, (int)(qn_period_us * 3 / 1000));
      if (level >= 4 && s16 == 8) voice_note(P_BASS, chord_tone(2, P_BASS), 72, 0, gate_16th_ms() * 3);
      if (!sparse && (block & 1) && s16 == 14) voice_note(P_BASS, r + 12, 70, 0, 120); // pickup
      if (!sparse && level >= 6 && s16 == 6) voice_note(P_BASS, chord_tone(2, P_BASS), 64, 0, 120);
    }
    if (sparse) {
      if ((s16 & 3) == 0) kick(70);
      return;
    }
    // hats: pattern rotates every 4 bars; HALF / DOUBLE override
    if (level >= 3 || div_mode == DIV_DOUBLE) {
      static const uint16_t HATS[4] = {0x4444, 0x5555, 0x4C4C, 0xEEEE};
      uint16_t m = div_mode == DIV_DOUBLE ? 0xFFFF : (div_mode == DIV_HALF ? 0x5555 : HATS[pat]);
      if ((m >> s16) & 1) hat(div_mode == DIV_HALF ? 34 : ((s16 & 1) ? 40 : 56));
    }
    // kick
    if (level >= 5 || div_mode == DIV_HALF) {
      static const uint16_t KICKS[4] = {0x1111, 0x0911, 0x1191, 0x2111};
      uint16_t m = div_mode == DIV_HALF ? 0x0001 : KICKS[pat];
      if ((m >> s16) & 1) kick(div_mode == DIV_HALF ? 84 : 58);
    }
    // snare: half-time backbeat, or tension backbeat below 25% bricks
    if (div_mode == DIV_HALF) {
      if (s16 == 8) snare(86);
    } else if (play && left_pct <= 25 && (s16 == 4 || s16 == 12)) {
      snare(80);
    }
    // fill before every pattern change
    if (play && level >= 3 && last_bar && s16 >= 12) snare(44 + (s16 - 12) * 16);
    // pad: one chord per change
    if (level >= 4 && bt == 0 && (rel & 1) == 0) {
      int gate = (int)(qn_period_us * 7 / 1000); // just under two bars
      for (int i = 0; i < 3; ++i) voice_note(P_PAD, chord_tone(i, P_PAD), 58, 0, gate);
    }
    // tension arpeggio below 50% bricks, 1/8 up-down
    if (play && left_pct <= 50 && (s16 & 1) == 0) {
      static const int8_t UD[6] = {0, 1, 2, 3, 2, 1};
      voice_note(P_PLUCK, chord_tone(UD[arp_i % 6] + 3, P_PLUCK) - 12, left_pct <= 25 ? 54 : 40, 0, 90);
      arp_i++;
    }
    // MULTI: counter-arpeggio on the dotted 1/8 while the second ball lives
    if (play && balls[1].alive && (tick % 6) == 0) {
      voice_note(P_BALL2, chord_tone(multi_i % 4, P_BALL2), 44, 0, 100);
      multi_i++;
    }
  }

  // one 1/32 tick of the main clock
  void on_tick() {
    uint32_t tk = tick;
    int bt = (int)(tk & 31);
    last_tick_us = time_us();
    bool live = state == ST_READY || state == ST_PLAY || state == ST_CLEAR;
    if (live) backing(bt);
    if (bt == 0 && tk > 0) {
      if (div_bars > 0 && --div_bars == 0) div_mode = DIV_NORMAL;
      if (wide_bars > 0 && --wide_bars == 0) set_width(4);
    }
    if (state == ST_CLEAR && tk >= msg_until) state = ST_READY;
    if (state == ST_READY && launch_pending && (tk & 1) == 0) launch(); // launch on the 16th grid
    if (state == ST_PLAY) {
      if ((tk & 3) == 0) {
        step_drops();
        dstep_us = last_tick_us;
      }
      int every = div_mode == DIV_HALF ? 4 : (div_mode == DIV_DOUBLE ? 1 : 2);
      if ((tk % every) == 0) {
        bstep_us = last_tick_us;
        bstep_per_us = every * tick_us();
        rows_now = 0;
        for (int i = 0; i < 2; ++i)
          if (balls[i].alive && !balls[i].held) step_ball(&balls[i]);
        if (rows_now) row_clear(rows_now);
        if (!balls[0].alive && balls[1].alive) { // multi ball takes over as main ball
          balls[0] = balls[1];
          balls[0].ch = 1;
          balls[1].alive = 0;
        }
        if (!balls_alive())
          lose_life();
        else if (!bricks_left())
          level_clear();
      }
    }
    tick++;
  }

  // =====================================================================
  // lifecycle
  // =====================================================================
  void setup_voice(synth_preset_t *bank, int p, int a, int d, int s, int r, int lp) {
    synth_preset_t *sp = &bank[p];
    set_lo_hi_same_for_all_corners(VOICE_PARAM_ATTACK, a, a, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_DECAY, d, d, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_SUSTAIN, s, s, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_RELEASE, r, r, sp);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_CUTOFF_LP, lp, lp, sp);
  }
  void set_p(synth_preset_t *bank, int p, int param, int v) { set_lo_hi_same_for_all_corners(param, v, v, &bank[p]); }

  void clear_runtime() {
    memset(&voice_allocator, 0, sizeof(voice_allocator));
    memset(hits, 0, sizeof(hits));
    for (int i = 0; i < NUM_HITS; ++i) hits[i].voice = -1;
    req_dx = 0;
    req_launch = req_play = false;
    req_audition = 0;
    g_down = false;
  }

  void to_idle() {
    memset(brick, 0, sizeof(brick));
    memset(balls, 0, sizeof(balls));
    memset(drops, 0, sizeof(drops));
    memset(echoes, 0, sizeof(echoes));
    paddle_w = 4;
    paddle_l = 6;
    div_mode = DIV_NORMAL;
    div_bars = wide_bars = 0;
    level = 1;
    lives = 3;
    score = 0;
    paused = false;
    launch_pending = false;
    key_mod = 0;
    chord_i = 0;
    combo = 0;
    bricks_total = bricks_now = 0;
    row_had = 0;
    memset(sweep_alive, 0, sizeof(sweep_alive));
    pv_q8 = paddle_l * 256;
    pw_q8 = paddle_w * 256;
    state = ST_IDLE;
  }

  void setup_default(synth_preset_t *bank) {
    clear_runtime();
    // lead
    setup_voice(bank, P_LEAD, 0, 50, 20, 40, 95);
    set_p(bank, P_LEAD, VOICE_PARAM_DELAY_SEND, 50);
    // pluck
    setup_voice(bank, P_PLUCK, 0, 30, 0, 25, 100);
    set_p(bank, P_PLUCK, VOICE_PARAM_RESONANCE, 40);
    set_p(bank, P_PLUCK, VOICE_PARAM_DELAY_SEND, 40);
    // perc (metallic)
    setup_voice(bank, P_PERC, 0, 22, 0, 20, 110);
    set_p(bank, P_PERC, VOICE_PARAM_WAVEFOLD, 100);
    // ball 2 bell
    setup_voice(bank, P_BALL2, 0, 55, 0, 55, 115);
    set_p(bank, P_BALL2, VOICE_PARAM_WAVEFOLD, 80);
    set_p(bank, P_BALL2, VOICE_PARAM_REVERB_SEND, 70);
    // bass
    setup_voice(bank, P_BASS, 0, 50, 40, 25, 45);
    set_p(bank, P_BASS, VOICE_PARAM_SUBOSC, 100);
    // pad
    setup_voice(bank, P_PAD, 60, 100, 90, 90, 65);
    set_p(bank, P_PAD, VOICE_PARAM_REVERB_SEND, 100);
    set_p(bank, P_PAD, VOICE_PARAM_CHORUS, 80);
    // drums: noise-ish, very short
    setup_voice(bank, P_DRUM, 0, 18, 0, 10, 120);
    set_p(bank, P_DRUM, VOICE_PARAM_SUBOSC, 10);

    to_idle();
    tick = 0;
    sound_sel = 0;
  }

  void on_load_finished(void) {
    clear_runtime();
    to_idle();
  }

  // =====================================================================
  // sequence (timing-critical)
  // =====================================================================
  void handle_audition() {
    if (req_audition) {
      int p = req_audition - 1;
      req_audition = 0;
      if (p == P_DRUM)
        kick(110);
      else if (p == P_BASS)
        voice_note(P_BASS, chord_tone(0, P_BASS), 100);
      else if (p == P_PAD)
        for (int i = 0; i < 3; ++i) voice_note(P_PAD, chord_tone(i, P_PAD), 80, 0, 1500);
      else
        voice_note(p, chord_tone(1, p), 100);
    }
  }

  // the selector is on screen: only sound previews run, the game is frozen
  void on_menu_sequence(int delta_time_us) {
    handle_audition();
    update_hits(delta_time_us);
  }

  // called from on_sequence when the selector starts or resumes this game
  void enter() {
    req_dx = 0;
    req_launch = req_play = false;
    g_down = false;
    clk.update(-1, 8, 1, UPDATE_DIV_ON_QUARTER_NOTE, true); // drop edges collected while away
    last_tick_us = time_us();
    if (state == ST_IDLE || state == ST_OVER)
      reset_game();
    else
      paused = true; // a run in progress waits on the pad
  }

  void on_sequence(int delta_time_us) {
    if (req_play) {
      req_play = false;
      if (state == ST_IDLE || state == ST_OVER)
        reset_game();
      else
        paused = !paused;
    }
    bool running = !paused && (state == ST_READY || state == ST_PLAY || state == ST_CLEAR);
    if (running && req_dx) move_paddle(req_dx);
    req_dx = 0;
    if (req_launch) {
      req_launch = false;
      if (running && state == ST_READY) launch_pending = true;
    }
    handle_audition();

    // freerunning 1/32 grid, phase-locked to transport when it plays
    int edges = clk.update(-1, 8, 1, UPDATE_DIV_ON_QUARTER_NOTE, true);
    if (edges > 4) edges = 4;
    while (edges-- > 0)
      if (!paused) on_tick();

    update_hits(delta_time_us);
  }

  // =====================================================================
  // UI
  // =====================================================================
  void handle_gesture() {
    int sp = 0, sx = 0, sy = 0;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        if (x == 15 && y == 15) continue; // play pad
        int p = get_touch_pressure_xy(x, y);
        if (p < 12) continue;
        sp += p;
        sx += p * x;
        sy += p * y;
      }
    uint32_t now = time_us();
    bool down = g_down ? sp > 20 : sp > 40; // release needs less pressure than press
    if (down) {
      g_last_us = now;
      int cx = (sx * 256) / sp + 128, cy = (sy * 256) / sp + 128;
      if (!g_down) {
        g_down = true;
        g_x0_q8 = cx;
        g_y0_q8 = cy;
        g_applied = 0;
        g_max_q8 = 0;
        g_launched = false;
        g_t0 = now;
        return;
      }
      int dx = cx - g_x0_q8, dy = cy - g_y0_q8;
      g_max_q8 = maxi(g_max_q8, abs(dx) + abs(dy));
      if (state == ST_IDLE || state == ST_OVER || paused) return;
      if (!g_launched && dy < -563 && -dy * 10 > abs(dx) * 13) { // > 2.2 cells up, mostly vertical
        g_launched = true;
        on_sequence_lock_guard_t guard;
        req_launch = true;
        return;
      }
      if (g_launched) return;
      // paddle follows the finger cell by cell, with a small dead zone at cell borders
      int delta = 0;
      while (dx - g_applied * 256 > 166) {
        g_applied++;
        delta++;
      }
      while (dx - g_applied * 256 < -166) {
        g_applied--;
        delta--;
      }
      if (delta) {
        on_sequence_lock_guard_t guard;
        req_dx = (int8_t)clampi(req_dx + delta, -15, 15);
      }
    } else if (g_down) {
      if ((uint32_t)(now - g_last_us) < 60000u) return; // short pressure gap between pads: same gesture
      g_down = false;
      uint32_t dur = g_last_us - g_t0;
      bool tap = !g_launched && g_applied == 0 && g_max_q8 < 154 && dur > 15000u && dur < 320000u;
      if (!tap) return;
      on_sequence_lock_guard_t guard;
      if (state == ST_OVER)
        req_play = true; // tap restarts after game over
      else if (state == ST_READY && !paused)
        req_launch = true;
    }
  }

  static const char *glyph(char c) {
    switch (c) {
    case 'A': return "010101111101101";
    case 'C': return "011100100100011";
    case 'E': return "111100110100111";
    case 'G': return "011100101101011";
    case 'L': return "100100100100111";
    case 'M': return "101111111101101";
    case 'O': return "010101101101010";
    case 'P': return "110101110100100";
    case 'R': return "110101110101101";
    case 'S': return "011100010001110";
    case 'U': return "101101101101111";
    case 'V': return "101101101101010";
    case 'Y': return "101101010010010";
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "110001010100111";
    case '3': return "110001010001110";
    case '4': return "101101111001001";
    case '5': return "111100110001110";
    case '6': return "011100110101010";
    case '7': return "111001010010010";
    case '8': return "010101010101010";
    case '9': return "010101011001110";
    default: return "000000000000000";
    }
  }

  // big scrolling text: 3x5 font doubled to 6x10, rows 3..12
  void draw_text(const char *s, int c, int b) {
    int len = 0;
    while (s[len]) len++;
    int w = len * 8;
    int off = 16 - (int)((time_us() / 70000u) % (uint32_t)(w + 16));
    for (int k = 0; k < len; ++k) {
      const char *g = glyph(s[k]);
      for (int j = 0; j < 15; ++j) {
        if (g[j] != '1') continue;
        int gx = off + k * 8 + (j % 3) * 2, gy = 3 + (j / 3) * 2;
        fb_add(gx, gy, c, b);
        fb_add(gx + 1, gy, c, b);
        fb_add(gx, gy + 1, c, b);
        fb_add(gx + 1, gy + 1, c, b);
      }
    }
  }

  static int flash_q8(int a) { return a < 0 ? 0 : maxi(0, 256 - a / 3); }

  // ball position along its last two steps; s_q8 = 256 at the current cell, 0 at the previous one
  void ball_pos(const prall_ball_t *b, int s_q8, int *xq, int *yq) const {
    int X = b->xq, P = b->pxq, Q = b->qxq, R = b->rxq; // true line, q8
    if (s_q8 >= 0) {
      *xq = P + (X - P) * s_q8 / 256;
      *yq = b->py * 256 + (b->y - b->py) * s_q8;
    } else if (s_q8 >= -256) {
      int u = 256 + s_q8;
      *xq = Q + (P - Q) * u / 256;
      *yq = b->qy * 256 + (b->py - b->qy) * u;
    } else {
      int u = 512 + maxi(-512, s_q8);
      *xq = R + (Q - R) * u / 256;
      *yq = b->ry * 256 + (b->qy - b->ry) * u;
    }
  }

  void draw_game(int dt) {
    memset(fb, 0, sizeof(fb));
    memset(nodither, 0, sizeof(nodither));
    uint32_t now = time_us();
    // eased paddle (visual only; collisions use the logical paddle)
    {
      int a = clampi(dt * 256 / 35000, 8, 256);
      int tl = paddle_l * 256, tw = paddle_w * 256;
      pv_q8 += ((tl - pv_q8) * a) >> 8;
      pw_q8 += ((tw - pw_q8) * a) >> 8;
      if (abs(tl - pv_q8) < 6) pv_q8 = tl;
      if (abs(tw - pw_q8) < 6) pw_q8 = tw;
    }
    bool stepping = state == ST_PLAY && !paused;
    int bf = stepping ? clampi((int)((now - bstep_us) * 256 / (uint32_t)maxi(1, bstep_per_us)), 0, 256) : 256;
    int df = stepping ? clampi((int)((now - dstep_us) * 256 / (uint32_t)(4 * tick_us())), 0, 256) : 0;
    int frac = clampi((int)((now - last_tick_us) * 256 / (uint32_t)tick_us()), 0, 255);
    int ph = (int)(tick & 7) * 32 + (frac >> 3); // 0..255 within a quarter
    int pulse = ((256 - ph) * (256 - ph)) >> 8;
    bool overlay = state == ST_OVER || state == ST_CLEAR || paused;

    if (state != ST_IDLE) {
      // bricks (flash briefly on every chord change)
      int ca = (int)(tick - chord_flash_tick) * 256 + frac;
      int cf = ca < 6 * 256 ? 110 - ca * 110 / (6 * 256) : 0;
      for (int y = 0; y < PR; ++y)
        for (int x = 0; x < N; ++x) {
          uint8_t br = brick[y][x];
          if (!br) continue;
          int pu = br >> 2;
          if (pu)
            fb_add(x, y, ((tick >> 2) & 1) ? C_WHITE : pu_col(pu), 190 + (pulse * 60 >> 8));
          else
            fb_add(x, y, (br & 3) - 1, 150 + (pulse * 50 >> 8));
          if (cf > 0) fb_add(x, y, C_WHITE, cf);
        }
      // row sweeps: light runs out from the last hit along the cleared row
      for (int k = 0; k < 4; ++k) {
        if (!sweep_alive[k]) continue;
        int a = (int)(tick - sweep_t[k]) * 256 + frac; // ticks q8
        if (a > 12 * 256) {
          sweep_alive[k] = 0;
          continue;
        }
        int r = a / 128; // two cells per tick
        int glow = maxi(0, 140 - a / 22);
        for (int xx = 0; xx < N; ++xx) {
          int d = abs(xx - sweep_x[k]);
          int e = (d == r) ? 256 : (d == r - 1 ? 120 : 0);
          fb_add(xx, sweep_y[k], C_WHITE, maxi(e * maxi(0, 3072 - a) / 3072, glow));
        }
      }
      // echoes: hit flash + two repeats on the dotted 1/8
      for (int i = 0; i < MAX_ECHO; ++i) {
        prall_echo_t *e = &echoes[i];
        if (!e->alive) continue;
        int a = (int)(tick - e->t0) * 256 + frac - 256;
        if (a > 18 * 256) {
          e->alive = 0;
          continue;
        }
        int b = flash_q8(a) + (flash_q8(a - 6 * 256) >> 1) + (flash_q8(a - 12 * 256) >> 2);
        fb_add(e->x, e->y, e->col, b);
        if (a < 640) { // small ring on impact
          int rb = flash_q8(a) * 130 >> 8;
          fb_add(e->x - 1, e->y, e->col, rb);
          fb_add(e->x + 1, e->y, e->col, rb);
          fb_add(e->x, e->y - 1, e->col, rb);
          fb_add(e->x, e->y + 1, e->col, rb);
        }
      }
      // falling power-ups glide down (they land on the paddle row exactly on their step)
      for (int i = 0; i < MAX_DROPS; ++i)
        if (drops[i].alive)
          splat(drops[i].x * 256, drops[i].y * 256 + (drops[i].y < PR ? df : 0), pu_col(drops[i].type),
                ((tick >> 1) & 1) ? 256 : 170);
      // paddle: anti-aliased, eased
      int pf = (int)(tick - flash_tick) < 3 ? 256 - (int)(tick - flash_tick) * 85 : 0;
      span(pv_q8, pw_q8, PR, wide_bars ? C_WIDE : C_PADDLE, 172);
      if (pf > 0) span(pv_q8, pw_q8, PR, C_WHITE, (pf * pf) >> 8);
      // balls: sub-pixel comet. At the step instant the ball sits on its contact cell (note fires),
      // in between it glides toward the cell it moved to.
      for (int i = 0; i < 2; ++i) {
        prall_ball_t *b = &balls[i];
        if (!b->alive) continue;
        int c = b->ch == 2 ? C_MULTI : C_BALL;
        if (b->held) {
          int hx = floor8(pv_q8 + (paddle_center() - paddle_l) * 256 + 128);
          fb_add(hx, PR - 1, c, 150 + (pulse * 106 >> 8));
          continue;
        }
        // head: ONE LED per frame. Vertically it changes row at half step. Horizontally it follows the true
        // flight line: it sits on its nearest column and only in the short phase around a column change
        // alternates between the two columns (error-diffusion position dithering, ~250 fps).
        bool moved = stepping && (b->px != b->x || b->py != b->y || b->pxq != b->xq);
        bool at_new = !moved || bf >= 128;
        int hy = at_new ? b->y : b->py;
        int hxq, hyq_dummy;
        ball_pos(b, moved ? bf : 256, &hxq, &hyq_dummy);
        hxq = clampi(hxq, 0, (N - 1) * 256);
        int hb = hxq >> 8, hf = hxq & 255;
        // dither only in a short window around the column change (frac 80..176 of a cell);
        // outside it the head sits still on its nearest column
        hf = clampi((hf - 80) * 256 / 96, 0, 256);
        int hx = hb;
        if (hf >= 256) {
          hx = mini(N - 1, hb + 1);
          hf = 0;
        }
        dacc[i] = (uint16_t)(dacc[i] + hf);
        if (dacc[i] >= 256) {
          dacc[i] = (uint16_t)(dacc[i] - 256);
          if (hf) hx = mini(N - 1, hb + 1);
        }
        // tail: finely sampled true line, anti-aliased horizontally only (Wu-style, two cells side by side
        // in the same row, never a square). Brightness per cell = most recent pass, fades over time.
        if (stepping) {
          memset(tmax, 0, sizeof(tmax));
          int len = div_mode == DIV_DOUBLE ? 28 : (div_mode == DIV_HALF ? 12 : 20); // samples, 1/16 step each
          for (int k = 1; k <= len; ++k) {
            int sq = bf - k * 16;
            if (sq < -500) break;
            int tx, ty;
            ball_pos(b, sq, &tx, &ty);
            int cy = floor8(ty + 128);
            if (cy < 0 || cy >= N) continue;
            tx = clampi(tx, 0, (N - 1) * 256);
            int bx = tx >> 8, fx = tx & 255;
            int e = 190 * (len + 1 - k) / (len + 1);
            for (int j = 0; j < 2; ++j) {
              int cx = bx + j;
              if (cx >= N || (cy == hy && (cx == hb || cx == hb + 1))) continue;
              int ej = j ? (e * fx) >> 8 : (e * (256 - fx)) >> 8;
              uint8_t *m = &tmax[cy * N + cx];
              if (ej > *m) *m = (uint8_t)ej;
            }
          }
          for (int q = 0; q < N * N; ++q)
            if (tmax[q]) {
              fb_add(q % N, q / N, c, tmax[q]);
              nodither[q] = 1;
            }
        }
        fb_add(hx, hy, c, 256);
        nodither[hy * N + hx] = 1;
      }
      // HUD row: lives, power-up bars, multi
      for (int k = 0; k < 3; ++k)
        if (k < lives) fb_add(k, 15, C_SIGNAL, 190);
      for (int k = 0; k < div_bars; ++k) fb_add(4 + k, 15, div_mode == DIV_HALF ? C_HALF : C_SIGNAL, 210);
      for (int k = 0; k < wide_bars; ++k) fb_add(8 + k, 15, C_WIDE, 210);
      if (balls[1].alive) fb_add(13, 15, C_MULTI, 210);
    }

    if (overlay) {
      fb_dim(56);
      if (paused) {
        draw_text("PAUSE", C_WHITE, 220);
      } else if (state == ST_CLEAR) {
        snprintf(msgbuf, sizeof(msgbuf), "LEVEL %d", (int)level);
        draw_text(msgbuf, C_WHITE, 240);
      } // ST_OVER: the console draws the score screen on top
    }
    fb_flush();

    // play / pause pad
    bool running = !paused && (state == ST_READY || state == ST_PLAY || state == ST_CLEAR);
    int slow = (int)((now / 3000u) & 511);
    int tri = slow < 256 ? slow : 511 - slow;
    pad_col = running ? lc(C_MULTI, 120 + (pulse * 136 >> 8)) : lc(C_SIGNAL, 90 + (tri * 166 >> 8));
  }

  void draw_sound_page(preset_pages_t &preset_pages, int y) {
    preset_pages.edit(sound_sel, y, 0, true);
    preset_pages.xy_pad(sound_sel, 8, y + 10);
    static const uint8_t SEL_COL[NUM_VOICES_EDIT] = {C_LEAD, C_PLUCK, C_PERC, C_MULTI, C_PADDLE, C_HALF, C_WHITE};
    for (int x = 0; x < NUM_VOICES_EDIT; ++x) {
      if (button(x, y + 15, lc(SEL_COL[x], x == sound_sel ? 256 : 110), ISOLATED, "Select voice")) {
        sound_sel = (uint8_t)x;
        on_sequence_lock_guard_t guard;
        req_audition = (int8_t)(x + 1);
      }
    }
    static const char *const names[NUM_VOICES_EDIT] = {"lead", "pluck", "perc", "ball 2", "bass", "pad", "drums"};
    set_help_text("Sound: #fc2#*%s#.  tap bottom row to select + audition", names[sound_sel]);
  }

  bool settings_page(int page) {
    bool changed = false;
    if (page == -1) {
      static const char *const keys[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
      int d = draw_system_style_settings_page("root", keys[root % 12], -1);
      if (d) {
        root = (uint8_t)((root + d + 120) % 12);
        changed = true;
      }
    } else if (page == -2) {
      static const char *const scales[NUM_SCALES] = {"MAJ",  "MIN",  "DOR",  "PHRY", "LYD",  "MIXO", "HMIN",
                                                     "PMAJ", "PMIN", "HIRA", "BLUE", "WHOL", "CHRO"};
      int d = draw_system_style_enum_settings_page("scal", scale_i, scales, NUM_SCALES);
      if (d) {
        scale_i = (uint8_t)clampi(scale_i + d, 0, NUM_SCALES - 1);
        changed = true;
      }
    } else if (page == -3) {
      int d = draw_system_style_bool_settings_page("midi", midi_on != 0);
      if (d) {
        midi_on = d > 0 ? 1 : 0;
        changed = true;
      }
    }
    return changed;
  }

  void ui_play(int delta_time_us) {
    if (!is_scroll_in_progress()) handle_gesture();
    draw_game(delta_time_us);
    if (paused)
      set_help_text("Paused  press #*play#. to continue, hold = menu");
    else if (state == ST_OVER)
      set_help_text("Game over  level #*%d#.  score #*%d#.  tap to restart, hold bottom right = menu", (int)level, (int)score);
    else if (state == ST_READY || state == ST_CLEAR)
      set_help_text("#*Tap#. or swipe #*up#. to launch  drag to move   lvl %d  score %d", (int)level, (int)score);
    else
      set_help_text("Drag #*left/right#. to move   lvl %d  score %d  combo %d", (int)level, (int)score, (int)combo);
  }
};

// =====================================================================
// ARCADE12 - the console: selector, sound sets, high scores, pages
// =====================================================================
struct arcade_12 : panel_t {
  enum { G_KLOTZ = 0, G_PRALL = 1 };
  enum { HOLD_US = 700000 };

  klotz_game klotz;
  prall_game prall;

  // Each game owns a complete sound set. Together they need 16 voices but the synth has 12 preset
  // slots, so the current game's set lives in the slots and both are stored here. Preset indices,
  // and with them the MIDI channels, stay exactly those of the standalone panels.
  synth_preset_t snd_k[klotz_game::NUM_VOICES_EDIT];
  synth_preset_t snd_p[prall_game::NUM_VOICES_EDIT];
  synth_preset_t snd_mix[2];

  uint8_t cur = G_KLOTZ; // whose sounds are in the slots; pages follow this game
  bool in_menu = true;

  // ---- requests from on_ui ----
  int8_t req_game; // game + 1
  bool req_menu;

  // ---- selector touch ----
  bool m_armed, m_down;
  uint8_t m_side;
  uint32_t m_last;

  // ---- play pad ----
  hold_timer_t pad_hold;
  bool pad_held, pad_hold_fired;

  // ---- high scores: panel settings, so they survive reboots and song changes ----
  enum { HS_N = 5, OV_MAX = 112 };
  uint32_t hs_score[2][HS_N];
  uint8_t hs_level[2][HS_N];
  uint16_t hs_lines[2][HS_N]; // KLOTZ only
  bool hs_dirty;              // new entry waits to be written to SD (done in on_ui)
  uint8_t last_state[2];
  // score screen after game over
  int8_t ov_rank = -1;
  uint8_t ov_game;
  bool ov_build, ov_ready;
  uint32_t ov_score, ov_t0;
  char ov_txt[OV_MAX];
  uint8_t ov_red[OV_MAX];
  uint8_t ov_len;

  // ---- selector demo scratch ----
  uint8_t d_occ[16][5];

  preset_pages_t preset_pages;
  panel_page_t panel_page;

  int get_num_pages() override { return 3; }
  int get_num_panel_settings_pages() override { return cur == G_PRALL ? 3 : 4; }
  int get_num_tracks(void) override { return cur == G_PRALL ? (int)prall_game::NUM_VOICES_EDIT : (int)klotz_game::NUM_VOICES_EDIT; }
  const char *get_track_name(int i, char buf[16]) override {
    return cur == G_PRALL ? prall.get_track_name(i, buf) : klotz.get_track_name(i, buf);
  }

  // =====================================================================
  // sound sets
  // =====================================================================
  // only what shapes the sound moves; bank, samples and slice bounds stay system-managed in the slot
  static void copy_sound(synth_preset_t *dst, const synth_preset_t *src) {
    memcpy(dst->corner_params, src->corner_params, sizeof(dst->corner_params));
    memcpy(dst->modulation_amount, src->modulation_amount, sizeof(dst->modulation_amount));
    memcpy(dst->xy, src->xy, sizeof(dst->xy));
    memcpy(dst->xy_recordings, src->xy_recordings, sizeof(dst->xy_recordings));
    for (int i = 0; i < PRESET_SLICE_COUNT; ++i) dst->slice[i].flags = src->slice[i].flags;
  }
  synth_preset_t *store_of(int g) { return g == G_PRALL ? snd_p : snd_k; }
  static int voices_of(int g) { return g == G_PRALL ? (int)prall_game::NUM_VOICES_EDIT : (int)klotz_game::NUM_VOICES_EDIT; }

  void sync_sound() { // slots -> store
    synth_preset_t *st = store_of(cur);
    for (int p = 0; p < voices_of(cur); ++p) copy_sound(&st[p], &synth_presets[p]);
    copy_sound(&snd_mix[cur], &synth_presets[MIX_PRESET_IDX]);
  }
  void load_sound(int g) { // store -> slots
    synth_preset_t *st = store_of(g);
    for (int p = 0; p < voices_of(g); ++p) {
      copy_sound(&synth_presets[p], &st[p]);
      refresh_synth_preset_modulation_masks(&synth_presets[p]);
    }
    copy_sound(&synth_presets[MIX_PRESET_IDX], &snd_mix[g]);
    refresh_synth_preset_modulation_masks(&synth_presets[MIX_PRESET_IDX]);
    refresh_modulation_masks();
    cur = (uint8_t)g;
  }

  void silence_current() {
    if (cur == G_PRALL)
      prall.release_all_hits();
    else
      klotz.release_all_hits();
  }

  // =====================================================================
  // high scores
  // =====================================================================
  bool game_over_now(int g) const {
    return g == G_PRALL ? prall.state == prall_game::ST_OVER : klotz.state == klotz_game::ST_OVER;
  }
  uint8_t state_of(int g) const { return g == G_PRALL ? prall.state : klotz.state; }

  // called from on_sequence on the edge into game over: rank the run, fanfare on a new best
  void record_run(int g) {
    uint32_t score = g == G_PRALL ? prall.score : klotz.score;
    int r = -1;
    if (score > 0)
      for (int i = 0; i < HS_N; ++i)
        if (score > hs_score[g][i]) {
          r = i;
          break;
        }
    if (r >= 0) {
      for (int i = HS_N - 1; i > r; --i) {
        hs_score[g][i] = hs_score[g][i - 1];
        hs_level[g][i] = hs_level[g][i - 1];
        hs_lines[g][i] = hs_lines[g][i - 1];
      }
      hs_score[g][r] = score;
      hs_level[g][r] = (uint8_t)(g == G_PRALL ? prall.level : klotz.level + 1);
      hs_lines[g][r] = (uint16_t)(g == G_PRALL ? 0 : klotz.lines);
      hs_dirty = true;
    }
    ov_rank = (int8_t)r;
    ov_game = (uint8_t)g;
    ov_score = score;
    ov_t0 = time_us();
    ov_ready = false;
    ov_build = true;
    if (r != 0) return;
    // new best: a rising fanfare after the game-over phrase
    if (g == G_PRALL) {
      int t = prall.tick_us();
      for (int i = 0; i < 6; ++i)
        prall.voice_note(prall_game::P_LEAD, prall.chord_tone(i + 3, prall_game::P_LEAD), 104, 18 * t + i * t, 220);
    } else {
      int sp = qn_period_us / 4;
      for (int i = 0; i < 6; ++i) klotz.play_stone(2, klotz.chord_tone(i) + 12, 104, 6 * sp + i * sp / 2, 220);
    }
  }

  // arcade start jingle on a fresh run: a rising run in the game's own voices, then a held chord
  void start_fanfare(int g) {
    if (g == G_PRALL) {
      int s16 = 2 * prall.tick_us();
      for (int i = 0; i < 5; ++i) prall.voice_note(prall_game::P_LEAD, prall.chord_tone(i + 2, prall_game::P_LEAD), 96, i * s16, 110);
      int t = 5 * s16;
      prall.voice_note(prall_game::P_PAD, prall.chord_tone(0, prall_game::P_PAD), 80, t, 900);
      prall.voice_note(prall_game::P_PAD, prall.chord_tone(2, prall_game::P_PAD), 76, t, 900);
      prall.voice_note(prall_game::P_LEAD, prall.chord_tone(7, prall_game::P_LEAD), 110, t, 500);
      prall.kick(100, t);
    } else {
      int s16 = qn_period_us / 4;
      for (int i = 0; i < 5; ++i) klotz.play_stone(2, klotz.chord_tone(i) + 12, 96, i * s16, 110);
      int t = 5 * s16;
      klotz.play_stone(0, klotz.chord_tone(0), 80, t, 900);
      klotz.play_stone(0, klotz.chord_tone(2), 76, t, 900);
      klotz.play_stone(3, klotz.chord_tone(5) + 12, 110, t, 500);
      klotz.kick(100, t);
    }
  }

  void enter_game(int g) {
    silence_current();
    if (g != cur) {
      sync_sound();
      load_sound(g);
    }
    in_menu = false;
    pad_held = pad_hold_fired = false;
    // a fresh run gets the start jingle, a resumed one just waits paused
    bool fresh = g == G_PRALL ? (prall.state == prall_game::ST_IDLE || prall.state == prall_game::ST_OVER)
                              : klotz.state != klotz_game::ST_PLAY;
    if (g == G_PRALL)
      prall.enter();
    else
      klotz.enter();
    if (fresh) start_fanfare(g);
    last_state[g] = state_of(g);
  }

  // =====================================================================
  // lifecycle
  // =====================================================================
  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    for (int p = 0; p < klotz_game::NUM_VOICES_EDIT; ++p) snd_k[p] = synth_presets[p];
    for (int p = 0; p < prall_game::NUM_VOICES_EDIT; ++p) snd_p[p] = synth_presets[p];
    snd_mix[G_KLOTZ] = synth_presets[MIX_PRESET_IDX];
    snd_mix[G_PRALL] = synth_presets[MIX_PRESET_IDX];
    klotz.setup_default(snd_k);
    prall.setup_default(snd_p);
    prall.tick = 0;
    last_state[0] = klotz.state;
    last_state[1] = prall.state;
    ov_rank = -1;
    ov_build = ov_ready = false;
    req_game = 0;
    req_menu = false;
    m_armed = m_down = false;
    in_menu = true;
    load_sound(G_KLOTZ);
  }

  void on_load_finished(void) override {
    klotz.on_load_finished();
    prall.on_load_finished();
    last_state[0] = klotz.state;
    last_state[1] = prall.state;
    ov_rank = -1;
    ov_build = ov_ready = false;
    req_game = 0;
    req_menu = false;
    m_armed = m_down = false;
    in_menu = true;
    load_sound(cur);
  }

  // =====================================================================
  // sequence
  // =====================================================================
  void on_sequence(int delta_time_us) override {
    if (req_menu) {
      req_menu = false;
      if (!in_menu) {
        silence_current();
        in_menu = true;
      }
    }
    if (req_game) {
      int g = req_game - 1;
      req_game = 0;
      enter_game(g);
    }
    if (in_menu) {
      if (cur == G_PRALL)
        prall.on_menu_sequence(delta_time_us);
      else
        klotz.on_menu_sequence(delta_time_us);
      return;
    }
    if (cur == G_PRALL)
      prall.on_sequence(delta_time_us);
    else
      klotz.on_sequence(delta_time_us);
    uint8_t st = state_of(cur);
    if (st != last_state[cur]) {
      if (game_over_now(cur)) record_run(cur);
      last_state[cur] = st;
    }
  }

  // =====================================================================
  // selector
  // =====================================================================
  void menu_touch() {
    int pl = 0, pr = 0;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        int p = get_touch_pressure_xy(x, y);
        if (p < 12) continue;
        if (x >= 5 && x <= 10) continue; // the lettering is not a button
        if (x < 8)
          pl += p;
        else
          pr += p;
      }
    int sum = pl + pr;
    uint32_t now = time_us();
    if (!m_armed) { // the finger that held the pad must leave first
      if (sum > 20)
        m_last = now;
      else if ((uint32_t)(now - m_last) > 80000u)
        m_armed = true;
      return;
    }
    if (m_down ? sum > 20 : sum > 40) {
      m_last = now;
      m_down = true;
      m_side = pl > pr ? G_KLOTZ : G_PRALL;
    } else if (m_down && (uint32_t)(now - m_last) > 60000u) {
      m_down = false;
      on_sequence_lock_guard_t guard;
      req_game = (int8_t)(m_side + 1);
    }
  }

  // left block (cols 0..4): a small KLOTZ round. Two squares and an I close the stack, four rows clear.
  void demo_klotz(int bright) {
    enum { KW = 5, NS = 3, STEP_US = 95000, FLASH_STEPS = 5 };
    static const uint8_t BASE[2][KW] = {{3, 3, 5, 5, 0}, {6, 6, 2, 2, 0}};
    static const uint8_t STONE[NS][3] = {{1, 0, 0}, {1, 0, 2}, {0, 1, 2}}; // type, rot, x
    int c = (int)(time_us() / STEP_US);
    int round = 0;
    for (int pass = 0; pass < 2; ++pass) {
      int left = pass ? c % round : 0;
      memset(d_occ, 0, sizeof(d_occ));
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < KW; ++x) d_occ[14 + y][x] = BASE[y][x];
      bool falling = false;
      int ft = 0, fr = 0, fx = 0, fy = 0;
      for (int k = 0; k < NS; ++k) {
        int t = STONE[k][0], r = STONE[k][1], x0 = STONE[k][2];
        int8_t cx[4], cy[4];
        int y = -3;
        for (;;) {
          klotz_game::cells_of(t, r, x0, y + 1, cx, cy);
          bool hit = false;
          for (int i = 0; i < 4; ++i)
            if (cy[i] >= 16 || (cy[i] >= 0 && cx[i] >= 0 && cx[i] < KW && d_occ[cy[i]][cx[i]])) hit = true;
          if (hit) break;
          y++;
          if (pass) {
            if (left-- <= 0) {
              falling = true;
              ft = t;
              fr = r;
              fx = x0;
              fy = y;
              break;
            }
          } else {
            round++;
          }
        }
        if (falling) break;
        klotz_game::cells_of(t, r, x0, y, cx, cy);
        for (int i = 0; i < 4; ++i)
          if (cy[i] >= 0) d_occ[cy[i]][cx[i]] = (uint8_t)(t + 1);
      }
      if (!pass) {
        round += FLASH_STEPS;
        continue;
      }
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < KW; ++x)
          if (d_occ[y][x]) set_led(x, y, klotz_game::lc(d_occ[y][x] - 1, bright * 150 >> 8));
      if (falling) {
        int8_t cx[4], cy[4];
        klotz_game::cells_of(ft, fr, fx, fy, cx, cy);
        for (int i = 0; i < 4; ++i)
          if (cy[i] >= 0) set_led(cx[i], cy[i], klotz_game::lc(ft, bright));
      } else { // all landed: the four full rows flash
        for (int y = 12; y < 16; ++y)
          for (int x = 0; x < KW; ++x) set_led(x, y, klotz_game::lc(klotz_game::C_WHITE, bright, 256));
      }
    }
  }

  // right block (cols 11..15): a small PRALL round. The ball bounces on the paddle and eats the wall,
  // one brick per ceiling touch, every column in turn.
  void demo_prall(int bright) {
    enum { X0 = 11, W = 5, TOP = 5, PAD_Y = 14, STEP_US = 80000, BOUNCES = W * 3 };
    const int xper = 2 * W, yspan = 2 * (PAD_Y - 1 - TOP);
    uint32_t c = (uint32_t)(time_us() / STEP_US);
    int t = (int)(c % (uint32_t)(yspan * BOUNCES));
    int xp = t % xper, yp = t % yspan;
    int bx = xp < W ? xp : xper - 1 - xp;
    int by = (PAD_Y - 1) - (yp <= yspan / 2 ? yp : yspan - yp);
    int done = (t + yspan / 2) / yspan; // ceiling touches so far
    uint8_t taken[W];
    memset(taken, 0, sizeof(taken));
    for (int i = 0; i < done; ++i) {
      int q = (yspan / 2 + i * yspan) % xper;
      int cx = q < W ? q : xper - 1 - q;
      if (taken[cx] < 3) taken[cx]++;
    }
    static const uint8_t ROWC[3] = {prall_game::C_LEAD, prall_game::C_PLUCK, prall_game::C_PERC};
    for (int x = 0; x < W; ++x)
      for (int r = 0; r < 3 - taken[x]; ++r)
        set_led(X0 + x, 2 + r, prall_game::lc(ROWC[r], bright * 170 >> 8));
    int pl = clampi(bx - 1, 0, W - 3);
    for (int x = 0; x < 3; ++x) set_led(X0 + pl + x, PAD_Y, prall_game::lc(prall_game::C_PADDLE, bright * 190 >> 8));
    set_led(X0 + bx, by, prall_game::lc(prall_game::C_BALL, bright));
  }

  // 4x5 letters, row-major
  static const char *coin_glyph(char c) {
    switch (c) {
    case 'S': return "01111000011000011110";
    case 'E': return "11111000111010001111";
    case 'L': return "10001000100010001111";
    case 'C': return "01111000100010000111";
    case 'T': return "11110110011001100110";
    case 'G': return "01111000101110010111";
    case 'A': return "01101001111110011001";
    case 'M': return "10011111111110011001";
    default: return "00000000000000000000";
    }
  }

  // SELECT GAME: upright letters stacked in the middle four columns, rolling upward
  void draw_coin() {
    static const char TXT[] = "SELECT GAME ";
    const int n = 12, stride = 6, total = n * stride;
    int off = (int)((time_us() / 110000u) % (uint32_t)total);
    uint32_t col = klotz_game::lc(1, 235, 70);
    for (int r = 0; r < 16; ++r) {
      int w = (r + off) % total, gr = w % stride;
      if (gr >= 5) continue;
      const char *g = coin_glyph(TXT[w / stride]);
      for (int x = 0; x < 4; ++x)
        if (g[gr * 4 + x] == '1') set_led(6 + x, r, col);
    }
  }

  void draw_menu() {
    int bl = 256, br = 256;
    if (m_down) {
      if (m_side == G_KLOTZ)
        br = 120;
      else
        bl = 120;
    }
    demo_klotz(bl);
    demo_prall(br);
    draw_coin();
  }

  // 3x5 letters, row-major
  static const char *hs_glyph(char c) {
    switch (c) {
    case 'A': return "010101111101101";
    case 'C': return "011100100100011";
    case 'E': return "111100110100111";
    case 'G': return "011100101101011";
    case 'H': return "101101111101101";
    case 'I': return "111010010010111";
    case 'M': return "101111111101101";
    case 'O': return "010101101101010";
    case 'P': return "110101110100100";
    case 'R': return "110101110101101";
    case 'S': return "011100010001110";
    case 'T': return "111010010010010";
    case 'V': return "101101101101010";
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "110001010100111";
    case '3': return "110001010001110";
    case '4': return "101101111001001";
    case '5': return "111100110001110";
    case '6': return "011100110101010";
    case '7': return "111001010010010";
    case '8': return "010101010101010";
    case '9': return "010101011001110";
    default: return "000000000000000";
    }
  }
  void ov_put(const char *t, bool red) {
    while (*t && ov_len < OV_MAX - 1) {
      ov_red[ov_len] = red ? 1 : 0;
      ov_txt[ov_len++] = *t++;
    }
    ov_txt[ov_len] = 0;
  }
  // GAME OVER  score  (HI SCORE)  TOP  1 ...  5 ...   - the run that just ended in red
  void build_overlay() {
    char num[12];
    ov_len = 0;
    ov_put("GAME OVER  ", false);
    snprintf(num, sizeof(num), "%u", (unsigned)ov_score);
    ov_put(num, ov_rank == 0);
    if (ov_rank == 0) ov_put("  HI SCORE", true);
    ov_put("   TOP", false);
    for (int i = 0; i < HS_N; ++i) {
      if (!hs_score[ov_game][i]) break;
      snprintf(num, sizeof(num), "  %d ", i + 1);
      ov_put(num, false);
      snprintf(num, sizeof(num), "%u", (unsigned)hs_score[ov_game][i]);
      ov_put(num, i == ov_rank);
    }
    ov_put("     ", false);
    ov_ready = true;
  }
  // big scrolling score text, rows 3..12 on a cleared band
  void draw_overlay() {
    if (ov_build) {
      ov_build = false;
      build_overlay();
    }
    if (!ov_ready) return;
    for (int y = 2; y <= 13; ++y)
      for (int x = 0; x < 16; ++x) set_led(x, y, 0);
    int w = ov_len * 8;
    int off = 16 - (int)(((time_us() - ov_t0) / 70000u) % (uint32_t)(w + 16));
    uint32_t cw = prall_game::lc(prall_game::C_WHITE, 230), cr = prall_game::lc(prall_game::C_SIGNAL, 250);
    for (int k = 0; k < ov_len; ++k) {
      int x0 = off + k * 8;
      if (x0 > 15 || x0 < -6) continue;
      const char *g = hs_glyph(ov_txt[k]);
      for (int j = 0; j < 15; ++j) {
        if (g[j] != '1') continue;
        int gx = x0 + (j % 3) * 2, gy = 3 + (j / 3) * 2;
        for (int d = 0; d < 4; ++d) {
          int px = gx + (d & 1), py = gy + (d >> 1);
          if (px >= 0 && px < 16) set_led(px, py, ov_red[k] ? cr : cw);
        }
      }
    }
  }

  // tap = play / pause on release, hold = selector. The pad washes to white while holding.
  void draw_pad(uint32_t col) {
    int held_us = pad_held ? pad_hold.hold_time() : 0;
    if (held_us > 150000) {
      int b = clampi((held_us - 150000) * 256 / (HOLD_US - 150000), 0, 256);
      col = klotz_game::lc(klotz_game::C_WHITE, b, 256);
    }
    button(15, 15, col, ISOLATED, "Tap: play / pause   Hold: game selector");
    if (is_last_widget_pressed()) pad_hold_fired = false;
    if (pad_hold.is_last_widget_held_for(HOLD_US)) {
      pad_hold_fired = true;
      m_armed = m_down = false;
      m_last = time_us();
      on_sequence_lock_guard_t guard;
      req_menu = true;
    }
    if (is_last_widget_released() && !pad_hold_fired) {
      on_sequence_lock_guard_t guard;
      if (cur == G_PRALL)
        prall.req_play = true;
      else
        klotz.req_toggle = true;
    }
    pad_held = is_last_widget_held();
  }

  // =====================================================================
  // UI
  // =====================================================================
  void on_ui(int delta_time_us) override {
    if (hs_dirty) { // the only SD write: once per new entry, never during play
      hs_dirty = false;
      save_settings_to_sd(false);
    }
    int page = get_scroll_page();
    if (page < 0) {
      bool changed = cur == G_PRALL ? prall.settings_page(page) : klotz.settings_page(page);
      if (changed) save_settings_to_sd(false);
      return;
    }

    leds_clear();
    if (page == 1) {
      if (cur == G_PRALL)
        prall.draw_sound_page(preset_pages, 16);
      else
        klotz.draw_sound_page(preset_pages, 16);
      return;
    }
    if (page == 2) {
      panel_page.saveload(32, true, FLAG_PICKER_ENABLE_DELETE);
      return;
    }
    if (page > 2) {
      scroll_to_page(2);
      return;
    }

    if (in_menu) {
      if (!is_scroll_in_progress()) menu_touch();
      draw_menu();
      set_help_text("#fc2#*ARCADE 12#.  tap #*left#. KLOTZ  or  #*right#. PRALL   best %u / %u", (unsigned)hs_score[0][0],
                    (unsigned)hs_score[1][0]);
      return;
    }
    if (cur == G_PRALL)
      prall.ui_play(delta_time_us);
    else
      klotz.ui_play();
    if (game_over_now(cur) && ov_game == cur) {
      draw_overlay();
      set_help_text("Game over  score #*%u#.  %s  best %u   tap = restart, hold bottom right = menu", (unsigned)ov_score,
                    ov_rank == 0 ? "#fc2#*HI SCORE#." : "", (unsigned)hs_score[cur][0]);
    }
    draw_pad(cur == G_PRALL ? prall.pad_col : klotz.pad_col);
  }

  bool on_serialise(serialiser_t &s, int version) override;
  bool on_serialise_settings(serialiser_t &s, int version) override;
};

// song: both sound sets. The slots are stored too because they carry the bank / sample choice.
static bool serialise(serialiser_t &s, arcade_12 &o) {
  if (!s.reading) {
    on_sequence_lock_guard_t guard;
    o.sync_sound();
  }
  OBJECT_BEGIN(s);
  FIELD_SYNTH_PRESET("slot0", 0);
  FIELD_SYNTH_PRESET("slot1", 1);
  FIELD_SYNTH_PRESET("slot2", 2);
  FIELD_SYNTH_PRESET("slot3", 3);
  FIELD_SYNTH_PRESET("slot4", 4);
  FIELD_SYNTH_PRESET("slot5", 5);
  FIELD_SYNTH_PRESET("slot6", 6);
  FIELD_SYNTH_PRESET("slot7", 7);
  FIELD_SYNTH_PRESET("slot8", 8);
  FIELD_MIX_PRESET("slotMix");
  FIELD("kI", o.snd_k[0], 0);
  FIELD("kO", o.snd_k[1], 0);
  FIELD("kT", o.snd_k[2], 0);
  FIELD("kS", o.snd_k[3], 0);
  FIELD("kZ", o.snd_k[4], 0);
  FIELD("kJ", o.snd_k[5], 0);
  FIELD("kL", o.snd_k[6], 0);
  FIELD("kSub", o.snd_k[7], 0);
  FIELD("kPerc", o.snd_k[8], 0);
  FIELD("kMix", o.snd_mix[arcade_12::G_KLOTZ], SERIALISE_PRESET_IS_MIX_PRESET);
  FIELD("pLead", o.snd_p[0], 0);
  FIELD("pPluck", o.snd_p[1], 0);
  FIELD("pPerc", o.snd_p[2], 0);
  FIELD("pBall2", o.snd_p[3], 0);
  FIELD("pBass", o.snd_p[4], 0);
  FIELD("pPad", o.snd_p[5], 0);
  FIELD("pDrums", o.snd_p[6], 0);
  FIELD("pMix", o.snd_mix[arcade_12::G_PRALL], SERIALISE_PRESET_IS_MIX_PRESET);
  OBJECT_END(s);
  return true;
}

bool arcade_12::on_serialise(serialiser_t &s, int version) {
  (void)version;
  return serialise(s, *this);
}

static bool serialise_arcade_settings(serialiser_t &s, arcade_12 &o) {
  OBJECT_BEGIN(s);
  FIELD("kRoot", o.klotz.root, 0u, 11u);
  FIELD("kProg", o.klotz.prog, 0u, 2u);
  FIELD("kScale", o.klotz.scl, 0u, 12u);
  FIELD("kMidi", o.klotz.midi_on, 0u, 1u);
  FIELD("pRoot", o.prall.root, 0u, 11u);
  FIELD("pScale", o.prall.scale_i, 0u, 12u);
  FIELD("pMidi", o.prall.midi_on, 0u, 1u);
  FIELD("kHs", o.hs_score[0]);
  FIELD("kHl", o.hs_level[0]);
  FIELD("kHn", o.hs_lines[0]);
  FIELD("pHs", o.hs_score[1]);
  FIELD("pHl", o.hs_level[1]);
  OBJECT_END(s);
  return true;
}

bool arcade_12::on_serialise_settings(serialiser_t &s, int version) {
  (void)version;
  return serialise_arcade_settings(s, *this);
}
