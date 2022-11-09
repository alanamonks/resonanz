// Tomas Ukkonen 10/2016, 2022 (microphone recording support)

#ifndef SDLMICLISTERNER_H_
#define SDLMICLISTERNER_H_

#include <vector>
#include <string>
#include <stdint.h>
#include <SDL.h>


#include <vorbis/vorbisenc.h>
#include <mutex>


class SDLMicListener {
public:

  SDLMicListener();
  virtual ~SDLMicListener();

  bool listen(const char* device = NULL); // start listening mic

  // starts/stops recording Mic to file in OGG format (Vorbis encoder)
  bool record(const std::string& filename);
  bool stopRecord();

  // returns signal power in DECIBELs
  // (max level is 0 and values are negative)
  virtual double getInputPower();

protected:
  SDL_AudioSpec snd;

  virtual bool listener(int16_t* buffer, int samples);

private:
  SDL_AudioDeviceID dev;
  SDL_AudioSpec desired;

  double currentPower = 0.0;

  friend void __sdl_soundcapture(void* unused, Uint8* stream, int len);
  
  
  bool recording = false;
  FILE* handle = NULL;
  std::mutex recording_mutex;

  void write_vorbis_data();

  // OGG datastream structures used for recording

  ogg_stream_state os; /* take physical pages, weld into a logical
                          stream of packets */
  ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet       op; /* one raw packet of data for decode */

  vorbis_info      vi; /* struct that stores all the static vorbis bitstream
                          settings */
  vorbis_comment   vc; /* struct that stores all the user comments */

  vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
  vorbis_block     vb; /* local working space for packet->PCM decode */
  
};

void __sdl_soundcapture(void* unused, Uint8* stream, int len);

#endif
