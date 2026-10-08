/*
@Name: PENDULUM
@Author: PERPLEX ON
@Firmware: beta
@Tags: sequencer, physics, pendulum, pressure, midi
@Preferred Panels: blocks
@Description: Pendulum music box. Swipe a row to launch a damped pendulum.

Each of the 16 rows is a pendulum lane. Row height = pitch (scale degrees, top row = highest).
Swipe speed sets the swing rate (quantised to the main clock), swipe direction sets the launch wall,
swipe pressure sets velocity and energy (alternatively: a settings page switches the rate source to swipe
distance from the launch-side edge toward the middle, 8 pads = fastest, for performers who prefer a
distance-based feel over a speed-based one). Every wall contact plays a note: left wall = row note,
right wall = the in-key fifth (scale note nearest +7 semitones). Energy decays with every hit, the pendulum slows down and finally rests.
Double-tap a row to clear it.
Swipe detection is event based (press timing per pad), so fast swipes that skip pads and drifting fingers still work.
*/
#define PANEL_PAD_COLOR PINK

struct pendulum : panel_t {
  // ---- constants -------------------------------------------------------------------------------------------------
  static constexpr int LANES = 16;
  static constexpr int NUM_RATES = 10;
  static constexpr int SWIPE_MIN_PADS = 3;   // pad distance between first and furthest press that counts as a swipe
  static constexpr int MAX_SESS = 4;         // simultaneous finger gestures
  static constexpr int MAX_EV = 16;          // position events remembered per gesture
  static constexpr int ATTACH_MAX_DX = 4;    // a new press within this many pads / +-1 row continues a gesture
  static constexpr int REST_ENERGY = 20;
  static constexpr int SYNTH_PRESET_IDX = 0;
  static constexpr uint8_t VOICE_PRIO = 16;    // base priority; newer lanes rank above older ones
  static constexpr int BUSY_ENV = 1500;        // env level (0..65535) above which a voice counts as audible
  static constexpr int VOICE_POOL = MAX_VOICES; // use every physical voice
  static constexpr uint32_t VOICE_SOURCE_BASE = 0x464c0000u;
  static constexpr uint32_t REST_FADE_US = 700000u;
  static constexpr uint32_t FLASH_US = 240000u;
  static constexpr uint32_t DOUBLE_TAP_US = 400000u;
  static constexpr uint32_t SESS_GRACE_US = 90000u; // keep a gesture alive across debounce gaps / skipped pads
  static constexpr uint32_t TAP_MAX_US = 350000u;
  static constexpr uint32_t REPRESS_US = 60000u;       // finger fully off this long, then pressed again = a new gesture
  static constexpr uint32_t SESS_CONTINUE_US = 40000u; // a press this soon after the last contact still belongs to the same finger
  static constexpr uint32_t SESS_CONTINUE_LONG_US = 150000u; // ...if it also lands ahead of the swipe direction
  static constexpr int CONTACT_MIN = 20;               // raw pressure that counts as finger contact, below the debounced 'down' threshold
  static constexpr uint32_t SWIPE_SETTLE_US = 70000u;  // no new pad for this long -> measure now
  static constexpr int SWIPE_FIRM_PADS = 4;            // ...or as soon as the finger is this far from its first pad

  enum lane_state_t : uint8_t { LS_IDLE, LS_ARMED, LS_RUN, LS_REST };
  enum request_t : uint8_t { REQ_NONE, REQ_LAUNCH, REQ_CLEAR };

  struct lane_t {
    uint8_t state;
    int8_t dir;        // +1 = travelling left->right, -1 = right->left
    uint8_t base_rate; // rate index chosen by swipe speed
    uint8_t rate;      // current rate index (slows down with friction)
    uint8_t hits;
    uint8_t energy;    // 0..255, decays on every wall contact
    uint8_t rest_wall;
    uint8_t speed_q8;  // normalised pendulum speed, for the LED trail
    uint16_t ticks_into;
    uint16_t half_ticks; // duration of one leg, in 1/64 note ticks
    uint16_t pos_q16;    // eased position 0 (left wall) .. 65535 (right wall)
    // request from UI thread
    uint8_t req;
    int8_t pend_dir;
    uint8_t pend_rate;
    uint8_t pend_energy;
    // visual hit info
    uint32_t hit_us[2];
    uint8_t hit_vel[2];
    uint32_t rest_us;
    // gate / note state
    bool gate_on;
    int8_t gate_voice;
    uint8_t gate_note;
    uint8_t gate_vel;
    uint32_t gate_off_us;
    uint32_t launch_seq; // order of launch: higher = newer
  };

