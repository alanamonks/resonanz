/*
 * MuseOSC4.h
 *
 * 4 channel MuseOSC values reading. 
 * Assumes headband can read all values from sensors without problems.
 *
 *  Created on: 18.2.2023
 *      Author: Tomas Ukkonen
 */

#ifndef MUSEOSC4_H_
#define MUSEOSC4_H_

#include "DataSource.h"

#include <vector>
#include <stdexcept>
#include <exception>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace whiteice {
namespace resonanz {

/**
 * Receives Muse OSC data from given UDP localhost port
 * Currently outputs 25 values = 4 channels and 6 measurements, + total power
 *
 * delta, theta, alpha, beta, gamma, spectral entropy absolute values fitted to [0,1] interval
 * total power: sum delta+theta+alpha+beta+gamma
 * spectral entropy: [0,1] interval value, 1 means max entropy (equal distribution), 0 means minimum entropy (single peak)
 *
 */
class MuseOSC4: public DataSource {
public:
  MuseOSC4(const unsigned int port); // throw(std::runtime_error)
  virtual ~MuseOSC4();
  
  /*
   * Returns unique DataSource name
   */
  virtual std::string getDataSourceName() const;
  
  /**
   * Returns true if connection and data collection 
   * to device is currently working.
   */
  virtual bool connectionOk() const;
  
  /**
   * returns current value
   */
  virtual bool data(std::vector<float>& x) const;
  
  virtual bool getSignalNames(std::vector<std::string>& names) const;
  
  virtual unsigned int getNumberOfSignals() const;
  
 private:
  const unsigned int port;
  
  void muse_loop(); // worker thread loop
  
  std::thread* worker_thread;
  bool running;
  
  bool hasConnection;
  std::mutex connection_mutex;
  std::condition_variable connection_cond;
  
  float quality; // quality of connection [0,1]
  
  mutable std::mutex data_mutex;
  std::vector<float> value; // currently measured value
  long long latest_sample_seen_t; // time of the latest measured value
  
};

} /* namespace resonanz */
} /* namespace whiteice */

#endif /* MUSEOSC4_H_ */
