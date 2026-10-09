// Generated from a Plinky 12 built-in example by scripts/update_community_panel_builds.sh.
/*
@Name: Worm
@Author: mmalex
@Description: A pressure-driven granular sampler backed by PSRAM.
@Tags: granular

Hold the bottom-right record pad while touching main pads to start recording stereo input into
PSRAM. The record head follows the touch centroid, preferring nearby empty pads. Lift all main
pad pressure to fade recording out. Touch recorded pads without record to seed a 32-grain
granular cloud.
*/
#define PANEL_PAD_COLOR PINK

struct worm : panel_t {
  static constexpr int MAIN_PADS = 240, PIECES = MAIN_PADS + 1;
  static constexpr int SILENCE_PAD = MAIN_PADS;
  static constexpr int RECORD_PAD = 255;
  static constexpr int CLEAR_X = 13, OCTAVE_X = 14, RECORD_X = 15, CONTROL_Y = 15;
  static constexpr int GRAINS = 32, MIX_SLIDERS = 11;
  static constexpr int ALPHA_MAX = 32767;
  static constexpr int GRAIN_SUM_SHIFT = 2;

  struct grain_t {
    uint32_t pos = 0;
    int env_q15 = 0;
    int denv_q15 = 0;
    uint8_t pad = 0;
    uint8_t volume = 0;
    int8_t octave = 0;
    bool active = false;
  };

  uint32_t pad_capacity = 0;
  uint32_t pad_col[PIECES]{};
  uint8_t pad_next[PIECES]{};
  grain_t grain[GRAINS]{};

  uint32_t cdf[MAIN_PADS]{};
  uint32_t cdf_total = 0;
  uint8_t octave_prob_q7 = 0;
  uint8_t octave_toggle_q7 = 0;
  uint8_t octave_press_peak_q7 = 0;

  slider_t gain_slider[3]{};
  slider_t mix_slider[MIX_SLIDERS]{};
  panel_page_t panel_page;
  uint8_t dry_send = 96, delay_send = 0, reverb_send = 24;
  uint8_t audio_source = 0;

  biquad_coeff_t colour_coeff[3]{};
  biquad_t colour_filter[3]{};
  uint32_t colour_energy[3]{};
  uint32_t colour_samples = 0;

  uint8_t record_pad = RECORD_PAD;
  uint8_t record_hue = 1;
  uint32_t record_pos = 0;
  uint32_t mix_overlay_last_touch_us = 0;
  uint16_t vu_peak = 0;
  int record_centroid_x_q8 = 0, record_centroid_y_q8 = 0;
  int record_alpha_q15 = 0;
  float comp_peak = 0.f;
  float comp_gain = 1.f;
  bool recording = false;
  bool record_fading_out = false;
  bool mix_overlay = false;
  bool octave_press_used = false;

  int get_num_pages() override { return 2; }
  int get_num_panel_settings_pages() override { return 1; }

  worm() {
    memset(pad_next, SILENCE_PAD, sizeof(pad_next));
    biquad_coeff_set_rbj_bandpass(&colour_coeff[0], 0.014f, 1.4f);
    biquad_coeff_set_rbj_bandpass(&colour_coeff[1], 0.075f, 1.4f);
    biquad_coeff_set_rbj_bandpass(&colour_coeff[2], 0.38f, 1.4f);
  }

