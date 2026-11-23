// Generated from a Plinky 12 built-in example by scripts/update_community_panel_builds.sh.
/*
@Name: MPE Surface
@Author: makingsoundmachines
@Date: 2026-11-19
@Description: A configurable MPE play surface for Plinky 12, based on mmalex's MPE Test. Two play surfaces (A
  and B), each with a dedicated brightness (CC74) slider mapped to Plinky's sample-start /
  wavetable-position parameter. It accepts normal MIDI on channels 1 and 16 for the two sounds, as
  well as MPE for up to two zones, supporting per-note pitch bend, brightness, and pressure. It
  also sends data in MPE format. The mode pads below the play surfaces open the usual synth edit
  and preset load/save pages, using the same parameter order as Toadstep and Blocks. Click the
  right-up button for the settings pages: root note, scale, string stride (semitones between
  strings; snapped to the scale on a scale surface, exact on a chromatic one), surface (scale:
  every pad is a scale note; chromatic: every pad is a semitone and only scale notes are lit),
  string direction, how the surface is split between A and B, start (root: the lowest string
  starts on the root; fix: it starts on the scale note nearest C1, or on C1 itself on a chromatic
  surface), and glide. Settings are saved with the song.
@Tags: midi

Page	Label	  Values
  1     root:   the musical Root note of the play surface. Values are semitones, C … B
  2     scl:    the musical Scale. For example, selecting A as root and min as scale will give you a surface in a minor
  3     strd:   string tuning or Stride. the interval from string to the next, 0 - 12 semitones.
                For a Scale surface (surf = scl), the beginning of each string is snapped to the next note in scale
                For a Chromatic surface (surf = chr), the beginning of each string is eactly the stride
  4     surf:   The way the notes are laid out on the play surface:
                scale: every pad is a scale note;
                chromatic: every pad is a semitone and only scale notes are lit
                octaves of the musical root note are highlighted for orientation
  5     dir:    Direction of the strings: vertical / horizontal.
                Horizontal puts the scale on the horizontal, ascending from left to right, and the stride on the vertical axis.
                Vertical puts the scale on the vertical, ascending from bottom to top, and the stride on the horizontal axis.
  6     splt:   Split
                vertical: two surfaces side by side
                horizontal: two surfaces top and bottom
                none: one single surface, no split
  7     strt:   Start note of the surface.
                root: the lowest string starts on the root
                fix: the lowest string starts on the scale note nearest C1, or on C1 itself on a chromatic surface
  8     glid:   Glide: yes or no
  9     follow: Harmonic following: the panel always publishes its root and scale to the other panels and
                the second screen; this picks what it takes in from them (and from MIDI chords).
                off: ignore incoming harmony
                key: follow the song key and scale
                chrd: follow the chord root and chord notes
                cscl: follow the chord root and chord scale

Modulation sources:
                On a synth edit page (A or B), the side buttons pick a modulation source:
                left-up A, right-up B, left-down X, right-down Y, both top or both bottom: pressure.
                The sliders and XY pad then set that source's modulation depth, and an LFO row
                (slower, value slider, faster, play/pause) replaces the brightness slider above the preview.
                Press the same button again to return to normal editing.
*/
#define PANEL_PAD_COLOR YELLOW

struct mpe_test : panel_t {
  static constexpr int TRACKS = 2;
  static constexpr uint8_t VOICE_PRIO = 16;
  static constexpr uint32_t VOICE_SOURCE_BASE = 0x6d000000u;
  // One touch voice per MPE member channel: setup_dual_zone() gives each zone 7 member channels.
  static constexpr int MAX_CHANNELS = 7;
  static constexpr int MAX_STRINGS = 16;
  static constexpr int BASE_NOTE = 24; // C1; the root note setting is added to this.
  static constexpr int PREVIEW_Y = 10;
  static constexpr int PREVIEW_W = 7;
  static constexpr int PREVIEW_H = 5;
  static constexpr int PREVIEW_POS_OFFSET = 5;
  // In modulation-source mode the LFO row takes the preview's top row and the brightness slider's column.
  static constexpr int MOD_LFO_X = 0;
  static constexpr int MOD_LFO_Y = PREVIEW_Y;
  static constexpr int MOD_LFO_W = PREVIEW_W + 1;
  static constexpr int SETTINGS_PAGES = 9;
  static constexpr int NUM_SCALES = 16;