  struct swipe_ev_t {
    int8_t x;
    uint32_t t;
  };
  struct session_t { // one finger gesture, tracked from pad-press events (independent of the touch "origin" heuristic)
    bool active, fired, down, sdown; // down = contact (incl. sub-threshold), sdown = debounced pad down
    int8_t row;     // lane the gesture belongs to (row of the strongest first press)
    int8_t cur_row; // row of the latest press, tolerates vertical drift of +-1 row
    int8_t last_x;
    int8_t dir; // swipe direction once fired
    int8_t n;
    uint16_t peak;
    uint32_t t_start, t_last_down, s_last; // s_last = last time a debounced pad was down
    swipe_ev_t ev[MAX_EV];
  };
  struct newp_t {
    int8_t x, y;
    uint16_t p;
  };

  lane_t lane[LANES];
  session_t sess[MAX_SESS];
  newp_t newp[8];
  // tap detection is per row on debounced pads only, independent of the swipe sessions
  bool row_down[LANES], row_swiped[LANES];
  int8_t row_minx[LANES], row_maxx[LANES];
  uint32_t row_t0[LANES];
  uint32_t last_tap_time = 0;
  int8_t last_tap_row = 0;
  clock_divider_t div;
  voice_allocator_t voice_allocator;
  uint32_t launch_counter;
  preset_pages_t preset_pages;
  panel_page_t panel_page;
  int8_t oct = 0;    // -2..+2 octaves relative to C3
  uint8_t damp = 3;  // 0..8, 0 = off (endless, no slowdown), higher = faster energy decay
  uint8_t speed_mode = 0; // 0 = swipe speed (default), 1 = swipe distance from the launch edge to mid-grid
  bool settings_changed = false;

  int get_num_pages() override { return 3; }                // 0 = play, 1 = sound, 2 = save/load
  int get_num_panel_settings_pages() override { return 5; } // key, scale, octave, damping, speed source

  // ---- small helpers ---------------------------------------------------------------------------------------------
  static inline int iabs(int v) { return v < 0 ? -v : v; }

  static int half_ticks_for(int rate) {
    static const uint8_t table[NUM_RATES] = {2, 4, 6, 8, 12, 16, 24, 32, 48, 64};
    return table[clampi(rate, 0, NUM_RATES - 1)];
  }

  static int rate_from_speed(uint32_t pads_per_s) {
    static const uint16_t thr[NUM_RATES - 1] = {110, 80, 60, 45, 32, 22, 15, 10, 6};
    for (int i = 0; i < NUM_RATES - 1; ++i)
      if (pads_per_s >= thr[i]) return i;
    return NUM_RATES - 1;
  }

  // Distance-based alternative: rate derives from how far the swipe reached from the launch-side edge
  // (x=0 for a left-to-right launch, x=15 for right-to-left) toward the middle, not from gesture timing.
  // 8 pads (edge to mid-grid) = fastest rate; SWIPE_MIN_PADS = slowest. Pure integer math, no gesture speed used.
  static int rate_from_distance(int dir, int far_x) {
    int edge = dir > 0 ? 0 : 15;
    int dist = clampi(iabs(far_x - edge), SWIPE_MIN_PADS, 8);
    int span = 8 - SWIPE_MIN_PADS;
    int rate = ((8 - dist) * (NUM_RATES - 1) + span / 2) / span;
    return clampi(rate, 0, NUM_RATES - 1);
  }

  static uint32_t ease_q16(uint32_t f) { // smoothstep: slow at the walls, fast in the middle
    uint64_t f2 = ((uint64_t)f * f) >> 16;
    uint64_t s = (f2 * (3u * 65536u - 2u * f)) >> 16;
    return s > 65535u ? 65535u : (uint32_t)s;
  }

  // OKLCH-derived at the sRGB gamut cusp (max saturation for readability), hue amber -> red -> magenta -> violet -> blue -> cyan.
  // 'pale' = same hue, lighter tint, used for the right (fifth) wall.
  static uint32_t row_col(int row, bool pale) {
    static const uint32_t base[16] = {
        LED_RGB(31, 13, 0), LED_RGB(31, 6, 0), LED_RGB(31, 0, 9), LED_RGB(31, 0, 14),
        LED_RGB(31, 0, 19), LED_RGB(31, 0, 25), LED_RGB(30, 0, 31), LED_RGB(23, 0, 31),
        LED_RGB(18, 2, 31), LED_RGB(13, 0, 31), LED_RGB(8, 1, 31), LED_RGB(0, 11, 31),
        LED_RGB(1, 19, 31), LED_RGB(0, 22, 31), LED_RGB(0, 25, 31), LED_RGB(0, 26, 29)};
    static const uint32_t hi[16] = {
        LED_RGB(31, 20, 15), LED_RGB(31, 16, 13), LED_RGB(31, 14, 15), LED_RGB(31, 15, 19),
        LED_RGB(31, 15, 22), LED_RGB(31, 15, 26), LED_RGB(31, 16, 31), LED_RGB(25, 13, 31),
        LED_RGB(21, 13, 31), LED_RGB(16, 12, 31), LED_RGB(11, 12, 31), LED_RGB(9, 16, 31),
        LED_RGB(14, 23, 31), LED_RGB(16, 25, 31), LED_RGB(20, 28, 31), LED_RGB(18, 29, 31)};
    int pos = clampi(15 - row, 0, 15); // top row = highest pitch = coolest colour
    return pale ? hi[pos] : base[pos];
  }

