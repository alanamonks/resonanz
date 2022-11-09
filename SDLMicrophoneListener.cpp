
#include "SDLMicrophoneListener.h"
#include <math.h>

#include <vorbis/vorbisenc.h>

#include <dinrhiw.h>


SDLMicListener::SDLMicListener()
{
  SDL_zero(desired);
  SDL_zero(snd);

  desired.freq = 44100;
  desired.format = AUDIO_S16SYS;
  desired.channels = 1; // use mono sound for now
  desired.samples = 4096;
  desired.callback = __sdl_soundcapture;
  desired.userdata = this;

  dev = 0;
  currentPower = 0.0;

  
  recording = false;
}


SDLMicListener::~SDLMicListener()
{
  if(recording){
    
    std::lock_guard<std::mutex> lock(recording_mutex);
    recording = false;
    
    ogg_stream_clear(&os);
    if(handle) fclose(handle);
    handle = NULL;

    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
  }

  
  if(dev != 0){
    SDL_CloseAudioDevice(dev);
    dev = 0;
    currentPower = 0.0;
  }

}


// start listening mic
bool SDLMicListener::listen(const char* device)
{
  if(dev == 0){
    dev = SDL_OpenAudioDevice(device, 1, &desired, &snd,
			      SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  }

  if(dev == 0){
    printf("SDL Error: %s\n", SDL_GetError());
    return false;
  }

  if(snd.format != AUDIO_S16SYS || snd.channels != 1){
    SDL_CloseAudioDevice(dev);
    dev = 0;
    return false;
  }
  
  SDL_PauseAudioDevice(dev, 0);
  
  return true;
}


// returns signal power in DECIBELs
// (max level is 0 and values are negative)
double SDLMicListener::getInputPower()
{
  // returns values as negative decibels
  double Pref = 32767.0*32767.0;
  double dbel = 10.0*(log(currentPower/Pref)/log(10.0));
  return dbel;
}


// starts/stops recording Mic to file in OGG format (Vorbis encoder)
bool SDLMicListener::record(const std::string& filename)
{
  if(recording || dev == 0) return false;

  std::lock_guard<std::mutex> lock(recording_mutex);

  if(recording) return false;

  handle = fopen(filename.c_str(), "wb");
  if(handle == NULL) return false;


  vorbis_info_init(&vi);
  vorbis_encode_init_vbr(&vi, 1, 44100, 0.8); // Mono 44.100 Hz, 0.8 quality = 256kbit/s

  vorbis_comment_init(&vc);
  vorbis_comment_add_tag(&vc, "ENCODER", "resonanz neurostim mic record");

  vorbis_analysis_init(&vd, &vi);
  vorbis_block_init(&vd,&vb);


  ogg_stream_init(&os,whiteice::rng.rand());

  {
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;
    
    vorbis_analysis_headerout(&vd,&vc,&header,&header_comm,&header_code);
    ogg_stream_packetin(&os,&header); /* automatically placed in its own
                                         page */
    ogg_stream_packetin(&os,&header_comm);
    ogg_stream_packetin(&os,&header_code);
    
    /* This ensures the actual
     * audio data will start on a new page, as per spec
     */
    while(1){
      int result=ogg_stream_flush(&os,&og);
      if(result==0)break;
      fwrite(og.header,1,og.header_len,handle);
      fwrite(og.body,1,og.body_len,handle);
    }

  }


  recording = true;

  return true;
}


bool SDLMicListener::stopRecord()
{
  if(!recording || dev == 0) return false;

  std::lock_guard<std::mutex> lock(recording_mutex);

  if(!recording) return false;

  ogg_stream_clear(&os);
  if(handle) fclose(handle);
  handle = NULL;

  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
  
  recording = false;

  return true;
}


bool SDLMicListener::listener(int16_t* buffer, int samples)
{
  // calculates current power
  double power = 0.0;
  
  for(int i=0;i<samples;i++){
    power += ((double)buffer[i])*((double)buffer[i])/((double)samples);
  }

  currentPower = power;

  if(recording){
    std::lock_guard<std::mutex> lock(recording_mutex);
    
    float** vorbisbuffer = vorbis_analysis_buffer(&vd, samples);
    for(int i=0;i<samples;i++)
      vorbisbuffer[0][i] = ((float)buffer[i])/36768.0f;

    vorbis_analysis_wrote(&vd, samples);

    write_vorbis_data();
  }
  
  return true;
}


void SDLMicListener::write_vorbis_data()
{
  
  while(vorbis_analysis_blockout(&vd,&vb)==1){
    
    /* analysis, assume we want to use bitrate management */
    vorbis_analysis(&vb,NULL);
    vorbis_bitrate_addblock(&vb);
    
    while(vorbis_bitrate_flushpacket(&vd,&op)){
      
      /* weld the packet into the bitstream */
      ogg_stream_packetin(&os,&op);
      
        /* write out pages (if any) */
      while(1){
	int result=ogg_stream_pageout(&os,&og);
	if(result==0) break;
	fwrite(og.header,1,og.header_len,handle);
	fwrite(og.body,1,og.body_len,handle);

	if(ogg_page_eos(&og)) break;
      }
    }
  }
  
}


void __sdl_soundcapture(void* unused, Uint8* stream, int len)
{
  SDLMicListener* s = (SDLMicListener*)unused;
  
  if(s == NULL) return;
  
  s->listener((int16_t*)stream, len/2);
}

