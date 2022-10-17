/*
 * NMCLoader.cpp
 *
 *  Created on: 15.6.2015
 *      Author: Tomas
 */

#include "NMCFile.h"
#include <stdio.h>

#include <memory>

namespace whiteice {
namespace resonanz {

NMCFile::NMCFile() {  }

NMCFile::~NMCFile() {  }


/**
 * creates program (length_secs seconds longs) 
 * which given target values
 */
bool NMCFile::createProgram(const DataSource& ds, 
			    const std::vector<float>& target,
			    unsigned int length_secs)
{
        if(target.size() != ds.getNumberOfSignals())
	  return false;
	
	if(length_secs <= 0)
	  return false;
	
	// finds first positive target values
	int first_target = -1;
	int second_target = -1;
	
	for(int i=0;i<target.size();i++){
	  if(target[i] >= 0.0f){
	    if(first_target == -1)
	      first_target = i;
	    else if(second_target == -1)
	      second_target = i;
	  }	   
	}
	
	if(first_target == -1 && second_target == -1)
	  return false;
	
	std::string names[NUMBER_OF_PROGRAMS];
	std::vector<float> pvalues[NUMBER_OF_PROGRAMS];
	
	for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
	  names[i] = "N/A";
	  pvalues[i].resize(length_secs);
	  for(unsigned int s=0;s<length_secs;s++)
	    pvalues[i][s] = -1.0f;
	}
	
	std::vector<std::string> snames;
	ds.getSignalNames(snames);
	
	if(first_target >= 0){
	  names[0] = snames[first_target];
	  pvalues[0].resize(length_secs);
	  for(unsigned int s=0;s<length_secs;s++)
	    pvalues[0][s] = target[first_target];
	}
	
	if(second_target >= 0){
	  names[1] = snames[second_target];
	  pvalues[1].resize(length_secs);
	  for(unsigned int s=0;s<length_secs;s++)
	    pvalues[1][s] = target[second_target];
	}
	
	// everything went ok
	
	for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
	  signalName[i] = "N/A";
	  program[i].resize(length_secs);
	  for(unsigned int s=0;s<length_secs;s++)
	    program[i][s] = -1.0;
	}

	for(unsigned int i=0;i<2;i++){
	  signalName[i] = names[i];
	  program[i] = pvalues[i];
	}
	
	return true;
}

  
  bool NMCFile::createProgram(const DataSource& ds,
			      const std::vector< std::vector<float> > programdata)
  {
    if(programdata.size() != ds.getNumberOfSignals())
      return false;

    std::vector<std::string> snames;
    ds.getSignalNames(snames);

    for(unsigned int i=0;i<programdata.size();i++)
      if(programdata[0].size() != programdata[i].size())
	return false; // programs must have same size

    // defaults for the programs (no program)
    for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
      signalName[i] = "N/A";
      program[i].resize(programdata[0].size());
      for(unsigned int s=0;s<programdata[0].size();s++)
	program[i][s] = -1.0;
    }

    // writes existing programs
    for(unsigned int i=0;i<programdata.size()&&i<NUMBER_OF_PROGRAMS;i++){
      signalName[i] = snames[i];
      program[i] = programdata[i];
    }

    return true;
  }
  