  enum page_t : uint8_t {
    PAGE_PLAY,
    PAGE_EDIT_A,
    PAGE_LOAD_A,
    PAGE_EDIT_B,
    PAGE_LOAD_B,
  };

  enum direction_t : uint8_t {
    DIRECTION_VERTICAL,   // strings run bottom to top, rising in pitch from left to right
    DIRECTION_HORIZONTAL, // strings run left to right, rising in pitch from bottom to top
  };

  enum split_t : uint8_t {
    SPLIT_VERTICAL,   // A on the left, B on the right
    SPLIT_HORIZONTAL, // A on top (rows 0-7), B below (rows 8-14)
    SPLIT_NONE,       // one full-width surface for A; B is played over MIDI only
  };

  enum surface_t : uint8_t {
    SURFACE_SCALE,     // every pad is a scale note
    SURFACE_CHROMATIC, // every pad is a semitone; only scale notes are lit
  };

  enum start_t : uint8_t {
    START_ROOT, // the lowest string starts on the root note
    START_FIX,  // the lowest string starts near BASE_NOTE, whatever the root
  };

  enum {
    HARMONIC_FOLLOW_IGNORE = harmonic_follower_t::OFF,
    HARMONIC_FOLLOW_KEY = harmonic_follower_t::KEY,
    HARMONIC_FOLLOW_CHORD_NOTES = harmonic_follower_t::CHORD_NOTES,
    HARMONIC_FOLLOW_CHORD_SCALE = harmonic_follower_t::CHORD_SCALE,
    HARMONIC_FOLLOW_LAST
  };

  struct rect_t {
    int x, y, w, h;
  };

  play_surface_t play_surface[TRACKS];
  voice_allocator_t voice_allocator{};
  mpe_output_t mpe;
  mpe_input_t mpe_input;
  slider_t brightness_slider[TRACKS];
  preset_pages_t preset_pages;
  mod_source_ui_state_t mod_source_ui;
  panel_page_t panel_page;
  uint8_t page = PAGE_PLAY;
  linear_scan_map_t<64, uint32_t, uint8_t> source_brightness;

  // ---- saved state ----
  // Everything the user sets on this panel lives here and is saved with the song from the panel picker.
  // Member initialisers are the defaults; a freshly constructed (staged) instance starts from them.
  typedef struct scene_t {
    uint8_t Direction = DIRECTION_VERTICAL;
    uint8_t Glide = 1;
    uint8_t Split = SPLIT_VERTICAL;
    uint8_t Root = 0;   // pitch class 0..11, C..B, added to BASE_NOTE
    uint8_t Stride = 5; // semitones between neighbouring string roots, 0..12, snapped to the scale
    uint16_t Scale = SCALE_CHROMATIC; // root-relative 12-bit pitch-class mask
    uint8_t Surface = SURFACE_SCALE;  // how notes are laid out along each string
    uint8_t Start = START_ROOT;       // where the lowest string starts
    uint8_t Brightness[TRACKS] = {64, 64};
    uint8_t harmonic_follow = HARMONIC_FOLLOW_CHORD_NOTES; // how incoming harmony sets Root and Scale
  } scene_t;