  // Guard against invalid key/scale at boot (both boot paths call this).
  static void ensure_harmony() {
    bool bad_key = current_key > 127;
    bool bad_scale = (current_scale & 0x0fff) == 0;
    if (bad_key || bad_scale)
      set_current_key_and_scale(bad_key ? 0 : current_key, bad_scale ? SCALE_MAJOR : current_scale);
  }

  int lane_note(int row, int wall) {
    ensure_harmony();
    int pos = 15 - row;
    int root_note = clampi(48 + 12 * oct + (current_key % 12), 0, 127);
    int n = scale_play_surface_note(root_note, pos, current_key, current_scale);
    if (wall) {
      // "Fifth" = the scale note closest to +7 semitones (perfect, diminished or augmented depending on the
      // degree), so the right wall is always in key. Built from scale_play_surface_note only.
      int best = n + 7, best_d = 99;
      for (int k = 1; k <= 8; ++k) {
        int c = scale_play_surface_note(root_note, pos + k, current_key, current_scale);
        int d = iabs(c - n - 7);
        if (d < best_d) {
          best_d = d;
          best = c;
        }
      }
      n = best;
    }
    return clampi(n, 0, 127);
  }

  void setup_default_panel_state() override {
    panel_t::setup_default_panel_state();
    memset(lane, 0, sizeof(lane));
    memset(sess, 0, sizeof(sess));
    memset(row_down, 0, sizeof(row_down));
    memset(row_swiped, 0, sizeof(row_swiped));
    memset(row_minx, 0, sizeof(row_minx));
    memset(row_maxx, 0, sizeof(row_maxx));
    memset(row_t0, 0, sizeof(row_t0));
    last_tap_time = 0;
    memset(&voice_allocator, 0, sizeof(voice_allocator));
    launch_counter = 0;
    memset(&div, 0, sizeof(div));
    oct = 0;
    damp = 3;
    speed_mode = 0;
    ensure_harmony();
  }

  void on_load_finished() override { ensure_harmony(); }

  bool on_serialise(serialiser_t &s, int version) override;

  bool on_serialise_settings(serialiser_t &s, int version) override {
    (void)version;
    OBJECT_BEGIN(s);
    FIELD("oct", oct, -2, 2);
    FIELD("damp", damp, 0u, 8u);
    FIELD("spdmode", speed_mode, 0u, 1u);
    OBJECT_END(s);
    return true;
  }

  // ---- sequence thread -------------------------------------------------------------------------------------------
  void end_gate(int row) {
    lane_t &L = lane[row];
    if (L.gate_voice >= 0) synth_note_up(L.gate_voice);
    voice_allocator.voice_allocate(VOICE_SOURCE_BASE + row, 0, 0, VOICE_POOL);
    L.gate_voice = -1;
    L.gate_on = false;
  }

  void trigger(int row, int wall, int vel, uint32_t now) {
    lane_t &L = lane[row];
    int note = lane_note(row, wall);
    int64_t half_us = ((int64_t)L.half_ticks * qn_period_us) >> 4; // 1 tick = 1/16 quarter note
    int gate_us = clampi((int)((half_us * 2) / 5), 8000, 45000);
    // load guard: count audible voices (gated or still ringing). When the pool runs low, shorten the gate so voices
    // free up sooner and stealing stays the exception. 3+ free = normal, 2 free = 75%, 1 free = 50%, 0 free = 25%.
    int busy = 0;
    for (int i = 0; i < VOICE_POOL; i++) if (get_synth_env_level_q16(i) > BUSY_ENV) busy++;
    int free_v = VOICE_POOL - busy;
    if (free_v < 3) gate_us = maxi(8000, (gate_us * (free_v <= 0 ? 1 : free_v == 1 ? 2 : 3)) >> 2);
    uint32_t src = VOICE_SOURCE_BASE + row;
    // recency priority: the newest lane gets the highest priority, so it steals from older lanes, never the reverse.
    // Stealing only happens when all voices are busy; otherwise every lane keeps its own voice.
    int rank = 0;
    for (int i = 0; i < LANES; i++) if (i != row && lane[i].state == LS_RUN && (int32_t)(lane[i].launch_seq - L.launch_seq) > 0) rank++;
    uint8_t prio = (uint8_t)(VOICE_PRIO + LANES - rank);
    int old_voice = voice_allocator.find_voice(src, 0, VOICE_POOL);
    int v = voice_allocator.voice_allocate(src, prio, 0, VOICE_POOL, SYNTH_PRESET_IDX);
    if (v != old_voice && old_voice >= 0) synth_note_up(old_voice);
    L.gate_voice = (int8_t)v;
    if (v >= 0) play_synth(v, SYNTH_PRESET_IDX, vel, note << 8, true);
    L.gate_on = true;
    L.gate_note = (uint8_t)note;
    L.gate_vel = (uint8_t)vel;
    L.gate_off_us = now + (uint32_t)gate_us;
    L.hit_us[wall] = now | 1u;
    L.hit_vel[wall] = (uint8_t)vel;
  }

