
#include "IsochronicPictureSynthesis.h"

#include <dinrhiw.h>


IsochronicPictureSynthesis::IsochronicPictureSynthesis()
{
  logF = 0.0;
  
  r = 1.0;
  g = 1.0;
  b = 1.0;
}

IsochronicPictureSynthesis::~IsochronicPictureSynthesis()
{
}

std::string IsochronicPictureSynthesis::getSynthesizerName()
{
  return "SDL-IsochronicPictureSynthesis";
}


bool IsochronicPictureSynthesis::reset()
{
  return true; 
}


// parameters are: frequency [1/s = Hz], (r, g, b) [r g b components are values 0-1]

bool IsochronicPictureSynthesis::getParameters(std::vector<float>& p)
{
  p.resize(4);
  
  p[0] = (logF+1.0)/5.0;
  p[1] = r;
  p[2] = g;
  p[3] = b;

  return true;
}

bool IsochronicPictureSynthesis::setParameters(const std::vector<float>& p)
{
  if(p.size() == 4){
    
    if(p[0] < 0.0f){
      logF = 0.0;
    }
    else if(p[0] > 1.0f){
      logF = 1.0;
    }
    else{
      logF = p[0];
    }

    logF *= 5.0;
    logF -= 1.0;
    
    
    if(p[1] < 0.0f){
      r = 0.0;
    }
    else if(p[1] > 1.0f){
      r = 1.0;
    }
    else{
      r = p[1];
    }

    if(p[2] < 0.0f){
      g = 0.0;
    }
    else if(p[2] > 1.0f){
      g = 1.0;
    }
    else{
      g = p[2];
    }

    if(p[3] < 0.0f){
      b = 0.0;
    }
    else if(p[3] > 1.0f){
      b = 1.0;
    }
    else{
      b = p[2];
    }

    return true;
  }
  else return false;
}


unsigned int IsochronicPictureSynthesis::getNumberOfParameters()
{
  return 4;
}


bool IsochronicPictureSynthesis::synthesize(unsigned long long tickTimeMS, SDL_Surface* picture)
{
  if(picture == NULL) return false;

  const double t = tickTimeMS/1000.0;
  const double F = whiteice::math::exp((double)logF);

  double A = whiteice::math::sin(F*t);
  if(A < 0.0) A = 0.0;
  A = whiteice::math::sqrt(A);
  A = whiteice::math::sqrt(A);
  A = whiteice::math::sqrt(A);
  A = whiteice::math::sqrt(A); // x^0.0625 => close to rectangular wave function

  const unsigned int R = A*r*255.0;
  const unsigned int G = A*g*255.0;
  const unsigned int B = A*b*255.0;

  SDL_FillRect(picture, NULL, SDL_MapRGB(picture->format, R, G, B));

  return true;
}

