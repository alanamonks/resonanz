/*
 * SDLSoundSynthesis.cpp
 *
 *  Created on: 15.2.2013
 *      Author: Tomas Ukkonen
 */

#include "SDLSoundSynthesis.h"

#ifdef WINOS
#include <windows.h>
#endif

#include <pthread.h>
#include <sched.h>

SDLSoundSynthesis::SDLSoundSynthesis()
{
  SDL_zero(desired);
  SDL_zero(snd);
  
  desired.freq = 44100;
  //desired.freq = 22050;
  desired.format = AUDIO_S16SYS;
  desired.channels = 1; // use mono sound for now
  desired.samples = 4096;
  desired.callback = __sdl_soundsynthesis_mixaudio;
  desired.userdata = this;
  
  dev = 0;
  
}

SDLSoundSynthesis::~SDLSoundSynthesis() {
  if(dev != 0){
    SDL_CloseAudioDevice(dev);
    dev = 0;
  }
  
}


bool SDLSoundSynthesis::play()
{
  if(dev == 0){
    dev = SDL_OpenAudioDevice(NULL, 0, &desired, &snd,
			      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  }
  
  if(dev == 0){
    printf("SDL Error: %s\n", SDL_GetError());

    const int numDrivers = SDL_GetNumAudioDrivers();

    if(numDrivers > 0)
      printf("Number of audio drivers: %d\n", numDrivers);

    for(int i=0;i<numDrivers;i++)
      printf("Driver %d : %s\n", i, SDL_GetAudioDriver(i));
    
    // prints audio devices
    const int numDevices = SDL_GetNumAudioDevices(0);
    if(numDevices >= 0)
      printf("Number of audio devices: %d\n", numDevices);
    
    for(int i=0;i<numDevices;i++){
      printf("Audio device %d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
    }

    fflush(stdout);
    
    return false;
  }
  
  if(snd.format != AUDIO_S16SYS || snd.channels != 1){
    if(dev != 0) SDL_CloseAudioDevice(dev);
    dev = 0;
    return false;
  }

  if(dev != 0)
    SDL_PauseAudioDevice(dev, 0);
  
  return true;
}


bool SDLSoundSynthesis::pause()
{
  if(dev != 0)
    SDL_PauseAudioDevice(dev, 1);
  else
    return false;
  
  return true;
}

static bool __sdl__soundsynth_setpriority = false;

void __sdl_soundsynthesis_mixaudio(void* unused, 
				   Uint8* stream, int len)
{
#if 0
  if(!__sdl__soundsynth_setpriority){
    sched_param sch_params;
    int policy = SCHED_FIFO; // SCHED_RR
    
    pthread_getschedparam(pthread_self(), &policy, &sch_params);
    
    policy = SCHED_FIFO;
    sch_params.sched_priority = sched_get_priority_max(policy);
    
    if(pthread_setschedparam(pthread_self(),
			     policy, &sch_params) != 0){
    }
    
#ifdef WINOS
    SetThreadPriority(GetCurrentThread(),
		      THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(GetCurrentThread(),
		      THREAD_PRIORITY_TIME_CRITICAL);
#endif

    __sdl__soundsynth_setpriority = true;
  }
#endif
  
  SDLSoundSynthesis* s = (SDLSoundSynthesis*)unused;
  
  if(s == NULL) return;
  
  s->synthesize((int16_t*)stream, len/2);
}

