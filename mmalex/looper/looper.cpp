// Generated from a Plinky 12 built-in example by scripts/update_community_panel_builds.sh.
/*
@Name: Looper
@Author: mmalex
@Description: An 8-track touch looper backed by PSRAM.
@Tags: looper

Eight track rows share the surface with per-track controls. Hold record and tap a row to arm it; stopped
recording starts from input level, while playing can sync to a loop head crossing. Playback pads
launch transient heads; hold tap tempo as shift to edit the latched loop head.
*/
#define PANEL_PAD_COLOR PURPLE

struct looper : panel_t {
  static constexpr int N = 8, BINS = 16, HEADS = 4, MIX_SLIDERS = 11, NO = 255;
  static constexpr int PEAK_BINS = 256, PEAK_SHIFT = 10;
  static constexpr uint8_t ALL_TRACKS_MASK = (uint8_t)((1u << N) - 1u);
  static constexpr int TAP_SHIFT_X = 11, RECORD_X = 12, CLEAR_X = 13, STOP_X = 14, PLAY_X = 15;
  static constexpr int AUTO_THRESHOLD = 32768 / 16, MIN_BOOTSTRAP_QN_US = 60000000 / 160, MAX_BOOTSTRAP_QN_US = 60000000 / 80;
  static constexpr float VOICE_COMP_INPUT_BITS = 15.5f, VOICE_COMP_UNITY_GAIN = 2048.f;
  static constexpr float VOICE_ATTACK_K = 0.0433217f / 8.6f, VOICE_RELEASE_K = 0.0433217f / 17.3f;
  struct head_t {
    int pos = 0, sample_at_fx_start = 0, fx_duration = 0;
    uint8_t volume = 0, gain = 0, pad = 0, keyed_by_fx = 0;
    bool fx_was_running_last_frame = false;
  };
  struct track_t {
    uint8_t loop_from = 0, loop_len = 16;
    uint8_t vol_lo = 96, vol_hi = 96;
    uint8_t last_nonzero_vol_lo = 96, last_nonzero_vol_hi = 96, vol_tap_lo = 96, vol_tap_hi = 96;
    uint8_t delay = 0, reverb = 0, reverse = 0, beat_repeat = 0, anchor = 0;
    uint8_t last_reverse = 0, last_beat_repeat = 0;
    int8_t filter = 0;
    head_t head[HEADS];
    float gain_smoothed = VOICE_COMP_UNITY_GAIN;
    biquad_coeff_t filter_coeff = {1.f, 0.f, 0.f, 0.f, 0.f};
    biquad_t filter_state[2]{};
    uint8_t peak[PEAK_BINS]{};
    slider_t volume_slider, filter_slider, delay_slider, reverb_slider;
    uint32_t vol_tap_start_us = 0;
    bool head0_playing = false, touching = false, dsp_touching = false, fx_pad_down = false;
    bool beat_repeat_was_down = false;
    bool vol_tap_tracking = false;
  } t[N];
  struct stereo_i32_t {
    int l = 0, r = 0;
  };

  uint8_t undo_peak[PEAK_BINS]{}, swap_peak[PEAK_BINS]{};
  slider_t mix_slider[MIX_SLIDERS]{};
  uint32_t loop_capacity = 0;
  uint8_t blank_tracks = ALL_TRACKS_MASK, solo_tracks = 0, record_track = NO, stop_request = 0, cancel_request = 0,
          undo_request = 0;
  uint8_t undo_track = NO, undo_loop_from = 0, undo_loop_len = 16;
  uint8_t swap_track = NO, swap_loop_from = 0, swap_loop_len = 16;
  uint32_t rec_pos = 0, rec_target = 0, rec_start = 0, rec_grid = 0, undo_start = 0, undo_len = 0, undo_grid = 0;
  uint32_t rec_wrap_start = 0, rec_wrap_span = 0, undo_wrap_start = 0, undo_wrap_span = 0;
  uint32_t swap_start = 0, swap_len = 0, swap_grid = 0, swap_pos = 0;
  uint32_t swap_wrap_start = 0, swap_wrap_span = 0;
  uint32_t rec_down_us = 0;
  uint16_t input_peak = 0;
  bool record_active = false, clear_touched_track = false, rec_shift_used = false;
  uint8_t arm_sync_track = NO, arm_sync_pad = 0, arm_record_from = 0, arm_record_len = 16;
  bool arm_keep_loop = false, record_free = false, record_keep_loop = false;
  bool undo_blank = true, undo_keep_loop = false, swap_blank = true, swap_keep_loop = false, swap_keep_undo = false,
       tempo_shift_used = false;
  bool edit_mix_params = false;
  uint8_t audio_source = 0;
  bool settings_changed = false;
  stereo_i32_t track_block[BLOCK_SIZE]{};

  int get_num_panel_settings_pages() override { return 1; }

  bool on_serialise_settings(serialiser_t &s, int version) override {
    (void)version;
    OBJECT_BEGIN(s);
    FIELD("audio_source", audio_source, 0u, 1u);
    OBJECT_END(s);
    return true;
  }

  void set_audio_source(int source) {
    uint8_t next_source = (uint8_t)clampi(source, 0, 1);
    if (next_source == audio_source)
      return;
    audio_source = next_source;
    codec_enable_mic(audio_source == 1);
    settings_changed = true;
  }

  void save_settings_if_changed() {
    if (settings_changed) {
      (void)save_settings_to_sd(false);
      settings_changed = false;
    }
  }