bool NMCFile::loadFile(const std::string& filename)
{
  FILE* handle = NULL;
  
  try{
    handle = fopen(filename.c_str(), "rb");
    if(handle == NULL) return false;
    
    char* name[NUMBER_OF_PROGRAMS];

    for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
      name[i] = new char[33]; // US-ASCII chars

      if(fread(name[i], sizeof(char), 32, handle) != 32){
	fclose(handle);
	return false;
      }

      name[i][32] = '\0';
    }

    unsigned int len = 0;
    
    if(fread(&len, sizeof(unsigned int), 1, handle) != 1){ // assumes little endianess here...11
      fclose(handle);
      return false;
    }
    
    if(len > 10000){ // sanity check [in practice values must be always "small"]
      fclose(handle);
      return false;
    }
    
		
    // next we read actual "program" data (floats)
    
    float* programdata[NUMBER_OF_PROGRAMS];

    for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
      programdata[i] = (new float[len]); // 32bit floats [little endian]

      if(fread(programdata[i], sizeof(float), len, handle) != len){
	fclose(handle);
	return false;
      }
    }


    for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
      // all data has been successfully read, stores values to internal class variables
      signalName[i] = std::string(name[i]);
      
      // trimming
      signalName[i].erase(signalName[i].find_last_not_of(" ")+1);
      
      program[i].resize(len);
      
      for(unsigned int k=0;k<len;k++){
	program[i][k] = programdata[i][k];
      }
    }

    for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
      delete[] programdata[i];
      delete[] name[i];
    }
    
    fclose(handle);
    return true;
  }
  catch(std::exception& e){
    return false;
  }

}


  bool NMCFile::saveFile(const std::string& filename) const
  {
    
    FILE* handle = NULL;
    
    try{
      handle = fopen(filename.c_str(), "wb");
      if(handle == NULL) return false;
      
      char* name[NUMBER_OF_PROGRAMS];
      
      for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
	name[i] = new char[33]; // US-ASCII chars

	for(unsigned int k=0;k<32&&k<=signalName[i].size();k++){
	  name[i][k] = signalName[i][k];
	}

	name[i][32] = '\0';
	
	if(fwrite(name[i], sizeof(char), 32, handle) != 32){
	  fclose(handle);
	  return false;
	}
	
	
      }

      unsigned int len = 0;
      
      len = program[0].size();
      
      if(len > 10000){ // sanity check [in practice values must be always "small"]
	fclose(handle);
	return false;
      }
      
      
      if(fwrite(&len, sizeof(unsigned int), 1, handle) != 1){ // assumes little endianess here...11
	fclose(handle);
	return false;
      }
      
      
      // next we write actual "program" data (floats)
      
      float* programdata[NUMBER_OF_PROGRAMS];
      
      for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
	programdata[i] = (new float[len]); // 32bit floats [little endian]
	
	for(unsigned int k=0;k<len;k++)
	  programdata[i][k] = -1.0f;
	
	for(unsigned int k=0;k<program[i].size();k++)
	  programdata[i][k] = program[i][k];
	
	if(fwrite(programdata[i], sizeof(float), len, handle) != len){
	  fclose(handle);
	  return false;
	}
      }
      
      
      for(unsigned int i=0;i<NUMBER_OF_PROGRAMS;i++){
	delete[] programdata[i];
	delete[] name[i]; 
      }
      
      fclose(handle);
      return true;
    }
    catch(std::exception& e){
      return false;
    }
    
    
  }
  
  unsigned int NMCFile::getNumberOfPrograms() const
  {
    return NUMBER_OF_PROGRAMS;
  }
  
  bool NMCFile::getProgramSignalName(unsigned int index, std::string& name) const
  {
    if(index > NUMBER_OF_PROGRAMS) return false;
    name = signalName[index];
    return true;
  }
  
  
  bool NMCFile::getRawProgram(unsigned int index, std::vector<float>& program) const
  {
    if(index > NUMBER_OF_PROGRAMS) return false;
    program = this->program[index];
    return true;
  }
  
  
  bool NMCFile::getInterpolatedProgram(unsigned int index, std::vector<float>& program) const
  {
    if(index > NUMBER_OF_PROGRAMS) return false;
    program = this->program[index];
    
    return interpolateProgram(program);
  }
  
  
  bool NMCFile::interpolateProgram(std::vector<float>& program)
  {
    // goes through the code program and finds the first positive point
    unsigned int prevPoint = program.size();
    for(unsigned int i=0;i<program.size();i++){
      if(program[i] >= 0.0f){ prevPoint = i; break; }
    }
    
    if(prevPoint == program.size()){ // all points are negative
      for(auto& p : program)
	p = 0.5f;
      
      return true;
    }
    
    for(unsigned int i=0;i<prevPoint;i++)
      program[i] = program[prevPoint];
    
    // once we have processed the first positive point, looks for the next one
    while(prevPoint+1 < program.size()){
      for(unsigned int p=prevPoint+1;p<program.size();p++){
	if(program[p] >= 0.0f){
	  const float ratio = (program[p] - program[prevPoint])/(p - prevPoint);
	  for(unsigned int i=prevPoint+1;i<p;i++)
	    program[i] = program[prevPoint] + ratio*(i - prevPoint);
	  prevPoint = p;
	  break;
	}
	else if(p+1 >= program.size()){ // last iteration of the for loop
	  // we couldn't find any more positive elements..
	  for(unsigned int i=prevPoint+1;i<program.size();i++)
	    program[i] = program[prevPoint];
	  
	  prevPoint = program.size();
	}
      }
    }
    
    return true;
  }
  
  
  
} /* namespace resonanz */
} /* namespace whiteice */
