
#include "pictureFeatureVector.h"

#include <dinrhiw.h>
#include "timing.h"


bool calculatePicFeatureVector(const SDL_Surface* pic,
			       std::vector<float>& features)
{
  // creates datapoints from picture, sample N=10.000 points (~ 100x100)

  if(pic == NULL) return false;

  unsigned int* buffer = (unsigned int*)pic->pixels;

  whiteice::KMeans<> kmeans;
  std::vector< whiteice::math::vertex<> > points;

  double r = 0.0,g = 0.0,b = 0.0;
  whiteice::math::vertex<> rgb;
  rgb.resize(3);

  for(unsigned int s=0;s<10000;s++){
    unsigned int x = 0, y = 0;
    
    x = rand() % pic->w;
    y = rand() % pic->h;

    
    if(buffer){
      const unsigned int rr = (buffer[x + y*(pic->pitch/4)] & 0xFF0000) >> 16;
      const unsigned int gg = (buffer[x + y*(pic->pitch/4)] & 0x00FF00) >> 8;
      const unsigned int bb = (buffer[x + y*(pic->pitch/4)] & 0x0000FF) >> 0;

      r = rr/255.0;
      g = gg/255.0;
      b = bb/255.0;
    }

    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;

    points.push_back(rgb);
  }

  kmeans.startTrain(5, points);

  unsigned int counter = 0;

  while(kmeans.isRunning() && counter < (2*60)){ // max 1 minute
    millisleep(500);
  }

  kmeans.stopTrain();

  // calculates percentages of different clusters;

  std::map<unsigned int, unsigned int> Npixels;
  unsigned int N = 0;

  for(unsigned int i=0;i<points.size();i++){
    unsigned int cluster = kmeans.getClusterIndex(points[i]);
    Npixels[cluster]++;
    N++;
  }

  // sorts clusters based on N
  std::map<unsigned int, unsigned int> Npercluster;

  for(auto& p : Npixels){
    Npercluster.insert(std::pair<unsigned int, unsigned int>(p.second, p.first));
  }

  features.resize(5*4);

  for(auto& c : Npercluster){
    const unsigned int cluster = c.second;

    features[cluster*4 + 0] = kmeans[cluster][0].c[0];
    features[cluster*4 + 1] = kmeans[cluster][1].c[0];
    features[cluster*4 + 2] = kmeans[cluster][2].c[0];
    features[cluster*4 + 3] = (c.first)/((float)N);
  }

  return true;
}
