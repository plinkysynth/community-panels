/*
@Name: Kristallwuchs
@Author: PERPLEX ON
@Firmware: beta
@Tags: generative, physics, midi, synth, visuals
@Preferred Panels: blocks
@Description: Diffusion-limited aggregation as an instrument. Random walkers freeze onto a growing crystal and every docking plays a note.

Walkers wander in from the edges until they touch the crystal. Each docking plays a clock-quantized note:
the distance to the seed sets the scale degree (core = low, tips = high), and each seed has its own octave.
When the crystal reaches the border it melts from the outside in, plays its growth backwards, then regrows.

Play page
- Tap an empty pad: new seed (max 4). Tap a seed: remove it together with its crystal branch.
- Remove every seed to silence the panel; the walkers keep drifting until you tap a new seed.
- Hold a pad: your finger becomes a walker emitter, pressure = emission rate.
- Swipe: wind that bends the growth. Tilt the device: walkers drift downhill (adds to the wind).
- Long-press the bottom-right pad: performance overlay (density, walk speed, stickiness, tilt amount, panic, reset seeds).

Pages: Play / Sound (preset editor + XY pad) / Save-Load (panel slots incl. sound and mix).
Settings pages (above Play): root, scale, quantize division. They are saved automatically as panel settings.
Sound: built-in synth (preset 0) plus a MIDI mirror on its channel. Notes are always clock-quantized.
*/
#define PANEL_PAD_COLOR CYAN

static const int KR_PRESET = 0;
static const int KR_MAX_SEEDS = 4;
static const int KR_MAX_WALKERS = 48;
static const int KR_GATES = 8;
static const int KR_QUEUE = 8;
static const int KR_FINGERS = 8;
static const int KR_MELT_AT = 160;
static const int32_t KR_GATE_US = 200000;
static const int KR_BASE_NOTE = 48;

// ---- float helpers without libm ----
static float kr_sqrt(float x) {
  if (x <= 0.f) return 0.f;
  union {
    float f;
    uint32_t i;
  } u;
  u.f = x;
  u.i = 0x5f3759dfu - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - 0.5f * x * y * y);
  y = y * (1.5f - 0.5f * x * y * y);
  return x * y;
}

static float kr_absf(float x) { return x < 0.f ? -x : x; }

static int kr_round(float x) { return x >= 0.f ? (int)(x + 0.5f) : -(int)(-x + 0.5f); }

// ---- scale helpers (panel-owned harmony, no dependency on boot-time globals) ----
static uint16_t kr_scale_mask(int scale_idx) {
  static const uint16_t MASKS[8] = {0xAB5, 0x5AD, 0x6AD, 0x295, 0x4A9, 0x18D, 0xAD5, 0x5AB};
  return MASKS[clampi(scale_idx, 0, 7)];
}

static int kr_scale_note(int root, int scale_idx, int degree, int base) {
  uint16_t mask = kr_scale_mask(scale_idx);
  int n = count_bits(mask);
  if (n <= 0) return clampi(base + root, 0, 127);
  int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
  int d = degree - oct * n;
  int semi = 0;
  for (int i = 0, k = 0; i < 12; ++i) {
    if (mask & (1u << i)) {
      if (k == d) {
        semi = i;
        break;
      }
      ++k;
    }
  }
  return clampi(base + root + oct * 12 + semi, 0, 127);
}

struct kr_walker_t {
  int8_t x, y;
  uint16_t life;
  bool alive;
};

struct kr_gate_t {
  uint32_t source_id;
  int32_t remaining_us;
  int8_t voice;
  uint8_t note, vel;
};

struct kr_event_t {
  uint8_t note, vel, cell;
};

struct kr_gesture_t {
  float sx, sy, lx, ly, maxmove;
  uint32_t t0;
  bool emitting, ignore;
};

struct kristallwuchs : panel_t {
  // ---- panel settings (global, saved via on_serialise_settings) ----
  uint8_t root = 0;
  uint8_t scale_idx = 5; // hirajoshi
  uint8_t div_idx = 3;   // 1/16
  bool settings_changed = false;

