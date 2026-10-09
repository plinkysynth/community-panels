// Generated from a Plinky 12 built-in example by scripts/update_community_panel_builds.sh.
/*
@Name: MPE Test
@Author: mmalex
@Date: 2026-07-29
@Description: This is a test of MPE input and output for Plinky 12. It presents as two large play surfaces,
  each with a dedicated brightness (CC74) slider which is mapped to Plinky's sample-start /
  wavetable-position parameter. It accepts normal MIDI on channels 1 and 16 for the two sounds, as
  well as MPE for up to two zones, supporting per-note pitch bend, brightness, and pressure. It
  also sends data in MPE format. The mode pads below the play surfaces open the usual synth edit
  and preset load/save pages, using the same parameter order as Toadstep and Blocks. This is a
  first test of MPE on Plinky 12, and sets the stage for adding MPE support to other panels. Code
  is available in the IDE.
@Tags: midi
*/
#define PANEL_PAD_COLOR YELLOW

struct mpe_test : panel_t {
  static constexpr int TRACKS = 2;
  static constexpr uint8_t VOICE_PRIO = 16;
  static constexpr uint32_t VOICE_SOURCE_BASE = 0x6d000000u;
  static constexpr int SURFACE_W = 7;
  static constexpr int SURFACE_H = 15;
  static constexpr int TRACK_B_X = 8;
  static constexpr int PREVIEW_Y = 9;
  static constexpr int PREVIEW_H = 6;
  static constexpr int PREVIEW_POS_OFFSET = 5;

  enum page_t : uint8_t {
    PAGE_PLAY,
    PAGE_EDIT_A,
    PAGE_LOAD_A,
    PAGE_EDIT_B,
    PAGE_LOAD_B,
  };

  play_surface_t play_surface[TRACKS];
  voice_allocator_t voice_allocator;
  mpe_output_t mpe;
  mpe_input_t mpe_input;
  slider_t brightness_slider[TRACKS];
  preset_pages_t preset_pages;
  panel_page_t panel_page;
  uint8_t page = PAGE_PLAY;
  uint8_t brightness[TRACKS] = {64, 64};
  linear_scan_map_t<64, uint32_t, uint8_t> source_brightness;

  static constexpr uint8_t string_roots[SURFACE_W] = {36, 41, 46, 51, 56, 61, 66};

  int get_num_pages() override { return 2; }
  int get_num_tracks(void) override { return TRACKS; }

