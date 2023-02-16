
#include <stdio.h>
#include <iostream>
#include "FMSoundSynthesis.h"
// #include "FluidSynthSynthesis.h"
#include "SDLMicrophoneListener.h"
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <SDL.h>

#include <dinrhiw.h>

#include "SDLTheora.h"
#include "SDLAVCodec.h"


#include <chrono>
using namespace std::chrono;



int main(int argc, char**argv)
{
  printf("Mini sound synthesis and capture test\n");

  whiteice::logging.setOutputFile("sound-engine.log");

  whiteice::RNG<> rng;
  unsigned int r = (unsigned int)rng.rand() % 7;

  printf("RAND: %d\n", r);
  fflush(stdout);

     
  bool useSDL = true;
  srand(time(0));

  // if(rand()&1) useSDL = true;

  SoundSynthesis* snd = NULL;
  SDLMicListener* mic = NULL;

  if(useSDL){
    SDL_Init(SDL_INIT_EVERYTHING);
    atexit(SDL_Quit);
    snd = new FMSoundSynthesis();
    // mic = new SDLMicListener();
  }
  else{
    // snd = new FluidSynthSynthesis("/usr/share/sounds/sf2/FluidR3_GM.sf2");
  }
  
  
  if(snd->play() == false){
    printf("Cannot start playback.\n");
    return -1;
  }

  if(mic){
    if(mic->listen() == false){
      printf("Cannot start audio capture.\n");
      return -1;
    }
  }
			   
  
  std::vector<float> p;
  p.resize(snd->getNumberOfParameters());
  
  for(unsigned int i=0;i<p.size();i++)
    p[i] = ((float)rand()) / ((float)RAND_MAX);


  // TESTS SDL THEORE CODEC
  
  //whiteice::resonanz::SDLTheora* codec = new whiteice::resonanz::SDLTheora(0.50);
  
  whiteice::resonanz::SDLAVCodec* codec = new whiteice::resonanz::SDLAVCodec(0.50);
  
  if(codec->startEncoding("test.mp4", 100, 100) == false){
    printf("CODEC INIT FAIL\n");
    return -1;
  }

  auto t1 = std::chrono::system_clock::now().time_since_epoch();
  auto ms_start = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
  
  
  while(1){
    for(unsigned int i=0;i<p.size();i++){
      do {
	float v = ((float)rand()) / ((float)RAND_MAX);
	p[i] = v;
	
	if(p[i] < 0.0f)
	  p[i] = 0.0f;
	else if(p[i] > 1.0f)
	  p[i] = 1.0f;
      }
      while(p[i] <= 0.0f);

    }

      
    auto t2 = std::chrono::system_clock::now().time_since_epoch();
    auto ms_now = std::chrono::duration_cast<std::chrono::milliseconds>(t2).count();
    
    const unsigned int msecs = (unsigned int)(ms_now - ms_start);
    
    SDL_Surface* surface = SDL_CreateRGBSurface(0, 100, 100, 32,
						0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));
    
    // creates random noise
    for(unsigned int y=0;y<(unsigned)100;y++){
      for(unsigned int x=0;x<(unsigned)100;x++){
	const unsigned int index = x + 100*y;
	unsigned int* source = (unsigned int*)(surface->pixels);
	
	const unsigned int r = whiteice::rng.rand() % 0xFF;
	const unsigned int g = whiteice::rng.rand() % 0xFF;
	const unsigned int b = whiteice::rng.rand() % 0xFF;
	
	source[index] = (r<<16) + (g<<8) + (b);
      }
    }
    
    
    if(codec->insertFrame(msecs, surface))
      std::cout << "Insert Frame OK: " << msecs << std::endl;
    else
      std::cout << "Insert Frame FAIL: " << msecs << std::endl;

    SDL_FreeSurface(surface);
    
    // if(p.size() > 0) p[0] = 0.8;
    
    if(snd->setParameters(p) == false)
      std::cout << "set parameters failed." << std::endl;
    
    if(mic)
      std::cout << "SIGNAL POWER: " << mic->getInputPower() << std::endl;
    
    // snd->reset();
    
    SDL_Delay(500);
  }
  
  
  
  if(codec) delete codec;
  if(snd) delete snd;
  if(mic) delete mic;
  
  SDL_Quit();
  
  return 0;
}