  // ---- song state (saved in panel slots) ----
  uint8_t seed_pos[KR_MAX_SEEDS] = {};
  bool seed_on[KR_MAX_SEEDS] = {};
  uint8_t density = 16;  // base walker count
  uint8_t speed = 45;    // walk rate
  uint8_t stick = 70;    // docking probability in %
  uint8_t tilt_amt = 60; // accelerometer influence on the walkers in %

  // ---- runtime ----
  uint8_t cell[256] = {};      // 0 = empty, k+1 = crystal of seed k
  uint32_t stick_ms[256] = {}; // sim time of docking
  uint8_t ping[256] = {};      // note flash
  uint8_t ghost[256] = {};     // melt afterglow
  uint8_t order[256] = {};     // growth order, replayed backwards when melting
  uint8_t order_note[256] = {};
  uint8_t order_vel[256] = {};
  uint16_t order_count = 0;
  bool melting = false;
  kr_walker_t walkers[KR_MAX_WALKERS] = {};
  float wind_x = 0.f, wind_y = 0.f;
  float tilt_x = 0.f, tilt_y = 0.f; // -1..1, written in on_ui, read in on_sequence
  int8_t emit_x = -1, emit_y = 0;
  uint8_t emit_p = 0;
  uint32_t sim_us = 0;
  int32_t step_acc_us = 0, emit_acc_us = 0;

  voice_allocator_t voice_allocator;
  clock_divider_t quant;
  kr_gate_t gates[KR_GATES] = {};
  kr_event_t queue[KR_QUEUE] = {};
  uint8_t q_head = 0, q_count = 0;
  uint32_t hit_counter = 0;

  finger_tracker_t ft;
  kr_gesture_t gest[KR_FINGERS] = {};
  uint8_t prev_mask = 0;
  uint32_t tap_down_us[256] = {};
  bool tap_ok[256] = {};
  hold_timer_t corner_hold = {};
  bool overlay_open = false;
  slider_t sl_density = {}, sl_speed = {}, sl_stick = {}, sl_tilt = {};
  preset_pages_t preset_pages;
  panel_page_t panel_page;
  float fb[3][256] = {};

  // ---------------------------------------------------------------- params
  int div_num() const {
    static const int8_t DIVN[5] = {1, 2, 3, 4, 8};
    return DIVN[clampi(div_idx, 0, 4)];
  }
  int32_t step_interval_us() const { return 1000000 / (10 + speed * 2); } // 10..210 steps/s
  int num_seeds() const {
    int n = 0;
    for (int k = 0; k < KR_MAX_SEEDS; ++k) n += seed_on[k] ? 1 : 0;
    return n;
  }

  // ---------------------------------------------------------------- lifecycle
  void reset_crystal() {
    memset(cell, 0, sizeof(cell));
    memset(ghost, 0, sizeof(ghost));
    order_count = 0;
    melting = false;
    q_head = q_count = 0;
    for (int k = 0; k < KR_MAX_SEEDS; ++k)
      if (seed_on[k]) cell[seed_pos[k]] = (uint8_t)(k + 1);
    for (int i = 0; i < KR_MAX_WALKERS; ++i) walkers[i].alive = false;
  }

  void reset_runtime() {
    memset(&voice_allocator, 0, sizeof(voice_allocator));
    memset(gates, 0, sizeof(gates));
    memset(ping, 0, sizeof(ping));
    wind_x = wind_y = 0.f;
    emit_x = -1;
    step_acc_us = emit_acc_us = 0;
    ft.clear();
    memset(gest, 0, sizeof(gest));
    prev_mask = 0;
    overlay_open = false;
    reset_crystal();
  }

  void default_seeds() {
    for (int k = 0; k < KR_MAX_SEEDS; ++k) seed_on[k] = false;
    seed_on[0] = true;
    seed_pos[0] = 7 * 16 + 7;
  }

