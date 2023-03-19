/*
 * IsochronicPictureSynthesis.h
 *
 * frequency and color (r,g,b) value parameters determine what picture is generated.
 *
 * (C) Copyright 2023 Tomas Ukkonen
 * 
 */


#ifndef ISOCHRONICPICTURESYNTHESIS_H_
#define ISOCHRONICPICTURESYNTHESIS_H_


#include <vector>
#include <list>
#include <string>

#include "SDLPictureSynthesis.h"


class IsochronicPictureSynthesis : public SDLPictureSynthesis
{
 public:

  IsochronicPictureSynthesis();
  virtual ~IsochronicPictureSynthesis();
  
  virtual std::string getSynthesizerName();

  virtual bool reset();

  // parameters are: frequency [1/s = Hz], (r, g, b) [r g b components are values 0-1]

  virtual bool getParameters(std::vector<float>& p);

  virtual bool setParameters(const std::vector<float>& p);

  virtual unsigned int getNumberOfParameters();

  virtual bool synthesize(unsigned long long tickTimeMS, SDL_Surface* picture);


 private:

  // synthesis parameters
  
  double logF; // isochronic frequency
  double r, g, b; // active color
		     
};


#endif