  void do_hit(int row, int wall, uint32_t now) {
    static const uint8_t decay[8] = {254, 252, 250, 246, 241, 234, 226, 214}; // per-hit energy retention (q8), gentle: default ~40 hits
    lane_t &L = lane[row];
    trigger(row, wall, clampi(L.energy >> 1, 1, 127), now);
    if (L.hits < 255) L.hits++;
    if (damp > 0) { // damp 0 = no energy loss and no slowdown: the pendulum runs until cleared or relaunched
      L.energy = (uint8_t)((L.energy * decay[damp - 1]) >> 8);
      int slow = mini(3, L.hits / 10); // friction: the pendulum stretches its legs as it tires
      L.rate = (uint8_t)mini(NUM_RATES - 1, L.base_rate + slow);
      L.half_ticks = (uint16_t)half_ticks_for(L.rate);
    }
    if (L.energy < REST_ENERGY) {
      L.state = LS_REST;
      L.rest_wall = (uint8_t)wall;
      L.rest_us = now;
      L.pos_q16 = wall ? 65535 : 0;
      L.speed_q8 = 0;
    }
  }

  void lane_tick(int row, uint32_t step, uint32_t now) {
    lane_t &L = lane[row];
    if (L.state == LS_ARMED) {
      int grid = mini(half_ticks_for(L.base_rate), 16);
      if (step % (uint32_t)grid) return; // wait for the lane's own grid line on the main clock
      L.state = LS_RUN;
      L.launch_seq = ++launch_counter;
      L.dir = L.pend_dir;
      L.hits = 0;
      L.energy = L.pend_energy;
      L.rate = L.base_rate;
      L.half_ticks = (uint16_t)half_ticks_for(L.rate);
      L.ticks_into = 0;
      do_hit(row, L.dir > 0 ? 0 : 1, now); // the launch itself plucks the start wall
      return;
    }
    if (L.state != LS_RUN) return;
    if (++L.ticks_into >= L.half_ticks) {
      L.ticks_into = 0;
      int wall = L.dir > 0 ? 1 : 0;
      L.dir = (int8_t)-L.dir;
      do_hit(row, wall, now);
    }
  }

  void on_sequence(int delta_time_us) override {
    (void)delta_time_us;
    uint32_t now = time_us();
    ensure_harmony();

    for (int i = 0; i < LANES; ++i) {
      lane_t &L = lane[i];
      if (L.req == REQ_LAUNCH) {
        L.state = LS_ARMED;
        L.base_rate = L.pend_rate;
        L.req = REQ_NONE;
      } else if (L.req == REQ_CLEAR) {
        L.state = LS_IDLE;
        L.req = REQ_NONE;
        if (L.gate_on) end_gate(i);
      }
    }

    // 1/64 note grid on the main clock; freerunning so it also works with the transport stopped.
    int edges = div.update(-1, 16, 1, UPDATE_DIV_NOW_AND_SNAP_CLOCK_BASE_TO_QUARTER_NOTE, true);
    edges = clampi(edges, 0, 4);
    uint32_t last_step = div.step_index();
    for (int e = edges - 1; e >= 0; --e)
      for (int i = 0; i < LANES; ++i) lane_tick(i, last_step - (uint32_t)e, now);

    uint32_t ph = div.phase_q16();
    for (int i = 0; i < LANES; ++i) {
      lane_t &L = lane[i];
      if (L.state == LS_RUN) {
        uint32_t f = ((uint32_t)L.ticks_into * 65536u + ph) / (uint32_t)maxi(1, L.half_ticks);
        if (f > 65535u) f = 65535u;
        uint32_t e = ease_q16(f);
        L.pos_q16 = (uint16_t)(L.dir > 0 ? e : 65535u - e);
        L.speed_q8 = (uint8_t)mini(255, (int)(((uint64_t)f * (65536u - f) * 4u) >> 24));
      } else if (L.state == LS_REST && (uint32_t)(now - L.rest_us) > REST_FADE_US) {
        L.state = LS_IDLE;
      }
      if (L.gate_on && (int32_t)(now - L.gate_off_us) >= 0) end_gate(i);
    }

    for (int i = 0; i < LANES; ++i) // level-triggered MIDI: one channel per row
      if (lane[i].gate_on) declare_midi_note((uint8_t)i, lane[i].gate_note, lane[i].gate_vel);
    send_declared_midi_notes();
  }

