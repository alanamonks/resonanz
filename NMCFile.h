/*
 * NMCLoader.h
 *
 *  Created on: 15.6.2015
 *      Author: Tomas
 */

#ifndef NMCFILE_H_
#define NMCFILE_H_

#include <string>
#include <vector>
#include <memory>

#include "DataSource.h"

namespace whiteice {
namespace resonanz {

/**
 * Implements .NMC file loader from the NeuromancerUI
 */
class NMCFile {
public:
  NMCFile();
  virtual ~NMCFile();
  
  /**
   * creates program (length_secs seconds longs) 
   * which given target values, negative values of target vector are ignored
   */
  bool createProgram(const DataSource& ds, 
		     const std::vector<float>& target,
		     unsigned int length_secs);
  
  bool createProgram(const DataSource& ds,
		     const std::vector< std::vector<float> > programs);
  
  bool loadFile(const std::string& filename);
  bool saveFile(const std::string& filename) const;
  
  unsigned int getNumberOfPrograms() const;
  
  bool getProgramSignalName(unsigned int index, std::string& name) const;
  
  bool getRawProgram(unsigned int index, std::vector<float>& program) const;
  bool getInterpolatedProgram(unsigned int index, std::vector<float>& program) const;
  
  static bool interpolateProgram(std::vector<float>& program);
  
private:
  static const unsigned int NUMBER_OF_PROGRAMS = 7; // allow many measurements
  
  std::string signalName[NUMBER_OF_PROGRAMS];
  std::vector<float> program[NUMBER_OF_PROGRAMS];
};

} /* namespace resonanz */
} /* namespace whiteice */

#endif /* NMCFILE_H_ */