  // Returns true when track `idx` is armed but not yet recording.
  bool is_armed(int idx) { return record_track == idx && !record_active; }
  // Returns true when track `idx` is the active recording target.
  bool is_recording(int idx) { return record_track == idx && record_active; }
  // Returns true when track `idx` has no recorded loop.
  bool track_blank(int idx) { return blank_tracks & ((uint8_t)1 << idx); }
  uint8_t track_bit(int idx) { return (uint8_t)1 << idx; }
  bool track_solod(int idx) { return solo_tracks & track_bit(idx); }
  bool track_audible(int idx) { return !solo_tracks || track_solod(idx); }
  // Marks track `idx` blank or nonblank in the shared blank bitmask.
  void set_track_blank(int idx, bool blank) {
    uint8_t bit = track_bit(idx);
    blank_tracks = blank ? (uint8_t)(blank_tracks | bit) : (uint8_t)(blank_tracks & (uint8_t)~bit);
    if (blank)
      solo_tracks &= (uint8_t)~bit;
  }
  // Returns true when track `idx` has a usable loop head for sync-armed recording.
  bool track_has_sync_head0(int idx) {
    return idx >= 0 && idx < N && !track_blank(idx) && t[idx].head0_playing;
  }
  // Returns the first track whose head 0 can sync a new arm, or `NO` to use input level.
  uint8_t first_record_sync_track(bool transport_playing) {
    if (!transport_playing) return NO;
    for (int i = 0; i < N; ++i)
      if (track_has_sync_head0(i))
        return i;
    return NO;
  }
  // Clears the current armed-record target and its latched sync/length policy.
  void clear_record_arm() {
    record_track = NO;
    arm_sync_track = NO;
    arm_sync_pad = arm_record_from = 0;
    arm_record_len = 16;
    arm_keep_loop = false;
  }
  // Returns the latched sync track for the current arm, or `NO` to use input level.
  uint8_t armed_record_sync_track() {
    return track_has_sync_head0(arm_sync_track) && pad_in_loop(t[arm_sync_track], arm_sync_pad) ? arm_sync_track : NO;
  }
  // Returns true when the first stopped recording should infer quarter-note period from input length.
  bool tempo_bootstrap_record(bool transport_playing) {
    return !transport_playing && get_clock_source() == CLOCK_SOURCE_INTERNAL && blank_tracks == ALL_TRACKS_MASK;
  }
  // Returns true when pad `pad` lies inside track `r`'s loop range.
  uint8_t pad_in_loop(track_t &r, int pad) {
    return ((pad - r.loop_from) & 15) < r.loop_len;
  }
  // Arms track `idx` from tapped pad `pad`, latching sync and recording range policy for DSP/UI.
  void arm_record_track(int idx, int pad, bool transport_playing) {
    if (idx < 0 || idx >= N || record_active) return;
    track_t &r = t[idx];
    pad &= 15;
    clear_record_arm();
    record_track = idx;
    arm_record_from = pad;
    arm_record_len = 16;
    if (tempo_bootstrap_record(transport_playing)) {
      r.loop_from = 0;
      r.loop_len = 16;
      arm_record_from = 0;
      return;
    }
    if (transport_playing && track_has_sync_head0(idx) && pad_in_loop(r, pad)) {
      arm_sync_track = idx;
      arm_sync_pad = pad;
      arm_record_len = maxi(1, (int)r.loop_len);
      arm_keep_loop = true;
      return;
    }
    r.anchor = pad;
    r.loop_from = pad;
    r.loop_len = 16;
    arm_record_from = r.loop_from;
    arm_record_len = r.loop_len;
    uint8_t sync = first_record_sync_track(transport_playing);
    if (sync != NO) {
      arm_sync_track = sync;
      arm_sync_pad = t[sync].loop_from;
    }
  }
  // Toggles track `idx` between disarmed and armed using tapped pad `pad`.
  void toggle_record_arm(int idx, int pad, bool transport_playing) {
    if (is_armed(idx)) clear_record_arm();
    else arm_record_track(idx, pad, transport_playing);
  }
  // Averages fixed 1024-sample peak bins covered by play pad `pad` at current pad size `step`.
  int peak_for_pad(track_t &r, int pad, int step) {
    int first = (pad & 15) * step;
    int last = first + maxi(1, step) - 1;
    int a = clampi(first >> PEAK_SHIFT, 0, PEAK_BINS - 1);
    int b = clampi(last >> PEAK_SHIFT, 0, PEAK_BINS - 1);
    int sum = 0;
    for (int i = a; i <= b; ++i)
      sum += r.peak[i];
    return sum / (b - a + 1);
  }
  // Clears all playheads and FX head state for track `r`.
  void clear_heads(track_t &r) {
    for (int i = 0; i < HEADS; ++i) {
      r.head[i].volume = 0;
      r.head[i].gain = 0;
      r.head[i].sample_at_fx_start = r.head[i].fx_duration = 0;
      r.head[i].keyed_by_fx = 0;
      r.head[i].fx_was_running_last_frame = false;
    }
  }
  // Starts head `h` on track `r` at pad `pad` with volume `vol` and step size `step`.
  void start_head(track_t &r, int h, int pad, int vol, int step) {
    r.head[h].pad = pad & 15;
    r.head[h].pos = (pad & 15) * step;
    r.head[h].sample_at_fx_start = r.head[h].fx_duration = 0;
    r.head[h].keyed_by_fx = 0;
    r.head[h].volume = clampi(vol, 0, 127);
    r.head[h].gain = 0;
    r.head[h].fx_was_running_last_frame = false;
  }
  void set_loop_head_playing(int idx, bool playing, int step) {
    track_t &r = t[idx];
    r.head0_playing = playing && !track_blank(idx);
    if (r.head0_playing) {
      start_head(r, 0, r.loop_from, 127, step);
    } else {
      r.head[0].volume = 0;
      r.head[0].sample_at_fx_start = r.head[0].fx_duration = 0;
      r.head[0].keyed_by_fx = 0;
      r.head[0].fx_was_running_last_frame = false;
    }
  }
  // Returns sample offset of `pos` relative to loop `start` within circular `grid`.
  int rel_sample(int pos, int start, int grid) {
    return pos >= start ? pos - start : grid - start + pos;
  }
  // Wraps `pos` into the circular window `start..start+span` inside `grid` samples.
  int loop_pos(int pos, int start, int span, int grid) {
    if (grid <= 0) return 0;
    if (!span) return start;
    if (pos >= 0 && pos < grid) {
      if (span >= grid || rel_sample(pos, start, grid) < span)
        return pos;
    }
    if (span >= grid) {
      pos %= grid;
      if (pos < 0) pos += grid;
      return pos;
    }
    int rel = pos - start;
    rel %= span;
    if (rel < 0) rel += span;
    int p = start + rel;
    return p >= grid ? p - grid : p;
  }
  // Maps recording offset `pos` from `start` into wrap window `wrap_start/span` inside `grid`.
  uint32_t record_sample_at(uint32_t start, uint32_t pos, uint32_t wrap_start, uint32_t wrap_span, uint32_t grid) {
    return (uint32_t)loop_pos((int)(start + pos), (int)wrap_start, (int)wrap_span, (int)grid);
  }
  // Renders one block from stereo frames `p` at `sample` into `dst`, ramping gain and reading only `[min_sample,max_sample)`.
  void render_block(stereo16_t *p, stereo_i32_t *dst, int sample, int start_gain, int end_gain, bool reverse,
                    int min_sample, int max_sample) {
    int gain_q7 = start_gain << 7;
    int dgain_q7 = ((end_gain - start_gain) << 7) >> BLOCK_SIZE_SH;
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      int src = reverse ? sample - i : sample + i;
      if (src >= min_sample && src < max_sample) {
        stereo16_t s = p[src];
        dst[i].l += ((int)s.l * gain_q7) >> 14;
        dst[i].r += ((int)s.r * gain_q7) >> 14;
      }
      gain_q7 += dgain_q7;
    }
  }
  // Updates track `y` playhead target volumes from touch state using pad size `step` and transport state.
  void update_playheads_from_touch(int y, uint8_t new_vol[HEADS], int step, bool transport_playing) {
    track_t &r = t[y];
    for (int h = 0; h < HEADS; ++h)
      new_vol[h] = r.head[h].volume;
    if (edit_mix_params) {
      r.reverse = r.beat_repeat = 0;
      r.fx_pad_down = false;
      if (!r.head0_playing) {
        new_vol[0] = 0;
      } else if (!new_vol[0]) {
        new_vol[0] = 127;
      }
      for (int h = 1; h < HEADS; ++h)
        new_vol[h] = 0;
      r.dsp_touching = false;
      return;
    }
    if (y == N - 1) {
      r.reverse = r.beat_repeat = 0;
      r.fx_pad_down = false;
    } else {
      int cy = ctrl_y(y);
      uint8_t reverse = get_touch_down(14, cy) ? clampi(get_touch_pressure_xy(14, cy), 0, 255) : 0;
      uint8_t beat_repeat = get_touch_down(15, cy) ? clampi(get_touch_pressure_xy(15, cy), 0, 255) : 0;
      bool fx_now = reverse || beat_repeat;
      bool fx_button_released = r.fx_pad_down && !fx_now;
      if (fx_now) {
        r.last_reverse = reverse;
        r.last_beat_repeat = beat_repeat;
      }
      r.reverse = reverse ? reverse : (fx_button_released && r.last_reverse ? 1 : 0);
      r.beat_repeat = beat_repeat ? beat_repeat : (fx_button_released && r.last_beat_repeat ? 1 : 0);
      r.fx_pad_down = fx_now;
    }
    bool down[16]{}, origin[16]{};
    uint8_t pressure[16]{};
    int row = play_y(y), first = -1, first_origin = -1, count = 0, origin_count = 0, press = 0;
    for (int x = 0; x < 16; ++x) {
      down[x] = get_touch_down(x, row);
      origin[x] = down[x] && does_touch_originate_here(x, row);
      pressure[x] = down[x] ? clampi(touch_pressure_curve_q7(get_touch_pressure_xy(x, row)), 32, 127) : 0;
      if (down[x]) {
        if (first < 0) first = x;
        ++count;
        press = maxi(press, (int)pressure[x]);
        if (origin[x]) {
          if (first_origin < 0) first_origin = x;
          ++origin_count;
        }
      }
    }
    bool row_edge = count && !r.dsp_touching;
    bool manage = !get_touch_down(RECORD_X, 15) && !get_touch_down(CLEAR_X, 15);
    bool shift_down = get_touch_down(TAP_SHIFT_X, 15);
    int fx_vol = clampi(touch_pressure_curve_q7(maxi((int)r.reverse, (int)r.beat_repeat)), 0, 127);
    if (manage && shift_down) {
      for (int h = 1; h < HEADS; ++h) {
        new_vol[h] = 0;
        r.head[h].keyed_by_fx = 0;
      }
      if (first >= 0) {
        bool stopped = false;
        if (row_edge)
          r.anchor = first_origin >= 0 ? first_origin : first;
        if (origin_count > 1) {
          for (int x = 0; x < 16; ++x)
            if (x != r.anchor && origin[x]) {
              r.loop_from = r.anchor;
              r.loop_len = ((x - r.anchor) & 15) + 1;
              if (!r.head[0].volume && !new_vol[0])
                start_head(r, 0, r.loop_from, 0, step);
              r.head0_playing = true;
              new_vol[0] = maxi((int)new_vol[0], press);
              break;
            }
        } else if (row_edge) {
          if (r.head[0].volume && pad_in_loop(r, first)) {
            new_vol[0] = 0;
            r.head0_playing = false;
            stopped = true;
          } else {
            if (!pad_in_loop(r, first))
              r.loop_from = first;
            start_head(r, 0, first, 0, step);
            r.head0_playing = true;
            new_vol[0] = press;
          }
        }
        if (!stopped && (r.head[0].volume || new_vol[0]) && press > new_vol[0])
          new_vol[0] = press;
      }
    } else {
      bool accounted[16]{};
      for (int h = 1; h < HEADS; ++h) {
        head_t &hd = r.head[h];
        if (hd.keyed_by_fx) {
          new_vol[h] = fx_vol;
          continue;
        }
        if (!hd.volume) {
          new_vol[h] = 0;
          continue;
        }
        int pad = hd.pad & 15;
        new_vol[h] = down[pad] ? pressure[pad] : 0;
        if (new_vol[h]) accounted[pad] = true;
      }
      if (manage) {
        for (int x = 0; x < 16; ++x) {
          if (accounted[x] || !down[x]) continue;
          for (int h = 1; h < HEADS; ++h)
            if (!r.head[h].keyed_by_fx && !r.head[h].volume && !new_vol[h]) {
              start_head(r, h, x, 0, step);
              new_vol[h] = pressure[x];
              accounted[x] = true;
              break;
            }
        }
        bool no_heads = true;
        for (int h = 0; h < HEADS; ++h) {
          if (h == 0 && !transport_playing) continue;
          if (r.head[h].volume || new_vol[h]) no_heads = false;
        }
        if (fx_vol && no_heads) {
          int pad = r.loop_from;
          if (r.beat_repeat)
            pad = (r.loop_from + rand() % maxi(1, (int)r.loop_len)) & 15;
          start_head(r, 1, pad, 0, step);
          r.head[1].keyed_by_fx = 1;
          new_vol[1] = fx_vol;
        }
      }
    }
    if (!r.head0_playing)
      new_vol[0] = 0;
    else if (!new_vol[0])
      new_vol[0] = 127;
    r.dsp_touching = count > 0;
  }
  // Returns the play-pad row for track `idx`.
  int play_y(int idx) { return idx * 2; }
  // Returns the control row for track `idx`.
  int ctrl_y(int idx) { return idx * 2 + 1; }
  // Returns the display colour assigned to track `idx`.
  uint32_t track_col(int idx) {
    static const uint8_t hue[N] = {0, 5, 7, 9, 10, 11, 12, 13};
    return palette[8][hue[idx & 7]];
  }
  uint32_t track_ui_col(int idx) {
    return track_audible(idx) ? track_col(idx) : DIMMER(WHITE);
  }
  // Returns compressor numerator from low/high volume sliders `minslider` and `maxslider`.
  float comp_num(int minslider, int maxslider) {
    return exp2f((maxslider - 127) * (2.f / 16.f) + (14.f + VOICE_COMP_INPUT_BITS) * 2.f);
  }
  // Returns compressor denominator from low/high volume sliders `minslider` and `maxslider`.
  float comp_denom(int minslider, int maxslider) {
    int gap_eff = (maxslider - minslider) >> 1;
    return exp2f((-gap_eff) * (2.f / 16.f) + VOICE_COMP_INPUT_BITS * 2.f);
  }
  // Smooths compressor `gain` toward `mono_in` level using precomputed `num` and `denom`.
  float update_comp_gain(float mono_in, float gain, float num, float denom) {
    float target = sqrtf(num / (denom + mono_in * mono_in));
    float delta = target - gain;
    return gain + delta * (delta < 0.f ? VOICE_ATTACK_K : VOICE_RELEASE_K);
  }
  // Returns a triangular pulse of `col` driven by the free-running quarter-note clock.
  uint32_t pulse_col(uint32_t col) {
    int tri = (int)freerunning_clock_frac_q16();
    if (tri > 32767) tri = 65535 - tri;
    return fade_col(col, 64 + ((tri >> 7) * 3 >> 2));
  }
  // Updates displayed input VU `input_peak` from raw block peak `peak` with instant attack and slower release.
  void update_input_peak_vu(int peak) {
    peak = mini(32767, peak);
    if (peak >= input_peak) {
      input_peak = peak;
    } else {
      int release = maxi(32, (int)input_peak >> 6);
      input_peak = (uint16_t)maxi(peak, (int)input_peak - release);
    }
  }
  // Maps signal `peak` between `lo` and `hi` onto brightness of base colour `col`.
  uint32_t vu_col(uint32_t col, int peak, int lo, int hi) {
    int bri = peak <= lo ? 0 : (peak >= hi ? 255 : ((peak - lo) * 255) / maxi(1, hi - lo));
    return add_col(DIMMESTEST(col), fade_col(col, bri));
  }
  // Draws the bottom-row input VU using the current `input_peak`.
  void draw_bottom_vu() {
    int hi = 32767;
    for (int i = 4; i >= 0; --i) {
      int lo = (hi * (i ? 23170 : 16384)) >> 15;
      uint32_t col = i == 4 ? RED : (i == 3 ? ORANGE : GREEN);
      set_led(6 + i, 15, vu_col(col, input_peak, lo, hi));
      hi = lo;
    }
  }
  // Draws the global mix sliders and label on the edit-mix page.
  void draw_mix_params() {
    static const int mix_params[MIX_SLIDERS] = {
      MIX_PARAM_AUDIO_IN_DRY + 128,
      MIX_PARAM_AUDIO_IN_DELAY + 128,
      MIX_PARAM_AUDIO_IN_REVERB + 128,
      MIX_PARAM_DELAY_TIME + 128,
      MIX_PARAM_DELAY_FEEDBACK + 128,
      MIX_PARAM_REVERB_SHIMMER + 128,
      MIX_PARAM_REVERB_FEEDBACK + 128,
      MIX_PARAM_EQ_BASS + 128,
      MIX_PARAM_EQ_MID + 128,
      MIX_PARAM_EQ_TREBLE + 128,
      MIX_PARAM_OUTPUT_LEVEL + 128,
    };
    synth_param_sliders_block(mix_slider, 5, 0, MIX_SLIDERS, 7, MIX_PRESET_IDX, mix_params);
    leds_draw_string(3, 9, FONT_4, BRIGHTER(PINK), "MIX");
  }
  // Returns the input VU colour for column `x` using base colour `col`.
  uint32_t vu_wash(uint32_t col, int x) {
    int hi = 32767;
    for (int i = 15; i >= x; --i) {
      int lo = (hi * 23170) >> 15;
      if (i == x) return vu_col(col, input_peak, lo, hi);
      hi = lo;
    }
    return BLACK;
  }
  // Returns distance in samples from track `idx` head 0 to pad `pad` in current playback direction.
  int head0_distance_to_pad(int idx, int pad, int step, int grid) {
    track_t &r = t[idx];
    int start = (r.loop_from & 15) * step;
    int span = mini(grid, step * maxi(1, (int)r.loop_len));
    int rel = rel_sample(r.head[0].pos, start, grid);
    if (rel >= span) rel = 0;
    int target = rel_sample((pad & 15) * step, start, grid);
    if (target >= span) return span;
    int dist = r.reverse ? rel - target : target - rel;
    if (dist < 0) dist += span;
    return dist;
  }
  // Returns true when track `idx`'s head 0 will cross pad `pad` during this block.
  bool head0_will_cross_pad(int idx, int pad, int step, int grid) {
    return head0_distance_to_pad(idx, pad, step, grid) < BLOCK_SIZE;
  }
  // Returns armed-record progress for pad `x`; full bar means sync track `idx` is about to cross `pad`.
  uint32_t armed_sync_wash(int idx, int pad, int x, int step, int grid, uint32_t col) {
    track_t &r = t[idx];
    int span = mini(grid, step * maxi(1, (int)r.loop_len));
    int dist = head0_distance_to_pad(idx, pad, step, grid);
    int progress_q8 = (int)(((uint32_t)(span - dist) * (uint32_t)(BINS * 256)) / (uint32_t)maxi(1, span));
    int bri = clampi(progress_q8 - x * 256, 0, 255);
    return add_col(DIMMESTEST(col), fade_col(col, bri));
  }
  // Clips integer sample `v` to signed 16-bit audio range.
  int clip(int v) {
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
  }
  void toggle_track_blank(int idx) {
    if (swap_track == idx) return;
    if (record_track == idx && record_active) return;
    bool blank = !track_blank(idx);
    set_track_blank(idx, blank);
    if (record_track == idx)
      clear_record_arm();
    set_help_text("Track %d #fc2#*%s#.", idx + 1, blank ? "blank" : "active");
  }
  // Begins recording track `idx`; `free_len` infers quarter-note period, otherwise uses `grid` and `step`.
  void begin_record(int idx, bool free_len, int grid, int step) {
    if (swap_track != NO || record_active || !loop_capacity || (record_track != NO && record_track != idx))
      return;
    track_t &r = t[idx];
    undo_track = idx;
    undo_loop_from = r.loop_from;
    undo_loop_len = r.loop_len;
    undo_blank = track_blank(idx);
    memcpy(undo_peak, r.peak, sizeof(undo_peak));
    if (undo_blank)
      memset(r.peak, 0, sizeof(r.peak));
    rec_pos = 0;
    rec_grid = free_len ? mini(loop_capacity, (uint32_t)((float)SAMPLE_FREQ * (float)MAX_BOOTSTRAP_QN_US *
                                                        (float)BINS * 0.0000005f))
                        : (uint32_t)grid;
    int record_steps = maxi(1, (int)arm_record_len);
    rec_target = free_len ? rec_grid : mini(rec_grid, (uint32_t)(step * record_steps));
    rec_start = free_len ? 0 : (uint32_t)((arm_record_from & 15) * step);
    rec_wrap_start = free_len ? 0 : (uint32_t)(((arm_keep_loop ? r.loop_from : arm_record_from) & 15) * step);
    rec_wrap_span = free_len ? rec_grid : rec_target;
    undo_start = rec_start;
    undo_len = 0;
    undo_grid = rec_grid;
    undo_wrap_start = rec_wrap_start;
    undo_wrap_span = rec_wrap_span;
    record_free = free_len;
    record_keep_loop = arm_keep_loop;
    undo_keep_loop = record_keep_loop;
    if (!record_keep_loop)
      clear_heads(r);
    record_track = idx;
    record_active = true;
  }
  // Finishes or cancels the active recording and starts its loop head using pad size `step`.
  void finish_record(bool cancel, int step) {
    if (!record_active || record_track == NO) return;
    uint8_t idx = record_track;
    track_t &r = t[idx];
    bool keep_loop = record_keep_loop;
    if (cancel || rec_pos < 256) {
      r.loop_from = undo_loop_from;
      r.loop_len = undo_loop_len;
      set_track_blank(idx, undo_blank);
      memcpy(r.peak, undo_peak, sizeof(r.peak));
      record_active = false;
      if (!keep_loop)
        clear_heads(r);
      clear_record_arm();
      record_keep_loop = false;
      if (undo_len) start_undo_swap(false);
      else {
        undo_track = NO;
        undo_keep_loop = false;
      }
      return;
    }
    bool free_record = record_free;
    if (record_free) {
      int steps = 16;
      int period_us = (int)((float)rec_pos * 2000000.f / ((float)SAMPLE_FREQ * (float)steps));
      while (period_us < MIN_BOOTSTRAP_QN_US && steps > 1) {
        steps >>= 1;
        period_us = (int)((float)rec_pos * 2000000.f / ((float)SAMPLE_FREQ * (float)steps));
      }
      period_us = clampi(period_us, MIN_BOOTSTRAP_QN_US, MAX_BOOTSTRAP_QN_US);
      set_qn_period_us(period_us, true);
      r.loop_from = 0;
      r.loop_len = steps;
    } else if (!keep_loop) {
      int steps = clampi((int)((rec_pos + (uint32_t)step / 2u) / (uint32_t)step), 1, BINS);
      uint32_t rounded_len = mini(rec_grid, (uint32_t)steps * (uint32_t)step);
      r.loop_len = (uint8_t)steps;
      if (undo_blank && rounded_len > rec_pos) {
        stereo16_t *dst = (stereo16_t *)get_psram_ptr() + (uint32_t)idx * loop_capacity;
        for (uint32_t p = rec_pos; p < rounded_len; ++p)
          dst[record_sample_at(rec_start, p, rec_wrap_start, rec_wrap_span, rec_grid)] = {};
        undo_len = rounded_len;
      }
    }
    set_track_blank(idx, false);
    if (!keep_loop) {
      clear_heads(r);
      set_loop_head_playing(idx, true, step);
    }
    if (free_record) {
      cue_transport(0, -1);
    }
    record_active = false;
    clear_record_arm();
    record_keep_loop = false;
  }
  // Starts an incremental audio/peak swap between undo data and track data; `keep_undo` preserves redo.
  void start_undo_swap(bool keep_undo) {
    if (swap_track != NO || undo_track == NO || !undo_len || !undo_grid) {
      if (!keep_undo) {
        undo_track = NO;
        undo_keep_loop = false;
      }
      return;
    }
    track_t &r = t[undo_track];
    swap_track = undo_track;
    swap_loop_from = r.loop_from;
    swap_loop_len = r.loop_len;
    swap_blank = track_blank(undo_track);
    swap_keep_loop = undo_keep_loop;
    swap_keep_undo = keep_undo;
    swap_start = undo_start;
    swap_len = mini(undo_len, undo_grid);
    swap_grid = undo_grid;
    swap_wrap_start = undo_wrap_start;
    swap_wrap_span = undo_wrap_span;
    swap_pos = 0;
    memcpy(swap_peak, r.peak, sizeof(swap_peak));
  }
  // Completes the pending undo/redo swap and restores saved track loop metadata.
  void finish_undo_swap() {
    uint8_t idx = swap_track;
    track_t &r = t[idx];
    r.loop_from = undo_loop_from;
    r.loop_len = undo_loop_len;
    set_track_blank(idx, undo_blank);
    if (!swap_keep_loop)
      clear_heads(r);
    memcpy(r.peak, undo_peak, sizeof(undo_peak));
    if (swap_keep_undo) {
      undo_loop_from = swap_loop_from;
      undo_loop_len = swap_loop_len;
      undo_blank = swap_blank;
      undo_keep_loop = swap_keep_loop;
      memcpy(undo_peak, swap_peak, sizeof(undo_peak));
    } else {
      undo_track = NO;
      undo_keep_loop = false;
    }
    swap_track = NO;
    swap_pos = swap_len = swap_grid = swap_wrap_start = swap_wrap_span = 0;
    swap_keep_loop = false;
  }
  // Copies a block of the pending undo/redo swap between the track and undo buffers.
  void run_undo_swap() {
    if (swap_track == NO) return;
    if (swap_pos >= swap_len) {
      finish_undo_swap();
      return;
    }
    stereo16_t *dst = (stereo16_t *)get_psram_ptr() + (uint32_t)swap_track * loop_capacity;
    stereo16_t *src = (stereo16_t *)get_psram_ptr() + (uint32_t)N * loop_capacity;
    uint32_t n = mini((uint32_t)BLOCK_SIZE, swap_len - swap_pos);
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t at = record_sample_at(swap_start, swap_pos + i, swap_wrap_start, swap_wrap_span, swap_grid);
      stereo16_t a = dst[at], b = src[at];
      dst[at] = b;
      src[at] = a;
    }
    swap_pos += n;
    if (swap_track != NO && swap_pos >= swap_len)
      finish_undo_swap();
  }
  // Draws and edits per-track controls for track `idx`.
  void draw_track_controls(int idx, int step, bool transport_playing, bool shift_down) {
    track_t &r = t[idx];
    int y = ctrl_y(idx);
    uint32_t col = track_ui_col(idx);
    uint32_t play_base_col = track_col(idx);
    uint8_t bit = track_bit(idx);
    if (shift_down) {
      uint32_t solo_col = track_solod(idx) ? BRIGHTEST(YELLOW) : DIMMER(YELLOW);
      if (button(0, y, solo_col, ISOLATED, track_solod(idx) ? "Unsolo track" : "Solo track")) {
        solo_tracks ^= bit;
        tempo_shift_used = true;
        set_help_text("Track %d solo #fc2#*%s#.", idx + 1, track_solod(idx) ? "on" : "off");
      }
    } else {
      uint32_t play_col = DIMMEST(play_base_col);
      if (!track_blank(idx) && r.head0_playing) {
        if (transport_playing) {
          play_col = BRIGHTER(play_base_col);
        } else {
          int tri = (int)freerunning_clock_frac_q16();
          if (tri > 32767) tri = 65535 - tri;
          play_col = fade_col(BRIGHTER(play_base_col), 48 + ((tri >> 7) * 3 >> 2));
        }
      }
      if (button(0, y, play_col, ISOLATED, r.head0_playing ? "Stop loop" : "Play loop")) {
        set_loop_head_playing(idx, !r.head0_playing, step);
        set_help_text("Loop #fc2#*%s#.", r.head0_playing ? (transport_playing ? "play" : "queued") : "off");
      }
    }
    uint8_t vol_lo_before = r.vol_lo, vol_hi_before = r.vol_hi;
    bool vol_pad_down = get_touch_down(1, y);
    if (vol_pad_down && !r.vol_tap_tracking && does_touch_originate_here(1, y)) {
      r.vol_tap_tracking = true;
      r.vol_tap_start_us = time_us();
      r.vol_tap_lo = vol_lo_before;
      r.vol_tap_hi = vol_hi_before;
    }
    uint32_t vol_col = (!r.vol_lo && !r.vol_hi) ? RED : col;
    uint32_t slider_alt_col = track_audible(idx) ? palette[8][(idx + 3) & 15] : DIMMER(WHITE);
    int vol_moved = r.volume_slider.range_slider(1, y, 5, HORIZONTAL | SHOW_STEM, vol_col, 0, 127, r.vol_lo,
                                                 r.vol_hi, "Volume", slider_alt_col);
    if (vol_moved) {
      r.vol_lo = clampi(last_widget_new_from(), 0, 127);
      r.vol_hi = clampi(last_widget_new_to(), r.vol_lo, 127);
    }
    if (r.vol_tap_tracking && !vol_pad_down) {
      uint32_t held = time_us() - r.vol_tap_start_us;
      if (held < 150 * 1000) {
        if (r.vol_tap_hi) {
          r.last_nonzero_vol_lo = r.vol_tap_lo;
          r.last_nonzero_vol_hi = r.vol_tap_hi;
          r.vol_lo = r.vol_hi = 0;
        } else {
          r.vol_lo = r.last_nonzero_vol_lo;
          r.vol_hi = r.last_nonzero_vol_hi;
        }
      }
      r.vol_tap_tracking = false;
    }
    if (!vol_moved && r.vol_hi) {
      r.last_nonzero_vol_lo = r.vol_lo;
      r.last_nonzero_vol_hi = r.vol_hi;
    }
    if (idx == N - 1) return;
    if (r.filter_slider.simple_slider(6, y, 3, HORIZONTAL | SHOW_STEM | CENTER_STEM, track_audible(idx) ? YELLOW : col,
                                      -127, 127, r.filter, "DJ filter"))
      r.filter = clampi(last_widget_new_value(), -127, 127);
    if (r.delay_slider.simple_slider(9, y, 3, HORIZONTAL | SHOW_STEM, track_audible(idx) ? BLUE : col, 0, 127, r.delay,
                                     "Delay"))
      r.delay = clampi(last_widget_new_value(), 0, 127);
    if (r.reverb_slider.simple_slider(12, y, 2, HORIZONTAL | SHOW_STEM, track_audible(idx) ? PINK : col, 0, 127,
                                      r.reverb, "Reverb"))
      r.reverb = clampi(last_widget_new_value(), 0, 127);
    int reverse = get_touch_down(14, y) ? clampi(get_touch_pressure_xy(14, y), 0, 255) : 0;
    uint32_t reverse_col = reverse ? fade_col(BRIGHTER(col), mini(255, 64 + reverse)) : DIMMEST(col);
    button(14, y, reverse_col, ISOLATED, "Reverse");
    int beat_repeat = get_touch_down(15, y) ? clampi(get_touch_pressure_xy(15, y), 0, 255) : 0;
    uint32_t repeat_col = beat_repeat ? fade_col(BRIGHTER(col), mini(255, 64 + beat_repeat)) : DIMMEST(col);
    button(15, y, repeat_col, ISOLATED, "Beat repeat");
  }

  // Initializes PSRAM loop capacity after the panel state has been zero-initialized.
  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("looper: on_setup_default_panel_state\n");
    loop_capacity = (get_psram_size() / sizeof(stereo16_t)) / (N + 1);
    codec_enable_mic(audio_source == 1);
  }

  // Handles touch UI, track editing, transport buttons, and LED drawing for one UI tick.
  void on_ui(int) override {
    if (get_scroll_page() == -1) {
      static const char *const audio_source_options[] = {"line", "mic"};
      int delta = draw_system_style_enum_settings_page("src", audio_source, audio_source_options, 2);
      set_audio_source((int)audio_source + delta);
      save_settings_if_changed();
      return;
    }
    set_touch_origin_ignore_row_mask(15, 0xffe0);
    leds_clear();
    uint32_t tap_col = fade_col(WHITE, maxi(0, 256 - freerunning_clock_frac_q16() / 128));
    button(TAP_SHIFT_X, 15, tap_col, ISOLATED, "Tap tempo / Shift");
    bool shift_down = is_last_widget_held(), shift_pressed = is_last_widget_pressed(), shift_released = is_last_widget_released();
    uint32_t rec_col = RED;
    if (record_active) {
      rec_col = BRIGHTEST(RED);
    } else if (record_track != NO) {
      int tri = (int)freerunning_clock_frac_q16();
      if (tri > 32767) tri = 65535 - tri;
      rec_col = fade_col(RED, 64 + ((tri >> 7) * 3 >> 2));
    }
    bool rec = shift_button(RECORD_X, 15, rec_col, ISOLATED, "Record");
    bool rec_pressed = is_last_widget_pressed(), rec_released = is_last_widget_released();
    bool clr = shift_button(CLEAR_X, 15, WHITE, ISOLATED, "Clear");
    bool clr_pressed = is_last_widget_pressed(), clr_released = is_last_widget_released();
    bool stopped = stop_button(STOP_X, 15, RED);
    bool played = play_button(PLAY_X, 15, GREEN);
    draw_bottom_vu();
    bool vu_pressed = false;
    for (int x = 6; x <= 10; ++x)
      if (get_touch_pressed(x, 15)) vu_pressed = true;
    if (edit_mix_params && rec_pressed)
      edit_mix_params = false;
    if (vu_pressed)
      edit_mix_params = !edit_mix_params;
    uint32_t now = time_us();
    bool transport_playing = is_transport_playing();
    int step = 1, max_step = (int)(loop_capacity / BINS);
    if (max_step) {
      int period_us = qn_period_us > 0 ? qn_period_us : 500000;
      step = (int)((float)SAMPLE_FREQ * (float)period_us * 0.0000005f);
      step = step < 256 ? 256 : (step > max_step ? max_step : step);
    }
    int grid = step * BINS;
    if (shift_pressed) tempo_shift_used = false;
    if (rec_pressed) {
      rec_down_us = now;
      rec_shift_used = false;
    }
    if (rec_pressed && record_active) {
      stop_request = 1;
      rec_shift_used = true;
    }
    if (stopped && record_active)
      stop_request = 1;
    if (played && record_active)
      stop_request = 1;
    if (clr_pressed) {
      clear_touched_track = false;
      if (record_active) {
        cancel_request = 1;
        clear_touched_track = true;
      }
    }

    if (edit_mix_params) {
      draw_mix_params();
    } else {
      for (int y = 0; y < N; ++y) draw_track_controls(y, step, transport_playing, shift_down);
      for (int y = 0; y < N; ++y) {
        track_t &r = t[y];
        int row = play_y(y);
        uint32_t col = track_ui_col(y);
        int first = -1, pressed = -1, count = 0, origin_count = 0;
        bool row_pressed = false, was_touching = r.touching;
        for (int x = 0; x < 16; ++x) {
          bool down = get_touch_down(x, row);
          bool origin = down && does_touch_originate_here(x, row);
          if (origin && get_touch_pressed(x, row)) {
            row_pressed = true;
            if (pressed < 0) pressed = x;
          }
          if (down) {
            if (first < 0) first = x;
            ++count;
            if (origin) ++origin_count;
          }
        }
        if (shift_down && first >= 0)
          tempo_shift_used = true;

        if (rec && first >= 0) {
          rec_shift_used = true;
          if (is_recording(y) && !record_keep_loop)
            rec_target = mini(rec_grid, step * (uint32_t)maxi(1, (int)r.loop_len));
          if (row_pressed && !was_touching && !record_active) {
            int pad = pressed >= 0 ? pressed : first;
            toggle_record_arm(y, pad, transport_playing);
          }
        } else if (clr && first >= 0) {
          clear_touched_track = true;
          if (row_pressed && !was_touching && origin_count == 1)
            toggle_track_blank(y);
        }
        r.touching = count > 0;

        for (int x = 0; x < 16; ++x) {
          uint32_t led = BLACK;
          if (!track_blank(y)) {
            bool loop = pad_in_loop(r, x);
            int pk = peak_for_pad(r, x, step);
            led = pk ? fade_col(BRIGHTER(col), mini(255, pk + (loop ? 32 : 0))) : BLACK;
            led = add_col(led, loop ? DIMMEST(col) : DIMMEST(WHITE));
            int cursor = 0;
            for (int h = 0; h < HEADS; ++h)
              if (r.head[h].volume && x == (int)((r.head[h].pos / step) & 15))
                cursor = maxi(cursor, (int)r.head[h].volume);
            if (cursor) {
              led = add_col(led, fade_col(WHITE, mini(255, cursor * 2)));
            }
          }
          if (is_armed(y)) {
            uint32_t armed_col = pulse_col(ORANGE);
            uint8_t sync_track = armed_record_sync_track();
            led = add_col(led, sync_track != NO ? armed_sync_wash(sync_track, arm_sync_pad, x, step, grid, armed_col)
                                                : vu_wash(armed_col, x));
          }
          if (is_recording(y)) led = add_col(led, vu_wash(RED, x));
          set_led(x, row, led);
        }
      }
    }
    if (shift_released && !tempo_shift_used) {
      show_bpm_display();
      on_clock_tap();
      set_help_text("Tap tempo #fc2#*%d#.bpm", (int)get_tempo_bpm());
    }
    if (rec_released) {
      if (rec_down_us && !rec_shift_used && now - rec_down_us < 500000 && !record_active) {
        uint8_t y = blank_tracks ? (uint8_t)__builtin_ctz((unsigned)blank_tracks) : NO;
        if (y < N) {
          if (is_armed(y)) clear_record_arm();
          else arm_record_track(y, 0, transport_playing);
        }
      }
      rec_down_us = 0;
      rec_shift_used = false;
    }
    if (clr_released && !clear_touched_track && !record_active && swap_track == NO) undo_request = 1;
  }

  // Processes one audio block from stereo input `in`, recording armed tracks and mixing loops.
  bool on_dsp(const int16_t *in, int16_t *out, mix_buffers_t *mix_buffers_out) override {
    if (loop_capacity < BINS) {
      return panel_t::on_dsp(in, out, mix_buffers_out);
    }
    (void)out;
    for (int i = 0; i < BLOCK_SIZE * 2; ++i) mix_buffers_out->dry[i] = in[i];
    memset(mix_buffers_out->delaysend, 0, sizeof(mix_buffers_out->delaysend));
    memset(mix_buffers_out->reverbsend, 0, sizeof(mix_buffers_out->reverbsend));
    int in_peak = 0;
    for (int i = 0; i < BLOCK_SIZE; ++i)
      in_peak = maxi(in_peak, maxi(abs((int)in[i * 2]), abs((int)in[i * 2 + 1])));
    update_input_peak_vu(in_peak);
    int step = (int)((float)SAMPLE_FREQ * (float)qn_period_us * 0.0000005f);
    int max_step = (int)(loop_capacity / BINS);
    step = step < 256 ? 256 : (step > max_step ? max_step : step);
    int grid = step * BINS;
    bool transport_playing = is_transport_playing();
    if (has_transport_just_reset()) {
      for (int y = 0; y < N; ++y) {
        track_t &r = t[y];
        r.head[0].pad = r.loop_from;
        r.head[0].pos = (r.loop_from & 15) * step;
        r.head[0].sample_at_fx_start = r.head[0].fx_duration = 0;
        r.head[0].keyed_by_fx = 0;
        r.head[0].fx_was_running_last_frame = false;
        r.head[0].volume = (!track_blank(y) && r.head0_playing) ? 127 : 0;
        for (int h = 1; h < HEADS; ++h) {
          r.head[h].volume = 0;
          r.head[h].sample_at_fx_start = r.head[h].fx_duration = 0;
          r.head[h].keyed_by_fx = 0;
          r.head[h].fx_was_running_last_frame = false;
        }
      }
    }
    uint8_t sync_track = armed_record_sync_track();
    if (cancel_request) {
      cancel_request = 0;
      stop_request = 0;
      finish_record(true, step);
    }
    if (stop_request) {
      stop_request = 0;
      finish_record(false, step);
    }
    if (!record_active && record_track != NO) {
      uint8_t y = record_track;
      bool free_len = tempo_bootstrap_record(transport_playing);
      bool start_recording = sync_track == NO ? in_peak > AUTO_THRESHOLD
                                              : head0_will_cross_pad(sync_track, arm_sync_pad, step, grid);
      if (start_recording)
        begin_record(y, free_len, grid, step);
    }
    if (record_active && record_track != NO) {
      uint8_t y = record_track;
      stereo16_t *dst = (stereo16_t *)get_psram_ptr() + (uint32_t)y * loop_capacity;
      stereo16_t *bak = (stereo16_t *)get_psram_ptr() + (uint32_t)N * loop_capacity;
      uint32_t wr[BLOCK_SIZE];
      stereo16_t old[BLOCK_SIZE];
      int n = 0;
      bool blank_take = track_blank(y);
      if (rec_pos >= rec_target) finish_record(false, step);
      for (; record_active && n < BLOCK_SIZE && rec_pos < rec_target; ++n, ++rec_pos) {
        wr[n] = record_sample_at(rec_start, rec_pos, rec_wrap_start, rec_wrap_span, rec_grid);
        old[n] = blank_take ? stereo16_t{} : dst[wr[n]];
      }
      for (int i = 0; i < n; ++i)
        bak[wr[i]] = old[i];
      for (int i = 0; i < n; ++i) {
        stereo16_t mixed = old[i] + make_stereo16(in[i * 2], in[i * 2 + 1]);
        dst[wr[i]] = mixed;
        uint32_t b = wr[i] >> PEAK_SHIFT;
        uint8_t p = mini(255, maxi(abs((int)mixed.l), abs((int)mixed.r)) >> 7);
        if (b < PEAK_BINS && p > t[y].peak[b]) t[y].peak[b] = p;
      }
      if (n) undo_len = rec_pos;
      if (record_active && rec_pos >= rec_target) finish_record(false, step);
    }
    if (undo_request) {
      undo_request = 0;
      if (!record_active) start_undo_swap(true);
    }
    run_undo_swap();

    for (int y = 0; y < N; ++y) {
      track_t &r = t[y];
      bool track_is_blank = track_blank(y);
      bool solo_muted = !track_audible(y);
      uint8_t new_vol[HEADS];
      update_playheads_from_touch(y, new_vol, step, transport_playing);
      memset(track_block, 0, sizeof(track_block));
      stereo16_t *p = (stereo16_t *)get_psram_ptr() + (uint32_t)y * loop_capacity;
      bool fx_down = r.reverse || r.beat_repeat;
      bool fx_from_release = fx_down && !r.fx_pad_down;
      bool beat_repeat_down = r.beat_repeat && r.fx_pad_down;
      bool beat_repeat_starting = beat_repeat_down && !r.beat_repeat_was_down;
      for (int h = 0; h < HEADS; ++h) {
        head_t &hd = r.head[h];
        int old_gain = track_is_blank ? 0 : hd.gain;
        int target_vol = new_vol[h];
        bool advance_head = transport_playing || h != 0;
        int base_start = 0, base_span = grid;
        if (h == 0 || hd.keyed_by_fx) {
          base_start = (r.loop_from & 15) * step;
          base_span = mini(grid, step * maxi(1, (int)r.loop_len));
        }
        if (!fx_down && hd.fx_duration) {
          hd.pos = loop_pos(hd.sample_at_fx_start + hd.fx_duration, base_start, base_span, grid);
          hd.sample_at_fx_start = hd.fx_duration = 0;
        }
        bool fx_release_tail = fx_from_release && hd.fx_was_running_last_frame && advance_head;
        int target_gain = (track_is_blank || solo_muted || (h == 0 && !transport_playing)) ? 0 : target_vol;
        if (fx_release_tail)
          target_gain = 0;
        if (hd.pos >= grid || rel_sample(hd.pos, base_start, grid) >= base_span)
          hd.pos = base_start;
        bool fx_starting = fx_down && !fx_from_release && !hd.fx_was_running_last_frame && advance_head;
        if (fx_starting || (beat_repeat_starting && advance_head)) {
          hd.sample_at_fx_start = hd.pos;
          hd.fx_duration = 0;
        }
        int start = base_start, span = base_span, repeat = r.beat_repeat ? step : 0;
        if (repeat && r.beat_repeat >= 103)
          repeat = maxi(1, repeat >> 1);
        // Beat repeat latches a 1/8 or 1/16 slice containing the playhead at FX start.
        if (repeat) {
          int snap = (rel_sample(hd.sample_at_fx_start, base_start, grid) / repeat) * repeat;
          start = base_start + snap;
          if (start >= grid) start -= grid;
          span = mini(repeat, base_span - snap);
        }
        int rel = rel_sample(hd.pos, start, grid);
        int advance = advance_head ? (r.reverse ? -BLOCK_SIZE : BLOCK_SIZE) : 0;
        bool loops = advance_head && (hd.pos >= grid || rel >= span || (r.reverse ? rel < BLOCK_SIZE : rel + BLOCK_SIZE >= span));
        if (loops) {
          render_block(p, track_block, hd.pos, old_gain, 0, r.reverse, 0, grid);
          if (hd.keyed_by_fx && repeat) {
            // FX-keyed beat repeat jumps to a random step on each slice boundary.
            int jump = loop_pos(base_start + (rand() % maxi(1, (int)r.loop_len)) * step, base_start, base_span, grid);
            if (r.reverse) jump = loop_pos(jump + step - 1, base_start, base_span, grid);
            render_block(p, track_block, jump, 0, target_gain, r.reverse, 0, grid);
            int snap = (rel_sample(jump, base_start, grid) / repeat) * repeat;
            start = base_start + snap;
            if (start >= grid) start -= grid;
            span = mini(repeat, base_span - snap);
            hd.sample_at_fx_start = start;
            hd.pos = loop_pos(jump + advance, start, span, grid);
          } else {
            // Normal loop/repeat wrap crossfades over the boundary.
            int second = hd.pos + (r.reverse ? span : -span);
            render_block(p, track_block, second, 0, target_gain, r.reverse, 0, grid);
            hd.pos = loop_pos(hd.pos + advance, start, span, grid);
          }
        } else {
          // Ordinary playback stays inside the current loop or repeat slice.
          render_block(p, track_block, hd.pos, old_gain, target_gain, r.reverse, 0, grid);
          hd.pos = loop_pos(hd.pos + advance, start, span, grid);
        }
        hd.volume = target_vol;
        hd.gain = target_gain;
        if (!target_vol) {
          hd.keyed_by_fx = hd.sample_at_fx_start = hd.fx_duration = 0;
          hd.fx_was_running_last_frame = false;
        } else if (fx_down && advance_head) {
          hd.fx_duration += BLOCK_SIZE;
          if (hd.fx_duration >= base_span)
            hd.fx_duration -= base_span;
          if (fx_release_tail) {
            hd.pos = loop_pos(hd.sample_at_fx_start + hd.fx_duration, base_start, base_span, grid);
            hd.sample_at_fx_start = hd.fx_duration = 0;
            hd.fx_was_running_last_frame = false;
          } else {
            hd.fx_was_running_last_frame = !fx_from_release;
          }
        } else {
          hd.sample_at_fx_start = 0;
          hd.fx_was_running_last_frame = false;
        }
      }
      if (fx_from_release)
        r.reverse = r.beat_repeat = 0;
      r.beat_repeat_was_down = beat_repeat_down;
      float num = comp_num(r.vol_lo, r.vol_hi), denom = comp_denom(r.vol_lo, r.vol_hi), gain_smoothed = r.gain_smoothed;
      float extrafade = mini(32, (int)r.vol_hi) * (1.f / 32.f);
      biquad_coeff_t target_coeff = biquad_coeff_make_dj_filter(r.filter);
      biquad_coeff_t coeff = r.filter_coeff;
      biquad_coeff_t dcoeff = biquad_coeff_mul(biquad_coeff_diff(target_coeff, coeff), 1.f / (float)BLOCK_SIZE);
      float prev_l = 0.f, prev_r = 0.f;
      for (int i = 0; i < BLOCK_SIZE; ++i) {
        biquad_coeff_accum(&coeff, dcoeff);
        float filtered_l = biquad_process_sample(&r.filter_state[0], &coeff, (float)track_block[i].l);
        float filtered_r = biquad_process_sample(&r.filter_state[1], &coeff, (float)track_block[i].r);
        if ((i & 1) == 0) {
          prev_l = filtered_l;
          prev_r = filtered_r;
          continue;
        }
        gain_smoothed = update_comp_gain(fabsf(prev_l) + fabsf(filtered_l) + fabsf(prev_r) + fabsf(filtered_r),
                                         gain_smoothed, num, denom);
        int gain_q11 = mini(65536, (int)(gain_smoothed * extrafade));
        // I found that with default settings, a gain of about 1/4000 matches the input.
        float out_gain = (float)gain_q11 * (1.f / 4000.f);
        int v0_l = clip((int)(prev_l * out_gain));
        int v0_r = clip((int)(prev_r * out_gain));
        int v1_l = clip((int)(filtered_l * out_gain));
        int v1_r = clip((int)(filtered_r * out_gain));
        int j = i - 1;
        mix_buffers_out->dry[j * 2] += v0_l;
        mix_buffers_out->dry[j * 2 + 1] += v0_r;
        mix_buffers_out->dry[i * 2] += v1_l;
        mix_buffers_out->dry[i * 2 + 1] += v1_r;
        int send0 = (v0_l + v0_r) >> 1;
        int send1 = (v1_l + v1_r) >> 1;
        mix_buffers_out->delaysend[j] += (send0 * r.delay) >> 7;
        mix_buffers_out->delaysend[i] += (send1 * r.delay) >> 7;
        mix_buffers_out->reverbsend[j] += (send0 * r.reverb) >> 9;
        mix_buffers_out->reverbsend[i] += (send1 * r.reverb) >> 9;
      }
      r.filter_coeff = target_coeff;
      r.gain_smoothed = gain_smoothed;
    }
    return false;
  }
};
