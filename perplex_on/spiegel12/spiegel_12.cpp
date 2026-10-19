/*
@Name: Spiegel 12
@Author: PERPLEX ON
@Firmware: beta
@Tags: instrument, harmony, generative, arpeggiator, xy, pressure
@Preferred Panels: blocks
@Description: An interpretation of Laurie Spiegel's Music Mouse for Plinky 12.
  The right 12x16 field is the "mouse": your finger position (x = columns, y = rows)
  drives two interlocking voice pairs (X pair + Y pair, interval apart), all snapped to
  a harmonic mode. The left strip is the "keyboard hand": six pressure pads (Vibrato,
  Tremolo, Filter, Accent, Delay, Random) shape the sound while you play, and tap pads
  choose mode, motion (parallel / contrary), treatment (Chord / Arp / Line / Improv),
  articulation, 10 patterns, interval, transpose, octave offsets and 3 sounds.
  Random varies the notes you are playing (neighbour / third / fifth in the scale), like Accent it only affects the played notes.
  Note changes are quantised to the clock (Off or 1/16): the cursor you touch
  is only adopted on the next grid line. Filter and Delay share one per-note slot,
  the harder-pressed pad wins. Runs on the free-running clock, no transport needed.
*/
#define PANEL_PAD_COLOR CYAN

struct spiegel_12 : panel_t {
  static constexpr int FX = 4;  // first playfield column
  static constexpr int FW = 12; // playfield width
  static constexpr int FH = 16; // playfield height
  static constexpr int NV = 4;  // voices: X1 X2 Y1 Y2
  static constexpr int SND = 3; // sound presets

  static constexpr int PAD_DEADZONE = 50;  // raw pressure ignored below this (raise = less sensitive)
  static constexpr int PAD_FULL_PCT = 125; // full depth at this % of the user's max pressure (raise = needs harder press)

  enum page_t : uint8_t { PAGE_PLAY, PAGE_EDIT, PAGE_LOAD };

  // ---- lookup tables ----
  // Kept as function-local statics: class-scope constexpr arrays are unreliable on the device toolchain.

