/*
 * SDLPictureSynthesis.h interface class
 *
 * (C) Copyright 2023 Tomas Ukkonen
 * 
 */

#ifndef SDLPICTURESYNTHESIS_H_
#define SDLPICTURESYNTHESIS_H_


#include <vector>
#include <list>
#include <string>

#include <SDL.h>


class SDLPictureSynthesis // interface class
{
 public:

  virtual ~SDLPictureSynthesis(){ }

  virtual std::string getSynthesizerName() = 0;

  virtual bool reset() = 0;

  virtual bool getParameters(std::vector<float>& p) = 0;

  virtual bool setParameters(const std::vector<float>& p) = 0;

  virtual unsigned int getNumberOfParameters() = 0;
  
  virtual bool synthesize(unsigned long long tickTimeMS, SDL_Surface* picture) = 0;

};


#endif
