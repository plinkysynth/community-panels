/*
@Name: N3VE
@Author: VRCVS (Filippo Rieder)
@Description: Generative ambient instrument for the Chords faceplate, ported from the norns script NEVE. Snow falls in the
  sky and plays low notes when it lands, tears answer with a melody, and a third voice is yours on a keyboard that
  always fits the chord. Tap the sky to drop flakes, choose chords on the top row, play phrases and the tears reply.
@Documentation: https://github.com/vrcvs/plinky12_n3ve
@Discussion: https://github.com/vrcvs/plinky12_n3ve/issues
@Tags: generative, ambient, harmony, midi, norns
@Preferred Panels: chords
@Category: generative

Version 0.2 (beta)
N3VE by VRCVS - https://www.instagram.com/vrcvs/
Manual (IT/EN): see the N3VE PDFs at https://github.com/vrcvs/plinky12_n3ve

CREDITS
  Port of NEVE, the norns script by VRCVS (https://github.com/vrcvs/NEVE, MIT).
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
  Three voices, three sounds: SNOW (low notes, white), TEARS (melody, cyan), LIVE (your keyboard, pink).
  Row 0       (12 pads: the last 4 are the logo) x0-6 chord degrees (the chord changes at the end of the bar; dim =
              unstable chord, plays the nearest stable) | x7 FREEZE (hold the chord) | x8 next chord
              x9 cell, x10 arpeggio, x11 snow (styles)
  Row 1       x0-7 DENSITY (how much snow) | x8-15 WIND (left / right, tap the lit end again = calm)
  Rows 2-7    SKY: tap = drop a flake from that height (lower = sooner, bottom row = next step), it plays when it lands;
              left = low root, right = higher chord tones. Hold = it keeps snowing there. Pressure = dynamics.
  Rows 8-13   LIVE keyboard, always in scale: one column = one scale step, one row up = about a fourth.
              Bright pads = notes of the current chord. Pressure = dynamics, slide = glide.
              REPLY on: play 3-6 notes and pause, the tears take your phrase as their motif and vary it.
  Row 14/15   triangles: x0 octave of the selected voice | x1 key | x2 scale (12 scales)
  Row 14      x7 arpeggio direction | x8 snow, x9 tears, x10 live: select the voice for octave, mute, MIX, SND, PRE
              x11 mute the selected voice (a muted voice blinks; muted live still teaches the tears)
              x12 MIX | x13 SND | x14 PRE | x15 REPLY (call and response, on by default)
  PRE         every voice can have its own sound: system picker (factory and saved presets); the blue pad opens
              your banks on the SD card (/PLINKY/BANKS: your presets and multisamples, e.g. from Bank Builder).
              Touch a preset to hear it, play = load it on the selected voice. Very large multisample banks load slowly.
  Row 15      x5-7 grid 1/4 1/8 1/16 | x8-11 chord length 1 / 2 / 4 / 8 bars | x12 PARAM | x13 progression
              x14 stop | x15 play (the generative voices run while the transport plays; the sky and keyboard always play)
  MIX         per voice (snow, tears, live from the left): volume, pan, delay send, reverb send. Bottom row, under each
              voice: MIDI out (white = Plinky only, green = Plinky + MIDI, blue = MIDI only), MIDI channel - and +.
              MIDI out on port 1 (USB + TRS), channels 1 2 3 by default; the live pressure is sent as aftertouch.
  PARAM       sliders: frequency, melodic variation, rhythmic variation, velocity min, velocity max, note length
  Right side button down: save/load (settings, the three sounds and the mix).
*/
#define PANEL_PAD_COLOR WHITE // colour of N3VE in the panel picker

static constexpr int NSCALES = 12;
static const scale_t k_scales[NSCALES] = {
    SCALE_DORIAN, SCALE_MINOR, SCALE_PHRYGIAN, SCALE_LOCRIAN, SCALE_HARMONIC_MINOR, SCALE_MELODIC_MINOR,
    SCALE_MAJOR, SCALE_LYDIAN, SCALE_MIXOLYDIAN, SCALE_PENTATONIC_MINOR, SCALE_PENTATONIC_MAJOR, SCALE_HIRAJOSHI,
};
static const char *k_scale_names[NSCALES] = {"scale: dorian", "scale: minor", "scale: phrygian", "scale: locrian",
                                             "scale: harmonic minor", "scale: melodic minor", "scale: major",
                                             "scale: lydian", "scale: mixolydian", "scale: pentatonic minor",
                                             "scale: pentatonic major", "scale: hirajoshi"};
// minor-type scales in cool colours, major-type in warm colours, pentatonic / hirajoshi in pink and white
static const uint32_t k_scale_col[NSCALES] = {CYAN, TEAL, BLUE, PURPLE, DIMMER(CYAN), DIMMER(TEAL),
                                              YELLOW, ORANGE, RED, PINK, DIMMER(PINK), WHITE};
// key: naturals C red, D orange, E yellow, F green, G cyan, A blue, B purple; sharps = the natural below, dimmer
static const uint32_t k_key_col[12] = {RED, DIMMER(RED), ORANGE, DIMMER(ORANGE), YELLOW, GREEN,
                                       DIMMER(GREEN), CYAN, DIMMER(CYAN), BLUE, DIMMER(BLUE), PURPLE};
static const uint32_t k_voice_col[3] = {WHITE, CYAN, PINK}; // snow, tears, live
static const char *k_voice_names[3] = {"snow (low notes)", "tears (melody)", "live (keyboard)"};
static const uint8_t k_quant16[3] = {4, 2, 1};          // grid in 16ths: 1/4, 1/8, 1/16
static const uint8_t k_harm_bars[4] = {1, 2, 4, 8};
static const uint8_t k_density[8] = {1, 2, 3, 5, 8, 12, 16, 24}; // max flakes falling at once
static const char *k_prog_names[7] = {"harmony: auto (fits the scale)", "harmony: i VI III VII", "harmony: i VII VI VII",
                                      "harmony: i iv VI v", "harmony: i III iv VI", "harmony: static", "harmony: wandering"};
static const uint32_t k_prog_col[7] = {WHITE, ORANGE, YELLOW, GREEN, CYAN, BLUE, PURPLE};
static const int8_t k_progs[5][4] = {{0, 5, 2, 6}, {0, 6, 5, 6}, {0, 3, 5, 4}, {0, 2, 3, 5}, {0, 0, 0, 0}};
static const int8_t k_prog_len[5] = {4, 4, 4, 4, 1};

// SOUND BROWSER (PRE, then the blue pad): your own presets and multisample banks from /PLINKY/BANKS on the SD card
static constexpr int MAXCATS = 16, MAXBANKS = 64;
static const uint32_t k_cat_col[8] = {PINK, CYAN, ORANGE, GREEN, YELLOW, BLUE, PURPLE, TEAL};
static const char *k_fallback_cat[2] = {"USER1", "USER2"};
static const char *k_fallback_bank[8] = {"BANK1", "BANK2", "BANK3", "BANK4", "BANK5", "BANK6", "BANK7", "BANK8"};

static inline int nv_min(int a, int b) { return a < b ? a : b; }
static inline int nv_max(int a, int b) { return a > b ? a : b; }
static inline int nv_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int nv_mod(int a, int n) { int r = a % n; return r < 0 ? r + n : r; }
static inline float nv_rand(void) { return (float)(rand() & 0xffff) / 65536.f; }

struct n3ve : panel_t {
  enum { SNOW = 0, TEARS = 1, LIVE = 2, NV = 3 };
  enum { KW = 16, KH = 6, KPADS = KW * KH, KY0 = 8 }; // live keyboard: rows 8-13
  enum { SKY0 = 2, SKY1 = 7 };                        // sky: rows 2-7
  enum { MAXFLAKES = 32, MAXDROPS = 8, MAXNOTES = 20, MAXSOUND = 24, MAXSCALE = 64 };
  enum { MODE_PLAY, MODE_MIX, MODE_SOUND, MODE_PRESET, MODE_PARAM };

  // ---------------- saved settings ----------------
  uint8_t scale_idx, root_pc, live_oct; // octaves: 3 = no shift
  uint8_t snow_oct, tears_oct;
  uint8_t style;                        // 0 cell, 1 arpeggio, 2 snow (random)
  uint8_t arp_dir;                      // 0 up, 1 down, 2 up & down
  uint8_t prog;                         // see k_prog_names
  uint8_t quant_idx, harm_idx, density_idx, wind; // wind: 0..8, 4 = calm
  uint8_t freeze, reply, mute_snow, mute_tears, mute_live;
  uint8_t p_freq;  // note frequency x100 (20..400)
  uint8_t p_melvar, p_rvar; // 0..100
  uint8_t p_vmin, p_vmax;   // 1..127
  uint8_t p_nlen;           // note length in tenths of a second (3..120)
  uint8_t midi_mode[3];     // per voice: 0 = Plinky only, 1 = Plinky + MIDI out, 2 = MIDI out only
  uint8_t midi_ch[3];       // per voice: MIDI channel, 0-based (snow 1, tears 2, live 3)

  // ---------------- scale & harmony ----------------
  int8_t iv[12];
  int per_oct;
  uint8_t sc[MAXSCALE]; // scale notes over 4 octaves (+1): low half = snow, high half = tears
  int nsc, bass_n, lead_lo, lead_hi, live_step;
  bool good[12];
  int8_t auto_seq[8];
  int n_auto;
  int center, prog_i, bass_step;
  bool harm_flag;
  int32_t harm_due;
  volatile int8_t pending_chord; // chosen on the top row (-2 = next of the progression): applied at the end of the bar

  // ---------------- melody (tears) ----------------
  int8_t motif[8];
  int mlen, motif_i, motif_reps, motif_lock, anchor, lead_pos;
  bool motif_scale;
  int slow_var;
  int8_t learn_buf[6];
  int32_t learn_tick[6]; // when each note of your phrase was played (16ths)
  int learn_n;
  uint32_t learn_t;

  // ---------------- clock ----------------
  clock_divider_t clk;
  int32_t tick16, bar_origin;
  uint32_t t_us;
  int32_t snow_next, tear_next, last_bass_tick;
  // slow cell / arpeggio scheduling (in 16ths)
  int32_t slow_cycle, slow_next;
  int8_t slow_slots[32];
  uint8_t slow_kinds[32]; // 0 cell, 1 arp, 2 top
  int n_slots, slot_i, slow_kind;
  int arp_k, arp_base;
  int32_t tear_q_at[8];
  uint8_t tear_q_kind[8];
  int n_tear_q;