  // Scale degrees (semitones) per harmonic mode.
  static int scale_note(int m, int i) {
    static const uint8_t T[6][12] = {
        {0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}, // Minor pentatonic
        {0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}, // Harmonic minor
        {0, 2, 4, 7, 11, 0, 0, 0, 0, 0, 0, 0}, // Maj9 (chord tones)
        {0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}, // Diatonic
        {0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0},  // Pentatonic
        {0, 5, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // Quartal
    };
    return T[m][i];
  }

  // Number of degrees per octave of each mode.
  static int scale_len(int m) {
    static const uint8_t T[6] = {5, 7, 5, 7, 5, 3};
    return T[m];
  }

  // Degree offset of step s in pattern p (pattern 0 = off).
  static int pattern_offset(int p, uint32_t s) {
    static const int8_t D[46] = {0, 0, 1, 2, 1, 0, 2, 4, 2, 0, -1, -2, -1, 0, 2, 1, 3, 0, 4, 2, 6, 4, 2, 0,
                                 0, 3, 0, 5, 0, 1, 0, -1, 0, 3, 6, 3, 0, -2, 0, 2, 4, 2, 0, 5, 3, 1};
    static const uint8_t O[11] = {0, 1, 5, 9, 13, 17, 23, 28, 32, 36, 42}; // pattern start in D
    static const uint8_t L[11] = {1, 4, 4, 4, 4, 6, 5, 4, 4, 6, 4};        // pattern length
    return D[O[p] + (int)(s % L[p])];
  }

  static int arp_voice(uint32_t s) {
    static const uint8_t A[4] = {0, 2, 1, 3};
    return A[s & 3];
  }

  static int gate_pct(int a) {
    static const uint8_t G[3] = {35, 80, 170}; // staccato, half legato, legato
    return G[a];
  }

  // Top-left corner of each 2x2 pressure pad.
  static int pad_x(int k) {
    static const uint8_t X[6] = {0, 2, 0, 2, 0, 2};
    return X[k];
  }
  static int pad_y(int k) {
    static const uint8_t Y[6] = {0, 0, 2, 2, 4, 4};
    return Y[k];
  }

  preset_pages_t preset_pages;
  panel_page_t panel_page;
  uint8_t page = PAGE_PLAY;

  // ---- panel state (serialised) ----
  uint8_t mode = 4;     // 0..5 harmonic mode
  uint8_t contrary = 0; // 0 parallel, 1 contrary motion
  uint8_t treat = 0;    // 0 chord, 1 arp, 2 line, 3 improv
  uint8_t art = 2;      // 0 staccato, 1 half legato, 2 legato
  uint8_t pat = 4;      // 0 = off, 1..10 patterns
  uint8_t interval = 3; // 1..7 degrees between paired voices
  uint8_t trn12 = 12;   // transpose + 12 (0..24)
  uint8_t xo2 = 2;      // X octave + 2 (0..4)
  uint8_t yo2 = 2;      // Y octave + 2 (0..4)
  uint8_t snd = 0;      // 0..SND-1
  uint8_t latch = 0;
  uint8_t qi = 1; // quantise: 0 = off, 1 = 1/16

  // ---- runtime ----
  int16_t finger = -1; // live cursor cell (c + FW*row) or -1
  int16_t cm = -1;     // committed cursor cell
  int32_t pv[6] = {0, 0, 0, 0, 0, 0}; // smoothed pressure pads, 0..127
  uint32_t last_fine = 0;
  uint32_t pstep = 0;
  uint32_t rng = 2463534242u;
  bool inited = false;
  bool need_release = false;
  bool v_on[NV] = {false, false, false, false};
  uint32_t v_off_at[NV] = {0, 0, 0, 0};
  int v_note_q8[NV] = {0, 0, 0, 0};
  uint8_t v_vel[NV] = {0, 0, 0, 0};

  int get_num_pages() override { return 2; }
  int get_num_tracks(void) override { return SND; }

  const char *get_track_name(int i, char buf[16]) override {
    if (!buf) return NULL;
    snprintf(buf, 16, "%d", i + 1);
    return buf;
  }

  void setup_default_panel_state() override {
    panel_t::setup_default_panel_state();
    synth_presets[0].hue = 0;
    synth_presets[1].hue = 40;
    synth_presets[2].hue = 80;
    finger = cm = -1;
    inited = false;
    need_release = true;
  }

  // ---------- music ----------
  uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  }

  int deg2midi(int d, int base, int oct) {
    int l = scale_len(mode);
    int o = d >= 0 ? d / l : -((-d + l - 1) / l);
    int m = d - o * l;
    return base + (trn12 - 12) + oct * 12 + o * 12 + scale_note(mode, m);
  }

  int voice_deg(int v, int c, int ry) {
    switch (v) {
    case 0: return c;
    case 1: return contrary ? (FW - 1 - c) : c + interval;
    case 2: return ry;
    default: return contrary ? (FH - 1 - ry) : ry - interval;
    }
  }

  int voice_midi(int v, int c, int ry, int off, int dd = 0) {
    int d = voice_deg(v, c, ry) + dd;
    int m = v < 2 ? deg2midi(d, 36, (int)xo2 - 2) : deg2midi(d + off, 55, (int)yo2 - 2);
    return clampi(m, 0, 127);
  }

  // Random pad (pv[5]): varies the notes of the played cursor/chord - never triggers anything on its own.
  // Per hit and voice the pad pressure sets the chance that the note moves a few scale degrees
  // (neighbour, third, fifth), so it stays inside the mode and follows the harmony you play.
  int random_shift() {
    if (pv[5] < 10 || (int)(rnd() % 127) >= (int)pv[5]) return 0;
    static const int8_t SH[8] = {-2, -1, -1, 1, 1, 2, 2, 4};
    return SH[rnd() & 7];
  }