  // ---- UI thread -------------------------------------------------------------------------------------------------
  void request_launch(int row, int dir, int rate, int energy) {
    on_sequence_lock_guard_t guard;
    lane_t &L = lane[row];
    L.pend_dir = (int8_t)dir;
    L.pend_rate = (uint8_t)rate;
    L.pend_energy = (uint8_t)clampi(energy, 40, 255);
    L.req = REQ_LAUNCH;
  }

  void request_clear(int row) {
    on_sequence_lock_guard_t guard;
    lane[row].req = REQ_CLEAR;
  }

  // While a launch is still waiting for its grid line, later (higher) pressure of the same gesture still counts.
  void bump_pending_energy(int row, int energy) {
    on_sequence_lock_guard_t guard;
    lane_t &L = lane[row];
    if (L.req == REQ_LAUNCH || L.state == LS_ARMED) L.pend_energy = (uint8_t)maxi((int)L.pend_energy, clampi(energy, 40, 255));
  }

  session_t *find_session(int x, int y, uint32_t now) {
    session_t *best = nullptr;
    int best_d = 99;
    for (int i = 0; i < MAX_SESS; ++i) {
      session_t &S = sess[i];
      if (!S.active || iabs(y - S.cur_row) > 1) continue;
      if (!S.fired && !S.sdown && (uint32_t)(now - S.s_last) > REPRESS_US) continue; // finger was lifted: new landing
      if (S.fired && !S.down) { // finished swipe: only a press right behind the finger's path continues it
        uint32_t gap = now - S.t_last_down;
        int fwd = (x - S.last_x) * S.dir;
        if (gap > SESS_CONTINUE_LONG_US || (gap > SESS_CONTINUE_US && (fwd < 1 || fwd > 3))) continue;
      }
      int d = iabs(x - S.last_x);
      if (d <= ATTACH_MAX_DX && d < best_d) {
        best_d = d;
        best = &S;
      }
    }
    return best;
  }

  session_t *new_session(const newp_t &np, uint32_t now) {
    for (int i = 0; i < MAX_SESS; ++i) { // a fresh landing replaces an unfinished gesture at the same spot
      session_t &O = sess[i];
      if (O.active && !O.fired && iabs(np.y - O.cur_row) <= 1 && iabs(np.x - O.last_x) <= ATTACH_MAX_DX) O.active = false;
    }
    session_t *slot = &sess[0];
    for (int i = 0; i < MAX_SESS; ++i) {
      if (!sess[i].active) {
        slot = &sess[i];
        break;
      }
      if ((uint32_t)(sess[i].t_start - slot->t_start) > 0x80000000u) slot = &sess[i]; // else steal the oldest
    }
    memset(slot, 0, sizeof(*slot));
    slot->active = slot->down = slot->sdown = true;
    slot->s_last = now;
    slot->row = slot->cur_row = np.y;
    slot->last_x = np.x;
    slot->n = 1;
    slot->ev[0].x = np.x;
    slot->ev[0].t = now;
    slot->peak = np.p;
    slot->t_start = slot->t_last_down = now;
    return slot;
  }

  void add_event(session_t &S, const newp_t &np, uint32_t now) {
    S.cur_row = np.y;
    S.down = true;
    S.t_last_down = now;
    S.peak = (uint16_t)maxi((int)S.peak, (int)np.p);
    for (int i = 0; i < S.n; ++i)
      if (S.ev[i].x == np.x) return; // same column (e.g. neighbour row of a straddling finger)
    if (S.n < MAX_EV) {
      S.ev[S.n].x = np.x;
      S.ev[S.n].t = now;
      S.n++;
      S.last_x = np.x;
    }
  }

  void fire_session(session_t &S, int dir, int far_x, uint32_t pads_per_s) {
    int vel = clampi(touch_pressure_curve_q7(S.peak), 24, 127);
    int rate = speed_mode ? rate_from_distance(dir, far_x) : rate_from_speed(pads_per_s);
    request_launch(S.row, dir, rate, vel * 2);
    S.fired = true;
    S.dir = (int8_t)dir;
    for (int y = maxi(0, S.row - 1); y <= mini(15, S.row + 1); ++y) row_swiped[y] = true; // not a tap
    last_tap_time = 0;
  }