  // ---------------- objects ----------------
  struct flake_t { int32_t spawn_q8, land_q8; int8_t col, row0; uint8_t vel; uint8_t flags; }; // flags: 1 active, 2 manual
  flake_t flakes[MAXFLAKES];
  struct drop_t { int32_t start_q8; int8_t col; bool active; };
  drop_t drops[MAXDROPS];
  uint32_t splash_until[16];
  struct gnote_t { uint32_t id, end_us; uint8_t note, vel, voice; bool active, started, synth, midi_wait; }; // synth: still on a Plinky voice
  gnote_t gnotes[MAXNOTES];
  uint32_t note_seq;
  struct sound_t { uint32_t end_us; uint8_t note; };
  sound_t sounding[MAXSOUND];
  uint32_t flash_until[128];
  uint8_t flash_voice[128];

  // ---------------- touch (written in on_ui) ----------------
  uint8_t live_vel[KPADS]; // 0 = not held
  uint8_t live_src[KPADS];
  uint8_t live_prev[KPADS]; // pads sounding last frame (for learn)
  int8_t sky_row[16];       // -1 = column not touched
  uint8_t sky_vel[16];
  volatile uint16_t sky_press; // columns just pressed
  int32_t sky_last[16];

  // ---------------- UI ----------------
  uint8_t mode, sel;
  voice_allocator_t va;
  preset_pages_t pp;
  panel_page_t song_page;
  slider_t mix_sliders[NV][4];
  slider_t par_sliders[6];
  uint32_t flash_us[3];
  uint32_t midi_help_until; // MIX: shows the MIDI setting of the voice you just changed
  uint8_t midi_help_v;
  // PRE: system picker (factory and saved presets) or SD browser (your banks: presets and multisamples)
  bool factory_picker;
  char cat_names[MAXCATS][17]; // category folders found in /PLINKY/BANKS
  int8_t ncat;                 // -1 = not scanned yet, 0 = listing not possible (fallback)
  char bank_names[MAXBANKS][17];
  int8_t nbank;
  uint8_t cur_cat;
  int8_t cur_bank;
  uint64_t preset_mask, preset_bad;
  char tr_cat[NV][17], tr_bank[NV][17]; // what each voice has loaded from the SD browser
  int8_t tr_preset[NV];
  int8_t cand_row, cand_col;  // auditioned preset waiting for LOAD (-1 = none)
  volatile int8_t preview_vel; // >0 while a preset pad is held: the preview plays on spare voice 11
  bool preview_on;
  // memory: Plinky keeps the samples of every loaded sound in 14 MB of flash. When the voice already has one of your
  // banks, the audition happens on the voice's own slot (its old bank can be freed) and cancel reloads the old one;
  // otherwise it uses the spare slot 11. So auditioning never needs room for one bank more than the voices use.
  volatile int8_t aud_slot;
  bool aud_inplace;
  uint8_t inplace_v;
  char prev_cat[17], prev_bank[17];
  int8_t prev_idx;

  // =========================================================
  // SCALE AND HARMONY (from NEVE.lua)
  // =========================================================
  scale_t cur_scale(void) const { return k_scales[scale_idx % NSCALES]; }
  uint8_t &voice_mute_ref(int v) { return v == SNOW ? mute_snow : v == TEARS ? mute_tears : mute_live; }
  bool voice_muted(int v) const { return v == SNOW ? mute_snow : v == TEARS ? mute_tears : mute_live; }
  uint8_t &voice_oct(int v) { return v == SNOW ? snow_oct : v == TEARS ? tears_oct : live_oct; }
  void chord_offsets(int &c1, int &c2) const { c1 = (4 * per_oct + 7) / 14; c2 = (8 * per_oct + 7) / 14; }
  int map_deg(int d) const { return nv_mod((2 * d * per_oct + 7) / 14, per_oct); }

  void build_scale(void) {
    scale_t s = cur_scale();
    per_oct = 0;
    for (int i = 0; i < 12; ++i) if (s & (1 << i)) iv[per_oct++] = (int8_t)i;
    if (per_oct < 2) { per_oct = 7; for (int i = 0; i < 7; ++i) iv[i] = (int8_t)degree_to_note(i, SCALE_MINOR); }
    int root = 36 + root_pc;
    nsc = 0;
    for (int o = 0; o < 4; ++o)
      for (int i = 0; i < per_oct && nsc < MAXSCALE - 1; ++i) sc[nsc++] = (uint8_t)(root + o * 12 + iv[i]);
    sc[nsc++] = (uint8_t)(root + 48);
    bass_n = 2 * per_oct;
    lead_lo = 2 * per_oct;
    lead_hi = nsc - 1;
    lead_pos = nv_clamp(lead_pos, lead_lo, lead_hi);
    center = nv_mod(center, per_oct);
    compute_chords();
    live_step = nv_max(2, (6 * per_oct + 7) / 14);
  }

  int chord_level(int d) const {
    int n = per_oct, c1, c2;
    chord_offsets(c1, c2);
    int r = iv[nv_mod(d, n)];
    int n1 = iv[(d + c1) % n] + ((d + c1) >= n ? 12 : 0);
    int n2 = iv[(d + c2) % n] + ((d + c2) >= n ? 12 : 0);
    int th = nv_mod(n1 - r, 12), fi = nv_mod(n2 - r, 12);
    if ((th == 3 || th == 4) && fi == 7) return 1;
    int ics[3] = {th, fi, nv_mod(fi - th, 12)};
    for (int i = 0; i < 3; ++i) if (ics[i] == 1 || ics[i] == 6 || ics[i] == 11) return 3;
    return 2;
  }

  int safe_center(int c) const {
    c = nv_mod(c, per_oct);
    if (good[c]) return c;
    static const int8_t offs[8] = {-2, 2, -1, 1, 3, -3, 4, -4};
    for (int i = 0; i < 8; ++i) { int d = nv_mod(c + offs[i], per_oct); if (good[d]) return d; }
    return c;
  }

  int take_chord(int from, const int8_t *cands, bool *used) {
    int n = per_oct;
    for (int i = 0; i < 6; ++i) {
      int d = nv_mod(from + map_deg(cands[i]), n);
      if (good[d] && !used[d]) { used[d] = true; if (n_auto < 8) auto_seq[n_auto++] = (int8_t)d; return d; }
    }
    for (int d = 0; d < n; ++d)
      if (good[d] && !used[d]) { used[d] = true; if (n_auto < 8) auto_seq[n_auto++] = (int8_t)d; return d; }
    return from;
  }

  void compute_chords(void) {
    int n = per_oct, lv[12], cnt[4] = {0, 0, 0, 0};
    for (int d = 0; d < n; ++d) { lv[d] = chord_level(d); cnt[lv[d]]++; }
    int limit = 1;
    if (cnt[1] < 3) limit = 2;
    if (cnt[1] + cnt[2] < 2) limit = 3;
    for (int d = 0; d < 12; ++d) good[d] = d < n && lv[d] <= limit;
    good[0] = true;
    bool used[12] = {};
    used[0] = true;
    n_auto = 0;
    auto_seq[n_auto++] = 0;
    static const int8_t ca[6] = {5, 3, 2, 4, 1, 6}, cb[6] = {3, 5, 2, 4, 1, 6}, cc[6] = {6, 4, 3, 6, 4, 3};
    int a = take_chord(0, ca, used);
    take_chord(a, cb, used);
    take_chord(0, cc, used);
    prog_i = 0;
    center = safe_center(center);
  }

  int harm16(void) const { return k_harm_bars[harm_idx & 3] * 16; }
  int32_t align_up(int32_t t, int q) const { // next multiple of q (relative to the bar origin), >= t
    int32_t rel = t - bar_origin;
    if (rel < 0) rel = 0;
    return bar_origin + ((rel + q - 1) / q) * q;
  }
  void schedule_harmony(void) {
    int32_t rel = tick16 - bar_origin;
    if (rel < 0) rel = 0;
    if (style != 2) { // cell / arpeggio: chords change at the start of a 2-bar cell
      int period = nv_max(32, ((harm16() + 16) / 32) * 32);
      harm_due = bar_origin + (rel / 32) * 32 + period;
      if (harm_due <= tick16 + 8) harm_due += 32;
    } else {
      harm_due = bar_origin + (rel / 16) * 16 + harm16();
      if (harm_due <= tick16 + 4) harm_due += 16;
    }
  }
  void next_harmony(void) {
    if (prog == 6) { // wandering: consonant jumps
      static const int8_t moves[4] = {2, 3, 4, -1};
      center = safe_center(center + moves[rand() & 3]);
    } else if (prog == 0) {
      int c = center;
      if (n_auto > 0) { prog_i = (prog_i + 1) % n_auto; c = auto_seq[prog_i]; }
      if (c == center && n_auto > 1) { prog_i = (prog_i + 1) % n_auto; c = auto_seq[prog_i]; }
      center = c;
    } else {
      int p = nv_clamp(prog - 1, 0, 4), L = k_prog_len[p];
      prog_i = (prog_i + 1) % L;
      int c = safe_center(map_deg(k_progs[p][prog_i]));
      if (c == center && L > 1) { prog_i = (prog_i + 1) % L; c = safe_center(map_deg(k_progs[p][prog_i])); }
      center = c;
    }
    chord_changed();
  }
  void set_harmony(int deg) {
    center = safe_center(deg);
    chord_changed();
  }
  void chord_changed(void) {
    bass_step = 0;
    harm_flag = true;
    last_bass_tick = -1000;
    // notes of the previous chord still ring out, but they no longer steer the melody
    for (int i = 0; i < MAXSOUND; ++i) sounding[i].end_us = t_us;
    schedule_harmony();
  }
  bool is_chord(int pos) const {
    int c1, c2;
    chord_offsets(c1, c2);
    int d = nv_mod(pos, per_oct);
    return d == center || d == (center + c1) % per_oct || d == (center + c2) % per_oct;
  }

