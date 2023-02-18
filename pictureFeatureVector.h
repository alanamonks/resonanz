/*
 * calculates feature vectors from pictures
 *
 * initially calculates only pixel clustering:
 *
 *  top five cluster means and p% value of points that belong to that cluster
 * 
 * => detect picture color distribution and differentiate between different pictures in machine learning component
 *
 */

#ifndef PICFEATVEC_H
#define PICFEATVEC_H

#include <SDL.h>
#include <vector>

bool calculatePicFeatureVector(const SDL_Surface* pic,
			       std::vector<float>& features);


#endif