  // ---------- voices ----------
  void send_voice(int v, bool retrigger) {
    uint32_t ms = time_us() / 1000u;
    int tri = (int)(ms % 200u);
    tri = (tri < 100 ? tri : 200 - tri) - 50; // -50..50, 5 Hz
    int vib = (int)pv[0] * tri * 48 / (127 * 50);
    int tri2 = (int)(ms % 140u);
    tri2 = tri2 < 70 ? tri2 : 140 - tri2; // 0..70, ~7 Hz
    int red = ((int)pv[1] * tri2 * 3) / (70 * 4);
    int vel = clampi((int)v_vel[v] * (127 - red) / 127, 1, 127);

    synth_voice_note_t n = {};
    n.note_q8 = v_note_q8[v] + vib;
    n.velocity = (uint8_t)vel;
    n.preset_idx = (int8_t)snd;
    n.x_override = SYNTH_VOICE_XY_OVERRIDE_NONE;
    n.y_override = SYNTH_VOICE_XY_OVERRIDE_NONE;
    n.param_override_idx = -1;
    int f = (int)pv[2], d = (int)pv[4];
    if (d > f && d > 8) {
      n.param_override_idx = VOICE_PARAM_DELAY_SEND;
      n.param_override_value = (int16_t)d;
    } else if (f > 8) {
      n.param_override_idx = VOICE_PARAM_CUTOFF_LP;
      n.param_override_value = (int16_t)(127 - f * 3 / 4);
    }
    play_synth(v, n, retrigger);
  }

  void fire(int v, int midi, int vel, int gate, uint32_t f) {
    v_note_q8[v] = midi << 8;
    v_vel[v] = (uint8_t)clampi(vel, 1, 127);
    v_on[v] = true;
    v_off_at[v] = f + (uint32_t)maxi(1, gate);
    send_voice(v, true);
  }

  void release_all() {
    for (int v = 0; v < NV; ++v)
      if (v_on[v]) {
        synth_note_up(v);
        v_on[v] = false;
      }
  }

  int gate_for(int sub) { return maxi(1, sub * gate_pct(art) / 100); }

  // ---------- clock grid (1 fine tick = 1/64 note = 1/4 of a 16th) ----------
  bool commit(uint32_t f) {
    int old = cm;
    cm = finger;
    if (cm < 0) return false;
    int c = cm % FW, ry = cm / FW;
    for (int v = 0; v < NV; ++v) {
      int m = voice_midi(v, c, ry, 0);
      if (old < 0 || voice_midi(v, old % FW, old / FW, 0) != m) fire(v, m, 80, gate_for(4), f);
    }
    return true; // caller skips this tick's stream step
  }

  void hit(uint32_t f, int ph, int sub) {
    int c = cm % FW, ry = cm / FW;
    uint32_t s = pstep++;
    int off = pattern_offset(pat, s);
    int vel = 80 + (ph == 0 ? (int)pv[3] / 3 : 0);
    int g = gate_for(sub);
    switch (treat) {
    case 0:
      if ((s & 1) == 0)
        for (int v = 0; v < NV; ++v) fire(v, voice_midi(v, c, ry, off, random_shift()), vel, g, f);
      break;
    case 1: {
      int v = arp_voice(s);
      fire(v, voice_midi(v, c, ry, off, random_shift()), vel, g, f);
      break;
    }
    case 2:
      fire(2, voice_midi(2, c, ry, off, random_shift()), vel, g, f);
      if ((s & 3) == 0) fire(0, voice_midi(0, c, ry, off, random_shift()), vel, g, f);
      break;
    default:
      if (rnd() % 10 < 7) {
        int v = (int)(rnd() & 3);
        fire(v, voice_midi(v, c, ry, off, random_shift()), vel, g, f);
      }
      break;
    }
  }

  void on_fine(uint32_t f) {
    for (int v = 0; v < NV; ++v)
      if (v_on[v] && (int32_t)(f - v_off_at[v]) >= 0) {
        synth_note_up(v);
        v_on[v] = false;
      }
    bool skip = false;
    if ((f & 3) == 0 && qi > 0 && finger != cm) skip = commit(f);
    if ((f & 3) != 0) return; // everything below only on the 16th grid
    if (cm < 0) return;
    if (skip) return;
    hit(f, 0, 4);
  }