  // =========================================================
  // SOUNDING NOTES + CONSONANCE FILTER
  // =========================================================
  void add_sounding(int note, float dur_s) {
    if (dur_s > 8.f) dur_s = 8.f;
    int slot = 0;
    uint32_t best = 0xffffffffu;
    for (int i = 0; i < MAXSOUND; ++i) {
      if ((int32_t)(sounding[i].end_us - t_us) <= 0) { slot = i; break; }
      if (sounding[i].end_us < best) { best = sounding[i].end_us; slot = i; }
    }
    sounding[slot].note = (uint8_t)note;
    sounding[slot].end_us = t_us + (uint32_t)(dur_s * 1000000.f);
  }
  bool consonant(int note, bool allow_color) const {
    static const uint16_t CONS = (1 << 0) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 9);
    for (int i = 0; i < MAXSOUND; ++i) {
      if ((int32_t)(sounding[i].end_us - t_us) <= 0) continue;
      int ic = nv_mod(note - sounding[i].note, 12);
      if (!(CONS & (1 << ic)) && !(allow_color && (ic == 2 || ic == 10))) return false;
    }
    for (int p = 0; p < KPADS; ++p) { // the notes you are holding count too
      if (!live_vel[p]) continue;
      int ic = nv_mod(note - live_note(p), 12);
      if (!(CONS & (1 << ic)) && !(allow_color && (ic == 2 || ic == 10))) return false;
    }
    return true;
  }

  // =========================================================
  // MELODY: snow (bass) and tears (lead)
  // =========================================================
  int pick_bass(void) {
    static const int8_t PAT[4] = {0, 2, 0, 1}; // 0 root, 1 third, 2 fifth: root, fifth, root, third
    bass_step++;
    int c1, c2;
    chord_offsets(c1, c2);
    int offs[3] = {0, c1, c2};
    int order[4] = {PAT[(bass_step - 1) & 3], 0, 2, 1};
    int fallback = -1;
    for (int i = 0; i < 4; ++i) {
      int idx = nv_clamp(center + offs[order[i]], 0, bass_n - 1);
      if (fallback < 0) fallback = idx;
      if (consonant(sc[idx], false)) return idx;
    }
    return fallback;
  }
  // sky column -> chord tone in the bass register (two columns per tone: root, third, fifth, root+8va...)
  int col_bass_idx(int col) const {
    int c1, c2;
    chord_offsets(c1, c2);
    int j = nv_clamp(col, 0, 15) / 2;
    int k = j % 3, o = j / 3;
    int idx = center + (k == 0 ? 0 : k == 1 ? c1 : c2) + o * per_oct;
    return nv_clamp(idx, 0, nsc - 1);
  }
  int bass_idx_col(int idx) const {
    for (int c = 0; c < 16; c += 2) if (col_bass_idx(c) == idx) return c + (rand() & 1);
    return nv_clamp((idx - center) * 16 / nv_max(1, bass_n), 0, 15);
  }
  int snap_chord(int pos) const {
    static const int8_t offs[7] = {0, 1, -1, 2, -2, 3, -3};
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 0; i < 7; ++i) {
        int p = pos + offs[i];
        if (p >= lead_lo && p <= lead_hi && is_chord(p) && (pass == 1 || consonant(sc[p], false))) return p;
      }
    return pos;
  }
  int chord_tones(int8_t *ct) const {
    int lo = lead_lo, hi = lead_hi;
    if (style != 2) {
      lo = lead_lo + (3 * per_oct) / 10;
      hi = nv_min(lead_hi, lo + per_oct + 3);
      if (style == 1) { lo = lead_lo; hi = nv_min(lead_hi, lo + (18 * per_oct) / 10); }
    }
    int n = 0;
    for (int p = lo; p <= hi && n < 24; ++p) if (is_chord(p)) ct[n++] = (int8_t)p;
    return n;
  }
  float melvar(void) const { return p_melvar / 100.f; }
  void new_motif(void) {
    float var = melvar();
    int len = (style != 2) ? 4 : 3 + (rand() % 3);
    slow_var = 1 + rand() % 3;
    motif[0] = 0;
    int cur = 0;
    for (int i = 1; i < len; ++i) {
      float r = nv_rand();
      int step = r < 0.45f ? -1 : r < 0.70f ? 1 : r < 0.82f ? -2 : 2;
      if (style == 2 && nv_rand() < var * 0.12f) step = (nv_rand() < 0.5f ? -1 : 1) * 3;
      cur += step;
      motif[i] = (int8_t)cur;
    }
    mlen = len;
    motif_reps = 0;
    motif_scale = false;
  }
  void motif_range(int &mn, int &mx) const {
    mn = 0; mx = 0;
    for (int i = 0; i < mlen; ++i) { mn = nv_min(mn, motif[i]); mx = nv_max(mx, motif[i]); }
  }
  // pick one of the 3 nearest candidates (50% / 30% / 20%)
  static int pick_near(const int16_t *val, const int16_t *dist, int n) {
    int b[3] = {-1, -1, -1};
    for (int i = 0; i < n; ++i) {
      for (int k = 0; k < 3; ++k) {
        if (b[k] < 0 || dist[i] < dist[b[k]]) {
          for (int m = 2; m > k; --m) b[m] = b[m - 1];
          b[k] = i;
          break;
        }
      }
    }
    float r = nv_rand();
    int k = r < 0.5f ? 0 : r < 0.8f ? 1 : 2;
    while (k > 0 && b[k] < 0) --k;
    return b[k] < 0 ? -1 : val[b[k]];
  }
  int choose_anchor(void) {
    int mn, mx;
    motif_range(mn, mx);
    int range = lead_hi - lead_lo, target = lead_pos;
    if (lead_pos < lead_lo + range * 3 / 10) target = lead_lo + range * 8 / 10;
    int16_t val[40], dist[40];
    for (int pass = 0; pass < 2; ++pass) {
      int n = 0;
      for (int p = lead_lo - mn; p <= lead_hi - mx && n < 40; ++p)
        if (p >= lead_lo && p <= lead_hi && is_chord(p) && (pass == 1 || consonant(sc[p], false))) {
          val[n] = (int16_t)p; dist[n] = (int16_t)abs(p - target); ++n;
        }
      if (n) return pick_near(val, dist, n);
    }
    return nv_clamp(target, lead_lo, lead_hi);
  }
  int choose_anchor_ct(const int8_t *ct, int n) { // returns an index into ct
    int mn, mx;
    motif_range(mn, mx);
    int lo = -mn, hi = n - 1 - mx;
    if (hi < lo) return nv_clamp(n / 2, 0, n - 1);
    int target = 0;
    for (int i = 0; i < n; ++i) if (ct[i] <= lead_pos) target = i;
    if (target < n * 35 / 100) target = nv_min(n - 1, target + 3);
    if (target > n * 85 / 100) target -= 1;
    int16_t val[24], dist[24];
    int m = 0;
    for (int i = lo; i <= hi && m < 24; ++i)
      if (ct[i] != lead_pos || hi == lo || style != 2) { val[m] = (int16_t)i; dist[m] = (int16_t)abs(i - target); ++m; }
    if (!m) return nv_clamp(n / 2, 0, n - 1);
    return pick_near(val, dist, m);
  }
  void start_phrase(void) {
    float var = melvar();
    harm_flag = false;
    if (motif_lock > 0) {
      if (motif_i >= mlen) motif_lock--;
      motif_reps = 0;
    } else if (style != 2) {
      motif_reps++;
      int max_reps = 4 + (int)(4 * (1 - var));
      if (motif_reps > max_reps) new_motif();
      else if (nv_rand() < 0.25f + var * 0.5f) motif[mlen - 1] += (nv_rand() < 0.5f ? -1 : 1);
    } else {
      motif_reps++;
      int max_reps = 3 + (int)(3 * (1 - var));
      if (motif_reps > max_reps || nv_rand() < var * 0.10f) new_motif();
      else {
        if (nv_rand() < var * 0.25f) for (int i = 1; i < mlen; ++i) motif[i] = (int8_t)-motif[i];
        if (mlen > 3 && nv_rand() < var * 0.3f) mlen--;
      }
    }
    if (motif_scale) anchor = choose_anchor();
    else { int8_t ct[24]; int n = chord_tones(ct); anchor = n ? choose_anchor_ct(ct, n) : 0; }
    motif_i = 0;
  }
  int pick_tear_note(bool &done) {
    float var = melvar();
    if (motif_i >= mlen || (harm_flag && style == 2)) start_phrase();
    int pos;
    bool strong = (motif_i == 0) || (motif_i == mlen - 1);
    bool color = nv_rand() < var * 0.5f;
    if (motif_scale) {
      pos = nv_clamp(anchor + motif[motif_i], lead_lo, lead_hi);
      if (strong) pos = snap_chord(pos);
      if (!consonant(sc[pos], color)) {
        static const int8_t o4[4] = {-1, 1, -2, 2};
        for (int i = 0; i < 4; ++i) {
          int p = pos + o4[i];
          if (p >= lead_lo && p <= lead_hi && consonant(sc[p], color) && (!strong || is_chord(p))) { pos = p; break; }
        }
      }
    } else {
      int8_t ct[24];
      int n = chord_tones(ct);
      if (!n) { done = true; return lead_lo; }
      int mn, mx;
      motif_range(mn, mx);
      if (mx - mn < n) anchor = nv_clamp(anchor, -mn, n - 1 - mx); // the chord changed mid-phrase: keep its shape
      int ci = nv_clamp(anchor + motif[motif_i], 0, n - 1);
      pos = ct[ci];
      if (!consonant(sc[pos], false)) {
        static const int8_t o4[4] = {-1, 1, -2, 2};
        for (int i = 0; i < 4; ++i) {
          int j = ci + o4[i];
          if (j >= 0 && j < n && consonant(sc[ct[j]], false)) { pos = ct[j]; break; }
        }
      }
      if (!strong && color && nv_rand() < 0.5f) {
        int p = pos + (nv_rand() < 0.5f ? 1 : -1);
        if (p >= lead_lo && p <= lead_hi && consonant(sc[p], true)) pos = p;
      }
    }
    lead_pos = pos;
    done = (motif_i == mlen - 1);
    motif_i++;
    return pos;
  }
  // REPLY: the last notes you played (3 to 6) become the motif of the tears
  void learn_add(int idx) {
    if (learn_n == 6) {
      for (int i = 0; i < 5; ++i) { learn_buf[i] = learn_buf[i + 1]; learn_tick[i] = learn_tick[i + 1]; }
      learn_n = 5;
    }
    learn_tick[learn_n] = tick16;
    learn_buf[learn_n++] = (int8_t)nv_clamp(idx, -100, 100);
    learn_t = t_us;
  }
  void learn_commit(void) {
    if (learn_n >= 3) {
      for (int i = 0; i < learn_n; ++i) motif[i] = (int8_t)nv_clamp(learn_buf[i] - learn_buf[0], -9, 9);
      mlen = learn_n;
      motif_scale = true;
      motif_i = mlen; // the next tear opens a new phrase
      motif_reps = 0;
      motif_lock = 4;
      // the answer comes soon. snow style: the tears reply right away, with the rhythm of your phrase (on the grid);
      // cell / arpeggio: the phrase becomes the motif of the next cell
      if (style == 2) {
        int q = k_quant16[quant_idx % 3];
        int32_t start = align_up(tick16 + 4, q), last = start;
        n_tear_q = 0;
        for (int i = 0; i < learn_n && n_tear_q < 8; ++i) {
          int32_t at = align_up(start + (learn_tick[i] - learn_tick[0]), q);
          if (at <= last && i > 0) at = last + q;
          tear_q_at[n_tear_q] = at;
          tear_q_kind[n_tear_q++] = 0;
          last = at;
        }
        tear_next = last + 8;
      }
    }
    learn_n = 0;
  }

  int rand_vel(void) const {
    int a = p_vmin, b = p_vmax;
    if (a > b) { int t = a; a = b; b = t; }
    return a + (b > a ? rand() % (b - a + 1) : 0);
  }
  float note_len(void) const { return p_nlen / 10.f; }
  float freq(void) const { return nv_max(10, p_freq) / 100.f; }

  // =========================================================
  // NOTES
  // =========================================================
  void play_note(int voice, int note, int vel, float dur_s) {
    if (voice == SNOW) note += 12 * ((int)snow_oct - 3); // octave of the voice (the harmony does not change)
    else if (voice == TEARS) note += 12 * ((int)tears_oct - 3);
    while (note > 108) note -= 12;
    while (note < 12) note += 12;
    int slot = -1;
    uint32_t oldest = 0xffffffffu;
    for (int i = 0; i < MAXNOTES; ++i) {
      if (!gnotes[i].active) { slot = i; break; }
      if (gnotes[i].end_us < oldest) { oldest = gnotes[i].end_us; slot = i; }
    }
    // the same note of the same voice still sounding: end it, so MIDI sends a note off and a new attack
    bool repeat = false;
    for (int i = 0; i < MAXNOTES; ++i)
      if (i != slot && gnotes[i].active && gnotes[i].voice == voice && gnotes[i].note == note) { gnotes[i].active = false; repeat = true; }
    gnote_t &g = gnotes[slot];
    g.midi_wait = repeat; // skip one frame on MIDI: note off, then note on
    g.id = 0x10000000u | (note_seq++ & 0x0fffffffu);
    g.note = (uint8_t)note;
    g.vel = (uint8_t)nv_clamp(vel, 1, 127);
    g.voice = (uint8_t)voice;
    g.end_us = t_us + (uint32_t)(dur_s * 1000000.f);
    g.active = true;
    g.started = false;
    g.synth = midi_mode[voice % 3] != 2;
    flash_until[note & 127] = t_us + 400000;
    flash_voice[note & 127] = (uint8_t)voice;
  }
  int live_note(int pad) const {
    int x = pad % KW, r = KH - 1 - pad / KW; // r = 0 bottom row
    int idx = x + r * live_step;
    int n = 48 + root_pc + 12 * ((int)live_oct - 3) + degree_to_note(idx, cur_scale());
    while (n > 108) n -= 12;
    while (n < 12) n += 12;
    return n;
  }
  int live_idx(int pad) const { int x = pad % KW, r = KH - 1 - pad / KW; return x + r * live_step; }

  // =========================================================
  // SNOW AND TEARS
  // =========================================================
  int32_t now_q8(void) const { return tick16 * 256 + (clk.phase_q16() >> 8); }
  int add_flake(int col, int row0, int vel, int32_t land_tick, bool manual) {
    int slot = -1;
    for (int i = 0; i < MAXFLAKES; ++i) if (!(flakes[i].flags & 1)) { slot = i; break; }
    if (slot < 0) return -1;
    flake_t &f = flakes[slot];
    f.spawn_q8 = now_q8();
    f.land_q8 = land_tick * 256;
    if (f.land_q8 <= f.spawn_q8) f.land_q8 = f.spawn_q8 + 1;
    f.col = (int8_t)col;
    f.row0 = (int8_t)row0;
    f.vel = (uint8_t)vel;
    f.flags = 1 | (manual ? 2 : 0);
    return slot;
  }
  int count_gen_flakes(void) const {
    int n = 0;
    for (int i = 0; i < MAXFLAKES; ++i) if ((flakes[i].flags & 3) == 1) ++n;
    return n;
  }
  int drift_cols(float p) const { return (int)floorf(((int)wind - 4) / 4.f * 2.5f * p + 0.5f); }
  void spawn_gen_flake(void) {
    float v = p_rvar / 100.f;
    int fall = (int)(16 * (1 + (nv_rand() * 2 - 1) * 0.4f * v));
    int q = k_quant16[quant_idx % 3];
    int32_t land = align_up(tick16 + fall, q);
    while (land - tick16 < 6) land += q;
    add_flake(rand() & 15, SKY0, rand_vel(), land, false);
  }
  void manual_flake(int col, int row, int vel) {
    int q = k_quant16[quant_idx % 3];
    int dist = SKY1 - row; // 0..5
    int32_t land = align_up(tick16 + 1 + dist * 2, q);
    add_flake(col, row, vel, land, true);
  }
  void land(flake_t &f) {
    int col = nv_clamp(f.col + drift_cols(1.f), 0, 15);
    float nl = note_len();
    if (f.flags & 2) { // your flake: always plays the tone of its column
      int idx = col_bass_idx(col);
      if (!mute_snow) { play_note(SNOW, sc[idx], f.vel, nl * 1.6f); add_sounding(sc[idx], nl * 1.6f); }
      splash_until[col] = t_us + 220000;
      return;
    }
    if (style != 2) { // pedal: root at the start of the chord, the fifth later; other flakes splash silently
      int gap = (int)(nv_max(32, harm16()) * 0.4f);
      if (tick16 - last_bass_tick < gap) { splash_until[col] = t_us + 120000; return; }
      last_bass_tick = tick16;
      int c1, c2;
      chord_offsets(c1, c2);
      int kind = (bass_step % 2 == 0) ? 0 : c2;
      bass_step++;
      int idx = nv_clamp(center + kind, 0, bass_n - 1);
      float dur = nl * 2.5f < 6.f ? 6.f : nl * 2.5f;
      if (!mute_snow) { play_note(SNOW, sc[idx], (p_vmin + p_vmax) / 2, dur); add_sounding(sc[idx], dur); }
      splash_until[bass_idx_col(idx)] = t_us + 220000;
      return;
    }
    int idx = pick_bass();
    if (!mute_snow) { play_note(SNOW, sc[idx], rand_vel(), nl * 1.6f); add_sounding(sc[idx], nl * 1.6f); }
    splash_until[bass_idx_col(idx)] = t_us + 220000;
  }
  void add_drop(int pos) {
    for (int i = 0; i < MAXDROPS; ++i)
      if (!drops[i].active) {
        drops[i].active = true;
        drops[i].start_q8 = now_q8();
        drops[i].col = (int8_t)nv_clamp((pos - lead_lo) * 15 / nv_max(1, lead_hi - lead_lo), 0, 15);
        return;
      }
  }
  int32_t next_interval16(float mean16) const {
    float v = p_rvar / 100.f, m = mean16 / freq();
    float rnd = -logf(1.f - nv_rand() * 0.999f);
    float r = m * ((1 - v) + v * rnd);
    return r < 1.f ? 1 : (int32_t)(r + 0.5f);
  }
  // a tear detaches: high note. kind 0 = phrase, 1 = arpeggio, 2 = long top note
  void fire_tear(int kind) {
    int vel = rand_vel();
    float dur = note_len() * 0.8f;
    int pos;
    bool done = false;
    if (kind == 1 || kind == 2) {
      int8_t ct[24];
      int m = chord_tones(ct);
      if (!m) return;
      int n = nv_min(4, m);
      int b = nv_clamp(arp_base, 0, nv_max(0, m - n));
      if (kind == 2) {
        pos = ct[nv_clamp(m - 1 - (rand() & 1), 0, m - 1)];
        dur = note_len() * 2.2f;
        vel = nv_min(127, vel * 11 / 10);
      } else {
        int8_t L[8];
        int nl = 0;
        if (arp_dir == 0) for (int i = 0; i < n; ++i) L[nl++] = (int8_t)i;
        else if (arp_dir == 1) for (int i = n - 1; i >= 0; --i) L[nl++] = (int8_t)i;
        else { for (int i = 0; i < n; ++i) L[nl++] = (int8_t)i; for (int i = n - 2; i >= 1; --i) L[nl++] = (int8_t)i; }
        arp_k++;
        pos = ct[nv_clamp(b + L[(arp_k - 1) % nl], 0, m - 1)];
        vel = nv_max(1, vel * 3 / 4);
      }
    } else {
      pos = pick_tear_note(done);
    }
    pos = nv_clamp(pos, 0, nsc - 1);
    if (!mute_tears) { play_note(TEARS, sc[pos], vel, dur); add_sounding(sc[pos], dur); }
    add_drop(pos);
    if (done && style == 2) tear_next += (int32_t)(26.f / freq() * (1.2f + nv_rand())); // a breath after the phrase
  }
  void build_cell(void) {
    n_slots = 0;
    if (style == 1) { // arpeggio: steady notes on the chord, a long top note on beat 1 (and maybe beat 3)
      float f = freq();
      int step = f >= 1.6f ? 1 : (f >= 0.7f ? 2 : 4);
      for (int t = 0; t < 28 && n_slots < 32; t += step) {
        slow_slots[n_slots] = (int8_t)t;
        slow_kinds[n_slots] = (t == 0 || (t == 16 && nv_rand() < 0.5f)) ? 2 : 1;
        ++n_slots;
      }
      arp_k = 0;
      arp_base = nv_rand() < 0.3f ? 1 : 0;
    } else { // cell: one note per beat, with a rhythmic variant. The phrase itself starts on its first note,
             // so it is built on the chord that is sounding then (chords change right at the start of a cell)
      int n = nv_min(mlen, 8);
      for (int i = 0; i < n; ++i) { slow_slots[i] = (int8_t)(i * 4); slow_kinds[i] = 0; }
      if (slow_var == 2 && n >= 4) slow_slots[n - 1] -= 2;
      else if (slow_var == 3 && n >= 3) slow_slots[1] += 2;
      n_slots = n;
      motif_i = mlen;
    }
    slot_i = 0;
  }

  // one call per 16th note
  void on_tick16(bool playing) {
    int q = k_quant16[quant_idx % 3];
    // the sky keeps snowing where you hold it
    for (int c = 0; c < 16; ++c)
      if (sky_row[c] >= 0 && tick16 - sky_last[c] >= q && nv_mod(tick16 - bar_origin, q) == 0) {
        manual_flake(c, sky_row[c], sky_vel[c]);
        sky_last[c] = tick16;
      }
    if (!playing) return;
    int32_t rel = tick16 - bar_origin;
    if (rel >= 0 && rel % 16 == 0 && pending_chord != -1) { // chosen chord (or "next"): at the bar line
      if (pending_chord == -2) next_harmony(); else set_harmony(pending_chord);
      pending_chord = -1;
    }
    if (!freeze && tick16 >= harm_due) next_harmony();
    if (freeze && tick16 >= harm_due) schedule_harmony();
    // snow
    if (tick16 >= snow_next) {
      if (count_gen_flakes() < k_density[density_idx & 7]) spawn_gen_flake();
      snow_next = tick16 + next_interval16(80.f / k_density[density_idx & 7]); // denser = more often
    }
    // tears
    if (style != 2) {
      if (slow_next >= 0 && slow_next < tick16 - 8) slow_next = -1;
      if (slow_next < 0) {
        if (slow_cycle < tick16) slow_cycle = align_up(tick16 + 8, 32);
        if (tick16 >= slow_cycle - 6) {
          build_cell();
          if (n_slots) { slow_next = slow_cycle + slow_slots[0]; slow_kind = slow_kinds[0]; slot_i = 1; }
          else slow_cycle += 32;
        }
      } else if (tick16 >= slow_next) {
        fire_tear(slow_kind);
        if (slot_i < n_slots) { slow_next = slow_cycle + slow_slots[slot_i]; slow_kind = slow_kinds[slot_i]; ++slot_i; }
        else { slow_cycle += 32; slow_next = -1; }
      }
    } else {
      if (tick16 >= tear_next) {
        int max_q = nv_clamp(1 + k_density[density_idx & 7] / 8, 1, 4);
        if (n_tear_q < max_q) { tear_q_at[n_tear_q] = align_up(tick16 + 3, q); tear_q_kind[n_tear_q] = 0; ++n_tear_q; }
        tear_next = tick16 + next_interval16(26.f);
      }
      for (int i = 0; i < n_tear_q;) {
        if (tick16 >= tear_q_at[i]) {
          fire_tear(tear_q_kind[i]);
          for (int k = i; k < n_tear_q - 1; ++k) { tear_q_at[k] = tear_q_at[k + 1]; tear_q_kind[k] = tear_q_kind[k + 1]; }
          --n_tear_q;
        } else ++i;
      }
    }
  }

  void restart_generative(int32_t origin) {
    bar_origin = origin;
    snow_next = tick16 + 2;
    tear_next = tick16 + 8;
    slow_cycle = -1;
    slow_next = -1;
    n_tear_q = 0;
    pending_chord = -1;
    schedule_harmony();
  }

  // =========================================================
  // PANEL
  // =========================================================
  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("N3VE by VRCVS v0.2\n");
    scale_idx = 0; root_pc = 2; live_oct = 3; snow_oct = 3; tears_oct = 3; // D dorian, like NEVE on norns
    style = 0; arp_dir = 2; prog = 0;
    quant_idx = 1; harm_idx = 2; density_idx = 3; wind = 4;
    freeze = 0; reply = 1; mute_snow = 0; mute_tears = 0; mute_live = 0;
    p_freq = 100; p_melvar = 25; p_rvar = 35; p_vmin = 30; p_vmax = 85; p_nlen = 30;
    for (int v = 0; v < NV; ++v) { midi_mode[v] = 1; midi_ch[v] = (uint8_t)v; } // Plinky + MIDI, channels 1 2 3
    for (int v = 0; v < NV; ++v) { tr_preset[v] = -1; tr_cat[v][0] = 0; tr_bank[v][0] = 0; }
    reset_runtime();
  }
  // Saved-song loads construct a fresh panel without calling the default-song hook.
  // Initialise transient state on both paths, preserving loaded settings and presets.
  void reset_runtime(void) {
    midi_help_until = 0;
    mode = MODE_PLAY; sel = LIVE;
    factory_picker = true; ncat = -1; nbank = 0; cur_cat = 0; cur_bank = -1; cand_row = -1; cand_col = 0;
    preset_mask = preset_bad = 0; preview_vel = 0; preview_on = false;
    aud_slot = 11; aud_inplace = false; inplace_v = 0; prev_idx = -1; prev_cat[0] = prev_bank[0] = 0;
    memset(flakes, 0, sizeof(flakes));
    memset(drops, 0, sizeof(drops));
    memset(gnotes, 0, sizeof(gnotes));
    memset(sounding, 0, sizeof(sounding));
    memset(flash_until, 0, sizeof(flash_until));
    memset(splash_until, 0, sizeof(splash_until));
    memset(live_vel, 0, sizeof(live_vel));
    memset(live_prev, 0, sizeof(live_prev));
    for (int c = 0; c < 16; ++c) { sky_row[c] = -1; sky_last[c] = -1000; }
    sky_press = 0;
    tick16 = 0; t_us = 1; note_seq = 0;
    center = 0; lead_pos = 0; bass_step = 0; harm_flag = false;
    learn_n = 0; motif_lock = 0; mlen = 0; motif_i = 0; motif_scale = false; slow_var = 1;
    build_scale();
    new_motif();
    restart_generative(tick16);
  }
  void rebuild_after_settings(void) {
    scale_idx %= NSCALES; root_pc %= 12; live_oct = (uint8_t)nv_clamp(live_oct, 0, 6);
    snow_oct = (uint8_t)nv_clamp(snow_oct, 1, 5); tears_oct = (uint8_t)nv_clamp(tears_oct, 1, 5);
    style %= 3; arp_dir %= 3; prog %= 7; quant_idx %= 3; harm_idx &= 3; density_idx &= 7;
    wind = (uint8_t)nv_clamp(wind, 0, 8);
    p_freq = (uint8_t)nv_clamp(p_freq, 20, 250); p_melvar = (uint8_t)nv_clamp(p_melvar, 0, 100);
    p_rvar = (uint8_t)nv_clamp(p_rvar, 0, 100); p_vmin = (uint8_t)nv_clamp(p_vmin, 1, 127);
    p_vmax = (uint8_t)nv_clamp(p_vmax, 1, 127); p_nlen = (uint8_t)nv_clamp(p_nlen, 3, 120);
    for (int v = 0; v < NV; ++v) {
      midi_mode[v] %= 3; midi_ch[v] &= 15;
      tr_cat[v][16] = tr_bank[v][16] = 0;
      if (tr_preset[v] < -1 || tr_preset[v] > 63 || !tr_cat[v][0] || !tr_bank[v][0]) tr_preset[v] = -1;
    }
    build_scale();
  }
  void on_load_finished() override { reset_runtime(); }

  int get_num_pages(void) override { return 2; }
  int get_num_tracks(void) override { return NV; }
  const char *get_track_name(int i, char buf[16]) override {
    if (!buf) return NULL;
    static const char *names[3] = {"SNOW", "TEARS", "LIVE"};
    snprintf(buf, 16, "%s", names[i % 3]);
    return buf;
  }

  // ---------------- MUSIC (timing-critical) ----------------
  void on_sequence(int delta_time_us) override {
    t_us += (uint32_t)delta_time_us;
    if (!t_us) t_us = 1;
    bool playing = is_transport_playing();
    int edges = clk.update(-1, 4, 1, UPDATE_DIV_NOW, true); // free-running 16ths: the sky works even when stopped
    if (has_transport_just_started()) // the bar starts on the next 16th
      restart_generative(tick16 + ((edges > 0 || clk.phase_q16() >= 32768) ? 1 : 0));
    if (edges > 4) edges = 4;
    for (int e = 0; e < edges; ++e) { tick16++; on_tick16(playing); }

    // flakes you dropped in the sky
    uint16_t pr = sky_press;
    if (pr) {
      sky_press = 0;
      for (int c = 0; c < 16; ++c)
        if ((pr >> c) & 1) { int row = sky_row[c] >= 0 ? (int)sky_row[c] : (int)SKY1; manual_flake(c, row, sky_vel[c]); sky_last[c] = tick16; }
    }
    int32_t nq = now_q8();
    for (int i = 0; i < MAXFLAKES; ++i)
      if ((flakes[i].flags & 1) && nq >= flakes[i].land_q8) { land(flakes[i]); flakes[i].flags = 0; }
    for (int i = 0; i < MAXDROPS; ++i)
      if (drops[i].active && nq - drops[i].start_q8 > 4 * 256) drops[i].active = false;

    // REPLY: a pause after your phrase turns it into the motif of the tears
    if (learn_n > 0 && (uint32_t)(t_us - learn_t) > 1800000u) learn_commit();

    // ---- voices ----
    bool loading = (mode == MODE_PRESET);
    static const uint8_t prio[3] = {1, 2, 3};
    // new notes first (they may take the oldest voice), then the notes already sounding: a note that has lost its
    // voice ends there (its release tail keeps ringing) instead of stealing it back
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 0; i < MAXNOTES; ++i) {
        gnote_t &g = gnotes[i];
        if (!g.active || g.started != (pass == 1)) continue;
        if ((int32_t)(g.end_us - t_us) <= 0) { g.active = false; continue; }
        g.started = true; // (MIDI only: the note lives until its end, without a Plinky voice)
        if (!g.synth || (loading && g.voice == sel)) continue;
        int old_v = va.find_voice(g.id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
        if (pass == 1 && old_v < 0) { g.synth = false; continue; }
        int v = va.voice_allocate(g.id, prio[g.voice], 0, DEFAULT_VOICE_ALLOCATOR_VOICES, g.voice);
        if (v < 0 || (pass == 1 && v != old_v)) { if (v >= 0 && v != old_v) va.voice_deallocate(g.id); g.synth = false; continue; }
        play_synth(v, g.voice, g.vel, g.note << 8, pass == 0);
      }
    // live keyboard: each finger is one source (its origin pad); sliding keeps the voice = glide
    int8_t best[KPADS];
    for (int i = 0; i < KPADS; ++i) best[i] = -1;
    for (int p = 0; p < KPADS; ++p) {
      if (!live_vel[p]) continue;
      int s = live_src[p] % KPADS, cur = best[s];
      if (cur < 0 || live_vel[p] >= live_vel[cur]) best[s] = (int8_t)p;
    }
    for (int s = 0; s < KPADS; ++s) {
      int p = best[s];
      bool on = p >= 0 && !(loading && sel == LIVE);
      if (on && !live_prev[s] && reply) learn_add(live_idx(p));
      uint32_t id = 0x20000000u | (uint32_t)s;
      if (!on) { live_prev[s] = 0; continue; }
      if (mute_live) { live_prev[s] = 1; continue; } // muted: silent, but your phrases still teach the tears
      if (midi_mode[LIVE] >= 1) declare_midi_note(midi_ch[LIVE] & 15, (uint8_t)live_note(p), live_vel[p], MIDI_PORT_1);
      if (midi_mode[LIVE] == 2) { live_prev[s] = 1; continue; }
      int old_v = va.find_voice(id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      int v = va.voice_allocate(id, prio[LIVE], 0, DEFAULT_VOICE_ALLOCATOR_VOICES, LIVE);
      if (v != old_v && old_v >= 0) synth_note_up(old_v);
      if (v >= 0) {
        int note = live_note(p);
        play_synth(v, LIVE, live_vel[p], note << 8, !live_prev[s] || v != old_v);
        flash_until[note & 127] = t_us + 150000;
        flash_voice[note & 127] = LIVE;
      }
      live_prev[s] = 1;
    }
    // MIDI out (port 1 = USB + TRS): every voice on its own channel; the runtime turns the notes declared in this frame
    // into note on / note off / aftertouch (the pressure of the live keyboard)
    for (int i = 0; i < MAXNOTES; ++i) {
      const gnote_t &g = gnotes[i];
      if (!g.active || midi_mode[g.voice % 3] < 1) continue;
      if (g.midi_wait) { gnotes[i].midi_wait = false; continue; }
      declare_midi_note(midi_ch[g.voice % 3] & 15, g.note, g.vel, MIDI_PORT_1);
    }
    send_declared_midi_notes(-1);
    // SD browser preview: voice 11 is outside the allocator (which uses voices 0-7), audition slot 11
    if (preview_vel > 0) { play_synth(11, aud_slot, preview_vel, 60 << 8, !preview_on); preview_on = true; }
    else if (preview_on) { synth_note_up(11); preview_on = false; }
    uint16_t stale = va.garbage_collect_voices(1, 0, 0, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
    while (stale) {
      int v = __builtin_ctz((unsigned)stale);
      synth_note_up(v);
      stale &= (uint16_t)(stale - 1);
    }
  }

  // ---------------- UI ----------------
  void read_keyboard(void) {
    uint8_t vel[KPADS], src[KPADS];
    for (int p = 0; p < KPADS; ++p) {
      int x = p % KW, y = KY0 + p / KW;
      vel[p] = 0;
      src[p] = (uint8_t)p;
      if (!get_touch_down(x, y)) continue;
      vel[p] = (uint8_t)nv_clamp(touch_pressure_curve_q7(get_touch_pressure_xy(x, y)), 1, 127);
      int ox = get_touch_origin_x(x, y), oy = get_touch_origin_y(x, y);
      if (ox >= 0 && ox < KW && oy >= KY0 && oy < KY0 + KH) src[p] = (uint8_t)((oy - KY0) * KW + ox);
    }
    on_sequence_lock_guard_t lock;
    memcpy(live_vel, vel, sizeof(vel));
    memcpy(live_src, src, sizeof(src));
  }
  void clear_keyboard(void) {
    on_sequence_lock_guard_t lock;
    memset(live_vel, 0, sizeof(live_vel));
    for (int c = 0; c < 16; ++c) sky_row[c] = -1;
  }
  void read_sky(void) {
    int8_t rows[16];
    uint8_t vels[16];
    uint16_t pressed = 0;
    for (int c = 0; c < 16; ++c) {
      rows[c] = -1;
      vels[c] = 0;
      for (int y = SKY0; y <= SKY1; ++y) {
        if (!get_touch_down(c, y)) continue;
        int v = nv_clamp(touch_pressure_curve_q7(get_touch_pressure_xy(c, y)), 1, 127);
        if (rows[c] < 0 || v > vels[c]) { rows[c] = (int8_t)y; vels[c] = (uint8_t)v; }
        if (get_touch_pressed(c, y)) pressed |= (uint16_t)(1 << c);
      }
    }
    on_sequence_lock_guard_t lock;
    for (int c = 0; c < 16; ++c) {
      sky_row[c] = rows[c];
      if (rows[c] >= 0) sky_vel[c] = vels[c];
    }
    sky_press |= pressed;
  }

  void draw_sky(void) {
    int32_t nq = now_q8();
    // the ground: chord tones of each column, roots a little brighter
    for (int c = 0; c < 16; ++c) {
      int j = c / 2;
      bool root = (j % 3) == 0;
      set_led(c, SKY1, root ? DIMMER(DIMMER(WHITE)) : DIMMER(DIMMER(BLUE)));
      if ((int32_t)(splash_until[c] - t_us) > 0) set_led(c, SKY1, BRIGHTEST(WHITE));
    }
    for (int i = 0; i < MAXFLAKES; ++i) {
      const flake_t &f = flakes[i];
      if (!(f.flags & 1)) continue;
      int32_t span = f.land_q8 - f.spawn_q8;
      float p = span > 0 ? (float)(nq - f.spawn_q8) / (float)span : 1.f;
      p = p < 0 ? 0 : (p > 1 ? 1 : p);
      int row = f.row0 + (int)((SKY1 - f.row0) * p + 0.5f);
      int col = nv_clamp(f.col + drift_cols(p), 0, 15);
      uint32_t c = (f.flags & 2) ? BRIGHTEST(WHITE) : (f.vel > 70 ? WHITE : DIMMER(WHITE));
      set_led(col, nv_clamp(row, SKY0, SKY1), c);
    }
    for (int i = 0; i < MAXDROPS; ++i) {
      if (!drops[i].active) continue;
      int row = SKY0 + (int)((nq - drops[i].start_q8) * 5 / (4 * 256));
      set_led(drops[i].col, nv_clamp(row, SKY0, SKY1), row <= SKY0 ? BRIGHTEST(CYAN) : CYAN);
    }
  }

  void draw_keyboard(void) {
    scale_t s = cur_scale();
    (void)s;
    int c1, c2;
    chord_offsets(c1, c2);
    int pc_root = (sc[center]) % 12, pc3 = sc[(center + c1)] % 12, pc5 = sc[(center + c2)] % 12;
    for (int p = 0; p < KPADS; ++p) {
      int x = p % KW, y = KY0 + p / KW;
      int note = live_note(p), pc = note % 12;
      uint32_t c;
      if (pc == pc_root) c = PINK;
      else if (pc == pc3 || pc == pc5) c = DIMMER(PINK);
      else c = DIMMER(DIMMER(PINK));
      if ((int32_t)(flash_until[note] - t_us) > 0 && flash_voice[note] != LIVE) c = k_voice_col[flash_voice[note] % 3];
      if (live_vel[p]) c = BRIGHTEST(PINK);
      set_led(x, y, c);
    }
  }

  void draw_top(void) {
    // row 0: chord degrees
    bool blink = (get_on_ui_time() >> 17) & 1;
    int nd = nv_min(per_oct, 7);
    for (int d = 0; d < nd; ++d) {
      uint32_t c = d == center ? BRIGHTEST(YELLOW) : (good[d] ? YELLOW : DIMMER(YELLOW));
      if (pending_chord == d && blink) c = BRIGHTEST(WHITE);
      if (button(d, 0, c, ISOLATED, "chord: changes at the end of the bar")) pending_chord = (int8_t)d;
    }
    // the Chords faceplate has only 12 pads on the top row (the last 4 are the "Plinky 12 Chords" logo)
    if (button(7, 0, freeze ? BRIGHTEST(CYAN) : DIMMER(CYAN), ISOLATED, "FREEZE: hold the current chord")) {
      freeze = !freeze;
      if (!freeze) { on_sequence_lock_guard_t lock; schedule_harmony(); }
    }
    if (button(8, 0, (pending_chord == -2 && blink) ? BRIGHTEST(WHITE) : YELLOW, ISOLATED, "next chord (end of the bar)"))
      pending_chord = -2;
    static const char *style_help[3] = {"style: slow cell", "style: arpeggio", "style: snow (random)"};
    for (int i = 0; i < 3; ++i)
      if (button(9 + i, 0, style == i ? BRIGHTEST(ORANGE) : DIMMER(ORANGE), ISOLATED, style_help[i]) && style != i) {
        on_sequence_lock_guard_t lock;
        style = (uint8_t)i;
        slow_next = -1; slow_cycle = -1; n_tear_q = 0;
        motif_scale = false; motif_lock = 0; new_motif();
      }
    // row 1: density and wind
    for (int i = 0; i < 8; ++i) {
      uint32_t c = i <= density_idx ? (i == density_idx ? BRIGHTEST(WHITE) : WHITE) : DIMMER(DIMMER(WHITE));
      if (button(i, 1, c, NOT_ISOLATED, "density: how much snow")) density_idx = (uint8_t)i;
    }
    for (int i = 0; i < 8; ++i) {
      int w = i < 4 ? i : i + 1; // pads 8-11 = wind left 4..1, pads 12-15 = right 1..4
      bool lit = (wind < 4 && w >= wind && w < 4) || (wind > 4 && w <= wind && w > 4);
      uint32_t c = lit ? (w == wind ? BRIGHTEST(TEAL) : TEAL) : DIMMER(DIMMER(TEAL));
      if (button(8 + i, 1, c, NOT_ISOLATED, "wind: tap the lit end again for calm")) wind = (uint8_t)(wind == w ? 4 : w);
    }
  }

  void draw_bottom(void) {
    uint32_t now = get_on_ui_time();
    static const uint32_t oct_col[7] = {PURPLE, RED, ORANGE, WHITE, CYAN, GREEN, YELLOW};
    uint8_t &oct = voice_oct(sel);
    int omin = sel == LIVE ? 0 : 1, omax = sel == LIVE ? 6 : 5; // snow and tears: two octaves down / up
    uint32_t oc = oct_col[oct % 7], kc = k_key_col[root_pc % 12], scc = k_scale_col[scale_idx % NSCALES];
    if (now - flash_us[0] < 150000) oc = BRIGHTEST(WHITE);
    if (now - flash_us[1] < 150000) kc = BRIGHTEST(WHITE);
    if (now - flash_us[2] < 150000) scc = BRIGHTEST(WHITE);
    if (button(0, 14, oc, ISOLATED, "octave + of the selected voice")) { oct = (uint8_t)nv_min(omax, oct + 1); flash_us[0] = now; }
    if (button(0, 15, oc, ISOLATED, "octave - of the selected voice")) { oct = (uint8_t)nv_max(omin, oct - 1); flash_us[0] = now; }
    if (button(1, 14, kc, ISOLATED, "key +")) { on_sequence_lock_guard_t l; root_pc = (root_pc + 1) % 12; build_scale(); flash_us[1] = now; }
    if (button(1, 15, kc, ISOLATED, "key -")) { on_sequence_lock_guard_t l; root_pc = (root_pc + 11) % 12; build_scale(); flash_us[1] = now; }
    if (button(2, 14, scc, ISOLATED, k_scale_names[(scale_idx + 1) % NSCALES])) {
      on_sequence_lock_guard_t l; scale_idx = (scale_idx + 1) % NSCALES; build_scale(); motif_scale = false; flash_us[2] = now;
    }
    if (button(2, 15, scc, ISOLATED, k_scale_names[(scale_idx + NSCALES - 1) % NSCALES])) {
      on_sequence_lock_guard_t l; scale_idx = (scale_idx + NSCALES - 1) % NSCALES; build_scale(); motif_scale = false; flash_us[2] = now;
    }
    // rectangles of row 15: arpeggio direction | voice select | mute of the selected voice | MIX SND PRE | REPLY
    static const char *dir_help[3] = {"arpeggio: up", "arpeggio: down", "arpeggio: up & down"};
    static const uint32_t dir_col[3] = {GREEN, BLUE, PURPLE};
    if (button(7, 14, style == 1 ? dir_col[arp_dir % 3] : DIMMER(dir_col[arp_dir % 3]), ISOLATED, dir_help[(arp_dir + 1) % 3]))
      arp_dir = (arp_dir + 1) % 3;
    for (int v = 0; v < NV; ++v) {
      uint32_t vc = sel == v ? BRIGHTEST(k_voice_col[v]) : DIMMER(k_voice_col[v]);
      if (voice_muted(v)) vc = (get_on_ui_time() >> 18) & 1 ? DIMMER(DIMMER(k_voice_col[v])) : DIMMER(k_voice_col[v]); // muted: slow blink
      if (button(8 + v, 14, vc, ISOLATED, k_voice_names[v])) sel = (uint8_t)v;
    }
    if (button(11, 14, voice_muted(sel) ? BRIGHTEST(RED) : DIMMER(RED), ISOLATED, "mute the selected voice"))
      voice_mute_ref(sel) = !voice_mute_ref(sel);
    if (button(15, 14, reply ? BRIGHTEST(PINK) : DIMMER(PINK), ISOLATED, "REPLY: the tears answer your phrases")) {
      reply = !reply;
      if (!reply) { on_sequence_lock_guard_t lock; learn_n = 0; motif_lock = 0; }
    }
    button(12, 14, GREEN, ISOLATED, "MIX: volume, pan, delay, reverb");
    if (is_last_widget_released()) mode = MODE_MIX;
    button(13, 14, PINK, ISOLATED, "SND: edit the sound of the selected voice");
    if (is_last_widget_released()) mode = MODE_SOUND;
    button(14, 14, BLUE, ISOLATED, "PRE: choose the sound of the selected voice");
    if (is_last_widget_released()) {
      pp.picker.reset_which_slot_is_selected();
      mode = MODE_PRESET; factory_picker = true; ncat = -1; cand_row = -1;
    }
    static const char *q_help[3] = {"grid 1/4", "grid 1/8", "grid 1/16"};
    for (int i = 0; i < 3; ++i)
      if (button(5 + i, 15, quant_idx == i ? BRIGHTEST(ORANGE) : DIMMER(ORANGE), ISOLATED, q_help[i])) quant_idx = (uint8_t)i;
    static const char *h_help[4] = {"chord length: 1 bar", "chord length: 2 bars", "chord length: 4 bars", "chord length: 8 bars"};
    for (int i = 0; i < 4; ++i)
      if (button(8 + i, 15, harm_idx == i ? BRIGHTEST(YELLOW) : DIMMER(YELLOW), ISOLATED, h_help[i])) {
        on_sequence_lock_guard_t l; harm_idx = (uint8_t)i; schedule_harmony();
      }
    button(12, 15, PURPLE, ISOLATED, "PARAM: frequency, variation, dynamics, note length");
    if (is_last_widget_released()) mode = MODE_PARAM;
    if (button(13, 15, k_prog_col[prog % 7], ISOLATED, k_prog_names[(prog + 1) % 7])) { on_sequence_lock_guard_t l; prog = (prog + 1) % 7; prog_i = 0; }
    stop_button(14, 15, RED);
    play_button(15, 15, GREEN);
  }

  void draw_mixer(void) {
    static const int mix_params[4] = {VOICE_PARAM_VOLUME, VOICE_PARAM_STEREO, VOICE_PARAM_DELAY_SEND, VOICE_PARAM_REVERB_SEND};
    for (int v = 0; v < NV; ++v) {
      synth_param_sliders_block(mix_sliders[v], v * 5, 1, 4, 13, v, mix_params); // from row 2: the top row has 12 pads
      for (int i = 0; i < 4; ++i)
        if (button(v * 5 + i, 14, sel == v ? BRIGHTEST(k_voice_col[v]) : DIMMER(k_voice_col[v]), ISOLATED, k_voice_names[v])) sel = (uint8_t)v;
      // bottom row: MIDI out of the voice. 1st pad = output (white Plinky, green Plinky + MIDI, blue MIDI only),
      // 2nd / 3rd pad = channel - / +
      static const uint32_t mode_col[3] = {WHITE, GREEN, BLUE};
      uint32_t now = get_on_ui_time();
      if (button(v * 5, 15, BRIGHTEST(mode_col[midi_mode[v] % 3]), ISOLATED, "MIDI out: Plinky only / Plinky + MIDI / MIDI only")) {
        midi_mode[v] = (midi_mode[v] + 1) % 3; midi_help_v = (uint8_t)v; midi_help_until = now + 2500000;
      }
      if (button(v * 5 + 1, 15, DIMMER(k_voice_col[v]), ISOLATED, "MIDI channel -")) {
        midi_ch[v] = (midi_ch[v] + 15) & 15; midi_help_v = (uint8_t)v; midi_help_until = now + 2500000;
      }
      if (button(v * 5 + 2, 15, k_voice_col[v], ISOLATED, "MIDI channel +")) {
        midi_ch[v] = (midi_ch[v] + 1) & 15; midi_help_v = (uint8_t)v; midi_help_until = now + 2500000;
      }
    }
    if ((int32_t)(midi_help_until - get_on_ui_time()) > 0) {
      static const char *names[3] = {"snow", "tears", "live"}, *modes[3] = {"Plinky only", "Plinky + MIDI", "MIDI only"};
      int v = midi_help_v % 3;
      set_help_text("%s: %s, MIDI channel %d", names[v], modes[midi_mode[v] % 3], midi_ch[v] + 1);
    }
  }
  void draw_params(void) {
    static const char *names[6] = {"note frequency", "melodic variation", "rhythmic variation", "velocity min",
                                   "velocity max", "note length"};
    static const uint32_t cols[6] = {WHITE, CYAN, ORANGE, PINK, PINK, YELLOW};
    uint8_t *vals[6] = {&p_freq, &p_melvar, &p_rvar, &p_vmin, &p_vmax, &p_nlen};
    static const int mins[6] = {20, 0, 0, 1, 1, 3}, maxs[6] = {250, 100, 100, 127, 127, 120};
    for (int i = 0; i < 6; ++i) {
      if (par_sliders[i].simple_slider(i * 2 + 1, 0, 14, VERTICAL | SHOW_STEM, cols[i], mins[i], maxs[i], *vals[i], names[i]))
        *vals[i] = (uint8_t)nv_clamp(last_widget_new_value(), mins[i], maxs[i]);
    }
  }
  // ---------------- SD BROWSER (from QU4TTRO) ----------------
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
  // a bank folder holds XY.json files: X = row 0-7, Y = column 0-7 of the 8x8 preset grid
  void select_bank(int b) {
    cur_bank = (int8_t)b;
    preset_mask = preset_bad = 0;
    cand_row = -1;
    if (b < 0 || b >= nbank) return;
    char path[60];
    snprintf(path, sizeof(path), "/PLINKY/BANKS/%s/%s", cat_names[cur_cat], bank_names[b]);
    DIR dir;
    FILINFO fi;
    if (f_opendir(&dir, path) != FR_OK) { preset_mask = ~0ull; return; } // cannot list: allow every slot
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
      const char *n = fi.fname;
      // "XY.json", or its short 8.3 form "XY~1.JSO"
      if (n[0] >= '0' && n[0] <= '7' && n[1] >= '0' && n[1] <= '7' && (n[2] == '.' || n[2] == '~'))
        preset_mask |= 1ull << ((n[0] - '0') * 8 + (n[1] - '0'));
    }
    f_closedir(&dir);
    if (!preset_mask) preset_mask = ~0ull;
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
    if (n <= 0) {
      ncat = 0;
      for (int i = 0; i < 2; ++i) { strncpy(cat_names[i], k_fallback_cat[i], 16); cat_names[i][16] = 0; }
    } else ncat = (int8_t)n;
    int ncats = ncat > 0 ? ncat : 2, start = 0;
    for (int i = 0; i < ncats; ++i) if (!strcmp(cat_names[i], "USER1")) start = i; // open on USER1 if it exists
    select_category(start);
  }
  // cancel an audition made on the voice itself: reload the bank the voice had before
  void restore_inplace(void) {
    if (!aud_inplace) return;
    preview_vel = 0;
    if (prev_idx >= 0) load_preset_from_filename_with_category(inplace_v, prev_cat, prev_bank, prev_idx);
    aud_inplace = false;
    aud_slot = 11;
    cand_row = -1;
  }
  // row 2 categories | rows 4-11: banks (left 8x8) and presets of the selected bank (right 8x8)
  // row 13: voice | bottom right: blue = system picker, X = cancel, play = LOAD on the selected voice
  // touch a preset = audition only (spare slot 11, plays while held, pressure = dynamics)
  void draw_my_banks(void) {
    static constexpr int AUD = 11, ROWC = 2, ROW0 = 4;
    if (ncat < 0) scan_categories();
    int ncats = ncat > 0 ? ncat : 2;
    int pv = 0;
    uint32_t base = BRIGHTEST(k_cat_col[cur_cat & 7]), base_lo = k_cat_col[cur_cat & 7];
    for (int i = 0; i < ncats && i < 16; ++i)
      if (button(i, ROWC, i == cur_cat ? BRIGHTEST(WHITE) : k_cat_col[i & 7], ISOLATED, cat_names[i])) select_category(i);
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
        if (!(preset_mask & bit)) continue;
        bool cand = cand_row == (i >> 3) && cand_col == (i & 7);
        bool cur = this_bank && tr_preset[sel] == i;
        set_led(x, y, cand ? BRIGHTEST(GREEN) : cur ? BRIGHTEST(WHITE) : (preset_bad & bit) ? RED : base);
        if (get_touch_pressed(x, y)) {
          // the voice already has one of your banks: audition on the voice itself, so its old bank is not kept too
          if (!aud_inplace && tr_preset[sel] >= 0) {
            aud_inplace = true; inplace_v = sel;
            memcpy(prev_cat, tr_cat[sel], 17); memcpy(prev_bank, tr_bank[sel], 17); prev_idx = tr_preset[sel];
          }
          int slot = aud_inplace ? inplace_v : AUD;
          aud_slot = (int8_t)slot;
          bool ok = load_preset_from_filename_with_category(slot, cat_names[cur_cat], bname, i);
          if (ok) { preset_bad &= ~bit; cand_row = (int8_t)(i >> 3); cand_col = (int8_t)(i & 7); }
          else { preset_bad |= bit; if (cand) cand_row = -1; }
        }
        if (get_touch_down(x, y) && cand_row == (i >> 3) && cand_col == (i & 7))
          pv = nv_clamp(touch_pressure_curve_q7(get_touch_pressure_xy(x, y)), 1, 127);
      }
    }
    preview_vel = (int8_t)pv;
    for (int v = 0; v < NV; ++v)
      if (button(v, 13, v == sel ? BRIGHTEST(k_voice_col[v]) : DIMMER(k_voice_col[v]), ISOLATED, k_voice_names[v]) && v != sel) {
        restore_inplace();
        sel = (uint8_t)v;
        cand_row = -1;
      }
    button(12, 15, BLUE, ISOLATED, "back to the system picker");
    if (is_last_widget_released()) { restore_inplace(); pp.picker.reset_which_slot_is_selected(); factory_picker = true; preview_vel = 0; }
    button(13, 15, RED, ISOLATED, "cancel");
    if (is_last_widget_released()) { restore_inplace(); mode = MODE_PLAY; preview_vel = 0; cand_row = -1; }
    bool have = cand_row >= 0 && cur_bank >= 0;
    button(15, 15, have ? BRIGHTEST(GREEN) : DIMMER(GREEN), ISOLATED, "LOAD on the selected voice");
    if (is_last_widget_released() && have) {
      int idx = cand_row * 8 + cand_col;
      const char *bname = bank_names[cur_bank];
      // load into the voice's own slot (copying the audition slot would alias its sample address); after an audition
      // on the voice itself the sound is already there
      bool ok = aud_inplace ? true : load_preset_from_filename_with_category(sel, cat_names[cur_cat], bname, idx);
      aud_inplace = false;
      aud_slot = 11;
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

  bool back_button(void) { button(15, 15, BRIGHTEST(WHITE), ISOLATED, "back to play"); return is_last_widget_released(); }

  bool on_serialise(serialiser_t &s, int version) override {
    (void)version;
    n3ve &o = *this;
    (void)o;
    OBJECT_BEGIN(s);
    FIELD_SYNTH_PRESET("sound_snow", 0);
    FIELD_SYNTH_PRESET("sound_tears", 1);
    FIELD_SYNTH_PRESET("sound_live", 2);
    FIELD_MIX_PRESET("mix");
    FIELD("scale", scale_idx, 0u, 11u);
    FIELD("key", root_pc, 0u, 11u);
    FIELD("live_oct", live_oct, 0u, 6u);
    FIELD("snow_oct", snow_oct, 1u, 5u);
    FIELD("tears_oct", tears_oct, 1u, 5u);
    FIELD("style", style, 0u, 2u);
    FIELD("arp_dir", arp_dir, 0u, 2u);
    FIELD("prog", prog, 0u, 6u);
    FIELD("grid", quant_idx, 0u, 2u);
    FIELD("chord_len", harm_idx, 0u, 3u);
    FIELD("density", density_idx, 0u, 7u);
    FIELD("wind", wind, 0u, 8u);
    FIELD("freeze", freeze, 0u, 1u);
    FIELD("reply", reply, 0u, 1u);
    FIELD("mute_snow", mute_snow, 0u, 1u);
    FIELD("mute_tears", mute_tears, 0u, 1u);
    FIELD("mute_live", mute_live, 0u, 1u);
    FIELD("freq", p_freq, 20u, 250u);
    FIELD("melvar", p_melvar, 0u, 100u);
    FIELD("rvar", p_rvar, 0u, 100u);
    FIELD("vel_min", p_vmin, 1u, 127u);
    FIELD("vel_max", p_vmax, 1u, 127u);
    FIELD("note_len", p_nlen, 3u, 120u);
    FIELD_BASE64_ARRAY("midi_mode", midi_mode);
    FIELD_BASE64_ARRAY("midi_ch", midi_ch);
    FIELD_BASE64_ARRAY("sd_cat", tr_cat);     // which of your banks each voice has (for the memory-friendly audition)
    FIELD_BASE64_ARRAY("sd_bank", tr_bank);
    FIELD_BASE64_ARRAY("sd_preset", tr_preset);
    OBJECT_END(s);
    if (s.reading) rebuild_after_settings();
    return true;
  }

  // The song page lists /PLINKY/<panel>/<bank>/ (for example /PLINKY/u_n3ve/00/05.json). On a card where this panel has
  // never saved, those folders do not exist: the picker cannot open them, its OK pad stays dark and nothing can be saved.
  // Create them the first time the page opens.
  bool song_folder_ok = false;
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
    if (!song_folder_ok) printf("song folder /PLINKY/%s: cannot create (%d %d)\n", name, (int)a, (int)b);
  }
  void on_ui(int delta_time_us) override {
    (void)delta_time_us;
    int sp = get_scroll_page();
    if (sp < 0) return;
    leds_clear();
    if (sp == 1) { clear_keyboard(); ensure_song_folder(); song_page.saveload(16, true, FLAG_PICKER_ENABLE_DELETE); return; }
    if (sp > 1) { scroll_to_page(1); return; }
    if (mode == MODE_MIX) { clear_keyboard(); draw_mixer(); if (back_button()) mode = MODE_PLAY; return; }
    if (mode == MODE_SOUND) {
      clear_keyboard();
      pp.edit(sel, 0);
      pp.xy_pad(sel, 8, 10);
      if (back_button()) mode = MODE_PLAY;
      return;
    }
    if (mode == MODE_PARAM) { clear_keyboard(); draw_params(); if (back_button()) mode = MODE_PLAY; return; }
    if (mode == MODE_PRESET) {
      clear_keyboard();
      if (factory_picker) {
        // system picker (factory and saved presets, instant preview); the blue pad opens the SD browser
        pp.picker.preset_picker(sel, 2, 10, FLAG_PICKER_ENABLE_DELETE);
        for (int v = 0; v < NV; ++v)
          if (button(v, 13, v == sel ? BRIGHTEST(k_voice_col[v]) : DIMMER(k_voice_col[v]), ISOLATED, k_voice_names[v])) {
            sel = (uint8_t)v;
            pp.picker.reset_which_slot_is_selected();
          }
        button(12, 15, BLUE, ISOLATED, "your banks on the SD card: presets and multisamples");
        if (is_last_widget_released()) { factory_picker = false; ncat = -1; cand_row = -1; return; }
        if (pp.picker.cancel_button(13, 15)) { mode = MODE_PLAY; return; }
        int action = pp.picker.preset_ok_button(sel, false, 15, 15); // no save on long press: a long load must not become a save
        if (action > 0) { tr_preset[sel] = -1; tr_cat[sel][0] = tr_bank[sel][0] = 0; mode = MODE_PLAY; } // not one of your SD banks
        return;
      }
      draw_my_banks();
      return;
    }
    read_sky();
    read_keyboard();
    draw_sky();
    draw_keyboard();
    draw_top();
    draw_bottom();
  }
};

static_assert(sizeof(n3ve) <= 128 * 1024, "N3VE is larger than the 128 KB panel arena");
