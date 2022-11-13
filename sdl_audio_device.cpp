
#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char** argv)
{
  SDL_Init( SDL_INIT_EVERYTHING );
  
  std::cout << SDL_GetError() << std::endl;
  
  atexit( SDL_Quit );

  std::cout << "SDL Audio Play Devices:" << std::endl;

  int count = SDL_GetNumAudioDevices(0);

  for (int i = 0; i < count; ++i)
  {
    std::cout << "Device " << i << ": " << SDL_GetAudioDeviceName(i, 0) << std::endl;
  }
  
  count = SDL_GetNumAudioDevices(1);

  std::cout << "SDL Audio Capture Devices:" << std::endl;
  
  for (int i = 0; i < count; ++i)
  {
    std::cout << "Device " << i << ": " << SDL_GetAudioDeviceName(i, 1) << std::endl;
  }
  
  return 0;
}