  // Swipe = a run of new pad presses that moves >= SWIPE_MIN_PADS away from the first press. Works with skipped
  // pads (fast swipes), vertical drift, and pads that flicker; speed is taken from press timing, not from position.
  bool try_fire_from_events(session_t &S, uint32_t now) {
    int kbest = 0, span = 0;
    for (int i = 1; i < S.n; ++i) {
      int d = iabs(S.ev[i].x - S.ev[0].x);
      if (d >= span) {
        span = d;
        kbest = i;
      }
    }
    if (span < SWIPE_MIN_PADS) return false;
    // Speed mode: fire as soon as the gesture is firmly a swipe, so timing stays close to the actual motion.
    // Distance mode: the final reach is what sets the rate, so wait for the finger to pause or lift before
    // locking it in - firing early on a partial span would understate how far the swipe was meant to go.
    bool ready = !S.down || (uint32_t)(now - S.ev[S.n - 1].t) >= SWIPE_SETTLE_US || (speed_mode == 0 && span >= SWIPE_FIRM_PADS);
    if (!ready) return false;
    int dxp = span;
    uint32_t dt = S.ev[kbest].t - S.ev[0].t;
    if (kbest >= 2 && S.ev[kbest].x != S.ev[1].x) { // ignore the landing dwell: time from the 2nd press onward
      dxp = iabs(S.ev[kbest].x - S.ev[1].x);
      dt = S.ev[kbest].t - S.ev[1].t;
    }
    dt = (uint32_t)maxi(4000, (int)dt);
    fire_session(S, S.ev[kbest].x > S.ev[0].x ? 1 : -1, S.ev[kbest].x, ((uint32_t)dxp * 1000000u) / dt);
    return true;
  }

  void update_sessions(uint32_t now) {
    for (int i = 0; i < MAX_SESS; ++i) {
      session_t &S = sess[i];
      if (!S.active) continue;
      bool down = false, sd = false;
      int max_p = 0, fb_dx = 0, fb_x = 0, sum_p = 0, sum_xp = 0;
      int y0 = maxi(0, S.cur_row - 1), y1 = mini(15, S.cur_row + 1);
      for (int y = y0; y <= y1; ++y)
        for (int x = 0; x < 16; ++x) {
          int p = get_touch_pressure_xy(x, y);
          bool d = get_touch_down(x, y);
          if (d) sd = true;
          if (d || p >= CONTACT_MIN) down = true; // contact also counts below the debounced threshold: bridges pad handover
          if (p >= CONTACT_MIN) {
            max_p = maxi(max_p, p);
            sum_p += p;
            sum_xp += x * p;
          }
          if (d && !S.fired && iabs(get_touch_origin_y(x, y) - S.row) <= 1) { // fallback: hardware origin inheritance
            int dx = x - get_touch_origin_x(x, y);
            if (iabs(dx) > iabs(fb_dx)) {
              fb_dx = dx;
              fb_x = x;
            }
          }
        }
      if (sum_p > 0) { // pressure centroid: a continuous finger position even when pads are skipped or flicker
        int cx = (sum_xp + sum_p / 2) / sum_p;
        if (cx != S.last_x) {
          newp_t vp;
          vp.x = (int8_t)cx;
          vp.y = S.cur_row;
          vp.p = 0;
          add_event(S, vp, now);
        }
      }
      S.down = down;
      S.sdown = sd;
      if (sd) S.s_last = now;
      if (down) {
        S.t_last_down = now;
        S.peak = (uint16_t)maxi((int)S.peak, max_p);
      }
      if (!S.fired) {
        // The hardware-origin fallback only covers speed mode's eager-fire case; distance mode always
        // waits for release (handled above), so the event path alone is accurate there.
        if (!try_fire_from_events(S, now) && speed_mode == 0 && iabs(fb_dx) >= SWIPE_FIRM_PADS) {
          uint32_t dt = (uint32_t)maxi(4000, (int)(now - S.t_start));
          fire_session(S, fb_dx > 0 ? 1 : -1, fb_x, ((uint32_t)iabs(fb_dx) * 1000000u) / dt);
        }
      } else if (down) {
        bump_pending_energy(S.row, clampi(touch_pressure_curve_q7(S.peak), 24, 127) * 2);
      }
      if (!down && (uint32_t)(now - S.t_last_down) > SESS_GRACE_US) {
        S.active = false;
      }
    }
  }

  bool swipe_motion_near(int y) { // any gesture around this row that moved like a swipe
    for (int i = 0; i < MAX_SESS; ++i) {
      session_t &S = sess[i];
      if (!S.active || iabs(S.cur_row - y) > 1) continue;
      if (S.fired) return true;
      for (int k = 1; k < S.n; ++k)
        if (iabs(S.ev[k].x - S.ev[0].x) >= SWIPE_MIN_PADS) return true;
    }
    return false;
  }

