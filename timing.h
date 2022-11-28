/*
 * timing.h
 *
 *  Created on: 3.3.2013
 *      Author: Tomas Ukkonen
 */

#ifndef TIMING_H_
#define TIMING_H_

// works around buggy usleep in MINGW/windows [use microsleep instead]

#ifdef WINNT
#include <windows.h>
#define millisleep(msec) Sleep(msec)
#else
#include <unistd.h>
#define millisleep(msec) usleep(msec*1000);
#endif


#endif /* TIMING_H_ */
