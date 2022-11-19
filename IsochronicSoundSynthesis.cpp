/*
 * IsochronicSoundSynthesis.cpp
 *
 */

#include "IsochronicSoundSynthesis.h"
#include <iostream>
#include <chrono>
#include <math.h>
#include <vector>

#ifndef M_PI
#define M_PI 3.141592653
#endif

IsochronicSoundSynthesis::IsochronicSoundSynthesis() {
  
  // default parameters: silence
  currentp.resize(3);
  
  for(auto& pi : currentp)
    pi = 0.0f;
  
  A = 0.0;
  Fc = 0.0;
  F = 0.0;
  
  oldA = 0.0;
  oldFc = 0.0;
  oldF = 0.0;
  
  tbase = 0.0;
  
  fadeoutTime = 200.0; // 200ms fade out between parameter changes
}


IsochronicSoundSynthesis::~IsochronicSoundSynthesis() {
  this->pause();
}


std::string IsochronicSoundSynthesis::getSynthesizerName()
{
  return "SDL Isochronic Sound Synthesis (brain modulating)";
}

bool IsochronicSoundSynthesis::reset()
{
  // resets sound generation (timer)
  tbase = 0.0;
  return true;
}


bool IsochronicSoundSynthesis::getParameters(std::vector<float>& p)
{
  p = currentp;
  
  return true;
}


bool IsochronicSoundSynthesis::setParameters(const std::vector<float>& p_)
{
  auto p = p_;
  
  if(p.size() != 3) return false;
  
  // limit parameters to [0,1] range
  for(auto& pi : p){
    if(pi < 0.0f) pi = 0.0f;
    if(pi > 1.0f) pi = 1.0f;
  }
  
  currentp = p; // copies values for getParameters()
  
  oldA = A;
  oldFc = Fc;
  oldF = F;
  
  A = p[0];
  
  // sound base frquency: [55 Hz, 880 Hz] => note interval: A-1 - A-5
  float f = 220.0;
  {
    // converts [0,1] to note [each note is equally probable]
    const float NOTES = 24.9999; // note interval: (A-1 - A-5) [48 notes]
    double note = 2.0*(floor(p[1]*NOTES) - 12.0); // note = 0 => A-3
    
    // printf("note = %f\n", note); fflush(stdout);
    
    f = 220.0*pow(2.0, note/12.0); // converts note to frequency
  }
  
  
  Fc = f;
  
  {
    // isochronic modulating freq: F = 1..40 Hz
    
    f = 1.0 + 47.0*p[2];
  }

  F = f;
  
  resetTime = getMilliseconds();
  // tbase = 0.0;
  
  return true;
}


int IsochronicSoundSynthesis::getNumberOfParameters(){
  return 3;
}


unsigned long long IsochronicSoundSynthesis::getSoundSynthesisSpeedMS()
{
  return fadeoutTime;
}


// milliseconds since epoch
unsigned long long IsochronicSoundSynthesis::getMilliseconds()
{
  auto t1 = std::chrono::system_clock::now().time_since_epoch();
  auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
  return (unsigned long long)t1ms;
}


double IsochronicSoundSynthesis::getSynthPower()
{
  // returns values as negative decibels
  double Pref = 32767.0*32767.0;
  double dbel = 10.0*log10(currentPower/Pref);
  return dbel;
}


bool IsochronicSoundSynthesis::synthesize(int16_t* buffer, int samples)
{
  double hz = (double)snd.freq;
  
  double timeSinceReset = (double)(getMilliseconds() - resetTime);

  const unsigned int MEANBUFFER_MAX_SIZE = (unsigned int)(0.001*hz + 1);
  
  for(int i=0;i<samples;i++){
    const double dt = ((double)i)/hz;
    const double t = tbase + dt;
    
    const double now = timeSinceReset + dt*1000.0;

    
    double a = A*sin(2.0*M_PI*F*t);
    if(a <= 0.0) a = 0.0;
    double value = a*sin(2.0*M_PI*Fc*t);
    
    if(value <= -1.0) value = -1.0;
    else if(value >= 1.0) value = 1.0;

    // mixes partially previous sound to sound if it is fade out period
    if(now < fadeoutTime){
      double c = now/fadeoutTime;

      // also fades parameters towards target value [less clicking?]
      double oldA0 = oldA*(1-c) + A*c;
      //double oldF0 = oldF*(1-c) + F*c;
      //double oldFc0 = oldFc*(1-c) + Fc*c;

          
      double old_a = oldA0*sin(2.0*M_PI*oldF*t);
      if(old_a <= 0.0) old_a = 0.0;
      double old_value = old_a*sin(2.0*M_PI*oldFc*t);
      
      if(old_value <= -1.0) old_value = -1.0;
      else if(old_value >= 1.0) old_value = 1.0;
      
      
      value = (1.0 - c)*old_value + c*value;
    }
    
    
    
    meansum += value;
    meanbuffer.push_back(value);
    while(meanbuffer.size() > MEANBUFFER_MAX_SIZE){
      meansum -= meanbuffer.front();
      meanbuffer.pop_front();
    }

    double mean = meansum / meanbuffer.size(); 
    
    buffer[i] = (int16_t)( mean*32767 );
    // buffer[i] = (int16_t)( value*32767 );
  }

  
  double power = 0.0;

  for(int i=0;i<samples;i++){
    power += ((double)buffer[i])*((double)buffer[i])/((double)samples);
  }

  currentPower = power;
  
  tbase += ((double)samples)/hz;

  
  return true;
}


