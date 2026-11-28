/*
@Name: RTTonnetz
@Author: rthib
@Date: 2026-10-14
@Level: Simple
@Tags: touch, harmony, midi
@Description: A Tonnetz keyboard.

A small implementation of the Tonnetz (https://en.wikipedia.org/wiki/Tonnetz) for the Plinky12.

The triangle vertices are displayed in purple and correspond to the notes of the Tonnetz, namely:

  . . . . . . .
  F . C . G . D
  * * * * * * *
  . A . E . B .
  * * * * * * *
  F#. C#. G#. D#
  * * * * * * *
  . A#. E . B .
  * * * * * * *

The green vertices indicate the note C.
The Plinky plays the notes and sends them via MIDI.

Pages: Play / Touch Pressure Debug / Sound (preset editor + XY pad) / Save-Load (panel slots incl. sound and mix).
Settings pages (above Play): standar settings

*/
#define PANEL_PAD_COLOR GREEN

static void paint_draw_touch_pressure_debug(void) {
  for (int y=48;y<64;++y) {
    for (int x=0;x<16;++x) {
      int pressure = get_touch_pressure_xy(x, y);
      int r = clampi(pressure, 0, 255);
      int g = clampi(pressure - 32, 0, 255);
      int b = clampi(pressure - 64, 0, 255);
      set_led(x, y, LED_RGB(r, g, b));
    }
  }
}

struct rttonnetz : panel_t {
  int activePad[2048]; // pad "on"
  int forcedPad[2048]; // pad "on" forced by the chord pad
  int activeNote[12]; // note "on"

  int baseNote=36;
  voice_allocator_t voice_allocator;
  static constexpr int SYNTH_PRESET_IDX = 0;
  static constexpr uint8_t VOICE_PRIO = 16;
  preset_pages_t preset_pages;
  panel_page_t panel_page;
  
  int get_num_pages() override { return 4; } // Play / Sound / Save-Load
  
  void on_setup_default_panel_state() override {
    panel_t::on_setup_default_panel_state();
    printf("paint: on_setup_default_panel_state\n");
    memset(&voice_allocator, 0, sizeof(voice_allocator));
  }

  void finger_onoff(int x, int y, int pressure, int forced) {
    uint32_t theColor=palette[8][0];
    uint32_t theColorDo=palette[8][7];
    uint32_t theColorOn=palette[8][1];
    int ligne=y/2;
    int lignePaire=ligne/2;
    int colonne=x/2;
    int hauteur=ligne*4-lignePaire*7 + colonne*7;
    int note = hauteur % 12;
    int pad = y*16+x;
    if (pressure>30) {// finger on     if(forced || !forcedPad[pad] ) {
        if (activePad[pad]==0) {// finger was off -> note on
          activePad[pad]=1;
          if (forced) forcedPad[pad]=1;
          activeNote[note]++;
          printf("note on : %d\n", hauteur);
          midi_write(MIDI_PORT_USB1, MAKE_NOTEONMSG(0, hauteur+baseNote, 127));
          uint32_t source_id = pad;
          int old_voice = voice_allocator.find_voice(source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
          int synth_voice = voice_allocator.voice_allocate(source_id, VOICE_PRIO, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
          if (synth_voice != old_voice) synth_note_up(old_voice);
          if (synth_voice >= 0)
            play_synth(synth_voice, SYNTH_PRESET_IDX, 127, (hauteur+baseNote) << 8, true);
        }
    } else {//finger off
        if (activePad[pad]==1 && ((forced && forcedPad[pad]) || (!forced &&  !forcedPad[pad]))) {// finger was on -> note off
          activePad[pad]=0;
          if (forced) forcedPad[pad]=0;
          activeNote[note]--;
          printf("note off : %d\n", hauteur);
          midi_write(MIDI_PORT_USB1, MAKE_NOTEOFFMSG(0, hauteur+baseNote, 127));
          uint32_t source_id = pad;
          int old_voice = voice_allocator.find_voice(source_id, 0, DEFAULT_VOICE_ALLOCATOR_VOICES);
          synth_note_up(old_voice);
        }
    }
    uint32_t noteColor=theColor;
    if(note==0) noteColor=theColorDo;
    if(activeNote[note]>0) noteColor=theColorOn;
    set_led(x, y, noteColor);
  }      

  void on_ui(int delta_time_us) override {
    int page = get_scroll_page();
 		if (page == 1) {
      leds_clear();
      preset_pages.edit(SYNTH_PRESET_IDX, 16, 0, true);
      preset_pages.xy_pad(SYNTH_PRESET_IDX, 8, 26);
      return;
    }
    else if (page == 2) {
      leds_clear();
      preset_pages.saveload(SYNTH_PRESET_IDX,32,FLAG_PICKER_ENABLE_DELETE);
      return;
    }
    else if (page == 3) {
      leds_clear();
      paint_draw_touch_pressure_debug();
      return;
    }   
    leds_clear();
    for (int x=0;x<16;++x) {
        for (int y=0;y<15;++y) {
            int pressure = get_touch_pressure_xy(x, y);
            // looping over 16x15 'buttons', we change colour when touched...
            if(y % 2 == 1) {// the odd line is center of triangle 
              int pad = y*16+x;
              if (pressure>30 || forcedPad[pad]) {
                forcedPad[pad]=(pressure>30) ? true : false;
                // Determine the 3 neighboring notes
                if ((x + (y-1)/2) % 2 == 0) {
                  if (y-1>=0) finger_onoff(x,y-1,pressure,true);
                  if (x-1>=0 && y+1<15) finger_onoff(x-1,y+1,pressure,true);
                  if (x+1<16 && y+1<15) finger_onoff(x+1,y+1,pressure,true);
                } else {
                  if (x+1<16 && y-1>=0) finger_onoff(x+1,y-1,pressure,true);
                  if (x-1>=0 && y-1>=0) finger_onoff(x-1,y-1,pressure,true);
                  if (y+1<15) finger_onoff(x,y+1,pressure,true);          
                }
              }
            }
            if((x+y/2) % 2 == 0 && y % 2 == 0) {
              finger_onoff(x,y,pressure,false);
            }
        } // loop over y direction
    } // loop over x direction
  } // ui function
}; // our panel