  const char *get_track_name(int i, char buf[16]) override {
    if (!buf) return NULL;
    snprintf(buf, 16, "%c", i == 1 ? 'B' : 'A');
    return buf;
  }

  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("mpe_test: on_setup_default_panel_state\n");
    memset(&voice_allocator, 0, sizeof(voice_allocator));
    memset(&mpe, 0, sizeof(mpe));
    mpe.generation_now = 1;
    mpe.setup_dual_zone();
    mpe_input.clear();
    mpe_input.setup_dual_zone();
    brightness[0] = 64;
    brightness[1] = 64;
    for (int track = 0; track < TRACKS; ++track)
      mpe_input.set_zone_brightness(track, brightness[track]);
    source_brightness.clear();
    synth_presets[0].hue = 0;
    synth_presets[1].hue = 63;
    set_lo_hi_same_for_all_corners(VOICE_PARAM_ATTACK, 0, 0, &synth_presets[1]);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_DECAY, 16, 48, &synth_presets[1]);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_SUSTAIN, 0, 0, &synth_presets[1]);
    set_lo_hi_same_for_all_corners(VOICE_PARAM_RELEASE, 16, 16, &synth_presets[1]);
    refresh_synth_preset_modulation_masks(&synth_presets[1]);
  }

  void process_track_surface(int track, int x, int y, int w, int h, bool active, int string_pos_offset = 0) {
    play_surface_t *surface = &play_surface[track];
    if (active)
      surface->update_from_touch(x, y, w, h, VERTICAL | STRINGOPHONIC_MONO, SURFACE_W, 0xffff, string_pos_offset);
    surface->update_voices();
    for (int surface_voice = 0; surface_voice < SURFACE_W; ++surface_voice) {
      finger_t finger = surface->get_finger_for_voice(surface_voice);
      if (!finger.pressure)
        continue;
      uint32_t source_id = VOICE_SOURCE_BASE | ((uint32_t)track << 8) | (uint32_t)surface_voice;
      int string_idx = clampi((int)finger.string_idx, 0, w - 1);
      int note_q8 = ((int)string_roots[string_idx] << 8) + (int)finger.fretless_q8();
      bool finger_is_new = finger.is_new != 0;
      if (finger_is_new)
        source_brightness.set(source_id, brightness[track]);
      uint8_t latched_brightness = source_brightness.get_or(source_id, brightness[track]);
      mpe.declare_note(track, source_id, note_q8, finger.pressure, finger.pressure, latched_brightness, finger_is_new);

      int old_voice = voice_allocator.find_voice(source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      int synth_voice = voice_allocator.voice_allocate(source_id, VOICE_PRIO, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      if (synth_voice != old_voice)
        synth_note_up(old_voice);
      if (synth_voice < 0)
        continue;
      play_synth(synth_voice, track, finger.pressure, note_q8, finger_is_new || synth_voice != old_voice);
    }
  }

  uint8_t played_note_brightness(int track, int note) {
    int brightness = 0;
    for (int surface_voice = 0; surface_voice < SURFACE_W; ++surface_voice) {
      finger_t finger = play_surface[track].get_finger_for_voice(surface_voice);
      if (!finger.pressure)
        continue;
      int string_idx = clampi((int)finger.string_idx, 0, SURFACE_W - 1);
      int finger_note = (((int)string_roots[string_idx] << 8) + (int)finger.fretless_q8() + 128) >> 8;
      if (finger_note == note)
        brightness = maxi(brightness, clampi(finger.pressure, 0, 127));
    }
    for (int slot = 0; slot < mpe_input_t::MAX_SLOTS; ++slot) {
      mpe_input_note_t *midi_note = &mpe_input.notes[slot];
      if (!midi_note->active || midi_note->zone != track)
        continue;
      int midi_note_number = (midi_note->note_q8 + 128) >> 8;
      if (midi_note_number == note)
        brightness = maxi(brightness, midi_note->synth_velocity());
    }
    return (uint8_t)brightness;
  }

  void on_dsp_param_override(int preset_idx, int voice_idx, voice_params *params) override {
    (void)preset_idx;
    if (!params || (uint32_t)voice_idx >= DEFAULT_VOICE_ALLOCATOR_VOICES)
      return;
    voice_alloc_state_t *voice = &voice_allocator._state[voice_idx];
    if (!voice->source_id)
      return;
    uint8_t value = 0;
    if (!source_brightness.get(voice->source_id, &value))
      return;
    params->sample_start_q14 = clampi((int)value << 7, 0, 127 << 7);
  }

  void on_midi(uint32_t midimsg) override {
    mpe_input.process_midi(midimsg);
    if (IS_CC(midimsg)) {
      uint8_t channel = CHANNEL_BYTE(midimsg) & 15;
      if (channel == 0)
        process_midi_message_for_synth_cc(midimsg, 0);
      else if (channel == 15)
        process_midi_message_for_synth_cc(midimsg, 1);
    }
  }

  void on_sequence(int delta_time_us) override {
    (void)delta_time_us;
    bool top_page = get_scroll_page() == 0;
    if (top_page && page == PAGE_PLAY) {
      process_track_surface(0, 0, 0, SURFACE_W, SURFACE_H, true);
      process_track_surface(1, TRACK_B_X, 0, SURFACE_W, SURFACE_H, true);
    } else if (top_page && (page == PAGE_EDIT_A || page == PAGE_LOAD_A || page == PAGE_EDIT_B || page == PAGE_LOAD_B)) {
      int track = page >= PAGE_EDIT_B;
      process_track_surface(0, 0, PREVIEW_Y, SURFACE_W, PREVIEW_H, track == 0, PREVIEW_POS_OFFSET);
      process_track_surface(1, 0, PREVIEW_Y, SURFACE_W, PREVIEW_H, track == 1, PREVIEW_POS_OFFSET);
    } else {
      process_track_surface(0, 0, 0, SURFACE_W, SURFACE_H, false);
      process_track_surface(1, TRACK_B_X, 0, SURFACE_W, SURFACE_H, false);
    }
    for (int slot = 0; slot < mpe_input_t::MAX_SLOTS; ++slot) {
      mpe_input_note_t *note = &mpe_input.notes[slot];
      if (!note->active)
        continue;
      int track = note->zone;
      if ((uint32_t)track >= TRACKS)
        continue;
      source_brightness.set(note->source_id, note->brightness);

      int old_voice = voice_allocator.find_voice(note->source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      int synth_voice = voice_allocator.voice_allocate(note->source_id, VOICE_PRIO, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
      if (synth_voice != old_voice)
        synth_note_up(old_voice);
      if (synth_voice < 0)
        continue;
      play_synth(synth_voice, track, note->synth_velocity(), note->note_q8, note->retrigger || synth_voice != old_voice);
      note->retrigger = false;
    }
    uint16_t stale = voice_allocator.garbage_collect_voices(1, 0, 0, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
    while (stale) {
      uint16_t bit = stale & (uint16_t)-stale;
      int voice = __builtin_ctz((unsigned)bit);
      synth_note_up(voice);
      stale &= (uint16_t)~bit;
    }
    if (mpe.update(MIDI_PORT_1)) {
      send_synth_cc_out(MIDI_PORT_1, 0, 0);
      send_synth_cc_out(MIDI_PORT_1, 1, 15);
    }
  }

  void draw_surface(int track, int x, int y, int w, int h, int string_pos_offset = 0) {
    uint32_t col = track ? TEAL : PINK;
    for (int relx = 0; relx < w; ++relx) {
      int root = string_roots[clampi(relx, 0, SURFACE_W - 1)];
      for (int rely = 0; rely < h; ++rely) {
        int pad_y = y + rely;
        int note = root + string_pos_offset + (h - 1 - rely);
        uint32_t pad_col = (note % 12) == 0 ? DIMMER(col) : DIMMESTEST(col);
        int note_brightness = played_note_brightness(track, note);
        if (note_brightness > 0)
          pad_col = add_col(pad_col, fade_col(BRIGHTEST(col), clampi(note_brightness * 2, 32, 255)));
        set_led(x + relx, pad_y, pad_col);
      }
    }
  }

  void draw_mode_button(int x, uint8_t target_page, const char *help_text) {
    uint32_t col = target_page >= PAGE_EDIT_B ? TEAL : PINK;
    if (button(x, 15, page == target_page ? BRIGHTER(col) : DIMMEST(col), ISOLATED, help_text))
      page = page == target_page ? (uint8_t)PAGE_PLAY : target_page;
  }

  void draw_brightness_slider(int track, int x, int y, int h) {
    bool changed = brightness_slider[track].simple_slider(x, y, h, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, WHITE, 0, 127,
                                                          brightness[track], "Brightness");
    if (changed)
      brightness[track] = (uint8_t)clampi(last_widget_new_value(), 0, 127);
    if (changed || is_last_widget_held())
      mpe_input.set_zone_brightness(track, brightness[track]);
  }

  void draw_synth_page_controls(int track) {
    draw_surface(track, 0, PREVIEW_Y, SURFACE_W, PREVIEW_H, PREVIEW_POS_OFFSET);
    draw_brightness_slider(track, SURFACE_W, PREVIEW_Y, PREVIEW_H);
    preset_pages.xy_pad(track, 8, 10);
  }

  void on_ui(int delta_time_us) override {
    (void)delta_time_us;
    int scroll_page = get_scroll_page();
    if (scroll_page < 0)
      return;

    leds_clear();
    if (scroll_page == 1) {
      panel_page.saveload(16, true, FLAG_PICKER_ENABLE_DELETE);
      return;
    }
    if (scroll_page > 1) {
      scroll_to_page(1);
      return;
    }

    if (page == PAGE_EDIT_A || page == PAGE_EDIT_B) {
      int track = page >= PAGE_EDIT_B;
      preset_pages.edit(track, 0, 0, false);
      draw_synth_page_controls(track);
    } else if (page == PAGE_LOAD_A || page == PAGE_LOAD_B) {
      int track = page >= PAGE_EDIT_B;
      int action = preset_pages.saveload_action(track, 0, FLAG_PICKER_ENABLE_DELETE);
      if (action)
        page = PAGE_PLAY;
      draw_synth_page_controls(track);
    } else {
      draw_surface(0, 0, 0, SURFACE_W, SURFACE_H);
      draw_brightness_slider(0, SURFACE_W, 0, SURFACE_H);
      draw_surface(1, TRACK_B_X, 0, SURFACE_W, SURFACE_H);
      draw_brightness_slider(1, 15, 0, SURFACE_H);
    }
    draw_mode_button(0, PAGE_EDIT_A, "edit A preset");
    draw_mode_button(1, PAGE_LOAD_A, "load A preset");
    draw_mode_button(8, PAGE_EDIT_B, "edit B preset");
    draw_mode_button(9, PAGE_LOAD_B, "load B preset");
  }

  bool on_serialise(serialiser_t &s, int version) override;
};

static bool serialise(serialiser_t &s, mpe_test &o) {
  OBJECT_BEGIN(s);
  FIELD_SYNTH_PRESET("presetA", 0);
  FIELD_SYNTH_PRESET("presetB", 1);
  FIELD_MIX_PRESET("presetMix");
  FIELD("brightnessA", o.brightness[0], 0u, 127u);
  FIELD("brightnessB", o.brightness[1], 0u, 127u);
  OBJECT_END(s);
  return true;
}

bool mpe_test::on_serialise(serialiser_t &s, int version) {
  (void)version;
  bool ok = serialise(s, *this);
  if (ok && s.reading)
    for (int track = 0; track < TRACKS; ++track)
      mpe_input.set_zone_brightness(track, brightness[track]);
  return ok;
}