  // ---------- input ----------
  void read_pressure_pads(bool active) {
    for (int k = 0; k < 6; ++k) {
      int raw = 0;
      if (active)
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) {
            int x = pad_x(k) + dx, y = pad_y(k) + dy;
            if (get_touch_down(x, y))
              raw = maxi(raw, get_touch_pressure_xy(x, y));
          }
      // Dead zone, then a gently exponential curve (mix of x^2 and x^3): slow start,
      // full depth only at strong pressure (a bit above the user's "well pressed" level).
      int hi = mini(255, maxi(get_guessed_max_touch_pressure(), 128) * PAD_FULL_PCT / 100);
      int lin = clampi((raw - PAD_DEADZONE) * 127 / (hi - PAD_DEADZONE), 0, 127);
      int sq = lin * lin / 127;
      raw = (sq + sq * lin / 127) / 2;
      int p = (int)pv[k];
      p = raw > p ? p + ((raw - p + 2) >> 2) : p - ((p - raw + 3) >> 2); // gentle rise, soft spring back
      pv[k] = p;
    }
  }

  void read_cursor(bool active) {
    if (!active) {
      finger = -1;
      return;
    }
    int best = 0, cell = -1;
    for (int x = FX; x < 16; ++x)
      for (int y = 0; y < 16; ++y)
        if (get_touch_down(x, y)) {
          int p = get_touch_pressure_xy(x, y);
          if (p > best) {
            best = p;
            cell = (x - FX) + FW * (15 - y);
          }
        }
    if (cell >= 0)
      finger = (int16_t)cell;
    else if (!latch)
      finger = -1; // latch keeps the last cursor
  }

  void on_sequence(int delta_time_us) override {
    (void)delta_time_us;
    bool input = get_scroll_page() == 0 && page == PAGE_PLAY; // touches belong to the field
    bool active = get_scroll_page() == 0 && (input || latch); // Latch: keep sounding on Edit/Load pages
    if (need_release) {
      release_all();
      need_release = false;
    }
    read_pressure_pads(input);
    if (input || !latch) read_cursor(input); // latched: cursor stays frozen while editing the sound
    uint32_t fine = freerunning_clock_32bit() >> 12;
    if (!inited) {
      last_fine = fine;
      inited = true;
    }
    if (!active) {
      if (cm >= 0 || v_on[0] || v_on[1] || v_on[2] || v_on[3]) release_all();
      cm = -1;
      last_fine = fine;
      return;
    }
    if (qi == 0 && finger != cm) commit(fine); // quantise Off: adopt immediately
    int guard = 0;
    while (last_fine != fine && guard++ < 8) {
      ++last_fine;
      on_fine(last_fine);
    }
    last_fine = fine;
    for (int v = 0; v < NV; ++v)
      if (v_on[v]) send_voice(v, false); // continuous vibrato / tremolo / filter / delay
  }

  // ---------- drawing ----------
  void draw_field() {
    int l = scale_len(mode);
    int c0 = -1, r0 = -1;
    if (cm >= 0) {
      c0 = cm % FW;
      r0 = cm / FW;
    }
    for (int c = 0; c < FW; ++c)
      for (int r = 0; r < FH; ++r) {
        bool root = (c % l) == 0 || (r % l) == 0;
        uint32_t col = root ? DIMMEST(WHITE) : 0; // neutral grey: clearly apart from the coloured function pads
        if (c == c0 || r == r0) col = add_col(col, DIMMEST(WHITE));
        set_led(FX + c, 15 - r, col);
      }
    if (finger >= 0 && finger != cm) set_led(FX + finger % FW, 15 - finger / FW, WHITE);
    if (cm >= 0) {
      set_led(FX + c0, 15 - r0, BRIGHTER(WHITE));
      int x1 = c0, x2 = contrary ? FW - 1 - c0 : mini(FW - 1, c0 + (int)interval);
      int y1 = r0, y2 = contrary ? FH - 1 - r0 : maxi(0, r0 - (int)interval);
      static const uint32_t vc[NV] = {BLUE, PURPLE, GREEN, RED};
      int px[NV] = {FX + x1, FX + x2, 15, 15};
      int py[NV] = {15, 15, 15 - y1, 15 - y2};
      for (int v = 0; v < NV; ++v) set_led(px[v], py[v], v_on[v] ? BRIGHTER(vc[v]) : vc[v]);
    }
  }

  void draw_pressure_pads() {
    static const uint32_t pc[6] = {CYAN, TEAL, ORANGE, YELLOW, PURPLE, RED};
    static const char *const names[6] = {"Vibrato", "Tremolo", "Filter", "Accent", "Delay", "Random variation"};
    for (int k = 0; k < 6; ++k) {
      uint32_t col = add_col(DIMMESTEST(pc[k]), fade_col(pc[k], clampi((int)pv[k] * 2, 0, 255)));
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) set_led(pad_x(k) + dx, pad_y(k) + dy, col);
      if (pv[k] > 10) set_help_text(names[k]);
    }
  }

  bool tap(int x, int y, uint32_t col, bool on, const char *help, bool isolated = ISOLATED) {
    return button(x, y, on ? BRIGHTER(col) : DIMMEST(col), isolated, help);
  }

  void draw_taps() {
    static const char *const mode_help[6] = {"Minor pentatonic", "Harmonic minor", "Major 9", "Diatonic", "Pentatonic", "Quartal"};
    static const char *const treat_help[4] = {"Chord", "Arpeggio", "Line", "Improv"};
    static const char *const art_help[3] = {"Staccato", "Half legato", "Legato"};
    static const char *const snd_help[SND] = {"Sound 1", "Sound 2", "Sound 3"};
    static const char *const qhelp[2] = {"Quantise off", "Quantise 1/16"};
    const uint32_t sound_col = LED_RGB(31, 0, 22); // magenta: a warmer sibling of the PINK Edit-sound pad
    for (int i = 0; i < 4; ++i)
      // row 6 touches the Delay/Random pressure pads (row 5): not isolated, so it still reacts while they are held
      if (tap(i, 6, ORANGE, mode == i, mode_help[i], NOT_ISOLATED)) mode = (uint8_t)i;
    for (int i = 0; i < 2; ++i)
      if (tap(i, 7, ORANGE, mode == 4 + i, mode_help[4 + i])) mode = (uint8_t)(4 + i);
    if (tap(2, 7, GREEN, !contrary, "Parallel motion")) contrary = 0;
    if (tap(3, 7, GREEN, contrary, "Contrary motion")) contrary = 1;
    for (int i = 0; i < 4; ++i)
      if (tap(i, 8, CYAN, treat == i, treat_help[i])) treat = (uint8_t)i;
    for (int i = 0; i < 3; ++i)
      if (tap(i, 9, PINK, art == i, art_help[i])) art = (uint8_t)i;
    if (tap(3, 9, YELLOW, latch, "Latch cursor")) latch = !latch;
    for (int i = 0; i < 11; ++i) {
      int x = i & 3, y = 10 + (i >> 2);
      if (tap(x, y, TEAL, pat == i, i == 0 ? "Pattern off" : "Pattern")) pat = (uint8_t)i;
    }
    if (button(3, 12, fade_col(WHITE, 50 + qi * 50), ISOLATED, qhelp[qi])) qi = (uint8_t)((qi + 1) % 2);
    if (tap(0, 13, BLUE, false, "Interval -")) interval = (uint8_t)maxi(1, interval - 1);
    if (tap(1, 13, BLUE, false, "Interval +")) interval = (uint8_t)mini(7, interval + 1);
    if (tap(2, 13, PURPLE, false, "Transpose -")) trn12 = (uint8_t)maxi(0, trn12 - 1);
    if (tap(3, 13, PURPLE, false, "Transpose +")) trn12 = (uint8_t)mini(24, trn12 + 1);
    if (tap(0, 14, BLUE, xo2 < 2, "X octave -")) xo2 = (uint8_t)maxi(0, xo2 - 1);
    if (tap(1, 14, BLUE, xo2 > 2, "X octave +")) xo2 = (uint8_t)mini(4, xo2 + 1);
    if (tap(2, 14, GREEN, yo2 < 2, "Y octave -")) yo2 = (uint8_t)maxi(0, yo2 - 1);
    if (tap(3, 14, GREEN, yo2 > 2, "Y octave +")) yo2 = (uint8_t)mini(4, yo2 + 1);
    for (int i = 0; i < SND; ++i)
      if (tap(i, 15, sound_col, snd == i, snd_help[i])) {
        snd = (uint8_t)i;
        need_release = true;
      }
    if (button(3, 15, DIMMEST(PINK), ISOLATED, "Edit sound")) page = PAGE_EDIT;
  }

  void on_ui(int delta_time_us) override {
    (void)delta_time_us;
    int scroll_page = get_scroll_page();
    if (scroll_page < 0) return;
    leds_clear();
    if (scroll_page == 1) {
      panel_page.saveload(16, true, FLAG_PICKER_ENABLE_DELETE);
      return;
    }
    if (scroll_page > 1) {
      scroll_to_page(1);
      return;
    }
    if (page == PAGE_EDIT) {
      preset_pages.edit(snd, 0, 0, false);
      preset_pages.xy_pad(snd, 8, 10);
    } else if (page == PAGE_LOAD) {
      int action = preset_pages.saveload_action(snd, 0, FLAG_PICKER_ENABLE_DELETE);
      if (action) page = PAGE_PLAY;
      preset_pages.xy_pad(snd, 8, 10);
    }
    if (page != PAGE_PLAY) {
      if (button(0, 15, page == PAGE_EDIT ? BRIGHTER(PINK) : DIMMEST(PINK), ISOLATED, "Edit sound"))
        page = page == PAGE_EDIT ? (uint8_t)PAGE_PLAY : (uint8_t)PAGE_EDIT;
      if (button(1, 15, page == PAGE_LOAD ? BRIGHTER(PINK) : DIMMEST(PINK), ISOLATED, "Load sound"))
        page = page == PAGE_LOAD ? (uint8_t)PAGE_PLAY : (uint8_t)PAGE_LOAD;
      return;
    }
    draw_field();
    draw_pressure_pads();
    draw_taps();
  }

  bool on_serialise(serialiser_t &s, int version) override;
};