  void register_tap(int y, uint32_t now) {
    if (last_tap_time && (uint32_t)(now - last_tap_time) < DOUBLE_TAP_US && iabs(y - last_tap_row) <= 1) {
      request_clear(y);
      last_tap_time = 0;
    } else {
      last_tap_time = now | 1u;
      last_tap_row = (int8_t)y;
    }
  }

  // Taps use debounced pads only (a decaying pressure tail must not glue two taps together).
  void update_taps(uint32_t now) {
    for (int y = 0; y < LANES; ++y) {
      int minx = 99, maxx = -1;
      for (int x = 0; x < 16; ++x)
        if (get_touch_down(x, y)) {
          minx = mini(minx, x);
          maxx = maxi(maxx, x);
        }
      bool rd = maxx >= 0;
      if (rd && !row_down[y]) {
        row_down[y] = true;
        row_swiped[y] = false;
        row_t0[y] = now;
        row_minx[y] = (int8_t)minx;
        row_maxx[y] = (int8_t)maxx;
      } else if (rd) {
        row_minx[y] = (int8_t)mini(row_minx[y], minx);
        row_maxx[y] = (int8_t)maxi(row_maxx[y], maxx);
      } else if (row_down[y]) {
        row_down[y] = false;
        if (!row_swiped[y] && row_maxx[y] - row_minx[y] <= 1 && (uint32_t)(now - row_t0[y]) < TAP_MAX_US && !swipe_motion_near(y))
          register_tap(y, now);
      }
    }
  }