  bool on_serialise(serialiser_t &s, int version) override {
    (void)version;
    worm &o = *this;
    int pad_col_bytes = (int)sizeof(pad_col);
    int pad_next_bytes = (int)sizeof(pad_next);
    OBJECT_BEGIN(s);
    FIELD_MIX_PRESET("presetMix");
    FIELD("dry", dry_send, 0u, 127u);
    FIELD("delay", delay_send, 0u, 127u);
    FIELD("reverb", reverb_send, 0u, 127u);
    FIELD("oct", octave_toggle_q7, 0u, 127u);
    FIELD("hue", record_hue, 0u, 15u);
    FIELD_BASE64("pad_col", pad_col, pad_col_bytes, (int)sizeof(pad_col));
    FIELD_BASE64("pad_next", pad_next, pad_next_bytes, (int)sizeof(pad_next));
    OBJECT_END(s);
    if (s.reading) {
      pad_capacity = (get_psram_size() / sizeof(stereo16_t)) / PIECES;
      if (pad_capacity)
        memset(get_psram_ptr(), 0, (size_t)pad_capacity * (size_t)PIECES * sizeof(stereo16_t));
      for (int pad = 0; pad < PIECES; ++pad)
        if (pad_next[pad] >= PIECES)
          pad_next[pad] = SILENCE_PAD;
      pad_col[SILENCE_PAD] = BLACK;
      pad_next[SILENCE_PAD] = SILENCE_PAD;
    }
    return serialise_psram_binary_footer(s, PSRAM_BINARY_FOOTER_CODEC_LOSSY_STEREO_8BIT);
  }

  bool on_serialise_settings(serialiser_t &s, int version) override {
    (void)version;
    OBJECT_BEGIN(s);
    FIELD("dry", dry_send, 0u, 127u);
    FIELD("delay", delay_send, 0u, 127u);
    FIELD("reverb", reverb_send, 0u, 127u);
    FIELD("audio_source", audio_source, 0u, 1u);
    OBJECT_END(s);
    return true;
  }