  friend bool serialise(serialiser_t &s, scene_t &o) {
    OBJECT_BEGIN(s);
    FIELD("dir", o.Direction, 0u, (unsigned)DIRECTION_HORIZONTAL);
    FIELD("glide", o.Glide, 0u, 1u);
    FIELD("split", o.Split, 0u, (unsigned)SPLIT_NONE);
    FIELD("root", o.Root, 0u, 11u);
    FIELD("stride", o.Stride, 0u, 12u);
    FIELD("scale", o.Scale, 0u, 0x0FFFu);
    FIELD("surface", o.Surface, 0u, (unsigned)SURFACE_CHROMATIC);
    FIELD("start", o.Start, 0u, (unsigned)START_FIX);
    FIELD("brightA", o.Brightness[0], 0u, 127u);
    FIELD("brightB", o.Brightness[1], 0u, 127u);
    FIELD("follow", o.harmonic_follow, 0u, (unsigned)HARMONIC_FOLLOW_CHORD_SCALE);
    OBJECT_END(s);
    return true;
  }

  scene_t Scene;

  // ---- derived state ----
  // Rebuilt from Scene by rebuild_derived_state(); never saved.
  // Bottom note of each logical string above the key note: in semitones, and as a scale degree.
  int8_t string_root_semitones[MAX_STRINGS] = {};
  int8_t string_root_degree[MAX_STRINGS] = {};

  // Shared harmony bookkeeping is transient, not saved with the scene.
  uint32_t notehash_last = 0;
  harmonic_follower_t harmony;

  int get_num_pages() override { return 2; }
  int get_num_panel_settings_pages() override { return SETTINGS_PAGES; } // root, scale, stride, surface, direction, split, start, glide, follow
  int get_num_tracks(void) override { return TRACKS; }

