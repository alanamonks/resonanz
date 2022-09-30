
#include "spectral_entropy.h"

#include <math.h>


namespace whiteice
{
  namespace resonanz
  {
    
    float spectral_entropy(const std::vector<float>& _P)
    {
      std::vector<float> P(_P);

      // converts spectral values to distibutions
      // (don't handle Power specrum BINs differences in wideness/values)
      
      float sumP = 0.0f;

      for(auto& p : P){
	p = fabs(p);
	sumP += p;
      }

      if(sumP > 0.0f){
	for(auto& p : P){
	  p /= sumP;
	}
      }

      float e = 0.0f;

      for(auto& p : P){
	if(p > 0.0f){
	  e += p*logf(1.0f/p);
	}
      }

      // scaling

      if(P.size())
	e /= logf((float)P.size());
      
      return e;
    }

  };
};