  static int pad_x(int pad) { return pad & 15; }
  static int pad_y(int pad) { return pad >> 4; }
  static int xy_pad(int x, int y) { return (y << 4) | x; }

  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("worm: on_setup_default_panel_state\n");
    pad_capacity = (get_psram_size() / sizeof(stereo16_t)) / PIECES;
    if (pad_capacity)
      memset(get_psram_ptr(), 0, (size_t)pad_capacity * (size_t)PIECES * sizeof(stereo16_t));
    codec_enable_mic(audio_source == 1);
  }

  void on_sequence(int) override {
    uint32_t total = 0;
    for (int pad = 0; pad < MAIN_PADS; ++pad) {
      int x = pad_x(pad), y = pad_y(pad);
      total += (uint32_t)touch_pressure_curve_q7(get_touch_pressure_xy(x, y));
      cdf[pad] = total;
    }
    cdf_total = total;
    int live_octave_q7 = get_touch_down(OCTAVE_X, CONTROL_Y) ? mini(127, touch_pressure_curve_q7(get_touch_pressure_xy(OCTAVE_X, CONTROL_Y))) : 0;
    octave_prob_q7 = (uint8_t)maxi((int)octave_toggle_q7, live_octave_q7);
    update_record_touch_centroid();
  }

  int update_record_touch_centroid() {
    int p = 0, x = 0, y = 0;
    get_pressure_and_pos_in_rect(0, 0, 16, 15, false, &p, &x, &y);
    if (p > 0) {
      record_centroid_x_q8 = x;
      record_centroid_y_q8 = y;
    }
    return maxi(0, p);
  }

  uint32_t centroid_dist2_q8(int x, int y) const {
    int dx = (x << 8) - record_centroid_x_q8;
    int dy = (y << 8) - record_centroid_y_q8;
    return (uint32_t)(dx * dx + dy * dy);
  }

  int level_to_led_channel(uint32_t energy) {
    if (!colour_samples) return 0;
    return clampi((int)(energy / colour_samples), 0, 96);
  }

  void update_record_colour() {
    if (record_pad >= MAIN_PADS || !colour_samples) return;
    int r = level_to_led_channel(colour_energy[0]);
    int g = level_to_led_channel(colour_energy[1]);
    int b = level_to_led_channel(colour_energy[2]);
    uint32_t col;
    if (r < 4 && g < 4 && b < 4)
      col = DIMMEST(WHITE);
    else
      col = LED_RGB(r, g, b);
    pad_col[record_pad] = lerp_col(col, palette[8][record_hue & 15], 77); // 30% record hue, 70% measured colour.
  }

  void begin_record_pad(int pad) {
    record_pad = (uint8_t)pad;
    record_pos = 0;
    for (int i = 0; i < 3; ++i) {
      biquad_reset(&colour_filter[i]);
      colour_energy[i] = 0;
    }
    colour_samples = 0;
    update_record_colour();
  }

  void request_record_stop() {
    if (recording)
      record_fading_out = true;
  }

  bool spawn_grain(grain_t &g) {
    uint32_t total = cdf_total;
    if (!total || !pad_capacity) {
      g.active = false;
      return false;
    }
    uint32_t r = (uint32_t)(rand() % (int)total);
    int pad = 0;
    while (pad < MAIN_PADS - 1 && cdf[pad] <= r) ++pad;
    int p = mini(127, touch_pressure_curve_q7(get_touch_pressure_xy(pad_x(pad), pad_y(pad))));
    uint32_t min_len = mini((uint32_t)(SAMPLE_FREQ / 500), pad_capacity);
    uint32_t max_len = (pad_capacity * (192+(rand()&63))) >> 8; // long grains are randomised in length a bit
    uint32_t len = min_len + (((uint32_t)p * (max_len - min_len)) / 127u);
    g.pad = (uint8_t)pad;
    g.pos = pad_capacity > 1 ? (uint32_t)(rand() % (int)pad_capacity) : 0;
    g.env_q15 = 0;
    g.denv_q15 = maxi(1, (int)(((uint32_t)ALPHA_MAX * 2u + len - 1u) / len));
    g.volume = (uint8_t)p;
    g.octave = ((rand() & 127) < octave_prob_q7) ? ((rand() & 1) ? 1 : -1) : 0;
    g.active = true;
    return true;
  }

  void mix_grain_sample(mix_buffers_t *mix_buffers_out, grain_t &g, int out, stereo16_t s) {
    int env = clampi(g.env_q15, 0, ALPHA_MAX);
    int gain = (env * (int)g.volume) >> 7;
    mix_buffers_out->dry[out * 2 + 0] += ((int)s.l * gain) >> (15 + GRAIN_SUM_SHIFT);
    mix_buffers_out->dry[out * 2 + 1] += ((int)s.r * gain) >> (15 + GRAIN_SUM_SHIFT);
    g.env_q15 += g.denv_q15;
    if (g.denv_q15 > 0 && g.env_q15 >= ALPHA_MAX) {
      g.env_q15 = ALPHA_MAX;
      g.denv_q15 = -g.denv_q15;
    } else if (g.denv_q15 < 0 && g.env_q15 <= 0) {
      g.env_q15 = 0;
      g.active = false;
    }
  }

  void mix_grains(mix_buffers_t *mix_buffers_out) {
    stereo16_t *psram = (stereo16_t *)get_psram_ptr();
    for (int gi = 0; gi < GRAINS; ++gi) {
      grain_t &g = grain[gi];
      int out = 0;
      while (out < BLOCK_SIZE) {
        if (!g.active && !spawn_grain(g)) break;
        if (g.pad >= PIECES)
          g.pad = SILENCE_PAD;
        stereo16_t *grain_p = psram + (uint32_t)g.pad * pad_capacity;
        uint32_t trigger = pad_capacity - 1;
        uint8_t next_pad = pad_next[g.pad];

#define READ_GRAIN_SAMPLE(SAMPLE)                                                                                                    \
  do {                                                                                                                               \
    (SAMPLE) = grain_p[g.pos];                                                                                                       \
    if (g.pos == trigger) {                                                                                                          \
      g.pad = next_pad;                                                                                                              \
      g.pos = 0;                                                                                                                     \
      if (g.pad >= PIECES)                                                                                                           \
        g.pad = SILENCE_PAD;                                                                                                         \
      grain_p = psram + (uint32_t)g.pad * pad_capacity;                                                                              \
      next_pad = pad_next[g.pad];                                                                                                    \
    } else {                                                                                                                         \
      g.pos++;                                                                                                                       \
    }                                                                                                                                \
  } while (0)

        if (g.octave > 0) {
          while (out < BLOCK_SIZE && g.active) {
            stereo16_t a, b;
            READ_GRAIN_SAMPLE(a);
            READ_GRAIN_SAMPLE(b);
            mix_grain_sample(mix_buffers_out, g, out++, make_stereo16(((int)a.l + b.l) >> 1, ((int)a.r + b.r) >> 1));
          }
        } else if (g.octave < 0) {
          while (out < BLOCK_SIZE && g.active) {
            stereo16_t s;
            READ_GRAIN_SAMPLE(s);
            mix_grain_sample(mix_buffers_out, g, out++, s);
            if (out < BLOCK_SIZE && g.active)
              mix_grain_sample(mix_buffers_out, g, out++, s);
          }
        } else {
          while (out < BLOCK_SIZE && g.active) {
            stereo16_t s;
            READ_GRAIN_SAMPLE(s);
            mix_grain_sample(mix_buffers_out, g, out++, s);
          }
        }

#undef READ_GRAIN_SAMPLE
      }
    }
  }

  void update_record_alpha() {
    if (record_fading_out) {
      int step = maxi(1, (ALPHA_MAX + (SAMPLE_FREQ / 5) - 1) / (SAMPLE_FREQ / 5));
      record_alpha_q15 -= step;
      if (record_alpha_q15 <= 0) {
        record_alpha_q15 = 0;
        recording = false;
        record_fading_out = false;
        update_record_colour();
        record_pad = RECORD_PAD;
      }
    } else {
      int step = maxi(1, (ALPHA_MAX + (SAMPLE_FREQ / 20) - 1) / (SAMPLE_FREQ / 20));
      record_alpha_q15 = mini(ALPHA_MAX, record_alpha_q15 + step);
    }
  }

  void clear_pad_audio(int pad) {
    if (!pad_capacity) return;
    stereo16_t *p = (stereo16_t *)get_psram_ptr() + (uint32_t)pad * pad_capacity;
    uint32_t fade = mini((uint32_t)(SAMPLE_FREQ / 20), pad_capacity >> 1);
    uint32_t tail = pad_capacity - fade;
    for (uint32_t i = 0; i < pad_capacity; ++i) {
      if (i < fade) {
        int gain = (int)(((uint32_t)(fade - i) * (uint32_t)ALPHA_MAX) / maxi(1u, fade));
        p[i] = make_stereo16(((int)p[i].l * gain) >> 15, ((int)p[i].r * gain) >> 15);
      } else if (i >= tail) {
        int gain = (int)(((uint32_t)(i - tail) * (uint32_t)ALPHA_MAX) / maxi(1u, fade));
        p[i] = make_stereo16(((int)p[i].l * gain) >> 15, ((int)p[i].r * gain) >> 15);
      } else {
        p[i] = stereo16_t{};
      }
    }
    pad_col[pad] = BLACK;
    pad_next[pad] = SILENCE_PAD;
    set_led(pad_x(pad), pad_y(pad), BLACK);
  }

  void clear_touched_pads() {
    for (int pad = 0; pad < MAIN_PADS; ++pad) {
      int x = pad_x(pad), y = pad_y(pad);
      if (get_touch_down(x, y) && pad_col[pad])
        clear_pad_audio(pad);
    }
  }

  void record_block(const int16_t *in) {
    if (!recording || !pad_capacity) return;
    stereo16_t *psram = (stereo16_t *)get_psram_ptr();
    for (int i = 0; i < BLOCK_SIZE && recording; ++i) {
      if (record_pos >= pad_capacity) {
        uint8_t from = record_pad;
        update_record_colour();

        static const int8_t dx[4] = {1, 0, -1, 0};
        static const int8_t dy[4] = {0, 1, 0, -1};
        int x = pad_x(record_pad), y = pad_y(record_pad), next = record_pad;
        uint32_t best_score = 0;
        for (int j = 0; j < 4; ++j) {
          int nx = (x + dx[j]) & 15;
          int ny = (y + dy[j]) & 15;
          int pad = xy_pad(nx, ny);
          if (pad >= MAIN_PADS) continue;
          uint32_t dist = centroid_dist2_q8(nx, ny);
          uint32_t score = 1000000000u / (1024u + dist);
          score = (score * (uint32_t)(80 + (rand() % 41))) / 100u;
          if (pad_col[pad])
            score = (score + 1023u) >> 10;
          if (score > best_score) {
            next = pad;
            best_score = score;
          }
        }
        pad_next[from] = (uint8_t)next;
        begin_record_pad(next);
      }

      uint32_t at = (uint32_t)record_pad * pad_capacity + record_pos;
      stereo16_t old = psram[at];
      int a = clampi(record_alpha_q15, 0, ALPHA_MAX);
      int inv = ALPHA_MAX - a;
      int l = ((int)old.l * inv + (int)in[i * 2 + 0] * a) >> 15;
      int r = ((int)old.r * inv + (int)in[i * 2 + 1] * a) >> 15;
      stereo16_t mixed = clip_stereo16(l, r);
      psram[at] = mixed;

      float mono = ((float)mixed.l + (float)mixed.r) * 0.5f;
      for (int b = 0; b < 3; ++b) {
        float y = biquad_process_sample(&colour_filter[b], &colour_coeff[b], mono);
        int e = clampi((int)fabsf(y) >> 5, 0, 96);
        if (b == 1)
          e = (e * 181) >> 7;
        else if (b == 2)
          e = (e * 295) >> 7;
        colour_energy[b] += (uint32_t)e;
      }
      colour_samples++;
      record_pos++;
      update_record_alpha();
    }
    update_record_colour();
  }

  void compress_grain_mix(mix_buffers_t *mix_buffers_out) {
    int peak = 0;
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      peak = maxi(peak, abs(mix_buffers_out->dry[i * 2 + 0]));
      peak = maxi(peak, abs(mix_buffers_out->dry[i * 2 + 1]));
    }
    float target_peak = (float)peak;
    float peak_delta = target_peak - comp_peak;
    comp_peak += peak_delta * (peak_delta > 0.f ? ((float)BLOCK_SIZE / ((float)SAMPLE_FREQ * 0.050f))
                                                 : ((float)BLOCK_SIZE / ((float)SAMPLE_FREQ * 0.250f)));

    float level = fmaxf(comp_peak, 500.f) / 500.f;
    float target_gain = 6.f * powf(level, -0.442f);
    target_gain = fminf(6.f, fmaxf(0.75f, target_gain));
    int gain_q25 = clampi((int)(comp_gain * 33554432.f), 0, 0x7fffffff);
    int target_gain_q25 = clampi((int)(target_gain * 33554432.f), 0, 0x7fffffff);
    int gain_step_q25 = (target_gain_q25 - gain_q25) >> BLOCK_SIZE_SH;
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      int l = smmul(mix_buffers_out->dry[i * 2 + 0] << 7, gain_q25);
      int r = smmul(mix_buffers_out->dry[i * 2 + 1] << 7, gain_q25);
      int mono = (l + r) >> 1;
      mix_buffers_out->dry[i * 2 + 0] = soft_clip((l * (int)dry_send) >> 7);
      mix_buffers_out->dry[i * 2 + 1] = soft_clip((r * (int)dry_send) >> 7);
      mix_buffers_out->delaysend[i] = soft_clip((mono * (int)delay_send) >> 7);
      mix_buffers_out->reverbsend[i] = soft_clip((mono * (int)reverb_send) >> 8);
      gain_q25 += gain_step_q25;
    }
    comp_gain = target_gain;
  }

  void update_input_vu_peak(const int16_t *in) {
    int peak = 0;
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      peak = maxi(peak, abs((int)in[i * 2 + 0]));
      peak = maxi(peak, abs((int)in[i * 2 + 1]));
    }
    vu_peak = (uint16_t)mini(peak, 32768);
  }

  bool on_dsp(const int16_t *in, int16_t *out, mix_buffers_t *mix_buffers_out) override {
    (void)out;
    update_input_vu_peak(in);
    memset(mix_buffers_out->dry, 0, sizeof(mix_buffers_out->dry));
    memset(mix_buffers_out->delaysend, 0, sizeof(mix_buffers_out->delaysend));
    memset(mix_buffers_out->reverbsend, 0, sizeof(mix_buffers_out->reverbsend));
    if (!pad_capacity)
      return false;

    record_block(in);
    if (!recording)
      mix_grains(mix_buffers_out);
    compress_grain_mix(mix_buffers_out);
    // false means the dry/delay/reverb sends in mix_buffers_out should run through Plinky's built-in FX/output chain.
    return false;
  }

  void draw_gain_page(int y) {
    if (gain_slider[0].simple_slider(1, y, 7, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, GREEN, 0, 127, dry_send, "Grain dry"))
      dry_send = (uint8_t)clampi(last_widget_new_value(), 0, 127);
    if (gain_slider[1].simple_slider(2, y, 7, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, BLUE, 0, 127, delay_send, "Grain delay"))
      delay_send = (uint8_t)clampi(last_widget_new_value(), 0, 127);
    if (gain_slider[2].simple_slider(3, y, 7, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, PINK, 0, 127, reverb_send, "Grain reverb"))
      reverb_send = (uint8_t)clampi(last_widget_new_value(), 0, 127);

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
    synth_param_sliders_block(mix_slider, 5, y, MIX_SLIDERS, 7, MIX_PRESET_IDX, mix_params);
    leds_draw_string(5, y + 9, FONT_4, BRIGHTER(PINK), "MIX");
  }

  void on_ui(int) override {
    if (get_scroll_page() == -1) {
      static const char *const audio_source_options[] = {"line", "mic"};
      int delta = draw_system_style_enum_settings_page("src", audio_source, audio_source_options, 2);
      uint8_t next_source = (uint8_t)clampi((int)audio_source + delta, 0, 1);
      if (next_source != audio_source) {
        audio_source = next_source;
        codec_enable_mic(audio_source == 1);
        (void)save_settings_to_sd(false);
      }
      return;
    }
    int page = get_scroll_page();
    if (page < 0)
      return;
    leds_clear();
    if (page >= 1) {
      mix_overlay = false;
      mix_overlay_last_touch_us = 0;
      if (recording && !record_fading_out)
        request_record_stop();
    }
    if (page == 1) {
      panel_page.saveload(16, true, FLAG_PICKER_ENABLE_DELETE);
      return;
    }
    if (page >= 2) {
      scroll_to_page(1);
      return;
    }

    uint32_t now = time_us_32();
    uint16_t bottom_pressed = 0;
    for (int x = 0; x < 16; ++x) {
      if (get_touch_pressed(x, CONTROL_Y))
        bottom_pressed |= (uint16_t)(1u << x);
    }
    bool was_mix_overlay = mix_overlay;
    if (!was_mix_overlay && (bottom_pressed & 0x1fffu)) {
      mix_overlay = true;
      mix_overlay_last_touch_us = now;
    }
    if (mix_overlay) {
      if (recording && !record_fading_out)
        request_record_stop();
      if (num_down)
        mix_overlay_last_touch_us = now;
      draw_gain_page(0);
      draw_vu_meter(0, CONTROL_Y, 13, HORIZONTAL, vu_peak);
      if (was_mix_overlay &&
          (bottom_pressed || (!num_down && mix_overlay_last_touch_us && now - mix_overlay_last_touch_us >= 3000000u))) {
        mix_overlay = false;
        mix_overlay_last_touch_us = 0;
      }
      return;
    }
    mix_overlay_last_touch_us = 0;

    int record_pressure_total = update_record_touch_centroid();
    uint32_t rec_base_col = palette[8][record_hue & 15];
    for (int pad = 0; pad < MAIN_PADS; ++pad) {
      int x = pad_x(pad), y = pad_y(pad);
      uint32_t col = pad_col[pad];
      int p = touch_pressure_curve_q7(get_touch_pressure_xy(x, y));
      if (p) {
        col = add_col(col, fade_col(CYAN, 32 + p * 2));
      }
      if (recording && pad == record_pad)
        col = add_col(col, fade_col(rec_base_col, 96 + (record_alpha_q15 >> 8)));
      set_led(x, y, col);
    }
    for (int i = 0; i < GRAINS; ++i) {
      if (!grain[i].active) continue;
      if (!pad_capacity) continue;
      int pad = grain[i].pad;
      if (pad >= MAIN_PADS) continue;
      int x = pad_x(pad), y = pad_y(pad);
      int bri = clampi((clampi(grain[i].env_q15, 0, ALPHA_MAX) * (int)grain[i].volume) >> 14, 0, 255);
      set_led(x, y, add_col(get_led(x, y), fade_col(WHITE, bri)));
    }
    draw_vu_meter(0, 15, 13, HORIZONTAL, vu_peak);
    bool clear_down = shift_button(CLEAR_X, CONTROL_Y, WHITE, ISOLATED, "Clear");
    if (clear_down)
      clear_touched_pads();
    int oct_pressure = mini(127, touch_pressure_curve_q7(get_touch_pressure_xy(OCTAVE_X, CONTROL_Y)));
    int octave_amount = maxi((int)octave_toggle_q7, get_touch_down(OCTAVE_X, CONTROL_Y) ? oct_pressure : 0);
    uint32_t oct_col = fade_col(BRIGHTER(TEAL), 32 + octave_amount * 4);
    shift_button(OCTAVE_X, CONTROL_Y, oct_col, ISOLATED, "Octave");
    bool oct_down = is_last_widget_held();
    if (is_last_widget_pressed()) {
      octave_press_peak_q7 = 0;
      octave_press_used = false;
    }
    if (oct_down) {
      octave_press_peak_q7 = (uint8_t)maxi((int)octave_press_peak_q7, oct_pressure);
      if (record_pressure_total)
        octave_press_used = true;
    }
    if (is_last_widget_released() && !octave_press_used)
      octave_toggle_q7 = octave_toggle_q7 ? 0 : octave_press_peak_q7;
    octave_amount = maxi((int)octave_toggle_q7, oct_down ? oct_pressure : 0);
    set_led(OCTAVE_X, CONTROL_Y, octave_amount ? fade_col(BRIGHTER(TEAL), clampi(64 + octave_amount * 2, 0, 255)) : TEAL);
    uint32_t rec_col = recording ? add_col(rec_base_col, fade_col(WHITE, record_alpha_q15 >> 7)) : rec_base_col;
    button(RECORD_X, CONTROL_Y, rec_col, ISOLATED, "Record");
    bool rec_down = is_last_widget_held();
    if (is_last_widget_released() && !recording)
      record_hue = (uint8_t)((record_hue + 3) & 15);
    if (rec_down && record_pressure_total >= 16 && !recording && record_alpha_q15 == 0 && pad_capacity) {
      int best_pad = xy_pad(clampi((record_centroid_x_q8 + 128) >> 8, 0, 15), clampi((record_centroid_y_q8 + 128) >> 8, 0, 14));
      record_alpha_q15 = 0;
      record_fading_out = false;
      recording = true;
      begin_record_pad(best_pad);
      set_help_text("Recording pad #fc2#*%d,%d#.", pad_x(best_pad) + 1, pad_y(best_pad) + 1);
    }

    if (recording && !record_fading_out && record_pressure_total < 16)
      request_record_stop();
  }
};