  void track_touches(uint32_t now) {
    int nnew = 0;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x)
        if (nnew < 8 && get_touch_pressed(x, y)) {
          newp[nnew].x = (int8_t)x;
          newp[nnew].y = (int8_t)y;
          newp[nnew].p = (uint16_t)get_touch_pressure_xy(x, y);
          nnew++;
        }
    for (int i = 1; i < nnew; ++i) { // strongest press first: it decides the lane of a finger straddling two rows
      newp_t k = newp[i];
      int j = i - 1;
      while (j >= 0 && newp[j].p < k.p) {
        newp[j + 1] = newp[j];
        --j;
      }
      newp[j + 1] = k;
    }
    for (int i = 0; i < nnew; ++i) {
      session_t *S = find_session(newp[i].x, newp[i].y, now);
      if (S) add_event(*S, newp[i], now);
      else new_session(newp[i], now);
    }
    update_sessions(now);
    update_taps(now);
  }

  void draw_lane(int y, uint32_t now) {
    lane_t &L = lane[y];
    uint32_t col = row_col(y, false), pale = row_col(y, true);

    for (int x = 1; x < 15; ++x) set_led(x, y, fade_col(col, 20)); // faint rail
    uint32_t wall_col[2] = {fade_col(col, 110), fade_col(pale, 110)};
    for (int w = 0; w < 2; ++w) {
      uint32_t age = now - L.hit_us[w];
      if (L.hit_us[w] && age < FLASH_US) {
        int fl = 256 - (int)((age * 256u) / FLASH_US); // linear ramp, then gamma
        fl = (fl * fl) >> 8;
        fl = (fl * mini(256, 96 + L.hit_vel[w] * 2)) >> 8;
        wall_col[w] = add_col(wall_col[w], add_col(fade_col(pale, fl), fade_col(WHITE, fl)));
      }
    }
    set_led(0, y, wall_col[0]);
    set_led(15, y, wall_col[1]);

    int xq8 = 0, ef = 0, sp = 0, dirs = 1;
    if (L.state == LS_RUN) {
      xq8 = ((uint32_t)L.pos_q16 * 15u) >> 8;
      ef = 90 + ((L.energy * 166) >> 8);
      sp = L.speed_q8;
      dirs = L.dir;
    } else if (L.state == LS_ARMED) {
      xq8 = L.pend_dir > 0 ? 0 : 15 * 256;
      ef = ((now >> 17) & 1) ? 256 : 96; // blink while waiting for the grid line
    } else if (L.state == LS_REST) {
      xq8 = L.rest_wall ? 15 * 256 : 0;
      uint32_t age = now - L.rest_us;
      ef = age >= REST_FADE_US ? 0 : (int)((200u * (REST_FADE_US - age)) / REST_FADE_US);
    }
    if (ef > 0) {
      const int R = 352, TR = 640; // dot radius 1.375 pads, trail 2.5 pads (q8)
      uint32_t dot_col = lerp_col(col, pale, L.pos_q16 >> 8); // colour morphs base -> fifth tint while travelling
      if (L.state != LS_RUN) dot_col = (L.state == LS_ARMED ? (L.pend_dir > 0 ? col : pale) : (L.rest_wall ? pale : col));
      for (int x = 0; x < 16; ++x) {
        int d = iabs(x * 256 - xq8);
        int t = d < R ? ((R - d) * 186) >> 8 : 0;
        int b = (((t * t) >> 8) * ef) >> 8;
        if (sp > 0) {
          int behind = dirs > 0 ? (xq8 - x * 256) : (x * 256 - xq8);
          if (behind > 0 && behind < TR) {
            int tr = ((TR - behind) * 102) / TR;
            int gt = (((tr * tr) >> 8) * sp) >> 8;
            b = maxi(b, (gt * ef) >> 8);
          }
        }
        if (b > 0) set_led(x, y, maxcol(get_led(x, y), fade_col(dot_col, b)));
      }
    }
    for (int x = 0; x < 16; ++x) { // pressure feedback under the finger
      if (!get_touch_down(x, y)) continue;
      int v = clampi(touch_pressure_curve_q7(get_touch_pressure_xy(x, y)) * 2, 0, 255);
      set_led(x, y, add_col(get_led(x, y), fade_col(WHITE, v)));
    }
  }

  void draw_settings_page(int page) {
    if (page == -1) {
      static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
      int pc = current_key % 12;
      int delta = draw_system_style_settings_page("key", names[pc], pc * 100 / 11, 0, 0, row_col(4, false));
      if (delta) {
        int npc = ((pc + delta) % 12 + 12) % 12;
        set_current_key((uint8_t)mini(127, (current_key / 12) * 12 + npc));
      }
    } else if (page == -2) {
      // Same 12-scale set as Press Cafe 12 / Charge Bounce 16 (masks are 12-bit, root-relative).
      static const char *const names[12] = {"chrom", "major", "minor", "dor",  "phryg", "lyd",
                                            "mixo",  "locr",  "hmin",  "pmaj", "pmin",  "blues"};
      static const uint16_t masks[12] = {SCALE_CHROMATIC, SCALE_MAJOR, SCALE_MINOR, 0x6AD, 0x5AB, 0xAD5,
                                         0x6B5,           0x56B,       0x9AD,       0x295, 0x4A9, 0x4E9};
      int idx = 1; // fall back to major
      for (int i = 0; i < 12; ++i)
        if (masks[i] == current_scale) idx = i;
      int delta = draw_system_style_enum_settings_page("scl", idx, names, 12, row_col(8, false));
      if (delta) set_current_scale(masks[((idx + delta) % 12 + 12) % 12]);
    } else if (page == -3) {
      char b[6];
      snprintf(b, sizeof(b), "%+d", (int)oct);
      int delta = draw_system_style_settings_page("oct", b, (oct + 2) * 100 / 4, 0, 0, row_col(12, false));
      if (delta) {
        oct = (int8_t)clampi(oct + delta, -2, 2);
        settings_changed = true;
      }
    } else if (page == -4) {
      char b[6];
      snprintf(b, sizeof(b), damp ? "%d" : "off", (int)damp);
      int delta = draw_system_style_settings_page("damp", b, damp * 100 / 8, 0, 0, row_col(2, false));
      if (delta) {
        damp = (uint8_t)clampi(damp + delta, 0, 8);
        settings_changed = true;
      }
    } else if (page == -5) {
      // Backup control scheme: rate from swipe distance (edge to mid-grid) instead of swipe speed, for a
      // steadier feel when gesture timing varies (e.g. slower, deliberate performers or unsteady touch).
      static const char *const names[2] = {"time", "dist"};
      int delta = draw_system_style_enum_settings_page("mode", speed_mode, names, 2, row_col(14, false));
      if (delta) {
        speed_mode = (uint8_t)clampi((int)speed_mode + delta, 0, 1);
        settings_changed = true;
      }
    }
  }

  void on_ui(int delta_time_us) override {
    (void)delta_time_us;
    int page = get_scroll_page();
    if (page < 0) {
      draw_settings_page(page);
    } else if (page == 1) {
      leds_clear();
      preset_pages.edit(SYNTH_PRESET_IDX, 16, 0, true);
      preset_pages.xy_pad(SYNTH_PRESET_IDX, 8, 26);
    } else if (page == 2) {
      leds_clear();
      panel_page.saveload(32, true, FLAG_PICKER_ENABLE_DELETE);
    } else if (page > 2) {
      scroll_to_page(2);
    } else {
      uint32_t now = time_us();
      leds_clear();
      track_touches(now);
      for (int y = 0; y < LANES; ++y) draw_lane(y, now);
    }
    if (settings_changed) {
      (void)save_settings_to_sd(false);
      settings_changed = false;
    }
  }
};

static bool serialise(serialiser_t &s, pendulum &o) {
  OBJECT_BEGIN(s);
  FIELD_SYNTH_PRESET("preset", 0);
  FIELD_MIX_PRESET("presetMix");
  OBJECT_END(s);
  return true;
}

bool pendulum::on_serialise(serialiser_t &s, int version) {
  (void)version;
  return serialise(s, *this);
}