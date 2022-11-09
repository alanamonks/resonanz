
#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char** argv)
{
  SDL_Init( SDL_INIT_EVERYTHING );
  
  std::cout << SDL_GetError() << std::endl;
  
  atexit( SDL_Quit );
  
  int count = SDL_GetNumAudioDevices(0);
  
  for (int i = 0; i < count; ++i)
    {
      std::cout << "Device " << i << ": " << SDL_GetAudioDeviceName(i, 0) << std::endl;
    }
  
  return 0;
}

