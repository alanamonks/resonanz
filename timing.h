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

void win_usleep(__int64 usec) 
{ 
  HANDLE timer; 
  LARGE_INTEGER ft; 
  
  ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time
  
  timer = CreateWaitableTimer(NULL, TRUE, NULL); 
  SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
  WaitForSingleObject(timer, INFINITE); 
  CloseHandle(timer); 
}

#define millisleep(msec) win_usleep(msec*1000)

// Sleep(msec)

#else

#include <unistd.h>

#define millisleep(msec) usleep(msec*1000);

#endif


#endif /* TIMING_H_ */