  void setup_default_panel_state() override {
    panel_t::setup_default_panel_state();
    default_seeds();
    reset_runtime();
  }

  void on_load_finished() override {
    reset_runtime(); // saved seed layout is kept as-is, including "no seeds" (silent)
  }

  // ---------------------------------------------------------------- note engine
  void push_event(uint8_t note, uint8_t vel, uint8_t c) {
    if (q_count >= KR_QUEUE) { // drop the oldest so the music stays current
      q_head = (uint8_t)((q_head + 1) % KR_QUEUE);
      --q_count;
    }
    kr_event_t &e = queue[(q_head + q_count) % KR_QUEUE];
    e.note = note;
    e.vel = vel;
    e.cell = c;
    ++q_count;
  }

  void play_note(uint8_t note, uint8_t vel) {
    uint32_t sid = 0x4b520000u | (hit_counter++ & 0xffffu);
    int v = voice_allocator.voice_allocate(sid, 1, 0, DEFAULT_VOICE_ALLOCATOR_VOICES, 0);
    if (v < 0) return;
    for (int i = 0; i < KR_GATES; ++i) // voice was stolen from a running gate: drop that gate
      if (gates[i].remaining_us > 0 && gates[i].voice == v) gates[i].remaining_us = 0;
    int slot = -1;
    for (int i = 0; i < KR_GATES; ++i)
      if (gates[i].remaining_us <= 0) {
        slot = i;
        break;
      }
    if (slot < 0) {
      slot = 0;
      for (int i = 1; i < KR_GATES; ++i)
        if (gates[i].remaining_us < gates[slot].remaining_us) slot = i;
      synth_note_up(gates[slot].voice);
      voice_allocator.voice_deallocate(gates[slot].source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
    }
    play_synth(v, KR_PRESET, vel, note << 8, true);
    kr_gate_t &g = gates[slot];
    g.source_id = sid;
    g.voice = (int8_t)v;
    g.note = note;
    g.vel = vel;
    g.remaining_us = KR_GATE_US;
  }

  void gate_tick(int dt_us) {
    for (int i = 0; i < KR_GATES; ++i) {
      kr_gate_t &g = gates[i];
      if (g.remaining_us <= 0) continue;
      g.remaining_us -= dt_us;
      if (g.remaining_us <= 0) {
        g.remaining_us = 0;
        synth_note_up(g.voice);
        voice_allocator.voice_deallocate(g.source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      }
    }
    uint8_t ch = get_midi_channel_for_preset_idx(KR_PRESET, true);
    for (int i = 0; i < KR_GATES; ++i)
      if (gates[i].remaining_us > 0) declare_midi_note(ch, gates[i].note, gates[i].vel);
    send_declared_midi_notes();
  }

  void on_clock_edge() {
    if (melting) { // replay growth backwards, outermost first
      if (order_count > 0) {
        --order_count;
        uint8_t c = order[order_count];
        cell[c] = 0;
        ghost[c] = 255;
        play_note(order_note[order_count], (uint8_t)clampi(order_vel[order_count] * 3 / 5, 20, 127));
      }
      if (order_count == 0) melting = false;
      return;
    }
    uint8_t played[2];
    int n = 0;
    while (q_count > 0 && n < 2) {
      kr_event_t e = queue[q_head];
      q_head = (uint8_t)((q_head + 1) % KR_QUEUE);
      --q_count;
      bool dup = false;
      for (int i = 0; i < n; ++i)
        if (played[i] == e.note) dup = true;
      if (dup || !cell[e.cell]) continue; // skip notes of cells removed in the meantime
      play_note(e.note, e.vel);
      played[n++] = e.note;
      ping[e.cell] = 255;
    }
  }

  void panic() {
    on_sequence_lock_guard_t lock;
    for (int i = 0; i < KR_GATES; ++i) {
      if (gates[i].remaining_us > 0) {
        synth_note_up(gates[i].voice);
        voice_allocator.voice_deallocate(gates[i].source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      }
      gates[i].remaining_us = 0;
    }
    release_declared_midi_notes();
    reset_crystal();
  }

  // ---------------------------------------------------------------- DLA physics
  int crystal_neighbour(int x, int y) {
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || nx > 15 || ny < 0 || ny > 15) continue;
        uint8_t c = cell[ny * 16 + nx];
        if (c) return c - 1;
      }
    return -1;
  }

  bool spawn_walker(int x, int y) {
    for (int i = 0; i < KR_MAX_WALKERS; ++i) {
      if (walkers[i].alive) continue;
      walkers[i].x = (int8_t)x;
      walkers[i].y = (int8_t)y;
      walkers[i].life = 0;
      walkers[i].alive = true;
      return true;
    }
    return false;
  }

  void spawn_at_border() {
    for (int tries = 0; tries < 8; ++tries) {
      int side = randi(0, 3), t = randi(0, 15);
      int x = side == 0 ? 0 : side == 1 ? 15 : t;
      int y = side == 2 ? 0 : side == 3 ? 15 : t;
      if (cell[y * 16 + x] || crystal_neighbour(x, y) >= 0) continue;
      spawn_walker(x, y);
      return;
    }
  }

  void dock(kr_walker_t &w, int k) {
    int idx = w.y * 16 + w.x;
    w.alive = false;
    if (cell[idx] || order_count >= 255) return;
    static const int8_t OCT[KR_MAX_SEEDS] = {0, 12, -12, 24};
    int sx = seed_pos[k] & 15, sy = seed_pos[k] >> 4;
    float dx = (float)(w.x - sx), dy = (float)(w.y - sy);
    float d = kr_sqrt(dx * dx + dy * dy);
    int degree = kr_round(d * 1.3f);
    uint8_t note = (uint8_t)kr_scale_note(root, scale_idx, degree, KR_BASE_NOTE + OCT[k]);
    uint8_t vel = (uint8_t)clampi(110 - (int)(d * 5.f) + randi(-8, 8), 40, 120);
    cell[idx] = (uint8_t)(k + 1);
    stick_ms[idx] = sim_us / 1000;
    order[order_count] = (uint8_t)idx;
    order_note[order_count] = note;
    order_vel[order_count] = vel;
    ++order_count;
    push_event(note, vel, (uint8_t)idx);
    bool border = w.x == 0 || w.x == 15 || w.y == 0 || w.y == 15;
    if (border || order_count >= KR_MELT_AT) {
      melting = true;
      q_head = q_count = 0;
    }
  }

  void step_walkers() {
    // swipe wind and device tilt add up to one drift vector
    float tilt_gain = tilt_amt * (1.f / 100.f);
    float drift_x = clampf(wind_x + tilt_x * tilt_gain, -1.f, 1.f);
    float drift_y = clampf(wind_y + tilt_y * tilt_gain, -1.f, 1.f);
    float awx = kr_absf(drift_x), awy = kr_absf(drift_y);
    int bias_x = (int)(awx * 60.f), bias_y = bias_x + (int)(awy * 60.f);
    for (int i = 0; i < KR_MAX_WALKERS; ++i) {
      kr_walker_t &w = walkers[i];
      if (!w.alive) continue;
      int dx = 0, dy = 0, r = randi(0, 99);
      if (r < bias_x) dx = drift_x > 0.f ? 1 : -1;
      else if (r < bias_y) dy = drift_y > 0.f ? 1 : -1;
      else {
        int d = randi(0, 3);
        dx = d == 0 ? 1 : d == 1 ? -1 : 0;
        dy = d == 2 ? 1 : d == 3 ? -1 : 0;
      }
      int nx = clampi(w.x + dx, 0, 15), ny = clampi(w.y + dy, 0, 15);
      if (!cell[ny * 16 + nx]) {
        w.x = (int8_t)nx;
        w.y = (int8_t)ny;
      }
      if (++w.life > 600) { // wandered too long: recycle
        w.alive = false;
        continue;
      }
      if (melting) continue;
      int k = crystal_neighbour(w.x, w.y);
      if (k >= 0 && randi(0, 99) < stick) dock(w, k);
    }
  }

  void on_sequence(int delta_time_us) override {
    sim_us += (uint32_t)delta_time_us;
    // wind decays over ~4 s
    float wd = clampf(1.f - delta_time_us * 2.5e-7f, 0.f, 1.f);
    wind_x *= wd;
    wind_y *= wd;

    step_acc_us += delta_time_us;
    int32_t interval = step_interval_us();
    if (step_acc_us > interval * 4) step_acc_us = interval * 4;
    while (step_acc_us >= interval) {
      step_acc_us -= interval;
      step_walkers();
    }

    // finger emitter: pressure = rain density near the finger
    if (emit_x >= 0) {
      emit_acc_us += delta_time_us;
      int32_t emit_interval = 1000000 / (5 + emit_p * 35 / 127);
      if (emit_acc_us >= emit_interval) {
        emit_acc_us = 0;
        int x = clampi(emit_x + randi(-1, 1), 0, 15), y = clampi(emit_y + randi(-1, 1), 0, 15);
        if (!cell[y * 16 + x]) spawn_walker(x, y);
      }
    } else {
      emit_acc_us = 0;
    }

    // keep the base rain alive
    int alive = 0;
    for (int i = 0; i < KR_MAX_WALKERS; ++i) alive += walkers[i].alive ? 1 : 0;
    for (int n = alive; n < density && n < KR_MAX_WALKERS; ++n) spawn_at_border();

    int edges = quant.update(-1, div_num(), 1, UPDATE_DIV_NOW, true);
    if (edges > 0) on_clock_edge();
    gate_tick(delta_time_us);
  }

  // ---------------------------------------------------------------- touch
  void ignore_all_fingers() {
    for (int i = 0; i < KR_FINGERS; ++i)
      if (ft.active(i)) gest[i].ignore = true;
    prev_mask = ft.active_mask;
    emit_x = -1;
    memset(tap_ok, 0, sizeof(tap_ok));
  }

  // Removes a seed together with its whole crystal branch. Caller holds the sequence lock.
  void remove_seed(int k) {
    seed_on[k] = false;
    for (int i = 0; i < 256; ++i)
      if (cell[i] == k + 1) {
        cell[i] = 0;
        ghost[i] = 255;
      }
    uint16_t n = 0; // compact the growth order so the melt replay skips removed cells
    for (uint16_t i = 0; i < order_count; ++i) {
      if (!cell[order[i]]) continue;
      order[n] = order[i];
      order_note[n] = order_note[i];
      order_vel[n] = order_vel[i];
      ++n;
    }
    order_count = n;
    if (order_count == 0) melting = false;
    if (num_seeds() == 0) q_head = q_count = 0;
  }

  void tap_at(int x, int y) {
    int idx = y * 16 + x;
    on_sequence_lock_guard_t lock;
    for (int k = 0; k < KR_MAX_SEEDS; ++k) {
      if (seed_on[k] && seed_pos[k] == idx) {
        remove_seed(k); // also the last one: no seed = no crystal = silence
        return;
      }
    }
    if (cell[idx]) return;
    for (int k = 0; k < KR_MAX_SEEDS; ++k) {
      if (seed_on[k]) continue;
      seed_on[k] = true;
      seed_pos[k] = (uint8_t)idx;
      cell[idx] = (uint8_t)(k + 1);
      ping[idx] = 255;
      return;
    }
  }

  // Absolute gravity direction (smoothed, not re-centred), so a held tilt keeps pulling.
  // Small deadzone for a device lying flat; full drift at roughly 0.5 g.
  void read_tilt() {
    float ax = get_accel_q24(0, ACCEL_MODE_SMOOTH) * (1.f / ACCEL_Q24_ONE_G);
    float ay = get_accel_q24(1, ACCEL_MODE_SMOOTH) * (1.f / ACCEL_Q24_ONE_G);
    const float dz = 0.06f, span = 0.44f;
    float tx = kr_absf(ax) < dz ? 0.f : (ax - (ax > 0.f ? dz : -dz)) / span;
    float ty = kr_absf(ay) < dz ? 0.f : (ay - (ay > 0.f ? dz : -dz)) / span;
    tilt_x = clampf(tx, -1.f, 1.f);
    tilt_y = clampf(ty, -1.f, 1.f);
  }

  void handle_gestures(uint32_t now) {
    for (int i = 0; i < KR_FINGERS; ++i) {
      uint8_t bit = (uint8_t)(1u << i);
      bool act = ft.active(i);
      bool was = (prev_mask & bit) != 0;
      kr_gesture_t &g = gest[i];
      if (act) {
        float x = ft.finger_x[i] * (1.f / 256.f), y = ft.finger_y[i] * (1.f / 256.f);
        if (!was) {
          g.sx = g.lx = x;
          g.sy = g.ly = y;
          g.t0 = now;
          g.maxmove = 0.f;
          g.emitting = false;
          g.ignore = (x > 14.5f && y > 14.5f); // overlay corner
        } else if (!g.ignore) {
          g.lx = x;
          g.ly = y;
          float dx = x - g.sx, dy = y - g.sy;
          g.maxmove = maxf(g.maxmove, kr_sqrt(dx * dx + dy * dy));
          if (!g.emitting && g.maxmove < 1.f && now - g.t0 > 250000) g.emitting = true;
          if (g.emitting) {
            emit_x = (int8_t)clampi(kr_round(x), 0, 15);
            emit_y = (int8_t)clampi(kr_round(y), 0, 15);
            emit_p = (uint8_t)clampi(touch_pressure_curve_q7(clampi(ft.finger_p[i], 0, 255)), 0, 127);
          }
        }
      } else if (was && !g.ignore) {
        if (g.emitting) {
          emit_x = -1;
        } else if (g.maxmove >= 1.5f) { // swipe = wind
          float dx = g.lx - g.sx, dy = g.ly - g.sy;
          float len = kr_sqrt(dx * dx + dy * dy);
          float gain = len > 0.f ? minf(1.f, len / 8.f) / len : 0.f;
          wind_x = clampf(dx * gain, -1.f, 1.f);
          wind_y = clampf(dy * gain, -1.f, 1.f);
        }
      }
    }
    prev_mask = ft.active_mask;
  }

  // Taps use the debounced per-pad edges (same path as button()), independent of the finger tracker.
  // A pad counts as a tap if the touch started on it, never slid to another pad, and was released quickly.
  void handle_taps(uint32_t now) {
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        int i = y * 16 + x;
        if (i == 255) continue; // overlay corner
        if (get_touch_pressed(x, y)) {
          if (does_touch_originate_here(x, y)) {
            tap_down_us[i] = now;
            tap_ok[i] = true;
          } else { // finger slid in from another pad: that origin is a swipe, not a tap
            tap_ok[i] = false;
            int ox = get_touch_origin_x(x, y), oy = get_touch_origin_y(x, y);
            if (ox >= 0 && ox < 16 && oy >= 0 && oy < 16) tap_ok[oy * 16 + ox] = false;
          }
        }
        if (get_touch_released(x, y)) {
          if (tap_ok[i] && now - tap_down_us[i] < 350000) tap_at(x, y);
          tap_ok[i] = false;
        }
      }
    }
  }

  // ---------------------------------------------------------------- drawing
  void add_px(int idx, const uint8_t *col, float w) {
    float w2 = w * w; // linear ramp in, gamma (square) right before quantizing
    fb[0][idx] += col[0] * w2;
    fb[1][idx] += col[1] * w2;
    fb[2][idx] += col[2] * w2;
  }

  void render(int delta_time_us) {
    // Age ramp: fresh ice-white -> teal -> deep indigo
    static const uint8_t AGE_RAMP[16][3] = {{25, 29, 31}, {21, 27, 29}, {18, 24, 28}, {15, 22, 26},
                                            {12, 20, 25}, {10, 17, 24}, {8, 15, 23},  {7, 13, 22},
                                            {6, 11, 21},  {5, 10, 20},  {4, 8, 19},   {3, 7, 18},
                                            {3, 5, 16},   {3, 4, 15},   {3, 3, 14},   {2, 2, 13}};
    static const uint8_t SEED_COL[3] = {31, 8, 30};
    static const uint8_t WALKER_COL[3] = {2, 21, 22};
    static const uint8_t WHITE_COL[3] = {31, 31, 31};
    int dt_ms = maxi(1, delta_time_us / 1000);
    uint32_t now_ms = sim_us / 1000;
    float beat = 1.f - freerunning_clock_frac_q16() * (1.f / 65536.f);

    for (int c = 0; c < 3; ++c)
      for (int i = 0; i < 256; ++i) fb[c][i] = 0.f;

    for (int i = 0; i < 256; ++i) {
      uint8_t c = cell[i];
      if (c) {
        int k = c - 1;
        if (seed_on[k] && seed_pos[k] == i) {
          add_px(i, SEED_COL, 0.75f + 0.25f * beat);
        } else {
          int age = (int)((now_ms - stick_ms[i]) / 1250u);
          add_px(i, AGE_RAMP[clampi(age, 0, 15)], 1.f);
        }
      } else if (ghost[i]) {
        add_px(i, AGE_RAMP[0], ghost[i] * (1.f / 255.f));
      }
      if (ping[i]) add_px(i, WHITE_COL, ping[i] * (1.f / 255.f));
      ping[i] = (uint8_t)maxi(0, ping[i] - dt_ms);                 // ~250 ms flash
      ghost[i] = (uint8_t)maxi(0, ghost[i] - (dt_ms * 255) / 600); // ~600 ms afterglow
    }

    for (int i = 0; i < KR_MAX_WALKERS; ++i) {
      if (!walkers[i].alive) continue;
      add_px(walkers[i].y * 16 + walkers[i].x, WALKER_COL, 0.5f);
    }
    if (emit_x >= 0) add_px(emit_y * 16 + emit_x, WALKER_COL, 0.4f + 0.4f * emit_p * (1.f / 127.f));

    add_px(255, WHITE_COL, 0.2f); // overlay corner hint

    for (int i = 0; i < 256; ++i) {
      int r = clampi((int)(fb[0][i] + 0.5f), 0, 31);
      int g = clampi((int)(fb[1][i] + 0.5f), 0, 31);
      int b = clampi((int)(fb[2][i] + 0.5f), 0, 31);
      set_led(i & 15, i >> 4, LED_RGB(r, g, b));
    }
  }

  void draw_overlay() {
    leds_clear();
    sl_density.simple_slider(3, 0, 15, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, LED_RGB(2, 21, 22), 4, 32, density,
                             "Rain density");
    density = (uint8_t)clampi(last_widget_new_value(), 4, 32);
    sl_speed.simple_slider(7, 0, 15, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, LED_RGB(13, 25, 31), 0, 100, speed,
                           "Walk speed");
    speed = (uint8_t)clampi(last_widget_new_value(), 0, 100);
    sl_stick.simple_slider(11, 0, 15, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, LED_RGB(31, 8, 30), 10, 100, stick,
                           "Stickiness: low = dense, high = feathery");
    stick = (uint8_t)clampi(last_widget_new_value(), 10, 100);
    sl_tilt.simple_slider(14, 0, 15, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, LED_RGB(6, 24, 20), 0, 100, tilt_amt,
                          "Tilt amount (accelerometer)");
    tilt_amt = (uint8_t)clampi(last_widget_new_value(), 0, 100);
    if (button(0, 15, RED, ISOLATED, "Panic: notes off, clear crystal")) panic();
    if (button(2, 15, LED_RGB(31, 8, 30), ISOLATED, "Reset seeds to one centre seed")) {
      on_sequence_lock_guard_t lock;
      default_seeds();
      reset_crystal();
    }
    if (button(15, 15, WHITE, ISOLATED, "Close overlay")) overlay_open = false;
  }

  // ---------------------------------------------------------------- settings
  void change_setting(uint8_t &field, int delta, int max_value) {
    int next = clampi(field + delta, 0, max_value);
    if (next == field) return;
    field = (uint8_t)next;
    settings_changed = true;
  }

  void draw_settings(int page) {
    static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const SCALE_NAMES[8] = {"maj", "min", "dor", "pent", "mpnt", "hira", "lyd", "phr"};
    static const char *const DIV_NAMES[5] = {"1/4", "1/8", "8T", "1/16", "1/32"};
    if (page == -1)
      change_setting(root, draw_system_style_enum_settings_page("root", root, NOTE_NAMES, 12), 11);
    else if (page == -2)
      change_setting(scale_idx, draw_system_style_enum_settings_page("scl", scale_idx, SCALE_NAMES, 8), 7);
    else if (page == -3)
      change_setting(div_idx, draw_system_style_enum_settings_page("div", div_idx, DIV_NAMES, 5), 4);
  }

  int get_num_pages() override { return 3; } // Play / Sound / Save-Load
  int get_num_panel_settings_pages() override { return 3; }

  // ---------------------------------------------------------------- pages
  // Any page other than Play drops transient touch state so nothing sticks while the pad area is used by widgets.
  void leave_play_page() {
    overlay_open = false;
    emit_x = -1;
    ft.clear();
    prev_mask = 0;
    memset(tap_ok, 0, sizeof(tap_ok));
  }

  void draw_play_page(int delta_time_us) {
    uint32_t now = get_on_ui_time();
    ft.update(0, 0, 16, 16);
    if (overlay_open) {
      ignore_all_fingers();
      draw_overlay();
      return;
    }
    handle_gestures(now);
    handle_taps(now);
    render(delta_time_us);
    invisible_button(15, 15, ISOLATED, "Hold: performance overlay");
    if (corner_hold.is_last_widget_held_for(600000)) {
      overlay_open = true;
      ignore_all_fingers();
    }
  }

  void on_ui(int delta_time_us) override {
    int page = get_scroll_page();
    read_tilt();
    if (page < 0) {
      leave_play_page();
      draw_settings(page);
    } else if (page == 1) {
      leave_play_page();
      leds_clear();
      preset_pages.edit(KR_PRESET, 16, 0, true);
      preset_pages.xy_pad(KR_PRESET, 8, 26);
    } else if (page == 2) {
      leave_play_page();
      leds_clear();
      panel_page.saveload(32, true, FLAG_PICKER_ENABLE_DELETE);
    } else if (page > 2) {
      scroll_to_page(2);
    } else {
      draw_play_page(delta_time_us);
    }
    if (settings_changed) {
      (void)save_settings_to_sd(false);
      settings_changed = false;
    }
  }

  // ---------------------------------------------------------------- save/load
  bool on_serialise_settings(serialiser_t &s, int version) override {
    (void)version;
    OBJECT_BEGIN(s);
    FIELD("root", root, 0u, 11u);
    FIELD("scale", scale_idx, 0u, 7u);
    FIELD("div", div_idx, 0u, 4u);
    OBJECT_END(s);
    return true;
  }

  bool on_serialise(serialiser_t &s, int version) override {
    (void)version;
    auto &o = *this;
    OBJECT_BEGIN(s);
    FIELD("density", density, 4u, 32u);
    FIELD("speed", speed, 0u, 100u);
    FIELD("stick", stick, 10u, 100u);
    FIELD("tilt", tilt_amt, 0u, 100u);
    FIELD("seed_pos", seed_pos);
    FIELD("seed_on", seed_on);
    FIELD_SYNTH_PRESET("preset", KR_PRESET);
    FIELD_MIX_PRESET("presetMix");
    OBJECT_END(s);
    return true;
  }
};
