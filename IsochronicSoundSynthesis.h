/*
 * IsochronicSoundSynthesis.h
 *
 * 2022 Tomas Ukkonen
 */

#ifndef ISOCHRONICSOUNDSYNTHESIS_H_
#define ISOCHRONICSOUNDSYNTHESIS_H_

#include "SDLSoundSynthesis.h"
#include <vector>
#include <list>


class IsochronicSoundSynthesis: public SDLSoundSynthesis {
public:
  IsochronicSoundSynthesis();
  virtual ~IsochronicSoundSynthesis();
  
  virtual std::string getSynthesizerName();
  
  virtual bool reset();
  
  virtual bool getParameters(std::vector<float>& p);
  
  virtual bool setParameters(const std::vector<float>& p);
  
  virtual int getNumberOfParameters();
  
  virtual unsigned long long getSoundSynthesisSpeedMS();

  virtual double getSynthPower();
  
 protected:
  // milliseconds since epoch
  unsigned long long getMilliseconds();
  
  double tbase;
  
  double A; // amplitude/volume of carrier
  double Fc; // carrier frequency
  double F; // isochronic frequency
  
  std::vector<float> currentp;
  
  unsigned long long resetTime = 0ULL;
  double timeSinceReset; // from tbase value (secs)
  double fadeoutTime; // in milliseconds
  
  double oldA, oldFc, oldF;

  std::list<double> meanbuffer;
  double meansum = 0.0f;
  
  static const unsigned int NBUFFERS = 10;
  std::vector<int16_t> prevbuffer[NBUFFERS];
  
  virtual bool synthesize(int16_t* buffer, int samples);


  double currentPower = 0.0; // current output signal power
  
};

#endif /* FMSOUNDSYNTHESIS_H_ */