static bool serialise(serialiser_t &s, spiegel_12 &o) {
  OBJECT_BEGIN(s);
  FIELD_SYNTH_PRESET("preset1", 0);
  FIELD_SYNTH_PRESET("preset2", 1);
  FIELD_SYNTH_PRESET("preset3", 2);
  FIELD_MIX_PRESET("presetMix");
  FIELD("mode", o.mode, 0u, 5u);
  FIELD("contrary", o.contrary, 0u, 1u);
  FIELD("treat", o.treat, 0u, 3u);
  FIELD("art", o.art, 0u, 2u);
  FIELD("pat", o.pat, 0u, 10u);
  FIELD("interval", o.interval, 1u, 7u);
  FIELD("trn12", o.trn12, 0u, 24u);
  FIELD("xo2", o.xo2, 0u, 4u);
  FIELD("yo2", o.yo2, 0u, 4u);
  FIELD("snd", o.snd, 0u, 2u);
  FIELD("latch", o.latch, 0u, 1u);
  FIELD("qi", o.qi, 0u, 1u);
  OBJECT_END(s);
  return true;
}

bool spiegel_12::on_serialise(serialiser_t &s, int version) {
  (void)version;
  bool ok = serialise(s, *this);
  if (ok && s.reading) {
    finger = cm = -1;
    need_release = true;
  }
  return ok;
}