  const char *get_track_name(int i, char buf[16]) override {
    if (!buf) return NULL;
    snprintf(buf, 16, "%c", i == 1 ? 'B' : 'A');
    return buf;
  }

  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    Scene = scene_t{};
    rebuild_derived_state();
    // Both tracks start from the system default preset; only the hue differs, to tell A and B apart.
    synth_presets[0].hue = 0;
    synth_presets[1].hue = 63;
    mod_source_ui.reset();
    PublishMusicalState();
  }

  void on_load_finished() override {
    mod_source_ui.reset();
    rebuild_derived_state();
    PublishMusicalState();
  }

  // Recompute everything that depends on Scene. Call after Scene changes: default song, load, settings edits.
  void rebuild_derived_state() {
    if (!(Scene.Scale & 1u)) // a scale must contain its root; fall back to chromatic for empty/odd masks
      Scene.Scale = SCALE_CHROMATIC;
    int key = key_note();
    int start = lowest_string_offset();
    for (int i = 0; i < MAX_STRINGS; ++i) {
      // Each string starts Stride semitones above the previous one. On a scale surface that is snapped
      // to the root-relative scale; on a chromatic surface it is exact, so the scale only changes which
      // pads are lit.
      int offset = start + i * Scene.Stride;
      if (Scene.Surface == SURFACE_SCALE)
        offset = quantize_note_to_scale(offset, Scene.Scale);
      offset = clampi(key + offset, 0, 127) - key;
      string_root_semitones[i] = (int8_t)offset;
      string_root_degree[i] = (int8_t)note_to_degree(offset, Scene.Scale);
    }
    for (int track = 0; track < TRACKS; ++track)
      mpe_input.set_zone_brightness(track, Scene.Brightness[track]);
  }

  // ---- layout ----

  bool track_has_surface(int track) const { return Scene.Split != SPLIT_NONE || track == 0; }

  rect_t surface_rect(int track) const {
    if (Scene.Split == SPLIT_HORIZONTAL)
      return track ? rect_t{0, 8, 15, 7} : rect_t{0, 0, 15, 8};
    if (Scene.Split == SPLIT_NONE)
      return rect_t{0, 0, 15, 15};
    return track ? rect_t{8, 0, 7, 15} : rect_t{0, 0, 7, 15};
  }

  rect_t slider_rect(int track) const {
    rect_t s = surface_rect(track);
    return rect_t{s.x + s.w, s.y, 1, s.h};
  }

  bool is_horizontal() const { return Scene.Direction == DIRECTION_HORIZONTAL; }

  int surface_flags() const {
    return (is_horizontal() ? HORIZONTAL : VERTICAL) | (Scene.Glide ? STRINGOPHONIC_POLY : POLYPHONIC);
  }

  int num_strings(int w, int h) const { return clampi(is_horizontal() ? h : w, 1, MAX_STRINGS); }

  // play_surface_t numbers horizontal strings from the top row down; we want pitch to rise upwards.
  int logical_string(int string_idx, int strings) const {
    string_idx = clampi(string_idx, 0, strings - 1);
    return is_horizontal() ? strings - 1 - string_idx : string_idx;
  }

  // ---- tuning ----

  int key_note() const { return BASE_NOTE + Scene.Root; }

  // Where the lowest string starts, in semitones from the key note (0 or below).
  int lowest_string_offset() const {
    if (Scene.Start != START_FIX)
      return 0;
    int offset = BASE_NOTE - key_note();
    // A chromatic surface can start on BASE_NOTE itself; a scale surface starts on the nearest scale note.
    return Scene.Surface == SURFACE_CHROMATIC ? offset : quantize_note_to_scale(offset, Scene.Scale);
  }

  // pos_q8 is the position along the string from the string's root (see rebuild_derived_state()):
  // scale degrees on a scale surface, semitones on a chromatic surface.
  int note_q8_on_string(int logical_idx, int pos_q8) const {
    logical_idx = clampi(logical_idx, 0, MAX_STRINGS - 1);
    int offset_q8;
    if (Scene.Surface == SURFACE_CHROMATIC)
      offset_q8 = string_root_semitones[logical_idx] * 256 + pos_q8;
    else
      offset_q8 = degree_q8_to_note_q8(string_root_degree[logical_idx] * 256 + pos_q8, Scene.Scale);
    return clampi(key_note() * 256 + offset_q8, 0, 127 * 256);
  }

  bool note_in_scale(int note) const {
    int pitch_class = ((note - key_note()) % 12 + 12) % 12;
    return (Scene.Scale >> pitch_class) & 1u;
  }

  // ---- harmonic following and publishing ----
  void read_harmonic_state_changes() {
    if (harmony.follow(Scene.harmonic_follow, Scene.Root, Scene.Scale))
      rebuild_derived_state();
  }

  void PublishMusicalState() { harmony.publish_scale(Scene.Root, Scene.Scale); }

  // ---- sequencing ----

  void process_track_surface(int track, rect_t r, bool active, int string_pos_offset = 0) {
    play_surface_t *surface = &play_surface[track];
    if (active)
      surface->update_from_touch(r.x, r.y, r.w, r.h, surface_flags(), MAX_CHANNELS, 0xffff, string_pos_offset);
    surface->update_voices();
    int strings = num_strings(r.w, r.h);
    for (int surface_voice = 0; surface_voice < MAX_CHANNELS; ++surface_voice) {
      finger_t finger = surface->get_finger_for_voice(surface_voice);
      if (!finger.pressure)
        continue;
      uint32_t source_id = VOICE_SOURCE_BASE | ((uint32_t)track << 8) | (uint32_t)surface_voice;
      int note_q8 = note_q8_on_string(logical_string(finger.string_idx, strings), finger.fretless_q8());
      bool finger_is_new = finger.is_new != 0;
      if (finger_is_new)
        source_brightness.set(source_id, Scene.Brightness[track]);
      uint8_t latched_brightness = source_brightness.get_or(source_id, Scene.Brightness[track]);
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
    read_harmonic_state_changes();
    bool top_page = get_scroll_page() == 0;
    rect_t preview = preview_rect();
    if (top_page && page == PAGE_PLAY) {
      for (int track = 0; track < TRACKS; ++track)
        process_track_surface(track, surface_rect(track), track_has_surface(track));
    } else if (top_page && page != PAGE_PLAY) {
      int edit_track = page >= PAGE_EDIT_B;
      for (int track = 0; track < TRACKS; ++track)
        process_track_surface(track, preview, track == edit_track, PREVIEW_POS_OFFSET);
    } else {
      for (int track = 0; track < TRACKS; ++track)
        process_track_surface(track, surface_rect(track), false);
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

  // ---- drawing ----

  static constexpr int MAX_LIT_NOTES = MAX_CHANNELS + mpe_input_t::MAX_SLOTS;

  struct lit_note_t {
    uint8_t note;
    uint8_t level;
  };

  // Gather the notes currently sounding on a track, from its surface fingers and from MIDI input.
  int collect_lit_notes(int track, int strings, lit_note_t *out) {
    int count = 0;
    for (int surface_voice = 0; surface_voice < MAX_CHANNELS; ++surface_voice) {
      finger_t finger = play_surface[track].get_finger_for_voice(surface_voice);
      if (!finger.pressure)
        continue;
      int note_q8 = note_q8_on_string(logical_string(finger.string_idx, strings), finger.fretless_q8());
      out[count++] = lit_note_t{(uint8_t)((note_q8 + 128) >> 8), (uint8_t)clampi(finger.pressure, 0, 127)};
    }
    for (int slot = 0; slot < mpe_input_t::MAX_SLOTS && count < MAX_LIT_NOTES; ++slot) {
      mpe_input_note_t *midi_note = &mpe_input.notes[slot];
      if (!midi_note->active || midi_note->zone != track)
        continue;
      out[count++] = lit_note_t{(uint8_t)clampi((midi_note->note_q8 + 128) >> 8, 0, 127), midi_note->synth_velocity()};
    }
    return count;
  }

  void draw_surface(int track, rect_t r, int string_pos_offset = 0) {
    uint32_t col = track ? TEAL : PINK;
    int strings = num_strings(r.w, r.h);
    lit_note_t lit[MAX_LIT_NOTES];
    int lit_count = collect_lit_notes(track, strings, lit);
    bool horizontal = is_horizontal();
    for (int relx = 0; relx < r.w; ++relx) {
      for (int rely = 0; rely < r.h; ++rely) {
        int string_idx = horizontal ? rely : relx;
        int pos = (horizontal ? relx : r.h - 1 - rely) + string_pos_offset;
        int note = (note_q8_on_string(logical_string(string_idx, strings), pos * 256) + 128) >> 8;
        uint32_t pad_col = (note % 12) == Scene.Root ? DIMMER(col) : DIMMESTEST(col);
        if (Scene.Surface == SURFACE_CHROMATIC && !note_in_scale(note))
          pad_col = BLACK; // off-scale pads stay dark, but still light up while played
        int level = 0;
        for (int i = 0; i < lit_count; ++i)
          if (lit[i].note == note)
            level = maxi(level, lit[i].level);
        if (level > 0)
          pad_col = add_col(pad_col, fade_col(BRIGHTEST(col), clampi(level * 2, 32, 255)));
        set_led(r.x + relx, r.y + rely, pad_col);
      }
    }
  }

  void draw_mode_button(int x, uint8_t target_page, const char *help_text) {
    uint32_t col = target_page >= PAGE_EDIT_B ? TEAL : PINK;
    if (button(x, 15, page == target_page ? BRIGHTER(col) : DIMMEST(col), ISOLATED, help_text)) {
      page = page == target_page ? (uint8_t)PAGE_PLAY : target_page;
      mod_source_ui.reset(); // every synth edit page opens on plain parameter editing
    }
  }

  // ---- modulation sources ----
  // On the synth edit pages the side buttons pick a modulation source (A, B, X, Y, pressure); the sliders and
  // XY pad then edit that source's modulation depth instead of the parameter values.

  bool is_synth_edit_page() const { return page == PAGE_EDIT_A || page == PAGE_EDIT_B; }

  int mod_flags() const { return is_synth_edit_page() ? mod_source_ui.flags() : 0; }

  void on_click(uint8_t button_mask) override {
    mod_source_ui.on_click(button_mask, is_synth_edit_page());
  }

  // The playable preview on the synth pages; one row shorter while the LFO row is shown above it.
  rect_t preview_rect() const {
    if (mod_flags())
      return rect_t{0, PREVIEW_Y + 1, PREVIEW_W, PREVIEW_H - 1};
    return rect_t{0, PREVIEW_Y, PREVIEW_W, PREVIEW_H};
  }

  void draw_brightness_slider(int track, rect_t r) {
    bool changed = brightness_slider[track].simple_slider(r.x, r.y, r.h, VERTICAL | SHOW_STEM | SHOW_BACKGROUND, WHITE, 0,
                                                          127, Scene.Brightness[track], "Brightness");
    if (changed)
      Scene.Brightness[track] = (uint8_t)clampi(last_widget_new_value(), 0, 127);
    if (changed || is_last_widget_held())
      mpe_input.set_zone_brightness(track, Scene.Brightness[track]);
  }

  void draw_synth_page_controls(int track) {
    rect_t preview = preview_rect();
    draw_surface(track, preview, PREVIEW_POS_OFFSET);
    int flags = mod_flags();
    if (flags)
      mod_source_ui.lfo_slider(MOD_LFO_X, MOD_LFO_Y, MOD_LFO_W);
    else
      draw_brightness_slider(track, rect_t{PREVIEW_W, PREVIEW_Y, 1, PREVIEW_H});
    // Same as preset_pages.xy_pad(track, 8, 10), plus the modulation-source flags.
    synth_xy_block(&preset_pages.the_xy_pad, track, 8, 10, 7, 5, WHITE, BLUE, false, 0, XY_BUTTONS_ON_RIGHT, false, false,
                   flags);
  }

  // ---- settings pages ----

  static int percent(int value, int max_value) { return max_value > 0 ? value * 100 / max_value : -1; }

  // Applies a left-button delta to one Scene field. Returns true if the value changed.
  bool change_setting(uint8_t &field, int delta, int max_value) {
    int next = clampi((int)field + delta, 0, max_value);
    if (!delta || next == field)
      return false;
    on_sequence_lock_guard_t guard;
    field = (uint8_t)next;
    rebuild_derived_state();
    return true;
  }

  void draw_settings_page(int scroll_page) {
    static const char *const ROOT_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const SCALE_NAMES[NUM_SCALES] = {
        "chr", "maj", "min", "dor", "phry", "lyd", "mixo", "locr",
        "harm", "melo", "penta maj", "penta min", "blues", "whole", "hira", "insen"};
    static const uint16_t SCALE_MASKS[NUM_SCALES] = {
        SCALE_CHROMATIC,      SCALE_MAJOR,         SCALE_MINOR,         SCALE_DORIAN,           SCALE_PHRYGIAN,
        SCALE_LYDIAN,         SCALE_MIXOLYDIAN,    SCALE_LOCRIAN,       SCALE_HARMONIC_MINOR,   SCALE_MELODIC_MINOR,
        SCALE_PENTATONIC_MAJOR, SCALE_PENTATONIC_MINOR, SCALE_BLUESMINOR, SCALE_WHOLETONE,        SCALE_HIRAJOSHI, SCALE_INSEN};
    static const char *const START_NAMES[2] = {"root", "fix"};
    static const char *const SURFACE_NAMES[2] = {"scl", "chr"};
    static const char *const DIRECTION_NAMES[2] = {"vert", "horz"};
    static const char *const SPLIT_NAMES[3] = {"vert", "horz", "none"};
    static const char *const FOLLOW_NAMES[HARMONIC_FOLLOW_LAST] = {"off", "key", "chrd", "cscl"};

    switch (scroll_page) {
    case -1:
      if (change_setting(Scene.Root, draw_system_style_enum_settings_page("root", Scene.Root, ROOT_NAMES, 12, RED), 11)) {
        on_sequence_lock_guard_t guard;
        PublishMusicalState();
      }
      break;
    case -2: {
      // The scale is saved as its mask; find it in the menu list (an unlisted mask shows as chromatic).
      uint8_t idx = 0;
      for (int i = 0; i < NUM_SCALES; ++i)
        if (SCALE_MASKS[i] == Scene.Scale)
          idx = (uint8_t)i;
      int delta = draw_system_style_enum_settings_page("scl", idx, SCALE_NAMES, NUM_SCALES, REDISH_PINK);
      int next = clampi(idx + delta, 0, NUM_SCALES - 1);
      if (next != idx) {
        on_sequence_lock_guard_t guard;
        Scene.Scale = SCALE_MASKS[next];
        rebuild_derived_state();
        PublishMusicalState();
      }
    } break;
    case -3: {
      char buf[4];
      snprintf(buf, sizeof(buf), "%d", (int)Scene.Stride);
      change_setting(Scene.Stride, draw_system_style_settings_page("strd", buf, percent(Scene.Stride, 12), 0, 0, ORANGE), 12);
    } break;
    case -4:
      change_setting(Scene.Surface, draw_system_style_enum_settings_page("surf", Scene.Surface, SURFACE_NAMES, 2, YELLOW),
                     SURFACE_CHROMATIC);
      break;
    case -5:
      change_setting(Scene.Direction, draw_system_style_enum_settings_page("dir", Scene.Direction, DIRECTION_NAMES, 2, ORANGE),
                     DIRECTION_HORIZONTAL);
      break;
    case -6:
      change_setting(Scene.Split, draw_system_style_enum_settings_page("splt", Scene.Split, SPLIT_NAMES, 3, REDISH_PINK), SPLIT_NONE);
      break;
    case -7:
      change_setting(Scene.Start, draw_system_style_enum_settings_page("strt", Scene.Start, START_NAMES, 2, RED), START_FIX);
      break;
    case -8:
      change_setting(Scene.Glide, draw_system_style_bool_settings_page("glid", Scene.Glide != 0), 1);
      break;
    case -9:
      // Options how to interpret incoming harmonic state
      if (change_setting(Scene.harmonic_follow, draw_system_style_enum_settings_page("follow", Scene.harmonic_follow, FOLLOW_NAMES, HARMONIC_FOLLOW_LAST, PURPLE), HARMONIC_FOLLOW_CHORD_SCALE)) {
        on_sequence_lock_guard_t guard; // change_setting() has released its own lock by now
        read_harmonic_state_changes();
        if (Scene.harmonic_follow == HARMONIC_FOLLOW_IGNORE) PublishMusicalState();
      }
      break;
    default:
      break;
    }
  }

  void on_ui(int delta_time_us) override {
    (void)delta_time_us;
    int scroll_page = get_scroll_page();
    if (scroll_page < 0) {
      draw_settings_page(scroll_page);
      return;
    }

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
      preset_pages.edit(track, 0, 0, false, mod_flags());
      draw_synth_page_controls(track);
    } else if (page == PAGE_LOAD_A || page == PAGE_LOAD_B) {
      int track = page >= PAGE_EDIT_B;
      int action = preset_pages.saveload_action(track, 0, FLAG_PICKER_ENABLE_DELETE);
      if (action)
        page = PAGE_PLAY;
      draw_synth_page_controls(track);
    } else {
      for (int track = 0; track < TRACKS; ++track) {
        if (!track_has_surface(track))
          continue;
        draw_surface(track, surface_rect(track));
        draw_brightness_slider(track, slider_rect(track));
      }
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
  FIELD("scene", o.Scene);
  OBJECT_END(s);
  return true;
}

// Derived state is rebuilt in on_load_finished(), once the loaded Scene is live.
bool mpe_test::on_serialise(serialiser_t &s, int version) {
  (void)version;
  return serialise(s, *this);
}
