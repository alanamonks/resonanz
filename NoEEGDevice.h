/*
 * NoEEGDevice.h
 *
 *  Created on: 2.7.2015
 *      Author: Tomas
 */

#ifndef NOEEGDEVICE_H_
#define NOEEGDEVICE_H_

#include "DataSource.h"

namespace whiteice {
namespace resonanz {

/**
 * Placeholder for empty EEG device producing no outputs.
 */
class NoEEGDevice: public DataSource {
public:
	NoEEGDevice(const unsigned int channels = 7);
	virtual ~NoEEGDevice();


	virtual std::string getDataSourceName() const { return "No EEG device"; };

	/**
	 * Returns true if connection and data collection to device is currently working.
	 */
	virtual bool connectionOk() const { return true; }

	/**
	 *	 returns current value
	 */
	virtual bool data(std::vector<float>& x) const {
	  x.resize(CHANNELS);
	  for(unsigned int i=0;i<x.size();i++)
	    x[i] = 0.5f;
	  return true;
	}

	virtual bool getSignalNames(std::vector<std::string>& names) const {
 	  names.resize(CHANNELS);
	  for(unsigned int i=0;i<names.size();i++)
	    names[i] = "Empty signal";
	  return true;
	}

	virtual unsigned int getNumberOfSignals() const { return CHANNELS; }

private:
        const unsigned int CHANNELS;
};

} /* namespace resonanz */
} /* namespace whiteice */

#endif /* NOEEGDEVICE_H_ */
