/*
 * ResonanzEngine.cpp
 *
 *  Created on: 13.6.2015
 *  Edited: 2021, 2022
 *      Author: Tomas Ukkonen
 */

#include "ResonanzEngine.h"
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <set>

#include <cmath>
#include <math.h>
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>

#include <dinrhiw.h>

#include "Log.h"
#include "NMCFile.h"


#include "NoEEGDevice.h"
#include "RandomEEG.h"

#ifndef EMOTIV_INSIGHT
// Enables experimental Emotiv Insight code
#define EMOTIV_INSIGHT
#endif

#ifdef LIGHTSTONE
#include "LightstoneDevice.h"
#endif

#ifdef EMOTIV_INSIGHT
//#include "EmotivInsightStub.h"
//#include "EmotivInsightPipeServer.h"
#ifdef _WIN32
#include "EmotivInsight.h"
#endif
#endif

#include "MuseOSC.h"
#include "MuseOSC4.h"

#include "FMSoundSynthesis.h"

#include "IsochronicSoundSynthesis.h"

#include "SDLTheora.h"
#include "SDLAVCodec.h"

#include "pictureFeatureVector.h"

#include "hermitecurve.h"

#include "timing.h"


#ifdef _WIN32
#include <windows.h>
#endif

namespace whiteice {
namespace resonanz {

  // FIXME: numDeviceChannels is NOT USED BY CODE AND SHOULD BE REMOVED FROM PARAMETERS
ResonanzEngine::ResonanzEngine(const unsigned int numDeviceChannels) 
{        
  logging.info("ResonanzEngine ctor starting");
  
  std::lock_guard<std::mutex> lock(thread_mutex);
  
  logging.info("ResonanzEngine() ctor started");
  
  // initializes random number generation here (again) this is needed?
  // so that JNI implementation gets different random numbers and experiments don't repeat each other..
  
  srand(rng.rand()); // initializes using RDRAND if availabe
  // otherwise uses just another value from rand()
  
  engine_setStatus("resonanz-engine: starting..");
  
  video = nullptr;
  eeg   = nullptr;
  synth = nullptr;
  mic   = nullptr;
  
  workerThread = nullptr;
  thread_is_running = true;
  
  {
    std::lock_guard<std::mutex> lock(command_mutex);
    incomingCommand = nullptr;
    currentCommand.command = ResonanzCommand::CMD_DO_NOTHING;
    currentCommand.showScreen = false;
  }
  
  {
    std::lock_guard<std::mutex> lock(eeg_mutex);
    eeg = new NoEEGDevice(numDeviceChannels);
    eegDeviceType = ResonanzEngine::RE_EEG_NO_DEVICE;
    
    std::vector<unsigned int> nnArchitecture;
    
    nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
    
    for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
      nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS));
      nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
    }
    nnArchitecture.push_back(eeg->getNumberOfSignals());
    
    nn = new whiteice::nnetwork<>(nnArchitecture);
    nn->setNonlinearity(whiteice::nnetwork<>::rectifier);
    nn->setNonlinearity(nn->getLayers()-1, whiteice::nnetwork<>::pureLinear);
    nn->setResidual(true);


    nnArchitecture.clear();
    nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
    for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
      nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS));
      nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
    }
    nnArchitecture.push_back(eeg->getNumberOfSignals());
    
    nnkey = new whiteice::nnetwork<>(nnArchitecture);
    nnkey->setNonlinearity(whiteice::nnetwork<>::rectifier);
    nnkey->setNonlinearity(nn->getLayers()-1, whiteice::nnetwork<>::pureLinear);
    nnkey->setResidual(true);

    
    // creates dummy synth neural network
    const int synth_number_of_parameters = 3;
    
    nnArchitecture.clear();
    nnArchitecture.push_back(eeg->getNumberOfSignals() + 2*synth_number_of_parameters + HMM_NUM_CLUSTERS);
    
    for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
      nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + 2*synth_number_of_parameters + HMM_NUM_CLUSTERS));
      nnArchitecture.push_back(eeg->getNumberOfSignals() + 2*synth_number_of_parameters + HMM_NUM_CLUSTERS);
    }
    nnArchitecture.push_back(eeg->getNumberOfSignals());
    
    nnsynth = new whiteice::nnetwork<>(nnArchitecture);
    nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
    nnsynth->setNonlinearity(nnsynth->getLayers()-1, whiteice::nnetwork<>::pureLinear);
    nnsynth->setResidual(true);
  }
	
  
  thread_initialized = false;
  keypressed = false;
  
  // starts updater thread thread
  workerThread = new std::thread(&ResonanzEngine::engine_loop, this);
  workerThread->detach();
  
#ifndef _WIN32	
  // for some reason this leads to DEADLOCK on Windows ???
  
  // waits for thread to initialize itself properly
  while(thread_initialized == false){
    logging.info("ResonanzEngine ctor waiting worker thread to init");
    std::chrono::milliseconds duration(1000); // 1000ms (thread sleep/working period is something like < 100ms)
    std::this_thread::sleep_for(duration);
  }
#endif

  logging.info("ResonanzEngine ctor finished");
}

ResonanzEngine::~ResonanzEngine()
{
  std::lock_guard<std::mutex> lock(thread_mutex);
  
  engine_setStatus("resonanz-engine: shutdown..");
  
  thread_is_running = false;
  if(workerThread == nullptr)
    return; // no thread is running
  
  // waits for thread to stop
  std::chrono::milliseconds duration(1000); // 1000ms (thread sleep/working period is something like < 100ms)
  std::this_thread::sleep_for(duration);
  
  // deletes thread whether it is still running or not
  delete workerThread;
  workerThread = nullptr;
  
  if(eeg != nullptr){
    std::lock_guard<std::mutex> lock(eeg_mutex);
    delete eeg;
    eeg = nullptr;
  }

  if(hmmUpdator){
    hmmUpdator->stop();
    delete hmmUpdator;
    hmmUpdator = nullptr;
  }

  if(kmeans && hmmUpdator == nullptr){
    delete kmeans;
    kmeans = nullptr;
  }

  if(hmm && hmmUpdator == nullptr){
    delete hmm;
    hmm = nullptr;
  }

  if(nn){
    delete nn;
    nn = nullptr;
  }

  if(nnkey){
    delete nnkey;
    nnkey = nullptr;
  }

  if(nnsynth){
    delete nnsynth;
    nnsynth = nullptr;
  }

  if(video){
    delete video;
    video = nullptr;
  }

  if(mic){
    delete mic;
    mic = nullptr;
  }

  if(synth){
    delete synth;
    synth = nullptr;
  }

  if(incomingCommand){
    delete incomingCommand;
    incomingCommand = nullptr;
  }
  
  engine_setStatus("resonanz-engine: halted");
}


// what resonanz is doing right now [especially interesting if we are optimizing model]
std::string ResonanzEngine::getEngineStatus() throw()
{
  std::lock_guard<std::mutex> lock(status_mutex);
  return engineState;
}

// resets resonanz-engine (worker thread stop and recreation)
bool ResonanzEngine::reset() throw()
{
  try{
    std::lock_guard<std::mutex> lock(thread_mutex);
    
    engine_setStatus("resonanz-engine: restarting..");
    
    if(thread_is_running || workerThread != nullptr){ // thread appears to be running
      thread_is_running = false;
      
      // waits for thread to stop
      std::chrono::milliseconds duration(1000); // 1000ms (thread sleep/working period is something like < 100ms)
      std::this_thread::sleep_for(duration);
      
      // deletes thread whether it is still running or not
      delete workerThread;
    }
    
    {
      std::lock_guard<std::mutex> lock(command_mutex);
      if(incomingCommand != nullptr) delete incomingCommand;
      incomingCommand = nullptr;
      currentCommand.command = ResonanzCommand::CMD_DO_NOTHING;
      currentCommand.showScreen = false;
    }
    
    workerThread = nullptr;
    thread_is_running = true;
    
    // starts updater thread thread
    workerThread = new std::thread(&ResonanzEngine::engine_loop, this);
    workerThread->detach();
    
    return true;
  }
  catch(std::exception& e){ return false; }
}

bool ResonanzEngine::cmdDoNothing(bool showScreen)
{
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  incomingCommand->command = ResonanzCommand::CMD_DO_NOTHING;
  incomingCommand->showScreen = showScreen;
  incomingCommand->pictureDir = "";
  incomingCommand->keywordsFile = "";
  incomingCommand->modelDir = "";
  
  return true;
}


bool ResonanzEngine::cmdRandom(const std::string& pictureDir, const std::string& keywordsFile,
			       const std::string& audioFile,
			       bool saveVideo) throw()
{
  if(pictureDir.length() <= 0 || keywordsFile.length() <= 0)
    return false;
  
  // TODO check that those directories and files actually exist
  
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  incomingCommand->command = ResonanzCommand::CMD_DO_RANDOM;
  incomingCommand->showScreen = true;
  incomingCommand->pictureDir = pictureDir;
  incomingCommand->keywordsFile = keywordsFile;
  incomingCommand->modelDir = "";
  incomingCommand->saveVideo = saveVideo;
  incomingCommand->audioFile = audioFile;
  
  return true;
}


bool ResonanzEngine::cmdMeasure(const std::string& pictureDir, const std::string& keywordsFile, const std::string& modelDir) throw()
{
  if(pictureDir.length() <= 0 || keywordsFile.length() <= 0 || modelDir.length() <= 0)
    return false;

  // TODO check that those directories and files actually exist
  
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  incomingCommand->command = ResonanzCommand::CMD_DO_MEASURE;
  incomingCommand->showScreen = true;
  incomingCommand->pictureDir = pictureDir;
  incomingCommand->keywordsFile = keywordsFile;
  incomingCommand->modelDir = modelDir;
  
  return true;
}


bool ResonanzEngine::cmdOptimizeModel(const std::string& pictureDir, const std::string& keywordsFile, const std::string& modelDir) throw()
{
  if(modelDir.length() <= 0)
    return false;
  
  // TODO check that those directories and files actually exist
  
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  incomingCommand->command = ResonanzCommand::CMD_DO_OPTIMIZE;
  incomingCommand->showScreen = false;
  incomingCommand->pictureDir = pictureDir;
  incomingCommand->keywordsFile = keywordsFile;
  incomingCommand->modelDir = modelDir;
  
  return true;
}


bool ResonanzEngine::cmdMeasureProgram(const std::string& mediaFile,
			const std::vector<std::string>& signalNames,
			const unsigned int programLengthTicks) throw()
{
  // could do more checks here but JNI code calling this SHOULD WORK CORRECTLY SO I DON'T
  
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  incomingCommand->command = ResonanzCommand::CMD_DO_MEASURE_PROGRAM;
  incomingCommand->showScreen = true;
  incomingCommand->audioFile = mediaFile;
  incomingCommand->signalName = signalNames;
  incomingCommand->blindMonteCarlo = false;
  incomingCommand->saveVideo = false;
  incomingCommand->programLengthTicks = programLengthTicks;
  
  return true;
}


bool ResonanzEngine::cmdExecuteProgram
(const std::string& pictureDir,
 const std::string& keywordsFile, const std::string& modelDir,
 const std::string& audioFile,
 const std::vector<std::string>& targetSignal,
 const std::vector< std::vector<float> >& program,
 bool blindMonteCarlo,
 bool saveVideo) throw()
{
  if(targetSignal.size() != program.size())
    return false;
  
  if(targetSignal.size() <= 0)
    return false;
  
  for(unsigned int i=0;i<targetSignal.size();i++){
    if(targetSignal[i].size() <= 0)
      return false;
    
    if(program[i].size() <= 0)
      return false;
    
    if(program[i].size() != program[0].size())
      return false;
  }
  
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  // interpolation of missing (negative) values between value points:
  // uses NMCFile functionality for this
  
  auto programcopy = program;
  
  for(auto& p : programcopy)
    NMCFile::interpolateProgram(p);
  
  incomingCommand->command = ResonanzCommand::CMD_DO_EXECUTE;
  incomingCommand->showScreen = true;
  incomingCommand->pictureDir = pictureDir;
  incomingCommand->keywordsFile = keywordsFile;
  incomingCommand->modelDir = modelDir;
  incomingCommand->audioFile = audioFile;
  incomingCommand->signalName = targetSignal;
  incomingCommand->programValues = programcopy;
  incomingCommand->blindMonteCarlo = blindMonteCarlo;
  incomingCommand->saveVideo = saveVideo;
  
  return true;
}



bool ResonanzEngine::cmdStopCommand() throw()
{
  std::lock_guard<std::mutex> lock(command_mutex);
  if(incomingCommand != nullptr) delete incomingCommand;
  incomingCommand = new ResonanzCommand();
  
  incomingCommand->command = ResonanzCommand::CMD_DO_NOTHING;
  incomingCommand->showScreen = false;
  incomingCommand->pictureDir = "";
  incomingCommand->keywordsFile = "";
  incomingCommand->modelDir = "";
  
  return true;
}


bool ResonanzEngine::isBusy() throw()
{
  if(currentCommand.command == ResonanzCommand::CMD_DO_NOTHING){
    if(incomingCommand != nullptr){
      logging.info("ResonanzEngine::isBusy() = true");
      return true; // there is incoming work to be processed
    }
    else{
      logging.info("ResonanzEngine::isBusy() = false");
      return false;
    }
  }
  else{
    logging.info("ResonanzEngine::isBusy() = true");
    return true;
  }
}


/**
 * has a key been pressed since the latest check?
 *
 */
bool ResonanzEngine::keypress(){
  std::lock_guard<std::mutex> lock(keypress_mutex);
  if(keypressed){
    keypressed = false;
    logging.info("ResonanzEngine::keypress() = true");
    return true;
  }
  else return false;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


bool ResonanzEngine::setEEGDeviceType(int deviceNumber)
{
  std::lock_guard<std::mutex> lock1(eeg_mutex);
  
  try{
    if(eegDeviceType == deviceNumber)
      return true; // nothing to do
    
    std::lock_guard<std::mutex> lock2(command_mutex);
    
    if(currentCommand.command != ResonanzCommand::CMD_DO_NOTHING)
      return false; // can only change EEG in inactive state
    
    if(deviceNumber == ResonanzEngine::RE_EEG_NO_DEVICE){
      if(eeg != nullptr) delete eeg;
      eeg = new NoEEGDevice();
    }
    else if(deviceNumber == ResonanzEngine::RE_EEG_RANDOM_DEVICE){
      if(eeg != nullptr) delete eeg;
      eeg = new RandomEEG();
    }
#ifdef EMOTIV_INSIGHT
#ifdef _WIN32
    else if(deviceNumber == ResonanzEngine::RE_EEG_EMOTIV_INSIGHT_DEVICE){
      if(eeg != nullptr) delete eeg;
      //eeg = new EmotivInsightPipeServer("\\\\.\\pipe\\emotiv-insight-data");
      eeg = new EmotivInsight();
    }
#endif
#endif
    else if(deviceNumber == ResonanzEngine::RE_EEG_IA_MUSE_DEVICE){
      if(eeg != nullptr) delete eeg;
      eeg = new MuseOSC(musePort); // 4545

      int counter = 0;

      while(counter < 10){
	millisleep(2000); // gives engine time connect MuseOSC object to UDP stream..
	if(eeg->connectionOk()) break;
	counter++;

	printf("Waiting connection to Muse OSC UDP server (localhost:%d)..\n", musePort);
	fflush(stdout);
      }


#if 0
      // reloads and creates databases [do we need to refresh models too??]
      if(engine_loadDatabase(latestModelDir) == false){
	logging.error("engine_loadDatabase() FAILED after EEG device change.");
      }
      else
	logging.error("engine_loadDatabase() SUCCESS after EEG device change.");
#endif
      
    }
    else if(deviceNumber == ResonanzEngine::RE_EEG_IA_MUSE_4CH_DEVICE){
      if(eeg != nullptr) delete eeg;
      eeg = new MuseOSC4(musePort); // 4545

      int counter = 0;

      while(counter < 10){
	millisleep(2000); // gives engine time connect MuseOSC object to UDP stream..
	if(eeg->connectionOk()) break;
	counter++;

	printf("Waiting connection to Muse OSC UDP server (localhost:%d)..\n", musePort);
	fflush(stdout);
      }

#if 0
      // reloads and creates databases
      if(engine_loadDatabase(latestModelDir) == false){
	logging.error("engine_loadDatabase() FAILED after EEG device change.");
      }
      else
	logging.error("engine_loadDatabase() SUCCESS after EEG device change.");
#endif
      
    }
#ifdef LIGHTSTONE
    else if(deviceNumber == ResonanzEngine::RE_WD_LIGHTSTONE){
      if(eeg != nullptr) delete eeg;
      eeg = new LightstoneDevice();
    }
#endif
    else{
      return false; // unknown device
    }
    
    // updates neural network model according to signal numbers of the EEG device
    {
      std::vector<unsigned int> nnArchitecture;
      
      nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
      for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS));
	nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
      }
      nnArchitecture.push_back(eeg->getNumberOfSignals());
      
      if(nn != nullptr) delete nn;
      
      nn = new whiteice::nnetwork<>(nnArchitecture);
      nn->setNonlinearity(whiteice::nnetwork<>::rectifier);
      nn->setNonlinearity(nn->getLayers()-1,
			  whiteice::nnetwork<>::pureLinear);
      nn->setResidual(true);
      

      nnArchitecture.clear();
      
      nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
      for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS));
	nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
      }
      nnArchitecture.push_back(eeg->getNumberOfSignals());
      
      if(nnkey != nullptr) delete nnkey;
      
      nnkey = new whiteice::nnetwork<>(nnArchitecture);
      nnkey->setNonlinearity(whiteice::nnetwork<>::rectifier);
      nnkey->setNonlinearity(nn->getLayers()-1,
			  whiteice::nnetwork<>::pureLinear);
      nnkey->setResidual(true);

      
      nnArchitecture.clear();
      
      if(synth){
	// nnsynth(synthBefore, synthProposed, currentEEG) = dEEG/dt (predictedEEG = currentEEG + dEEG/dT * TIMESTEP)
	
	nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				 2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
	
	for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	  nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*
				   (eeg->getNumberOfSignals() + 
				    2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS));
	  nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				   2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
	}
	
	nnArchitecture.push_back(eeg->getNumberOfSignals());
	
	if(nnsynth != nullptr) delete nnsynth;
	
	nnsynth = new whiteice::nnetwork<>(nnArchitecture);
	nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
	nnsynth->setNonlinearity(nnsynth->getLayers()-1,
				 whiteice::nnetwork<>::pureLinear);
	nnsynth->setResidual(true);
      }
      else{
	const int synth_number_of_parameters = 6;
	
	nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				 2*synth_number_of_parameters + HMM_NUM_CLUSTERS);
	for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	  nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*
				   (eeg->getNumberOfSignals() + 
				    2*synth_number_of_parameters) + HMM_NUM_CLUSTERS);
	  nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				   2*synth_number_of_parameters + HMM_NUM_CLUSTERS);
	}
	nnArchitecture.push_back(eeg->getNumberOfSignals());
	
	if(nnsynth != nullptr) delete nnsynth;
	
	nnsynth = new whiteice::nnetwork<>(nnArchitecture);
	nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
	nnsynth->setNonlinearity(nnsynth->getLayers()-1,
				 whiteice::nnetwork<>::pureLinear);
	nnsynth->setResidual(true);
      }
    }
    
    eegDeviceType = deviceNumber;
    
    return true;
  }
  catch(std::exception& e){
    std::string error = "setEEGDeviceType() internal error: ";
    error += e.what();
    
    logging.warn(error);
    
    eegDeviceType = ResonanzEngine::RE_EEG_NO_DEVICE;
    eeg = new NoEEGDevice();
    
    {
      std::vector<unsigned int> nnArchitecture;
      
      nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
      
      for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS));
	nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
      }
      
      nnArchitecture.push_back(eeg->getNumberOfSignals());
      
      if(nn != nullptr) delete nn;
      
      nn = new whiteice::nnetwork<>(nnArchitecture);
      nn->setNonlinearity(whiteice::nnetwork<>::rectifier);
      nn->setNonlinearity(nn->getLayers()-1,
			  whiteice::nnetwork<>::pureLinear);
      nn->setResidual(true);


      nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
      
      for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS));
	nnArchitecture.push_back(eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
      }
      
      nnArchitecture.push_back(eeg->getNumberOfSignals());
      
      if(nnkey != nullptr) delete nnkey;
      
      nnkey = new whiteice::nnetwork<>(nnArchitecture);
      nnkey->setNonlinearity(whiteice::nnetwork<>::rectifier);
      nnkey->setNonlinearity(nn->getLayers()-1,
			  whiteice::nnetwork<>::pureLinear);
      nnkey->setResidual(true);
      
      
      nnArchitecture.clear();
      
      if(synth){			  
	nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				 2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
	
	for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	  nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*
				   (eeg->getNumberOfSignals() + 
				    2*synth->getNumberOfParameters() +
				    HMM_NUM_CLUSTERS));
	  nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				   2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
	}
	
	nnArchitecture.push_back(eeg->getNumberOfSignals());
	
	if(nnsynth != nullptr) delete nnsynth;
	
	nnsynth = new whiteice::nnetwork<>(nnArchitecture);
	nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
	nnsynth->setNonlinearity(nnsynth->getLayers()-1,
				 whiteice::nnetwork<>::pureLinear);
	nnsynth->setResidual(true);
      }
      else{
	const int synth_number_of_parameters = 6;
	
	nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				 2*synth_number_of_parameters + HMM_NUM_CLUSTERS);
	
	for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	  nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*
				   (eeg->getNumberOfSignals() + 
				    2*synth_number_of_parameters +
				    HMM_NUM_CLUSTERS));
	  nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				   2*synth_number_of_parameters + HMM_NUM_CLUSTERS);
	}
	
	nnArchitecture.push_back(eeg->getNumberOfSignals());
	
	if(nnsynth != nullptr) delete nnsynth;
	
	nnsynth = new whiteice::nnetwork<>(nnArchitecture);
	nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
	nnsynth->setNonlinearity(nnsynth->getLayers()-1,
				 whiteice::nnetwork<>::pureLinear);
	nnsynth->setResidual(true);
      }
    }
    
    return false;
  }
}


int ResonanzEngine::getEEGDeviceType()
{
  std::lock_guard<std::mutex> lock(eeg_mutex);
  
  if(eeg != nullptr)
    return eegDeviceType;
  else
    return RE_EEG_NO_DEVICE;
}


const DataSource& ResonanzEngine::getDevice() const
{
  assert(eeg != nullptr);
  
  return (*eeg);
}


void ResonanzEngine::getEEGDeviceStatus(std::string& status)
{
  std::lock_guard<std::mutex> lock(eeg_mutex);
  
  if(eeg != nullptr){
    if(eeg->connectionOk()){
      std::vector<float> values;
      eeg->data(values);
      
      if(values.size() > 0){
	status = "Device is connected.\n";
	
	status = status + "Latest measurements: ";
	
	char buffer[80];
	for(unsigned int i=0;i<values.size();i++){
	  snprintf(buffer, 80, "%.2f ", values[i]);
	  status = status + buffer;
	}
	
	status = status + ".";
      }
      else{
	status = "Device is NOT connected.";
      }
    }
    else{
      status = "Device is NOT connected.";
    }
  }
  else{
    status = "No device.";
  }
}


bool ResonanzEngine::setParameter(const std::string& parameter, const std::string& value)
{
  {
    char buffer[256];
    snprintf(buffer, 256, "resonanz-engine::setParameter: %s = %s", parameter.c_str(), value.c_str());
    logging.info(buffer);
  }
  
  if(parameter == "pca-preprocess"){
    if(value == "true"){
      pcaPreprocess = true;
      return true;
    }
    else if(value == "false"){
      pcaPreprocess = false;
      return true;
    }
    else return false;
    
  }
  else if(parameter == "use-bayesian-nnetwork"){
    if(value == "true"){
      use_bayesian_nnetwork = true;
      return true;
    }
    else if(value == "false"){
      use_bayesian_nnetwork = false;
      return true;
    }
    else return false;
  }
  else if(parameter == "show-top-results"){
    this->SHOW_TOP_RESULTS = 1;
    this->SHOW_TOP_RESULTS = atoi(value.c_str());
    if(this->SHOW_TOP_RESULTS <= 0)
      this->SHOW_TOP_RESULTS = 1;
    
    return true;
  }
  else if(parameter == "use-data-rbf"){
    if(value == "true"){
      dataRBFmodel = true;
      return true;
    }
    else if(value == "false"){
      dataRBFmodel = false;
      return true;
    }
    else return false;
  }
  else if(parameter == "optimize-synth-only"){
    if(value == "true"){
      optimizeSynthOnly = true;
      return true;
    }
    else if(value == "false"){
      optimizeSynthOnly = false;
      return true;
    }
    else return false;
  }
  else if(parameter == "fullscreen"){
    if(value == "true"){
      fullscreen = true;
      return true;
    }
    else if(value == "false"){
      fullscreen = false;
      return true;
    }
    else return false;
  }
  else if(parameter == "loop"){
    if(value == "true"){
      loopMode = true;
      return true;
    }
    else if(value == "false"){
      loopMode = false;
      return true;
    }
    else return false;
  }
  else if(parameter == "debug-messages"){
    if(value == "true"){
      whiteice::logging.setPrintOutput(true);
    }
    else if(value == "false"){
      whiteice::logging.setPrintOutput(false);
    }
  }
  else if(parameter == "random-programs"){
    if(value == "true"){
      randomPrograms = true;
    }
    else if(value == "false"){
      randomPrograms = false;
    }
  }
  else if(parameter == "muse-port"){
    musePort = (unsigned int)atoi(value.c_str());
    std::cout << "MUSE OSC PORT IS NOW: " << musePort << std::endl;
  }
  else{
    return false;
  }
  
  return false;
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// main worker thread loop to execute commands

void ResonanzEngine::engine_loop()
{
  logging.info("engine_loop() started");
  
  
#ifdef _WIN32
  {
    // set process priority
    logging.info("windows os: setting resonanz thread high priority");
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
  }
#endif
  
  long long tickStartTime = 0;
  {
    auto t1 = std::chrono::system_clock::now().time_since_epoch();
    auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
    
    tickStartTime = t1ms;
  }
  
  long long lastTickProcessed = -1;
  tick = 0;
  
  long long eegLastTickConnectionOk = tick;
  
  // later autodetected to good values based on display screen resolution
  SCREEN_WIDTH  = 800;
  SCREEN_HEIGHT = 600;
  
  const std::string fontname = "Vera.ttf";
  
  bnn = new bayesian_nnetwork<>();
  
  unsigned int currentPictureModel = 0;
  unsigned int currentKeywordModel = 0;
  unsigned int currentHMMModel = 0;
  bool soundModelCalculated = false;
  
  // model optimization ETA information
  whiteice::linear_ETA<float> optimizeETA;
  
  // used to execute program [targetting target values]
  const float programHz = 1.0; // 1 program step means 1 second
  std::vector< std::vector<float> > program;
  std::vector< std::vector<float> > programVar;
  programStarted = 0LL; // program has not been started
  long long lastProgramSecond = 0LL;
  unsigned int eegConnectionDownTime = 0;

  std::vector<float> distanceTarget; // distance of program to target value

  long long lastHMMStateUpdateMS = 0; // last time HMM model has been updated
  HMMstate = 0;
  
  std::vector<float> eegCurrent;
  
  
  // thread has started successfully
  thread_initialized = true;
  
  
  // tries to initialize SDL library functionality - and load the font
  {
    bool initialized = false;
    
    while(thread_is_running){
      try{
	if(engine_SDL_init(fontname)){
	  initialized = true;
	  break;
	}
      }
      catch(std::exception& e){ }
      
      engine_setStatus("resonanz-engine: re-trying to initialize graphics..");
      engine_sleep(1000);
    }
    
    if(thread_is_running == false){
      if(initialized) engine_SDL_deinit();
      thread_initialized = true; // should never happen/be needed..
      return;
    }
  }
  
  
  
  
  while(thread_is_running){

    bool tick_delay_sleep = false;
    
    // sleeps until there is a new engine tick
    while(lastTickProcessed >= tick){
      auto t1 = std::chrono::system_clock::now().time_since_epoch();
      auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
      
      auto currentTick = (t1ms - tickStartTime)/TICK_MS;

      // UPDATE HMM STATE
      {
	const auto timeSinceLastUpdateMS = ((long long)t1ms) - lastHMMStateUpdateMS;
	
	if(timeSinceLastUpdateMS >= MEASUREMODE_DELAY_MS){
	  // update HMM state here
	  std::lock_guard<std::mutex> lock(hmm_mutex); // NOT REALLY NEEDED FOR NOW

	  if(hmm != NULL && kmeans != NULL && eeg != NULL){
	  
	    math::vertex<> data;
	  
	    if(eeg->data(eegCurrent)){
	      data.resize(eegCurrent.size());

	      for(unsigned int i=0;i<data.size();i++)
		data[i] = eegCurrent[i];
	      
	      unsigned int dataCluster = kmeans->getClusterIndex(data);
	      unsigned int nextState = 0;
	      if(HMMstate >= HMM_NUM_CLUSTERS){
		HMMstate = hmm->sampleInitialHiddenState();
	      }
	      hmm->next_state(HMMstate, nextState, dataCluster);
	      HMMstate = nextState;
	    }
	    
	    lastHMMStateUpdateMS = (long long)t1ms;
	  }
	}
	
      }
      
      if(tick < currentTick){
	tick = currentTick;
      }
      else{
	tick_delay_sleep = true;
	engine_sleep(TICK_MS/20);
      }
    }
    
    lastTickProcessed = tick;
		
		
    ResonanzCommand prevCommand = currentCommand;
    
    
    {
      char buffer[80];

      sprintf(buffer, "resonanz-engine: prev command code: %d", prevCommand.command);
      
      logging.info(buffer);
    }
    
    
    if(engine_checkIncomingCommand() == true){
      logging.info("new engine command received");
      // we must make engine state transitions, state transfer from the previous command to the new command
      
      // state exit actions:
      if(prevCommand.command == ResonanzCommand::CMD_DO_RANDOM){
	// stop playing sound
	if(synth){
	  logging.info("stop synth");
	  synth->pause();
	  synth->reset();
	}
	
	// stop playing audio if needed
	if(prevCommand.audioFile.length() > 0){
	  logging.info("stop audio file playback");
	  engine_stopAudioFile();
	}

	// stops encoding if needed
	if(video != nullptr){
	  auto t1 = std::chrono::system_clock::now().time_since_epoch();
	  auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
	  
	  logging.info("stopping theora video encoding.");
	  
	  video->stopEncoding((unsigned long long)(t1ms - programStarted));
	  delete video;
	  video = nullptr;
	  programStarted = 0;
	}
	
      }
      else if(prevCommand.command == ResonanzCommand::CMD_DO_MEASURE){
	// stop playing sound
	if(synth){
	  synth->pause();
	  synth->reset();
	  logging.info("stop synth");
	}
	
	engine_setStatus("resonanz-engine: saving database..");
	if(engine_saveDatabase(prevCommand.modelDir) == false){
	  logging.error("saving database failed");
	}
	else{
	  logging.error("saving database successful");
	}
	
	keywordData.clear();
	pictureData.clear();
	eegData.clear();
	
      }
      else if(prevCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE){
	// stops computation if needed

	if(hmmUpdator != nullptr){
	  hmmUpdator->stop();
	  delete hmmUpdator;
	  hmmUpdator = nullptr;
	}
	
	if(hmm != nullptr && hmmUpdator == nullptr){
	  hmm->stopTrain();
	  delete hmm;
	  hmm = nullptr;
	}

	if(kmeans != nullptr && hmmUpdator == nullptr){
	  kmeans->stopTrain();
	  delete kmeans;
	  kmeans = nullptr;
	}
	
	if(optimizer != nullptr){
	  optimizer->stopComputation();
	  delete optimizer;
	  optimizer = nullptr;
	}
	
	if(bayes_optimizer != nullptr){
	  bayes_optimizer->stopSampler();
	  delete bayes_optimizer;
	  bayes_optimizer = nullptr;
	}

	// also saves database because preprocessing parameters may have changed
	if(engine_saveDatabase(prevCommand.modelDir) == false){
	  logging.error("saving database failed");
	}
	else{
	  logging.error("saving database successful");
	}
	
	// removes unnecessarily data structures from memory (measurements database) [no need to save it because it was not changed]
	keywordData.clear();
	pictureData.clear();
	eegData.clear();
      }
      else if(prevCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
	// stop playing sound
	if(synth){
	  synth->pause();
	  synth->reset();
	}

	if(hmmUpdator != nullptr){
	  hmmUpdator->stop();
	  delete hmmUpdator;
	  hmmUpdator = nullptr;
	}

	if(hmm != nullptr){
	  hmm->stopTrain(); // to be sure
	  delete hmm;
	  hmm = nullptr;
	}

	if(kmeans != nullptr){
	  kmeans->stopTrain(); // to be sure
	  delete kmeans;
	  kmeans = nullptr;
	}
	
	keywordData.clear();
	pictureData.clear();
	eegData.clear();
	keywordModels.clear();
	pictureModels.clear();

	if(prevCommand.audioFile.length() > 0){
	  logging.info("stop audio file playback");
	  engine_stopAudioFile();
	}

	if(mcsamples.size() > 0)
	  mcsamples.clear();
	
	// stops encoding if needed
	if(video != nullptr){
	  auto t1 = std::chrono::system_clock::now().time_since_epoch();
	  auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
	  
	  logging.info("stopping theora video encoding.");
	  
	  video->stopEncoding((unsigned long long)(t1ms - programStarted));
	  delete video;
	  video = nullptr;
	  programStarted = 0;
	}
      }
      else if(prevCommand.command == ResonanzCommand::CMD_DO_MEASURE_PROGRAM){
	
	// clears internal data structure
	rawMeasuredSignals.clear();
	
	if(prevCommand.audioFile.length() > 0)
	  engine_stopAudioFile();
      }
      
      // state exit/entry actions:
      {
	char buffer[80];
	
	sprintf(buffer, "resonanz-engine: current command code: %d", currentCommand.command);
	
	logging.info(buffer);
      }
      
      
      // checks if we want to have open graphics window and opens one if needed
      if(currentCommand.showScreen == true && prevCommand.showScreen == false){
	if(window != nullptr) SDL_DestroyWindow(window);
	
	SDL_DisplayMode mode;
	
	if(SDL_GetCurrentDisplayMode(0, &mode) == 0){
	  SCREEN_WIDTH = mode.w;
	  SCREEN_HEIGHT = mode.h;
	}
	
	if(fullscreen){
	  window = SDL_CreateWindow(windowTitle.c_str(),
				    SDL_WINDOWPOS_CENTERED,
				    SDL_WINDOWPOS_CENTERED,
				    SCREEN_WIDTH, SCREEN_HEIGHT,
				    SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
	}
	else{
	  window = SDL_CreateWindow(windowTitle.c_str(),
				    SDL_WINDOWPOS_CENTERED,
				    SDL_WINDOWPOS_CENTERED,
				    (3*SCREEN_WIDTH)/4, (3*SCREEN_HEIGHT)/4,
				    SDL_WINDOW_SHOWN);
	}
	
	if(window != nullptr){
	  SDL_GetWindowSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
	  if(font) TTF_CloseFont(font);
	  double fontSize = 100.0*sqrt(((float)(SCREEN_WIDTH*SCREEN_HEIGHT))/(640.0*480.0));
	  unsigned int fs = (unsigned int)fontSize;
	  if(fs <= 0) fs = 10;
	  
	  font = 0;
	  font = TTF_OpenFont(fontname.c_str(), fs);
	  
	  
	  SDL_Surface* icon = IMG_Load(iconFile.c_str());
	  if(icon != nullptr){
	    SDL_SetWindowIcon(window, icon);
	    SDL_FreeSurface(icon);
	  }
	  
	  SDL_Surface* surface = SDL_GetWindowSurface(window);
	  SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));
	  SDL_RaiseWindow(window);
	  // SDL_SetWindowGrab(window, SDL_TRUE);
	  SDL_UpdateWindowSurface(window);
	  SDL_RaiseWindow(window);
	  // SDL_SetWindowGrab(window, SDL_FALSE);
	}

      }
      else if(currentCommand.showScreen == true && prevCommand.showScreen == true){
	// just empties current window with blank (black) screen
	
	SDL_DisplayMode mode;
	
	if(SDL_GetCurrentDisplayMode(0, &mode) == 0){
	  SCREEN_WIDTH = mode.w;
	  SCREEN_HEIGHT = mode.h;
	}
	
	if(window == nullptr){
	  if(fullscreen){
	    window = SDL_CreateWindow(windowTitle.c_str(),
				      SDL_WINDOWPOS_CENTERED,
				      SDL_WINDOWPOS_CENTERED,
				      SCREEN_WIDTH, SCREEN_HEIGHT,
				      SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
	  }
	  else{
	    window = SDL_CreateWindow(windowTitle.c_str(),
				      SDL_WINDOWPOS_CENTERED,
				      SDL_WINDOWPOS_CENTERED,
				      (3*SCREEN_WIDTH)/4, (3*SCREEN_HEIGHT)/4,
				      SDL_WINDOW_SHOWN);
	  }
	}
	
	if(window != nullptr){
	  SDL_GetWindowSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
	  if(font) TTF_CloseFont(font);
	  double fontSize = 100.0*sqrt(((float)(SCREEN_WIDTH*SCREEN_HEIGHT))/(640.0*480.0));
	  unsigned int fs = (unsigned int)fontSize;
	  if(fs <= 0) fs = 10;
	  
	  font = 0;
	  font = TTF_OpenFont(fontname.c_str(), fs);
	  
	  SDL_Surface* icon = IMG_Load(iconFile.c_str());
	  if(icon != nullptr){
	    SDL_SetWindowIcon(window, icon);
	    SDL_FreeSurface(icon);
	  }
	  
	  SDL_Surface* surface = SDL_GetWindowSurface(window);
	  SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));
	  SDL_RaiseWindow(window);
	  // SDL_SetWindowGrab(window, SDL_TRUE);
	  SDL_UpdateWindowSurface(window);
	  SDL_RaiseWindow(window);
	  // SDL_SetWindowGrab(window, SDL_FALSE);
	}
	
      }
      else if(currentCommand.showScreen == false){
	if(window != nullptr) SDL_DestroyWindow(window);
	window = nullptr;
      }
      
      // state entry actions:

      if(currentCommand.command == ResonanzCommand::CMD_DO_MEASURE || currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
	std::lock_guard<std::mutex> lock(eeg_mutex);
	if(eeg->connectionOk() == false){
	  logging.warn("eeg: no connection to eeg hardware => aborting measure/execute command");
	  cmdDoNothing(false);
	  continue;
	}
      }

      // (re)loads media resources (pictures, keywords) if we want to do stimulation
      if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM ||
	 currentCommand.command == ResonanzCommand::CMD_DO_MEASURE ||
	 currentCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE ||
	 currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE)
      {
	engine_setStatus("resonanz-engine: loading media files..");
	
	bool loadData = (currentCommand.command != ResonanzCommand::CMD_DO_OPTIMIZE);
	
	if(engine_loadMedia(currentCommand.pictureDir, currentCommand.keywordsFile, loadData) == false){
	  logging.error("loading media files failed");
	}
	else{
	  char buffer[80];
	  snprintf(buffer, 80, "loading media files successful (%d keywords, %d pics)",
		   (int)keywords.size(), (int)pictures.size());
	  logging.info(buffer);
	}
      }
			
			
      // (re)-setups and initializes data structures used for measurements
      if(currentCommand.command == ResonanzCommand::CMD_DO_MEASURE ||
	 currentCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE ||
	 currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE)
      {
	engine_setStatus("resonanz-engine: loading database..");
	
	if(engine_loadDatabase(currentCommand.modelDir) == false)
	  logging.error("loading database files failed");
	else
	  logging.info("loading database files successful");
      }
      
      if(currentCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE){
	engine_setStatus("resonanz-engine: initializing prediction model optimization..");
	currentHMMModel = 0;
	currentPictureModel = 0;
	currentKeywordModel = 0;
	soundModelCalculated = false;
	
	if(this->use_bayesian_nnetwork)
	  logging.info("model optimization uses BAYESIAN UNCERTAINTY estimation through sampling");
	
	// checks there is enough data to do meaningful optimization
	bool aborted = false;
	
	for(unsigned int i=0;i<pictureData.size() && !aborted;i++){
	  if(pictureData[i].size(0) < 10){
	    engine_setStatus("resonanz-engine: less than 10 data points per picture/keyword => aborting optimization");
	    logging.warn("aborting model optimization command because of too little data (less than 10 samples per case)");
	    cmdDoNothing(false);
	    aborted = true;
	    break;
	  }
	}
	
	for(unsigned int i=0;i<keywordData.size() && !aborted;i++){
	  if(keywordData[i].size(0) < 10){
	    engine_setStatus("resonanz-engine: less than 10 data points per picture/keyword => aborting optimization");
	    logging.warn("aborting model optimization command because of too little data (less than 10 samples per case)");
	    cmdDoNothing(false);
	    aborted = true;
	    break;
	  }
	}
	
	if(eegData.size(0) < 500){
	  engine_setStatus("resonanz-engine: less than 500 data points for HMM brain state analysis => aborting optimization");
	  logging.warn("abortinh model optimization command because of too little data (less than 500 samples)");
	  cmdDoNothing(false);
	  aborted = true;
	  break;
	}
	
	if(synth){
	  if(synthData.size(0) < 10){
	    engine_setStatus("resonanz-engine: less than 10 data points per picture/keyword => aborting optimization");
	    logging.warn("aborting model optimization command because of too little data (less than 10 samples per case)");
	    cmdDoNothing(false);
	    aborted = true;
	    break;
	  }
	}
	
	if(aborted)
	  continue; // do not start executing any commands [recheck command input buffer and move back to do nothing command]
	
	optimizeETA.start(0.0f, 1.0f);
      }

			
      if(currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
	try{
	  engine_setStatus("resonanz-engine: loading prediction model..");
	  
	  if(engine_loadModels(currentCommand.modelDir) == false && dataRBFmodel == false){
	    logging.error("Couldn't load models from model dir: " + currentCommand.modelDir);
	    this->cmdStopCommand();
	    continue; // aborts initializing execute command
	  }

	  logging.info("Converting program (targets) to internal format..");
	  
	  // convert input command parameters into generic targets that are used to select target values
	  std::vector<std::string> names;
	  eeg->getSignalNames(names);
	  
	  // inits program values into "no program" values
	  program.resize(names.size());
	  for(auto& p : program){
	    p.resize(currentCommand.programValues[0].size());
	    for(unsigned int i=0;i<p.size();i++)
	      p[i] = 0.5f;
	  }
	  
	  programVar.resize(names.size());
	  for(auto& p : programVar){
	    p.resize(currentCommand.programValues[0].size());
	    for(unsigned int i=0;i<p.size();i++)
	      p[i] = 1000000.0f; // 1.000.000 very large value (near infinite) => can take any value
	  }
	  
	  for(unsigned int j=0;j<currentCommand.signalName.size();j++){
	    for(unsigned int n=0;n<names.size();n++){
	      if(names[n] == currentCommand.signalName[j]){ // finds a matching signal in a command
		for(unsigned int i=0;i<program[n].size();i++){
		  if(currentCommand.programValues[j][i] >= 0.0f){
		    program[n][i] = currentCommand.programValues[j][i];
		    programVar[n][i] = 1.0f; // "normal" variance
		  }
		}
	      }
	    }
	  }
	  
	  logging.info("Converting program (targets) to internal format.. DONE.");
	  
	  
	  // initializes blind monte carlo data structures
	  if(currentCommand.blindMonteCarlo){
	    logging.info("Blind Monte Carlo mode activated/initialization...");
	    mcsamples.clear();
	    
	    for(unsigned int i=0;i<MONTE_CARLO_SIZE;i++){
	      math::vertex<> u(names.size());
	      for(unsigned int j=0;j<u.size();j++)
		u[j] = rng.uniform(); // [0,1] valued signals sampled from [0,1]^D
	      mcsamples.push_back(u);
	    }
	  }
	  
	  
	  if(currentCommand.audioFile.length() > 0){
	    logging.info("play audio file");
	    engine_playAudioFile(currentCommand.audioFile);
	  }
	  
	  // starts measuring time for the execution of the program
	  
	  auto t0 = std::chrono::system_clock::now().time_since_epoch();
	  auto t0ms = std::chrono::duration_cast<std::chrono::milliseconds>(t0).count();
	  programStarted = t0ms;
	  lastProgramSecond = -1;
	  
	  // RMS performance error calculation
	  programRMS = 0.0f;
	  programRMS_N = 0;
	  
	  logging.info("Started executing neurostim program..");
					
	}
	catch(std::exception& e){
	  
	}
      }
      
      if(currentCommand.command == ResonanzCommand::CMD_DO_MEASURE_PROGRAM){
	// checks command signal names maps to some eeg signals
	std::vector<std::string> names;
	eeg->getSignalNames(names);
	
	unsigned int matches = 0;
	for(auto& n : names){
	  for(auto& m : currentCommand.signalName){
	    if(n == m){ // string comparion
	      matches++;
	    }
	  }
	}
	
	if(matches == 0){
	  logging.warn("resonanz-engine: measure program signal names don't match to device signals");
	  
	  this->cmdDoNothing(false); // abort
	  continue;
	}
	
	rawMeasuredSignals.resize(names.size()); // setups data structure for measurements
	
	// invalidates old program
	this->invalidateMeasuredProgram();
	
	// starts measuring time for the execution of the program
	
	auto t0 = std::chrono::system_clock::now().time_since_epoch();
	auto t0ms = std::chrono::duration_cast<std::chrono::milliseconds>(t0).count();
	programStarted = t0ms;
	lastProgramSecond = -1;
	eegConnectionDownTime = 0;
	
				// currently just plays audio and shows blank screen
	if(currentCommand.audioFile.length() > 0)
	  engine_playAudioFile(currentCommand.audioFile);
	
	// => ready to measure
      }
      
      
      if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM || currentCommand.command == ResonanzCommand::CMD_DO_MEASURE ||
	 currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
	engine_setStatus("resonanz-engine: starting sound synthesis..");
	
	if(currentCommand.audioFile.length() <= 0){
	  if(synth){
	    if(synth->play() == false){
	      logging.error("starting sound synthesis failed");
	    }
	    else{
	      logging.info("starting sound synthesis..OK");
	    }
	  }
	}
      }
      
      
      if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM || currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
	if(currentCommand.saveVideo){
	  logging.info("Starting video encoder (theora)..");
	  
	  // starts video encoder
	  //video = new SDLTheora(0.50f); // 50% quality
	  video = new SDLAVCodec(0.50f); // 50% quality
	  
	  if(video->startEncoding("neurostim.mp4",
				  SCREEN_WIDTH, SCREEN_HEIGHT) == false) // "neurostim.ogv"
	    logging.error("starting theora video encoder failed");
	  else
	    logging.info("started theora video encoding");
	}
	else{
	  // do not save video
	  video = nullptr;
	}
      }

      
      if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM){
	auto t0 = std::chrono::system_clock::now().time_since_epoch();
	auto t0ms = std::chrono::duration_cast<std::chrono::milliseconds>(t0).count();
	programStarted = t0ms;
	lastProgramSecond = -1;
	
	if(currentCommand.audioFile.length() > 0){
	  logging.info("play audio file");
	  engine_playAudioFile(currentCommand.audioFile);
	}
      }
      
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // executes current command
    
    if(currentCommand.command == ResonanzCommand::CMD_DO_NOTHING){
      engine_setStatus("resonanz-engine: sleeping..");
      
      engine_pollEvents(); // polls for events
      engine_updateScreen(); // always updates window if it exists
    }
    else if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM){
      engine_setStatus("resonanz-engine: showing random examples..");
      
      engine_stopHibernation();
      
      if(pictures.size() > 0){
	if(keywords.size() > 0){
	  auto& key = currentKey;
	  auto& pic = currentPic;
	  
	  if(tick - latestKeyPicChangeTick > SHOWTIME_TICKS){
	    key = rng.rand() % keywords.size();
	    pic = rng.rand() % pictures.size();
	    
	    latestKeyPicChangeTick = tick;
	  }
	  
	  std::vector<float> sndparams;
	  
	  if(synth != NULL){
	    sndparams.resize(synth->getNumberOfParameters());
	    for(unsigned int i=0;i<sndparams.size();i++)
	      sndparams[i] = rng.uniform().c[0];
	  }
	  
	  if(engine_showScreen(keywords[key], pic, sndparams) == false)
	    logging.warn("random stimulus: engine_showScreen() failed.");
	  else
	    logging.warn("random stimulus: engine_showScreen() success.");
	}
	else{
	  auto& pic = currentPic;
	  
	  if(tick - latestKeyPicChangeTick > SHOWTIME_TICKS){
	    pic = rng.rand() % pictures.size();
	    
	    latestKeyPicChangeTick = tick;
	  }
	  
	  std::vector<float> sndparams;

	  if(synth != NULL){
	    sndparams.resize(synth->getNumberOfParameters());
	    for(unsigned int i=0;i<sndparams.size();i++)
	      sndparams[i] = rng.uniform().c[0];
	  }
	  
	  if(engine_showScreen(" ", pic, sndparams) == false)
	    logging.warn("random stimulus: engine_showScreen() failed.");
	  else
	    logging.warn("random stimulus: engine_showScreen() success.");
	}
      }
      
      engine_pollEvents(); // polls for events
      engine_updateScreen(); // always updates window if it exists
    }
    else if(currentCommand.command == ResonanzCommand::CMD_DO_MEASURE){
      engine_setStatus("resonanz-engine: measuring eeg-responses..");
      
      if(eeg->connectionOk() == false){
	eegConnectionDownTime = TICK_MS*(tick - eegLastTickConnectionOk);
	
	if(eegConnectionDownTime >= 2000){
	  logging.info("measure command: eeg connection failed => aborting measurements");
	  cmdDoNothing(false); // new command: stops and starts idling
	}
	
	engine_pollEvents(); // polls for events
	engine_updateScreen(); // always updates window if it exists
	
	continue;
      }
      else{
	eegConnectionDownTime = 0;
	eegLastTickConnectionOk = tick;
      }
      
      
      engine_stopHibernation();
      
      if(keywords.size() > 0 && pictures.size() > 0){
	unsigned int key = rng.rand() % keywords.size();
	unsigned int pic = rng.rand() % pictures.size();
	
	std::vector<float> eegBefore;
	std::vector<float> eegAfter;
	
	std::vector<float> synthBefore;
	std::vector<float> synthCurrent;
	
	if(synth){
	  synth->getParameters(synthBefore);
	  
	  synthCurrent.resize(synth->getNumberOfParameters());
	  
	  if(rng.uniform() < 0.20f){
	    // total random sound
	    for(unsigned int i=0;i<synthCurrent.size();i++)
	      synthCurrent[i] = rng.uniform().c[0];
	  }
	  else{
	    // generates something similar (adds random noise to current parameters)
	    for(unsigned int i=0;i<synthCurrent.size();i++){
	      synthCurrent[i] = synthBefore[i] + rng.normal().c[0]*0.20f;
	      if(synthCurrent[i] <= 0.0f) synthCurrent[i] = 0.0f;
	      else if(synthCurrent[i] >= 1.0f) synthCurrent[i] = 1.0f;
	    }
	  }
	  
	}
	
	eeg->data(eegBefore);
	
	engine_showScreen(keywords[key], pic, synthCurrent);
	engine_updateScreen(); // always updates window if it exists
	engine_sleep(MEASUREMODE_DELAY_MS);
	
	eeg->data(eegAfter);
	
	engine_pollEvents();
	
	if(engine_storeMeasurement(pic, key, eegBefore, eegAfter,
				   synthBefore, synthCurrent) == false)
	  logging.error("Store measurement FAILED");
      }
      else if(pictures.size() > 0){
	unsigned int pic = rng.rand() % pictures.size();
	
	std::vector<float> eegBefore;
	std::vector<float> eegAfter;
	
	std::vector<float> synthBefore;
	std::vector<float> synthCurrent;
	
	if(synth){
	  synth->getParameters(synthBefore);
	  
	  synthCurrent.resize(synth->getNumberOfParameters());
	  
	  if(rng.uniform() < 0.20f){
	    // total random sound
	    for(unsigned int i=0;i<synthCurrent.size();i++)
	      synthCurrent[i] = rng.uniform().c[0];
	  }
	  else{
	    // generates something similar (adds random noise to current parameters)
	    for(unsigned int i=0;i<synthCurrent.size();i++){
	      synthCurrent[i] = synthBefore[i] + rng.normal().c[0]*0.20f;
	      if(synthCurrent[i] <= 0.0f) synthCurrent[i] = 0.0f;
	      else if(synthCurrent[i] >= 1.0f) synthCurrent[i] = 1.0f;
	    }
	  }
	}
	
	eeg->data(eegBefore);
	
	engine_showScreen(" ", pic, synthCurrent);
	engine_updateScreen(); // always updates window if it exists
	engine_sleep(MEASUREMODE_DELAY_MS);
	
	eeg->data(eegAfter);
	
	engine_pollEvents();
	
	if(engine_storeMeasurement(pic, 0, eegBefore, eegAfter,
				   synthBefore, synthCurrent) == false)
	  logging.error("store measurement failed");
	
      }
      else{
	engine_pollEvents(); // polls for events
	engine_updateScreen(); // always updates window if it exists
      }
    }
    else if(currentCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE){
      const float percentage =
	(currentHMMModel + currentPictureModel + currentKeywordModel + (soundModelCalculated == true))/((float)(pictureData.size()+keywordData.size()+2));
      
      optimizeETA.update(percentage);
      
      {
	float eta = optimizeETA.estimate();
	eta = eta / 60.0f; // ETA in minutes
	
	char buffer[160];
	snprintf(buffer, 160, "resonanz-engine: optimizing prediction model (%.2f%%) [ETA %.1f min]..",
		 100.0f*percentage, eta);
	
	engine_setStatus(buffer);
      }
      
      engine_stopHibernation();
      
      if(engine_optimizeModels(currentHMMModel, currentPictureModel, currentKeywordModel, soundModelCalculated) == false)
	logging.warn("model optimization failure");
		  
    }
    else if(currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){

      {
	char buffer[80];

	float meand = 0.0f;
	for(unsigned int i=0;i<distanceTarget.size();i++)
	  meand += distanceTarget[i];

	if(distanceTarget.size())
	  meand /= ((float)distanceTarget.size());

	if(tick_delay_sleep){
	  snprintf(buffer, 80, "resonanz-engine: executing program (in sync) [error: %f]..", meand);
	}
	else{
	  snprintf(buffer, 80, "resonanz-engine: executing program (out of sync) [error: %f]..", meand);
	}

	distanceTarget.clear();
	
	logging.info(buffer);
	engine_setStatus(buffer);
      }
      
      engine_stopHibernation();
      
      auto t1 = std::chrono::system_clock::now().time_since_epoch();
      auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
      
      long long currentSecond = (long long)
	(programHz*(t1ms - programStarted)/1000.0f); // gets current second for the program value
      
      
      if(loopMode){
	
	if(currentSecond/programHz >= program[0].size()){ // => restarts program
	  currentSecond = 0;
	  lastProgramSecond = -1;
	  
	  auto t1 = std::chrono::system_clock::now().time_since_epoch();
	  auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
	  
	  programStarted = (long long)t1ms;
	}
      }
      
      if(currentSecond > lastProgramSecond && lastProgramSecond >= 0){
	eeg->data(eegCurrent);
	
	logging.info("Calculating RMS error");
	
	// calculates RMS error
	std::vector<float> current;
	std::vector<float> target;
	std::vector<float> eegTargetVariance;
	
	target.resize(program.size());
	eegTargetVariance.resize(program.size());
	
	for(unsigned int i=0;i<program.size();i++){
	  // what was our target BEFORE this time tick [did we move to target state]
	  target[i] = program[i][lastProgramSecond/programHz];
	  eegTargetVariance[i] = programVar[i][lastProgramSecond/programHz];
	}
	
	eeg->data(current);
	
	int numElements = 0;
	
	if(target.size() == current.size()){
	  float rms = 0.0f;
	  for(unsigned int i=0;i<target.size();i++){
	    rms += (current[i] - target[i])*(current[i] - target[i])/eegTargetVariance[i];
	    if(eegTargetVariance[i] < 100000.0f)
	      numElements++; // small enough for taken into account for error term
	  }
	  
	  rms = sqrt(rms); 
	  if(numElements > 0) 
	    rms /= numElements; // per element error
	  
	  // adds current rms to the global RMS
	  programRMS += rms;
	  programRMS_N++;
	  
	  {
	    char buffer[256];
	    snprintf(buffer, 256, "Program current RMS (per element) error: %.2f (average RMS error: %.2f)",
		     rms, programRMS/programRMS_N);
	    logging.info(buffer);
	  }					
	}
      }
      else if(currentSecond > lastProgramSecond && lastProgramSecond < 0){
	eeg->data(eegCurrent);
      }
      
      lastProgramSecond = currentSecond;
      
      {
	char buffer[80];
	snprintf(buffer, 80, "Executing program (pseudo)second: %d/%d",
		 (unsigned int)(currentSecond/programHz), (int)program[0].size());
	logging.info(buffer);
      }
      
      
      if(currentSecond/programHz < (signed)program[0].size()){
	logging.info("Executing program: calculating current targets");
	
	// executes program
	std::vector<float> eegTarget;
	std::vector<float> eegTargetVariance;
	
	eegTarget.resize(eegCurrent.size());
	eegTargetVariance.resize(eegCurrent.size());

	float distance = 0.0f;
	
	for(unsigned int i=0;i<eegTarget.size();i++){
	  eegTarget[i] = program[i][currentSecond/programHz];
	  eegTargetVariance[i] = programVar[i][currentSecond/programHz];

	  float d = (eegTarget[i] - eegCurrent[i])/eegTargetVariance[i];
	  distance += d*d;
	}

	distance = sqrt(distance);
	distanceTarget.push_back(distance);
	
	// shows picture/keyword which model predicts to give closest match to target
	// minimize(picture) ||f(picture,eegCurrent) - eegTarget||/eegTargetVariance
	
	const float timedelta = 1.0f/programHz; // current delta between pictures [in seconds]
	//const float timedelta = 1.0f; // CHANGED: 1 sec between picture changes, 

	// const float timedelta = TICK_MS/1000.0f; // current delta between pictures [length of single tick which the image is shown]
	
	
	if(currentCommand.blindMonteCarlo == false)
	  engine_executeProgram(eegCurrent, eegTarget, eegTargetVariance, timedelta);
	else
	  engine_executeProgramMonteCarlo(eegTarget, eegTargetVariance, timedelta);
      }
      else{
	
	// program has run to the end => stop
	logging.info("Executing the given program has stopped [program stop time].");
	
	if(video){
	  auto t1 = std::chrono::system_clock::now().time_since_epoch();
	  auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
	  
	  logging.info("stopping theora video encoding.");
	  
	  video->stopEncoding((unsigned long long)(t1ms - programStarted));
	  delete video;
	  video = nullptr;
	}
	
	cmdStopCommand();
      }
    }
    else if(currentCommand.command == ResonanzCommand::CMD_DO_MEASURE_PROGRAM){
      engine_setStatus("resonanz-engine: measuring program..");
      
      engine_stopHibernation();
      
      auto t1 = std::chrono::system_clock::now().time_since_epoch();
      auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
		  
      long long currentSecond = (long long)
	(programHz*(t1ms - programStarted)/1000.0f); // gets current second for the program value
      
      if(currentSecond <= lastProgramSecond)
	continue; // nothing to do
      
      // measures new measurements
      for(;lastProgramSecond <= currentSecond; lastProgramSecond++){
	// measurement program continues: just measures values and do nothing
	// as the background thread currently handles playing of the music
	// LATER: do video decoding and showing..
	
	std::vector<float> values(eeg->getNumberOfSignals());
	eeg->data(values);
	
	for(unsigned int i=0;i<rawMeasuredSignals.size();i++)
	  rawMeasuredSignals[i].push_back(values[i]);
      }
      
      
      if(currentSecond < currentCommand.programLengthTicks){
	engine_updateScreen();
	engine_pollEvents();
      }
      else{
	// stops measuring program
	
	// transforms raw signals into measuredProgram values
	
	std::lock_guard<std::mutex> lock(measure_program_mutex);
	
	std::vector<std::string> names;
	eeg->getSignalNames(names);
	
	measuredProgram.resize(currentCommand.signalName.size());
	
	for(unsigned int i=0;i<measuredProgram.size();i++){
	  measuredProgram[i].resize(currentCommand.programLengthTicks);
	  for(auto& m : measuredProgram[i])
	    m = -1.0f;
	}
	
	for(unsigned int j=0;j<currentCommand.signalName.size();j++){
	  for(unsigned int n=0;n<names.size();n++){
	    if(names[n] == currentCommand.signalName[j]){ // finds a matching signal in a command
	      unsigned int MIN = measuredProgram[j].size();
	      if(rawMeasuredSignals[n].size() < MIN*programHz)
		MIN = rawMeasuredSignals[n].size()/programHz;
	      
	      for(unsigned int i=0;i<MIN;i++){
		
		auto mean = 0.0f;
		auto N = 0.0f;
		for(unsigned int k=0;k<programHz;k++){
		  if(rawMeasuredSignals[n][i*programHz + k] >= 0.0f){
		    mean += rawMeasuredSignals[n][i*programHz + k];
		    N++;
		  }
		}
		
		if(N > 0.0f)
		  measuredProgram[j][i] = mean / N;
		else
		  measuredProgram[j][i] = 0.5f;
		
	      }
	      
	    }
	  }
	}
	
	
	cmdStopCommand();
      }
    }
    
    engine_pollEvents();

    
    if(keypress()){
      if(currentCommand.command != ResonanzCommand::CMD_DO_NOTHING &&
	 currentCommand.command != ResonanzCommand::CMD_DO_MEASURE_PROGRAM)
	{
	  logging.info("Received keypress: stopping command..");
	  cmdStopCommand();
	}
    }
    
    
    // monitors current eeg values and logs them into log file
    {
      std::lock_guard<std::mutex> lock(eeg_mutex); // mutex might change below use otherwise..
      
      if(eeg->connectionOk() == false){
	std::string line = "eeg ";
	line += eeg->getDataSourceName();
	line += " : no connection to hardware";
	logging.info(line);
      }
      else{
	std::string line = "eeg ";
	line += eeg->getDataSourceName();
	line + " :";
	
	std::vector<float> x;
	eeg->data(x);
	
	for(unsigned int i=0;i<x.size();i++){
	  char buffer[80];
	  snprintf(buffer, 80, " %.2f", x[i]);
	  line += buffer;
	}
	
	logging.info(line);
      }
    }
    
  }
  
  if(window != nullptr)
    SDL_DestroyWindow(window);
  
  {
    std::lock_guard<std::mutex> lock(eeg_mutex);
    if(eeg) delete eeg;
    eeg = nullptr;
    eegDeviceType = ResonanzEngine::RE_EEG_NO_DEVICE;
  }
  
  if(nn != nullptr){
    delete nn;
    nn = nullptr;
  }

  if(nnkey != nullptr){
    delete nnkey;
    nnkey = nullptr;
  }
  
  if(nnsynth != nullptr){
    delete nnsynth;
    nnsynth = nullptr;
  }

  if(hmmUpdator != nullptr){
    hmmUpdator->stop();
    delete hmmUpdator;
    hmmUpdator = nullptr;
  }

  if(kmeans != nullptr){
    delete kmeans;
    kmeans = nullptr;
  }

  if(hmm){
    delete hmm;
    hmm = nullptr;
  }
  
  if(bnn != nullptr){
    delete bnn;
    bnn = nullptr;
  }
  
  engine_SDL_deinit();
  
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

// loads prediction models for program execution, returns false in case of failure
bool ResonanzEngine::engine_loadModels(const std::string& modelDir)
{
  try{
    if(hmmUpdator != nullptr) return  false; // already computing HMM/K-Means models.
    
    std::string filename = calculateHashName("KMeans" + eeg->getDataSourceName()) + ".kmeans";
    filename = modelDir + "/" + filename;

    auto newkmeans = new whiteice::KMeans<>();
    auto newhmm = new whiteice::HMM();

    if(newkmeans->load(filename) == false){
      logging.error("KMeans::load() fails loading K-Means model.");
      delete newkmeans;
      delete newhmm;
      return false;
    }

    filename = calculateHashName("HMM" + eeg->getDataSourceName()) + ".hmm";
    filename = modelDir + "/" + filename;

    if(newhmm->loadArbitrary(filename) == false){
      logging.error("HMM::loadArbitrary() fails loading HMM model.");
      delete newkmeans;
      delete newhmm;
      return false;
    }

    if(newhmm->getNumVisibleStates() != newkmeans->size() || 
       newhmm->getNumHiddenStates() !=  HMM_NUM_CLUSTERS){
      logging.error("HMM visible/hidden states mismatch when loading models.");
      delete newkmeans;
      delete newhmm;
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(hmm_mutex);
      
      if(kmeans) delete kmeans;
      if(hmm) delete hmm;
      
      kmeans = newkmeans;
      hmm = newhmm;
      
      // updates HMM state (choses first randomly according to HMM PI parameter)
      
      HMMstate = hmm->sample(hmm->getPI());
    }
    
  }
  catch(std::exception& e){
    logging.error("");
    return false;
  }

  
  if(pictures.size() <= 0){
    logging.error("No pictures to which load models.");
    return false; // no loaded pictures
  }
  
  pictureModels.resize(pictures.size());
  
  whiteice::linear_ETA<float> loadtimeETA;
  loadtimeETA.start(0.0f, 1.0f);
  
  unsigned int pictureModelsLoaded = 0;
  unsigned int keywordModelsLoaded = 0;
  
  // #pragma omp parallel for
  for(unsigned int i=0;i<pictureModels.size();i++){
    std::string filename = calculateHashName(pictures[i] + eeg->getDataSourceName()) + ".model";
    filename = modelDir + "/" + filename;
    
    if(pictureModels[i].load(filename) == false){
      logging.error("Loading picture model file failed: " + filename);
      continue;
    }
    
    
    // pictureModels[i].downsample(100); // keeps only 100 random models
    
    pictureModelsLoaded++;
    
    {
      char buffer[100];
      const float percentage = ((float)i)/((float)(keywords.size()+pictures.size()+1));
      
      loadtimeETA.update(percentage);
      
      snprintf(buffer, 100, "resonanz-engine: loading prediction model (%.1f%%) [ETA %.2f mins]..", 
	       100.0f*percentage, loadtimeETA.estimate()/60.0f);
      
      logging.info(buffer);
      engine_setStatus(buffer);
    }

    engine_pollEvents();
  }
  
  keywordModels.resize(keywords.size());
  
  // #pragma omp parallel for
  for(unsigned int i=0;i<keywordModels.size();i++){
    std::string filename = calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".model";
    filename = modelDir + "/" + filename;
    
    if(keywordModels[i].load(filename) == false){
      logging.error("Loading keyword model file failed: " + filename);
      continue;
    }
    
    // keywordModels[i].downsample(100); // keeps only 100 random samples
    
    keywordModelsLoaded++;
    
    {
      char buffer[100];
      const float percentage = 
	((float)(i + pictures.size()))/((float)(keywords.size()+pictures.size()+1));
      
      loadtimeETA.update(percentage);
      
      snprintf(buffer, 100, "resonanz-engine: loading prediction model (%.1f%%) [ETA %.2f mins]..", 
	       100.0f*percentage, loadtimeETA.estimate()/60.0f);
      
      logging.info(buffer);
      engine_setStatus(buffer);
    }

    engine_pollEvents();
  }

  unsigned int synthModelLoaded = 0;
  
  if(synth){
    std::string filename = calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".model";
    filename = modelDir + "/" + filename;
    
    if(synthModel.load(filename) == false){
      logging.error("Loading synth model file failed: " + filename);
    }
    else{
      char buffer[256];
      snprintf(buffer, 256, "loading synth model success: %d - %d",
	       synthModel.inputSize(), synthModel.outputSize());
      logging.info(buffer);

      synthModelLoaded++; 
    }

    engine_pollEvents();
    
    // synthModel.downsample(100); // keeps only 100 random models
  }
  else{
    synthModelLoaded++; // hack to make it work
  }
  
  // returns true if could load at least one model for pictures and one model for keywords..
  return (pictureModelsLoaded > 0 && synthModelLoaded > 0);
}



int gaussian_random_select(const std::multimap<float,int>& squared_errors)
{
  if(squared_errors.size() <= 1) return 0;

  // converts values to p-values

  std::vector<float> pvalues;
  pvalues.resize(squared_errors.size());

  float psum = 0.0f;

  unsigned int i=0;
  for(const auto& s : squared_errors){

    const float p = expf(-(s.first*s.first));

    pvalues[i] = p;
    psum += p;

    i++;
  }

  for(auto& p : pvalues) p /= psum;

  // calculates cumulative PDF function

  for(unsigned int i=1;i<pvalues.size();i++)
    pvalues[i] += pvalues[i-1];

  float select = (float)(rng.uniform().c[0]);

  for(unsigned int i=0;i<pvalues.size();i++){
    if(select <= pvalues[i]) return i;
  }

  if(pvalues.size() >= 1) 
    return (pvalues.size()-1);
  else
    return 0;
}



// shows picture/keyword which model predicts to give closest match to target
// minimize(picture) ||f(picture,eegCurrent) - eegTarget||/eegTargetVariance
bool ResonanzEngine::engine_executeProgram(const std::vector<float>& eegCurrent,
					   const std::vector<float>& eegTarget, 
					   const std::vector<float>& eegTargetVariance,
					   float timestep_)
{
  // was: 3, was: 1 (only selects the best result) [how many top results show]
  const unsigned int NUM_TOPRESULTS = SHOW_TOP_RESULTS; 

  // how many bayesian neural networks use to calculate mean and cov.
  const unsigned int MODEL_SAMPLES = 11;
  
  std::multimap<float, int> bestKeyword;
  std::multimap<float, int> bestPicture;
  
  std::vector<float> soundParameters;
  
  math::vertex<> target(eegTarget.size());
  math::vertex<> current(eegCurrent.size());
  math::vertex<> targetVariance(eegTargetVariance.size());
  const whiteice::math::blas_real<float> timestep = timestep_;
  
  for(unsigned int i=0;i<target.size();i++){
    target[i] = eegTarget[i];
    current[i] = eegCurrent[i];
    targetVariance[i] = eegTargetVariance[i];
  }

  // updates HMM brain state according to measurement
  {
    std::lock_guard<std::mutex> lock(hmm_mutex);
    
    if(kmeans == NULL || hmm == NULL){
      logging.error("executeProgram(): no K-Means and HMM models loaded");
      return false;
    }
  }
  
  std::vector< std::pair<float, int> > results(keywordData.size());
  std::vector< float > model_error_ratio(keywordData.size());
  
  logging.info("engine_executeProgram() calculate keywords");

#pragma omp parallel for schedule(dynamic)	
  for(unsigned int index=0;index<keywordData.size();index++){
    
    math::vertex<> x(eegCurrent.size() + HMM_NUM_CLUSTERS);
    
    for(unsigned int i=0;i<eegCurrent.size();i++)
      x[i] = eegCurrent[i];
    for(unsigned int i=eegCurrent.size();i<x.size();i++){
      if(i-eegCurrent.size() == HMMstate) x[i] = 1.0f;
      else x[i] = 0.0f;
    }
    
    auto original = x;
    
    if(keywordData[index].preprocess(0, x) == false){
      logging.warn("skipping bad keyword prediction model");
      continue;
    }
    
    math::vertex<> m;
    math::matrix<> cov;

    unsigned int SAMPLES = MODEL_SAMPLES;
    
    if(dataRBFmodel){
      engine_estimateNN(x, keywordData[index], m , cov);
      SAMPLES = 1;
    }
    else{
      whiteice::bayesian_nnetwork<>& model = keywordModels[index];
      
      if(model.inputSize() != (eegCurrent.size()+HMM_NUM_CLUSTERS) ||
	 model.outputSize() != eegTarget.size())
      {
	logging.warn("skipping bad keyword prediction model");
	continue; // bad model/data => ignore
      }


      if(model.getNumberOfSamples() < SAMPLES)
	SAMPLES = model.getNumberOfSamples();

	 
      if(model.calculate(x, m, cov, 1, SAMPLES) == false){
	logging.warn("skipping bad keyword prediction model");
	continue;
      }
    }
    
    
    if(keywordData[index].invpreprocess(1, m, cov) == false){
      logging.warn("skipping bad keyword prediction model");
      continue;
    }

    m *= timestep; // corrects delta to given timelength
    cov *= timestep*timestep;

    // converts cov to cov of mean
    cov /= whiteice::math::blas_real<float>((float)SAMPLES);
    
    // now we have prediction x to the response to the given keyword
    // calculates error (weighted distance to the target state)

    //auto predicted_value = original + m;
    auto predicted_value = m;
    for(unsigned int i=0;i<m.size();i++)
      predicted_value[i] += original[i];
    
    // no out of range values (clips to [0,1] interval)
    for(unsigned int i=0;i<predicted_value.size();i++){
      if(predicted_value[i] < 0.0f) predicted_value[i] = 0.0f;
      else if(predicted_value[i] > 1.0f) predicted_value[i] = 1.0f;
    }
    
    auto delta = target - predicted_value;
#if 1
    auto stdev = m;
    
    for(unsigned int i=0;i<stdev.size();i++){
      stdev[i] = math::sqrt(math::abs(cov(i,i)));
    }
    
    // calculates average stdev/delta ratio
    auto ratio = stdev.norm()/m.norm();
    model_error_ratio[index] = ratio.c[0];
    
    for(unsigned int i=0;i<delta.size();i++){
      delta[i] = math::abs(delta[i]) + 0.50f*stdev[i]; // FIXME: was 1 (handles uncertainty in weights)
      delta[i] /= math::sqrt(targetVariance[i]);
    }
#else
    for(unsigned int i=0;i<delta.size();i++)
      delta[i] /= math::sqrt(targetVariance[i]);
#endif
    
    auto error = delta.norm();
    
    std::pair<float, int> p;
    p.first = error.c[0];
    p.second = index;
    
    results[index] = p;
    
    // engine_pollEvents(); // polls for incoming events in case there are lots of models
  }
  
	
  // estimates quality of results
  if(model_error_ratio.size() > 0)
  {
    float mean_ratio = 0.0f;
    for(auto& r : model_error_ratio)
      mean_ratio += r;
    mean_ratio /= model_error_ratio.size();
    
    if(mean_ratio > 1.0f){
      char buffer[80];
      snprintf(buffer, 80, "Optimizing program: KEYWORD PREDICTOR ERROR LARGER THAN OUTPUT (%.2f larger)", mean_ratio);
      logging.warn(buffer);	    
    }
  }
  
  
  // selects the best result
  
  for(auto& p : results)
    bestKeyword.insert(p);
  
  while(bestKeyword.size() > NUM_TOPRESULTS){
    auto i = bestKeyword.end(); i--;
    bestKeyword.erase(i); // removes the largest element
  }
  
  engine_pollEvents();
  
  
  results.resize(pictureData.size());
  model_error_ratio.resize(pictureData.size());
  
  logging.info("engine_executeProgram(): calculate pictures");

  // presets values..
  {
    for(unsigned int index=0;index<pictureData.size();index++){
    
      std::pair<float, int> p;
      p.first = 1e6f; // very large error as the default value [=> ignotes this picture]
      p.second = index;
    
      results[index] = p;

      model_error_ratio[index] = 1.0f; // dummy value [no clue what to set]
    }
  }


  
  std::set<unsigned int> picIndexes;

  {
    // only selects picture from set of was: 50, 100 randomly chosen pictures from all pictures [to keep in sync/realtime requirements]
    for(unsigned int i=0;i<pictureData.size();i++){
      picIndexes.insert(i);
    }

    while(picIndexes.size() > PIC_DATASET_SIZE){
      // keeps removing random elements until dataset size is PIC_DATASET_SIZE
      unsigned int remove = rng.rand() % picIndexes.size();
      auto iter = picIndexes.begin();

      while(remove > 0){
	iter++;
	remove--;
      }

      picIndexes.erase(iter); // removes remove:th element from the set
    }
  }
  

#pragma omp parallel for schedule(dynamic)	
  for(unsigned int picindex=0;picindex<picIndexes.size();picindex++){

    auto piciter = picIndexes.begin();
    for(unsigned int p=0;p<picindex;p++)
      piciter++;
    
    const unsigned int index = (*piciter); // select (all) pictures from PicIndexes set
    
    math::vertex<> x(eegCurrent.size() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
    
    for(unsigned int i=0;i<eegCurrent.size();i++){
      x[i] = eegCurrent[i];
    }
    
    for(unsigned int i=eegCurrent.size();i<(eegCurrent.size()+HMM_NUM_CLUSTERS);i++){
      if(i-eegCurrent.size() == HMMstate) x[i] = 1.0f;
      else x[i] = 0.0f;
    }

    for(unsigned int i=eegCurrent.size()+HMM_NUM_CLUSTERS, index=0;i<x.size();i++,index++){
      x[i] = imageFeatures[picindex][index];
    }
    
    auto original = x;
    
    if(pictureData[index].preprocess(0, x) == false){
      logging.warn("skipping bad picture prediction model (1)");
      continue;
    }
    
    math::vertex<> m;
    math::matrix<> cov;

    unsigned int SAMPLES = MODEL_SAMPLES;
    
    if(dataRBFmodel){
      engine_estimateNN(x, pictureData[index], m , cov);
      SAMPLES = 1;
    }
    else{
      whiteice::bayesian_nnetwork<>& model = pictureModels[index];
      
      if(model.inputSize() != (eegCurrent.size()+HMM_NUM_CLUSTERS) ||
	 model.outputSize() != eegTarget.size()){
	
	logging.warn("skipping bad picture prediction model (2)");
	continue; // bad model/data => ignore
      }

      if(model.getNumberOfSamples() < SAMPLES)
	SAMPLES = model.getNumberOfSamples();
      
      if(model.calculate(x, m, cov, 1, SAMPLES) == false){
	logging.warn("skipping bad picture prediction model (3)");
	continue;
      }
      
    }
    
    if(pictureData[index].invpreprocess(1, m, cov) == false){
      logging.warn("skipping bad picture prediction model (4)");
      continue;
    }
    
    m *= timestep; // corrects delta to given timelength
    cov *= timestep*timestep;

    // converts cov to cov of mean
    cov /= whiteice::math::blas_real<float>((float)SAMPLES);
    
    // now we have prediction x to the response to the given picture
    // calculates error (weighted distance to the target state)
    
    //auto predicted_value = original + m;
    auto predicted_value = m;
    for(unsigned int i=0;i<m.size();i++)
      predicted_value[i] += original[i];
    
    // no out of range values (clips to [0,1] interval)
    for(unsigned int i=0;i<predicted_value.size();i++){
      if(predicted_value[i] < 0.0f) predicted_value[i] = 0.0f;
      else if(predicted_value[i] > 1.0f) predicted_value[i] = 1.0f;
    }
    
    auto delta = target - predicted_value;
#if 1
    auto stdev = m;
    
    for(unsigned int i=0;i<stdev.size();i++){
      stdev[i] = math::sqrt(math::abs(cov(i,i)));
    }
    
    // calculates average stdev/delta ratio
    auto ratio = stdev.norm()/m.norm();
    model_error_ratio[index] = ratio.c[0];


    for(unsigned int i=0;i<delta.size();i++){
      delta[i] = math::abs(delta[i]) + 0.50f*stdev[i]; // FIXME: was 1 (handles uncertainty)
      delta[i] /= math::sqrt(targetVariance[i]);
    }
#else
    
    for(unsigned int i=0;i<delta.size();i++)
      delta[i] /= math::sqrt(targetVariance[i]);
#endif
    
    auto error = delta.norm();
    
    std::pair<float, int> p;
    p.first = error.c[0];
    p.second = index;
    
    results[index] = p;
		
    // engine_pollEvents(); // polls for incoming events in case there are lots of models
  }
  
  
  // estimates quality of results
  if(model_error_ratio.size() > 0)
  {
    float mean_ratio = 0.0f;
    for(auto& r : model_error_ratio)
      mean_ratio += r;
    mean_ratio /= model_error_ratio.size();
    
    if(mean_ratio > 1.0f){
      char buffer[80];
      snprintf(buffer, 80, "Optimizing program: PICTURE PREDICTOR ERROR LARGER THAN OUTPUT (%.2f larger)", mean_ratio);
	    logging.warn(buffer);	    
    }
  }
  
  
  for(auto& p : results)
    bestPicture.insert(p);
  
  while(bestPicture.size() > NUM_TOPRESULTS){
    auto i = bestPicture.end(); i--;
    bestPicture.erase(i); // removes the largest element
  }
  
  engine_pollEvents(); // polls for incoming events in case there are lots of models

  
  if(synth){
    logging.info("engine_executeProgram(): calculate synth model");
    
    // initial sound parameters are random
    soundParameters.resize(synth->getNumberOfParameters());
    for(unsigned int i=0;i<soundParameters.size();i++)
      soundParameters[i] = rng.uniform().c[0];
    
    math::vertex<> input;
    input.resize(synthModel.inputSize());
    input.zero();
    
    std::vector<float> synthBefore;
    std::vector<float> synthTest;
    
    synth->getParameters(synthBefore);
    synthTest.resize(synthBefore.size());
    
    // nn(synthNow, synthProposed, currentEEG) = dEEG/dt
    if((2*synthBefore.size() + eegCurrent.size() + HMM_NUM_CLUSTERS) != synthModel.inputSize()){
      char buffer[256];
      snprintf(buffer, 256, "engine_executeProgram(): synth model input parameters (dimension) mismatch! (%d + %d != %d)\n",
	       (int)synthBefore.size(), (int)eegCurrent.size(), synthModel.inputSize());
      logging.fatal(buffer);
    }
    
    for(unsigned int i=0;i<synthBefore.size();i++){
      input[i] = synthBefore[i];
    }
    
    
    for(unsigned int i=0;i<eegCurrent.size();i++){
      input[2*synthBefore.size() + i] = eegCurrent[i];
    }

    // sets HMM state variable to input
    input[2*synthBefore.size()+eegCurrent.size()+HMMstate] = 1.0f;
    
    math::vertex<> original(eegCurrent.size());
    
    for(unsigned int i=0;i<original.size();i++)
      original[i] = eegCurrent[i];
    
    std::vector< std::pair<float, std::vector<float> > > errors;
    errors.resize(SYNTH_NUM_GENERATED_PARAMS);
    
    model_error_ratio.resize(SYNTH_NUM_GENERATED_PARAMS);
    
    // generates synth parameters randomly and selects parameter
    // with smallest predicted error to target state

    logging.info("engine_executeProgram(): parallel synth model search start..");
    
#pragma omp parallel for schedule(dynamic)
    for(unsigned int param=0;param<SYNTH_NUM_GENERATED_PARAMS;param++){
      
      if(rng.uniform() < -0.20f) // NEVER FOR NOW!
      {
	// generates random parameters [random search]
	for(unsigned int i=0;i<synthTest.size();i++)
	  synthTest[i] = rng.uniform().c[0];
      }
      else
	{
	  // or: adds gaussian noise to current parameters 
	  //     [random jumps around neighbourhood]
	  for(unsigned int i=0;i<synthTest.size();i++){
	    synthTest[i] = synthBefore[i] + rng.normal().c[0]*0.10f;
	    if(synthTest[i] <= 0.0f) synthTest[i] = 0.0f;
	    else if(synthTest[i] >= 1.0f) synthTest[i] = 1.0f;
	  }
	}
      
      // copies parameters to input vector
      for(unsigned int i=0;i<synthTest.size();i++){
	input[synthBefore.size()+i] = synthTest[i];
      }
      
      // calculates approximated response
      auto x = input;
      
      if(synthData.preprocess(0, x) == false){
	logging.warn("skipping bad synth prediction (0)");
	continue;
      }

      math::vertex<> m;
      math::matrix<> cov;

#if 0
      if(dataRBFmodel){
	// engine_estimateNN(x, synthData, m , cov);
	// NOT SUPPORTED YET...

 	assert(0);
	
	// now change high variance output
	m = x;
	cov.resize(x.size(), x.size());
	cov.identity();
	SAMPLES = 1;
      }
#endif
      
      unsigned int SAMPLES = MODEL_SAMPLES;
	
      {
	auto& model = synthModel;
	
	if(model.inputSize() != x.size() || model.outputSize() != eegTarget.size()){
	  logging.warn("skipping bad synth prediction model (1)");
	  continue; // bad model/data => ignore
	}

	if(model.getNumberOfSamples() < SAMPLES)
	  SAMPLES = model.getNumberOfSamples();
	
	if(model.calculate(x, m, cov, 1, SAMPLES) == false){
	  logging.warn("skipping bad synth prediction model (2)");
	  continue;
	}
	
      }
      
      if(synthData.invpreprocess(1, m, cov) == false){
	char buffer[256];

	sprintf(buffer, "skipping bad synth prediction model (3): clusters: %d %d %d %d\n",
		synthData.getNumberOfClusters(), m.size(), synthData.dimension(1), synthData.size(0));

	logging.warn(buffer);
	continue;
      }
      
      m *= timestep; // corrects delta to given timelength
      cov *= timestep*timestep;

      // converts cov to cov of mean
      cov /= whiteice::math::blas_real<float>((float)SAMPLES);
      
      // now we have prediction m to the response to the given keyword
      // calculates error (weighted distance to the target state)
      
      auto delta = target - (original + m);
#if 1
      auto stdev = m;
      stdev.zero();
      
      for(unsigned int i=0;i<stdev.size();i++){
	stdev[i] = math::sqrt(math::abs(cov(i,i)));
      }
      
      // calculates average stdev/delta ratio
      auto ratio = stdev.norm()/m.norm();
      model_error_ratio[param] = ratio.c[0];

      
      for(unsigned int i=0;i<delta.size();i++){
	delta[i] = math::abs(delta[i]) + 0.50f*stdev[i]; // FIXME: was 0.5 (handles uncertainty)
	delta[i] /= targetVariance[i];
      }
#else
      for(unsigned int i=0;i<delta.size();i++){
	delta[i] /= targetVariance[i];
      }
#endif
      
      auto error = delta.norm();
      
      std::pair<float, std::vector<float> > p;
      p.first = error.c[0];
      p.second = synthTest;
      
      errors[param] = p;
      
    }
    
    logging.info("engine_executeProgram(): parallel synth model search start.. DONE");
    
    
    // estimates quality of results
    {
      float mean_ratio = 0.0f;
      for(auto& r : model_error_ratio)
	mean_ratio += r;
      mean_ratio /= model_error_ratio.size();
      
      if(mean_ratio > 1.0f){
	char buffer[80];
	snprintf(buffer, 80, "Optimizing program: SYNTH PREDICTOR ERROR LARGER THAN OUTPUT (%.2fx larger)", mean_ratio);
	logging.warn(buffer);	    
      }
    }
    
    
    // finds the best error
    float best_error = 10e20;
    for(unsigned int i=0;i<errors.size();i++){
      if(errors[i].first < best_error){
	soundParameters = errors[i].second; // synthTest
	best_error = errors[i].first;
      }
    }
    
    if(randomPrograms){
      soundParameters = synthTest;
      
      for(unsigned int i=0;i<soundParameters.size();i++)
	soundParameters[i] = rng.uniform().c[0];
    }
    
  }
  
  
  if((bestKeyword.size() <= 0 && keywordData.size() > 0) || bestPicture.size() <= 0){
    logging.error("Execute command couldn't find picture or keyword command to show (no models?)");
    engine_pollEvents();
    return false;
  }

  unsigned int keyword = 0;
  unsigned int picture = 0;
  
  {
    unsigned int elem = 0;

    //keyword = gaussian_random_select(bestKeyword);
    
    
    if(keywordData.size() > 0){
      elem = rng.rand() % bestKeyword.size();
      for(auto& k : bestKeyword){
	if(elem <= 0){
	  keyword = k.second;
	  break;
	}
	else elem--;
      }
    }
    
    
    
    //picture = gaussian_random_select(bestPicture);

    
    elem = rng.rand() % bestPicture.size();
    for(auto& p : bestPicture){
      if(elem <= 0){
	picture = p.second;
	break;
      }
      else elem--;
    }
    
    
    if(randomPrograms){
      if(keywords.size() > 0)
	keyword = rng.rand() % keywords.size();
      else
	keyword = 0;
      
      if(pictures.size() > 0)
	picture = rng.rand() % pictures.size();
      else
	picture = 0;
    }
  }
  
  
  if(keywordData.size() > 0)
  {
    char buffer[256];
    snprintf(buffer, 256, "prediction model selected keyword/best picture: %s %s",
	     keywords[keyword].c_str(), pictures[picture].c_str());
    logging.info(buffer);
  }
  else{
    char buffer[256];
    snprintf(buffer, 256, "prediction model selected best picture: %s",
	     pictures[picture].c_str());
    logging.info(buffer);
  }
  
  // now we have best picture and keyword that is predicted
  // to change users state to target value: show them
  
  if(keywordData.size() > 0){
    std::string message = keywords[keyword];
    engine_showScreen(message, picture, soundParameters);
  }
  else{
    engine_showScreen(" ", picture, soundParameters);
  }
  
  engine_updateScreen();
  engine_pollEvents();
  
  return true;
}


// executes program blindly based on Monte Carlo sampling and prediction models
// [only works for low dimensional target signals and well-trained models]
//
// FIXME: don't support randomPrograms flag which instead selects model randomly
// FIXME: REMOVE THIS NOT USABLE CODE
//
bool ResonanzEngine::engine_executeProgramMonteCarlo(const std::vector<float>& eegTarget,
						     const std::vector<float>& eegTargetVariance, float timestep_)
{
	int bestKeyword = -1;
	int bestPicture = -1;
	float bestError = std::numeric_limits<double>::infinity();

	math::vertex<> target(eegTarget.size());
	math::vertex<> targetVariance(eegTargetVariance.size());
	whiteice::math::blas_real<float> timestep = timestep_;

	for(unsigned int i=0;i<target.size();i++){
		target[i] = eegTarget[i];
		targetVariance[i] = eegTargetVariance[i];
	}

	if(mcsamples.size() <= 0)
	        return false; // internal program error should have MC samples


	for(unsigned int index=0;index<keywordModels.size();index++){
		whiteice::bayesian_nnetwork<>& model = keywordModels[index];

		if(model.inputSize() != mcsamples[0].size() || model.outputSize() != eegTarget.size()){
			logging.warn("skipping bad keyword prediction model");
			continue; // bad model/data => ignore
		}

		// calculates average error for this model using MC samples
		float error = 0.0f;

#pragma omp parallel for
		for(unsigned int mcindex=0;mcindex<mcsamples.size();mcindex++){
			auto x = mcsamples[mcindex];

			if(keywordData[index].preprocess(0, x) == false){
				logging.warn("skipping bad keyword prediction model");
				continue;
			}

			math::vertex<> m;
			math::matrix<> cov;

			unsigned int SAMPLES = 50;
			
			if(model.getNumberOfSamples() < SAMPLES)
			  SAMPLES = model.getNumberOfSamples();
			
			if(model.calculate(x, m, cov, 1, SAMPLES) == false){
				logging.warn("skipping bad keyword prediction model");
				continue;
			}

			if(keywordData[index].invpreprocess(1, m, cov) == false){
				logging.warn("skipping bad keyword prediction model");
				continue;
			}

			m *= timestep; // corrects delta to given timelength
			cov *= timestep*timestep;

			cov /= whiteice::math::blas_real<float>((float)SAMPLES);

			// now we have prediction x to the response to the given keyword
			// calculates error (weighted distance to the target state)

			auto delta = target - (m + x);

			for(unsigned int i=0;i<delta.size();i++){
				delta[i] = math::abs(delta[i]) + math::sqrt(cov(i,i)); // Var[x - y] = Var[x] + Var[y]
				delta[i] /= targetVariance[i];
			}

			auto e = delta.norm();

			float ef = 0.0f;
			math::convert(ef, e);

#pragma omp critical(update_error1)
			{
				error += ef / mcsamples.size();
			}
		}

		if(error < bestError){
			bestError = error;
			bestKeyword = index;
		}

		engine_pollEvents(); // polls here for incoming events in case there are lots of models and this is slow..
	}


	bestError = std::numeric_limits<double>::infinity();

	for(unsigned int index=0;index<pictureModels.size();index++){
		whiteice::bayesian_nnetwork<>& model = pictureModels[index];

		if(model.inputSize() != mcsamples[0].size() || model.outputSize() != eegTarget.size()){
			logging.warn("skipping bad picture prediction model");
			continue; // bad model/data => ignore
		}

		// calculates average error for this model using MC samples
		float error = 0.0f;

#pragma omp parallel for
		for(unsigned int mcindex=0;mcindex<mcsamples.size();mcindex++){
			auto x = mcsamples[mcindex];

			if(pictureData[index].preprocess(0, x) == false){
				logging.warn("skipping bad picture prediction model");
				continue;
			}

			math::vertex<> m;
			math::matrix<> cov;

			unsigned int SAMPLES = 50;
			
			if(model.getNumberOfSamples() < SAMPLES)
			  SAMPLES = model.getNumberOfSamples();
			
			if(model.calculate(x, m, cov, 1, SAMPLES) == false){
				logging.warn("skipping bad picture prediction model");
				continue;
			}

			if(pictureData[index].invpreprocess(1, m, cov) == false){
				logging.warn("skipping bad picture prediction model");
				continue;
			}

			m *= timestep; // corrects delta to given timelength
			cov *= timestep*timestep;

			cov /= whiteice::math::blas_real<float>((float)SAMPLES);

			// now we have prediction x to the response to the given keyword
			// calculates error (weighted distance to the target state)

			auto delta = target - (m + x);

			for(unsigned int i=0;i<delta.size();i++){
				delta[i] = math::abs(delta[i]) + math::sqrt(cov(i,i)); // Var[x - y] = Var[x] + Var[y]
				delta[i] /= targetVariance[i];
			}

			auto e = delta.norm();

			float ef = 0.0f;
			math::convert(ef, e);

#pragma omp critical(update_error2)
			{
				error += ef / mcsamples.size();
			}
		}

		if(error < bestError){
			bestError = error;
			bestPicture = index;
		}

		engine_pollEvents(); // polls for incoming events in case there are lots of models
	}
	
	if(bestPicture < 0){
		logging.error("Execute command couldn't find picture to show (no models?)");
		engine_pollEvents();
		return false;
	}
	else{
	        if(bestKeyword >= 0 && bestPicture >= 0){
		  char buffer[80];
		  snprintf(buffer, 80, "prediction model selected keyword/best picture: %s %s",
			   keywords[bestKeyword].c_str(), pictures[bestPicture].c_str());
		  logging.info(buffer);
		}
		else{
		  char buffer[80];
		  snprintf(buffer, 80, "prediction model selected best picture: %s",
			   pictures[bestPicture].c_str());
		  logging.info(buffer);
		}
	}


	// calculates estimates for MCMC samples [user state after stimulus] for the next round
	// we have two separate prediction models for this: keywords and pictures:
	// * for each sample we decide randomly which prediction model to use.
	// * additionally, we now postprocess samples to stay within [0,1] interval
	//   as our eeg-metasignals are always within [0,1]-range
	{
		for(auto& x : mcsamples){

			if((rand()&1) == 0 && bestKeyword >= 0){ // use keyword model to predict the outcome
				const auto& index = bestKeyword;

				whiteice::bayesian_nnetwork<>& model = keywordModels[index];

				if(keywordData[index].preprocess(0, x) == false){
					logging.error("mc sampling: skipping bad keyword prediction model");
					continue;
				}

				math::vertex<> m;
				math::matrix<> cov;
				
				unsigned int SAMPLES = 50;
				
				if(model.getNumberOfSamples() < SAMPLES)
				  SAMPLES = model.getNumberOfSamples();

				if(model.calculate(x, m, cov, 1 , SAMPLES) == false){
					logging.warn("skipping bad keyword prediction model");
					continue;
				}

				if(keywordData[index].invpreprocess(1, m, cov) == false){
					logging.error("mc sampling: skipping bad keyword prediction model");
					continue;
				}

				m *= timestep; // corrects delta to given timelength
				cov *= timestep*timestep;

				cov /= whiteice::math::blas_real<float>((float)SAMPLES);

				// TODO take a MCMC sample from N(mean, cov)

				x = m + x; // just uses mean value here..
			}
			else{ // use picture model to predict the outcome
				const auto& index = bestPicture;

				whiteice::bayesian_nnetwork<>& model = pictureModels[index];

				if(pictureData[index].preprocess(0, x) == false){
					logging.error("mc sampling: skipping bad picture prediction model");
					continue;
				}

				math::vertex<> m;
				math::matrix<> cov;

				unsigned int SAMPLES = 50;

				if(model.getNumberOfSamples() < SAMPLES)
				  SAMPLES = model.getNumberOfSamples();

				if(model.calculate(x, m, cov, 1, SAMPLES) == false){
					logging.warn("skipping bad picture prediction model");
					continue;
				}

				if(pictureData[index].invpreprocess(1, m, cov) == false){
					logging.error("mc sampling: skipping bad picture prediction model");
					continue;
				}

				// FIXME if output is PCAed then covariance matrix should be preprocessed too

				m *= timestep; // corrects delta to given timelength
				cov *= timestep*timestep;

				cov /= whiteice::math::blas_real<float>((float)SAMPLES);

				// TODO take a MCMC sample from N(mean, cov)

				x = m + x; // just uses mean value here..
			}

			// post-process predicted x to be within [0,1] interval
			for(unsigned int i=0;i<x.size();i++){
				if(x[i] < 0.0f) x[i] = 0.0f;
				else if(x[i] > 1.0f) x[i] = 1.0f;
			}

			// 20% of the samples will be reset to random points (viewer state "resets")
			if(rng.uniform() < 0.20){
			  for(unsigned int j=0;j<x.size();j++)
			    x[j] = rng.uniform(); // [0,1] valued signals sampled from [0,1]^D
			}

			engine_pollEvents(); // polls for incoming events in case there are lots of samples
		}
	}

	// now we have best picture and keyword that is predicted
	// to change users state to target value: show them	

	{
	  std::vector<float> synthParams;
	  if(synth){
	    synthParams.resize(synth->getNumberOfParameters());
	    for(unsigned int i=0;i<synthParams.size();i++)
	      synthParams[i] = 0.0f;
	  }

	  if(bestKeyword >= 0){
	    std::string message = keywords[bestKeyword];
	    engine_showScreen(message, bestPicture, synthParams);
	  }
	  else{
	    std::string message = " ";
	    engine_showScreen(message, bestPicture, synthParams);
	  }
	}

	engine_updateScreen();
	engine_pollEvents();

	return true;
}


// FIXME: optimize K-Means and HMM models in background
bool ResonanzEngine::engine_optimizeModels(unsigned int& currentHMMModel,
					   unsigned int& currentPictureModel, 
					   unsigned int& currentKeywordModel, 
					   bool& soundModelCalculated)
{
  // always first optimizes sound model
  
  if(synth == NULL && soundModelCalculated == false){
    soundModelCalculated = true; // we skip synth optimization
    logging.info("Audio/synth is disabled so skipping synthesizer optimizations");
    nnsynth->randomize();
  }

  if(currentHMMModel <= 1){
    // calculates K-Means and HMM models and saves them to disk

    if(kmeans == nullptr){
      kmeans = new whiteice::KMeans<>();
      std::vector<whiteice::math::vertex<> > eegTS; // eeg time-series

      if(eegData.getData(0,eegTS) == false){
	logging.error("Loading EEG data from datastructure failed");
	return false;
      }

      printf("KMeans: EEG input data size: %d\n", (int)eegTS.size()); // REMOVE ME
      
      if(kmeans->startTrain(KMEANS_NUM_CLUSTERS, eegTS) == false){
	logging.error("Starting K-Means optimization FAILED.");
	return false;
      }
    }
    else if(kmeans->isRunning() == true){
      // do nothing while optimizer runs

      char buffer[512];
      snprintf(buffer, 512, "resonanz K-Means optimization running. error: %f",
	       kmeans->getSolutionError());
      logging.info(buffer);
    }
    else if(kmeans->isRunning() == false && hmm == nullptr){
      // computation has stopped

      // saves optimization results to a file
      std::string filename = calculateHashName("KMeans" + eeg->getDataSourceName()) + ".kmeans";
      filename = currentCommand.modelDir + "/" + filename;

      if(kmeans->save(filename) == false){
	logging.error("Saving K-Means solution FAILED.");
	return false;
      }
      else logging.info("Saving K-Means solution OK.");

      // starts HMM optimizer
      hmm = new HMM(KMEANS_NUM_CLUSTERS, HMM_NUM_CLUSTERS);

      std::vector<unsigned int> observations;
      {
	// converts data to observation variables
	
	for(unsigned int i=0;i<eegData.size(0);i++){
	  unsigned int c = kmeans->getClusterIndex(eegData.access(0, i));
	  observations.push_back(c);
	}
      }
      
      if(hmm->startTrain(observations) == false){
	logging.error("Starting HMM optimization FAILED.");
	return false;
      }
      
    }
    else if(hmm->isRunning()){
      // does nothing during optimization

      char buffer[512];
      snprintf(buffer, 512, "resonanz HMM optimization running. log(prob): %f",
	       hmm->getSolutionGoodness());
      logging.info(buffer);
    }
    else if(hmm->isRunning() == false && currentHMMModel == 0){
      hmm->stopTrain();

      std::string filename = calculateHashName("HMM" + eeg->getDataSourceName()) + ".hmm";
      filename = currentCommand.modelDir + "/" + filename;

      if(hmm->saveArbitrary(filename) == false){
	logging.error("Saving HMM solution FAILED.");
	return false;
      }
      else logging.info("Saving HMM solution OK.");

      currentHMMModel++;
    }
    else if(hmmUpdator == nullptr && currentHMMModel == 1){
      
      hmmUpdator = new HMMStateUpdatorThread(kmeans, hmm,
					     &eegData,
					     &pictureData,
					     &keywordData,
					     &synthData);
      
      hmmUpdator->start();
    }
    else if(hmmUpdator->isRunning() == true){
      // does nothing during optimization

      char buffer[512];
      snprintf(buffer, 512, "resonanz HMM updates data classification (%d/%d).",
	       hmmUpdator->getProcessedElements(), (int)(pictureData.size() + keywordData.size()));
      logging.info(buffer);
    }
    else if(hmmUpdator->isRunning() == false){

      hmmUpdator->stop();

      delete hmmUpdator;
      hmmUpdator = nullptr;

      currentHMMModel++;
    }
    
  }
  
  else if(soundModelCalculated == false){
	  
    // first model to be optimized (no need to store the previous result)
    if(optimizer == nullptr && bayes_optimizer == nullptr){
      whiteice::math::vertex<> w;
      
      nnsynth->randomize();
      nnsynth->exportdata(w);
      
      {
	char buffer[512];
	snprintf(buffer, 512, "resonanz model optimization started: synth model. database size: %d %d",
		 synthData.size(0), synthData.size(1));
	fprintf(stdout, "%s\n", buffer);
	logging.info(buffer);
      }

      
      //optimizer = new whiteice::pLBFGS_nnetwork<>(*nnsynth, synthData, false, false);
      //optimizer->minimize(NUM_OPTIMIZER_THREADS);
      
      optimizer = new whiteice::math::NNGradDescent<>();
      optimizer->setUseMinibatch(true);
      if(optimizer->startOptimize(synthData, *nnsynth, 
				  NUM_OPTIMIZER_THREADS) == false){
	logging.error("nnsynth NNGradDescent::startOptimize() FAILED.");
      }
      
    }
    else if(optimizer != nullptr && use_bayesian_nnetwork){ // pre-optimizer is active
      
      whiteice::math::blas_real<float> error = 1000.0f;
      whiteice::math::vertex<> w;
      whiteice::nnetwork<> tmpnn;
      unsigned int iterations = 0;
      
      //optimizer->getSolution(w, error, iterations);
      optimizer->getSolution(tmpnn, error, iterations);
      
      if(iterations >= NUM_OPTIMIZER_ITERATIONS){
	// gets finished solution
	
	logging.info("DEBUG: ABOUT TO STOP COMPUTATION");
	
	optimizer->stopComputation();
	//optimizer->getSolution(w, error, iterations);
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz model NNGradDescent<> optimization stopped. synth model. iterations: %d error: %f",
		   iterations, error.c[0]);
	  logging.info(buffer);
	}
	
	nnsynth->importdata(w);
	
	// switches to uncertainty analysis
	const bool adaptive = true;
	
	logging.info("DEBUG: STARTING HMC SAMPLER");
	
	bayes_optimizer = new whiteice::UHMC<>(*nnsynth, synthData, adaptive);
	bayes_optimizer->setMinibatch(true);
	bayes_optimizer->startSampler();
	
	delete optimizer;
	optimizer = nullptr;
      }
      else{
	{
	  whiteice::nnetwork<> tmpnn;
	  math::vertex< math::blas_real<float> > w;
	  math::blas_real<float> error;
	  unsigned int iterations = 0;
	  
	  //optimizer->getSolution(w, error, iterations);
	  optimizer->getSolution(tmpnn, error, iterations);
	  tmpnn.exportdata(w);
	  
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz NNGradDescent<> model optimization running. synth model. number of iterations: %d/%d. error: %f",
		   iterations, NUM_OPTIMIZER_ITERATIONS, error.c[0]);
	  logging.info(buffer);
	}
      }
      
    }
    else if(bayes_optimizer != nullptr){
      if(bayes_optimizer->getNumberOfSamples() >= BAYES_NUM_SAMPLES){ // time to stop computation [got uncertainly information]
	
	bayes_optimizer->stopSampler();
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz bayes synth model optimization stopped. iterations: %d",
		   bayes_optimizer->getNumberOfSamples());
	  logging.info(buffer);
	}
	
	// saves optimization results to a file
	std::string dbFilename = currentCommand.modelDir + "/" +
	  calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".model";
	
	bayes_optimizer->getNetwork(*bnn);
	
	if(bnn->save(dbFilename) == false)
	  logging.error("saving bayesian nn configuration file failed");
	
	delete bayes_optimizer;
	bayes_optimizer = nullptr;
	
	soundModelCalculated = true;
      }
      else{
	unsigned int samples = bayes_optimizer->getNumberOfSamples();
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz bayes model optimization running. synth model. number of samples: %d/%d",
		   bayes_optimizer->getNumberOfSamples(), BAYES_NUM_SAMPLES);
	  logging.info(buffer);
	}
	
      }
    }
    else if(optimizer != nullptr){
      whiteice::math::blas_real<float> error = 1000.0f;
      whiteice::nnetwork<> tmpnn;
      whiteice::math::vertex<> w;
      unsigned int iterations = 0;
      
      //optimizer->getSolution(w, error, iterations);
      optimizer->getSolution(tmpnn, error, iterations);
      tmpnn.exportdata(w);
      
      if(iterations >= NUM_OPTIMIZER_ITERATIONS){
	// gets finished solution
	
	optimizer->stopComputation();
	//optimizer->getSolution(w, error, iterations);
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz model optimization stopped. synth model. iterations: %d error: %f",
		   iterations, error.c[0]);
	  logging.info(buffer);
	}
	
	// saves optimization results to a file
	std::string modelFilename = currentCommand.modelDir + "/" + 
	  calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".model";
	
	nnsynth->importdata(w);
	
	bnn->importNetwork(*nnsynth);
	
	if(bnn->save(modelFilename) == false)
	  logging.error("saving nn configuration file failed");
	
	delete optimizer;
	optimizer = nullptr;
	
	soundModelCalculated = true;
      }
      else{
	{
	  whiteice::nnetwork<> tmpnn;
	  math::vertex< math::blas_real<float> > w;
	  math::blas_real<float> error;
	  unsigned int iterations = 0;
	  
	  //optimizer->getSolution(w, error, iterations);
	  optimizer->getSolution(tmpnn, error, iterations);
	  tmpnn.exportdata(w);
	  
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz NNGradDescent<> model optimization running. synth model. number of iterations: %d/%d. error: %f",
		   iterations, NUM_OPTIMIZER_ITERATIONS, error.c[0]);
	  
	  logging.info(buffer);
	}
      }
    }
    
    
  }
  else if(currentPictureModel < pictureData.size() && optimizeSynthOnly == false){
    
    // first model to be optimized (no need to store the previous result)
    if(optimizer == nullptr && bayes_optimizer == nullptr){
      whiteice::math::vertex<> w;
      
      nn->randomize();
      nn->exportdata(w);
      
      //optimizer = new whiteice::pLBFGS_nnetwork<>(*nn, pictureData[currentPictureModel], false, false);
      //optimizer->minimize(NUM_OPTIMIZER_THREADS);
      
      optimizer = new whiteice::math::NNGradDescent<>();
      optimizer->setUseMinibatch(true);
      if(optimizer->startOptimize(pictureData[currentPictureModel], *nn,
				  NUM_OPTIMIZER_THREADS) == false){
	logging.error("nn NNGradDescent::startOptimize() FAILED.");
      }
      
      {
	char buffer[512];
	snprintf(buffer, 512, "resonanz model optimization started: picture %d database size: %d",
		 currentPictureModel, pictureData[currentPictureModel].size(0));
	fprintf(stdout, "%s\n", buffer);
	logging.info(buffer);
      }
      
    }
    else if(optimizer != nullptr && use_bayesian_nnetwork){ // pre-optimizer is active
      
      whiteice::math::blas_real<float> error = 1000.0f;
      whiteice::nnetwork<> tmpnn;
      whiteice::math::vertex<> w;
      unsigned int iterations = 0;
      
      //optimizer->getSolution(w, error, iterations);
      optimizer->getSolution(tmpnn, error, iterations);
      tmpnn.exportdata(w);
      
      if(iterations >= NUM_OPTIMIZER_ITERATIONS){
	// gets finished solution
	
	optimizer->stopComputation();
	//optimizer->getSolution(w, error, iterations);
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz model NNGradDescent<> optimization stopped. picture model. iterations: %d error: %f",
		   iterations, error.c[0]);
	  logging.info(buffer);
	}
	
	nn->importdata(w);
	
	// switches to uncertainty analysis
	const bool adaptive = true;
	
	bayes_optimizer = new whiteice::UHMC<>(*nn, pictureData[currentPictureModel], adaptive);
	bayes_optimizer->setMinibatch(true);
	bayes_optimizer->startSampler();
	
	delete optimizer;
	optimizer = nullptr;
      }
      else{
	{
	  char buffer[512];
	  unsigned int iterations;
	  whiteice::math::blas_real<float> error;
	  optimizer->getSolutionStatistics(error, iterations);
	  
	  //snprintf(buffer, 512, "resonanz L-BFGS model optimization running. picture model. number of iterations: %d/%d",
	  //   optimizer->getIterations(), NUM_OPTIMIZER_ITERATIONS);
	  snprintf(buffer, 512, "resonanz NNGradDescent<> model optimization running. picture model. number of iterations: %d/%d",
		   iterations, NUM_OPTIMIZER_ITERATIONS);
	  logging.info(buffer);
	}
      }
      
    }
    else if(bayes_optimizer != nullptr){
      if(bayes_optimizer->getNumberOfSamples() >= BAYES_NUM_SAMPLES){ // time to stop computation [got uncertainly information]
	
	bayes_optimizer->stopSampler();
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz bayes model optimization stopped. picture: %d iterations: %d",
		   currentPictureModel, bayes_optimizer->getNumberOfSamples());
	  logging.info(buffer);
	}
	
	// saves optimization results to a file
	std::string dbFilename = currentCommand.modelDir + "/" +
	  calculateHashName(pictures[currentPictureModel] + eeg->getDataSourceName()) + ".model";
	
	bayes_optimizer->getNetwork(*bnn);
	
	if(bnn->save(dbFilename) == false)
	  logging.error("saving bayesian nn configuration file failed");
	
	delete bayes_optimizer;
	bayes_optimizer = nullptr;
	
	// starts new computation
	currentPictureModel++;
	if(currentPictureModel < pictures.size()){
	  {
	    char buffer[512];
	    snprintf(buffer, 512, "resonanz bayes model optimization started: picture %d database size: %d",
		     currentPictureModel, pictureData[currentPictureModel].size(0));
	    fprintf(stdout, "%s\n", buffer);
	    logging.info(buffer);
	  }
	  
	  // actives pre-optimizer
	  whiteice::math::vertex<> w;
	  
	  nn->randomize();
	  nn->exportdata(w);
	  
	  //optimizer = new whiteice::pLBFGS_nnetwork<>(*nn, pictureData[currentPictureModel], false, false);
	  //optimizer->minimize(NUM_OPTIMIZER_THREADS);
	  
	  optimizer = new whiteice::math::NNGradDescent<>();
	  optimizer->setUseMinibatch(true);
	  if(optimizer->startOptimize(pictureData[currentPictureModel], *nn,
				      NUM_OPTIMIZER_THREADS) == false){
	    logging.error("nn NNGradDescent::startOptimize() FAILED.");
	  }
	  
	}
      }
      else{
	unsigned int samples = bayes_optimizer->getNumberOfSamples();
	
	if((samples % 100) == 0){
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz bayes model optimization running. picture: %d number of samples: %d/%d",
		   currentPictureModel, bayes_optimizer->getNumberOfSamples(), BAYES_NUM_SAMPLES);
	  logging.info(buffer);
	}
	
      }
    }
    else if(optimizer != nullptr){
      whiteice::math::blas_real<float> error = 1000.0f;
      whiteice::nnetwork<> tmpnn;
      whiteice::math::vertex<> w;
      unsigned int iterations = 0;
      
      //optimizer->getSolution(w, error, iterations);
      optimizer->getSolution(tmpnn, error, iterations);
      tmpnn.exportdata(w);
      
      if(iterations >= NUM_OPTIMIZER_ITERATIONS){
	// gets finished solution
	
	optimizer->stopComputation();
	//optimizer->getSolution(w, error, iterations);
	
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz model optimization stopped. picture: %d iterations: %d error: %f",
		   currentPictureModel, iterations, error.c[0]);
	  fprintf(stdout, "%s\n", buffer);
	  logging.info(buffer);
	}
	
	// saves optimization results to a file
	std::string dbFilename = currentCommand.modelDir + "/" +
	  calculateHashName(pictures[currentPictureModel] + eeg->getDataSourceName()) + ".model";
	nn->importdata(w);
	
	bnn->importNetwork(*nn);
	
	if(bnn->save(dbFilename) == false)
	  logging.error("saving nn configuration file failed");
	
	delete optimizer;
	optimizer = nullptr;
	
	// starts new computation
	currentPictureModel++;
	if(currentPictureModel < pictures.size()){
	  nn->randomize();
	  nn->exportdata(w);
	  
	  {
	    char buffer[512];
	    snprintf(buffer, 512, "resonanz model optimization started: picture %d database size: %d",
		     currentPictureModel, pictureData[currentPictureModel].size(0));
	    logging.info(buffer);
	  }
	  
	  //optimizer = new whiteice::pLBFGS_nnetwork<>(*nn, pictureData[currentPictureModel], false, false);
	  //optimizer->minimize(NUM_OPTIMIZER_THREADS);
	  
	  optimizer = new whiteice::math::NNGradDescent<>();
	  optimizer->setUseMinibatch(true);
	  if(optimizer->startOptimize(pictureData[currentPictureModel], *nn,
				      NUM_OPTIMIZER_THREADS) == false){
	    logging.error("nn NNGradDescent::startOptimize() FAILED.");
	  }
	}
      }
    }
    else{
      {
	math::vertex< math::blas_real<float> > w;
	whiteice::nnetwork<> tmpnn;
	math::blas_real<float> error;
	unsigned int iterations = 0;
	
	//optimizer->getSolution(w, error, iterations);
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	char buffer[512];
	snprintf(buffer, 512, "resonanz L-BFGS model optimization running. picture model %d/%d. number of iterations: %d/%d. error: %f",
		 currentPictureModel, (int)pictures.size(), 
		 iterations, NUM_OPTIMIZER_ITERATIONS, error.c[0]);
	fprintf(stdout, "%s\n", buffer);
	logging.info(buffer);
      }
    }
    
  }
  else if(currentKeywordModel < keywords.size() && optimizeSynthOnly == false){
    
    if(optimizer == nullptr && bayes_optimizer == nullptr){
      // first model to be optimized (no need to store the previous result)
      
      whiteice::math::vertex<> w;
      
      nnkey->randomize();
      nnkey->exportdata(w);
      
      //optimizer = new whiteice::pLBFGS_nnetwork<>(*nn, keywordData[currentKeywordModel], false, false);
      //optimizer->minimize(NUM_OPTIMIZER_THREADS);
      
      optimizer = new whiteice::math::NNGradDescent<>();
      optimizer->setUseMinibatch(true);
      if(optimizer->startOptimize(keywordData[currentKeywordModel], *nnkey,
				  NUM_OPTIMIZER_THREADS) == false){
	logging.error("nnkey NNGradDescent::startOptimize() FAILED.");
      }
      
      {
	char buffer[512];
	snprintf(buffer, 512, "resonanz model optimization started: keyword %d database size: %d",
		 currentKeywordModel, keywordData[currentKeywordModel].size(0));
	fprintf(stdout, "%s\n", buffer);
	logging.info(buffer);
      }
      
    }
    else if(optimizer != nullptr && use_bayesian_nnetwork){ // pre-optimizer is active
      
      whiteice::math::blas_real<float> error = 1000.0f;
      whiteice::nnetwork<> tmpnn;
      whiteice::math::vertex<> w;
      unsigned int iterations = 0;
      
      //optimizer->getSolution(w, error, iterations);
      optimizer->getSolution(tmpnn, error, iterations);
      tmpnn.exportdata(w);
      
      if(iterations >= NUM_OPTIMIZER_ITERATIONS){
	// gets finished solution
	
	optimizer->stopComputation();
	
	//optimizer->getSolution(w, error, iterations);
	
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz model NNGradDescent<> optimization stopped. keyword model. iterations: %d error: %f",
		   iterations, error.c[0]);
	  logging.info(buffer);
	}
	
	nnkey->importdata(w);
	
	// switches to bayesian optimizer
	const bool adaptive = true;
	
	bayes_optimizer = new whiteice::UHMC<>(*nnkey, keywordData[currentKeywordModel], adaptive);
	bayes_optimizer->setMinibatch(true);
	bayes_optimizer->startSampler();
	
	delete optimizer;
	optimizer = nullptr;
      }
      else{
	{
	  char buffer[512];
	  unsigned int iterations;
	  whiteice::math::blas_real<float> error;
	  optimizer->getSolutionStatistics(error, iterations);
	  
	  //snprintf(buffer, 512, "resonanz L-BFGS model optimization running. keyword model. number of iterations: %d/%d",
	  //	   optimizer->getIterations(), NUM_OPTIMIZER_ITERATIONS);
	  snprintf(buffer, 512, "resonanz NNGradDescent<> model optimization running. keyword model. number of iterations: %d/%d",
		   iterations, NUM_OPTIMIZER_ITERATIONS);
	  logging.info(buffer);
	}
      }
      
    }
    else if(bayes_optimizer != nullptr){
      if(bayes_optimizer->getNumberOfSamples() >= BAYES_NUM_SAMPLES){ // time to stop computation
	
	bayes_optimizer->stopSampler();
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz bayes model optimization stopped. keyword: %d iterations: %d",
		   currentKeywordModel, bayes_optimizer->getNumberOfSamples());
	  logging.info(buffer);
	}
	
	// saves optimization results to a file
	std::string dbFilename = currentCommand.modelDir + "/" +
	  calculateHashName(keywords[currentKeywordModel] + eeg->getDataSourceName()) + ".model";
	
	bayes_optimizer->getNetwork(*bnn);
	
	if(bnn->save(dbFilename) == false)
	  logging.error("saving bayesian nn configuration file failed");
	
	delete bayes_optimizer;
	bayes_optimizer = nullptr;
	
	// starts new computation
	currentKeywordModel++;
	if(currentKeywordModel < keywords.size()){
	  {
	    char buffer[512];
	    snprintf(buffer, 512, "resonanz bayes model optimization started: keyword %d database size: %d",
		     currentKeywordModel, keywordData[currentKeywordModel].size(0));
	    fprintf(stdout, "%s\n", buffer);
	    logging.info(buffer);
	  }
	  
	  whiteice::math::vertex<> w;
	  nnkey->randomize();
	  nnkey->exportdata(w);
	  
	  //optimizer = new whiteice::pLBFGS_nnetwork<>(*nn, keywordData[currentKeywordModel], false, false);
	  //optimizer->minimize(NUM_OPTIMIZER_THREADS);
	  
	  optimizer = new whiteice::math::NNGradDescent<>();
	  optimizer->setUseMinibatch(true);
	  if(optimizer->startOptimize(keywordData[currentKeywordModel], *nnkey, 
				      NUM_OPTIMIZER_THREADS)){
	    logging.error("nnkey NNGradDescent::startOptimize() FAILED.");
	  }
	}
      }
      else{
	unsigned int samples = bayes_optimizer->getNumberOfSamples();
	
	if((samples % 100) == 0){
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz bayes model optimization running. keyword: %d number of samples: %d/%d",
		   currentKeywordModel, bayes_optimizer->getNumberOfSamples(), BAYES_NUM_SAMPLES);
	  logging.info(buffer);
	}
	
      }
    }
    else if(optimizer != nullptr){
      whiteice::math::blas_real<float> error = 1000.0f;
      whiteice::nnetwork<> tmpnn;
      whiteice::math::vertex<> w;
      unsigned int iterations = 0;
      
      //optimizer->getSolution(w, error, iterations);
      optimizer->getSolution(tmpnn, error, iterations);
      tmpnn.exportdata(w);
      
      if(iterations >= NUM_OPTIMIZER_ITERATIONS){
	// gets finished solution
	
	optimizer->stopComputation();
	
	//optimizer->getSolution(w, error, iterations);
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	
	{
	  char buffer[512];
	  snprintf(buffer, 512, "resonanz model optimization stopped. keyword: %d iterations: %d error: %f",
		   currentKeywordModel, iterations, error.c[0]);
	  fprintf(stdout, "%s\n", buffer);
	  logging.info(buffer);
	}
	
	// saves optimization results to a file
	std::string dbFilename = currentCommand.modelDir + "/" +
	  calculateHashName(keywords[currentKeywordModel] + eeg->getDataSourceName()) + ".model";
	nnkey->importdata(w);
	
	bnn->importNetwork(*nnkey);
	
	if(bnn->save(dbFilename) == false)
	  logging.error("saving nn configuration file failed");
	
	delete optimizer;
	optimizer = nullptr;
	
	// starts new computation
	currentKeywordModel++;
	if(currentKeywordModel < keywords.size()){
	  nnkey->randomize();
	  nnkey->exportdata(w);
	  
	  {
	    char buffer[512];
	    snprintf(buffer, 512, "resonanz model optimization started: keyword %d database size: %d",
		     currentKeywordModel, keywordData[currentKeywordModel].size(0));
	    logging.info(buffer);
	  }
	  
	  //optimizer = new whiteice::pLBFGS_nnetwork<>(*nn, keywordData[currentKeywordModel], false, false);
	  //optimizer->minimize(NUM_OPTIMIZER_THREADS);
	  
	  optimizer = new whiteice::math::NNGradDescent<>();
	  optimizer->setUseMinibatch(true);
	  if(optimizer->startOptimize(keywordData[currentKeywordModel], *nnkey, 
				      NUM_OPTIMIZER_THREADS) == false){
	    logging.error("nnkey NNGradDescent::startOptimize() FAILED.");
	  }
	}
      }
    }
    else{
      {
	math::vertex< math::blas_real<float> > w;
	whiteice::nnetwork<> tmpnn;
	math::blas_real<float> error;
	unsigned int iterations = 0;
	
	//optimizer->getSolution(w, error, iterations);
	optimizer->getSolution(tmpnn, error, iterations);
	tmpnn.exportdata(w);
	
	char buffer[512];
	snprintf(buffer, 512, "resonanz NNGradDescent<> model optimization running. keyword model %d/%d. number of iterations: %d/%d. error: %f",
		 currentKeywordModel, (int)keywords.size(), 
		 iterations, NUM_OPTIMIZER_ITERATIONS, error.c[0]);
	logging.info(buffer);
      }
    }
  }
  else{ // both synth, picture and keyword models has been computed or
    // optimizeSynthOnly == true and only synth model has been computed => stop
    cmdStopCommand();
  }
  
  return true;
}



// estimate output value N(m,cov) for x given dataset data uses nearest neighbourhood estimation (distance)
bool ResonanzEngine::engine_estimateNN(const whiteice::math::vertex<>& x,
				       const whiteice::dataset<>& data,
				       whiteice::math::vertex<>& m,
				       whiteice::math::matrix<>& cov)
{
  bool bad_data = false;
  
  if(data.size(0) <= 0) bad_data = true;
  if(data.getNumberOfClusters() != 2) bad_data = true;
  if(bad_data == false){
    if(data.size(0) != data.size(1))
      bad_data = true;
  }
  
  if(bad_data){
    m = x;
    cov.resize(x.size(), x.size());
    cov.identity();
    
    return true;
  }

  const unsigned int YMAX = data.access(1,0).size();
  
  m.resize(YMAX);
  m.zero();
  cov.resize(YMAX,YMAX);
  cov.zero();
  const float epsilon    = 0.01f;
  math::blas_real<float> sumweights = 0.0f;

  
  for(unsigned int i=0;i<data.size(0);i++){
    auto delta = x - data.access(0, i);
    
    auto w = math::blas_real<float>(1.0f / (epsilon + delta.norm().c[0]));
	  
    auto v = data.access(1, i);
    
    m += w*v;
    cov += w*v.outerproduct();
    
    sumweights += w;
  }
  
  m /= sumweights;
  cov /= sumweights;
  
  cov -= m.outerproduct();
  
  return true;
}



void ResonanzEngine::engine_setStatus(const std::string& msg) throw()
{
  try{
    std::lock_guard<std::mutex> lock(status_mutex);
    engineState = msg;
    logging.info(msg);
  }
  catch(std::exception& e){ }
}


void ResonanzEngine::engine_sleep(int msecs)
{
  // sleeps for given number of milliseconds
  
  // currently just sleeps()
  std::chrono::milliseconds duration(msecs);
  std::this_thread::sleep_for(duration);
}


bool ResonanzEngine::engine_checkIncomingCommand()
{
  logging.info("checking command");
  
  if(incomingCommand == nullptr) return false;
  
  std::lock_guard<std::mutex> lock(command_mutex);
  
  if(incomingCommand == nullptr) 
    return false; // incomingCommand changed while acquiring mutex..
  
  currentCommand = *incomingCommand;
  
  delete incomingCommand;
  incomingCommand = nullptr;
  
  return true;
}


bool ResonanzEngine::engine_loadMedia(const std::string& picdir, const std::string& keyfile, bool loadData)
{
  std::vector<std::string> tempKeywords;
  
  // first we get picture names from directories and keywords from keyfile
  if(this->loadWords(keyfile, tempKeywords) == false){
    logging.warn("loading keyword file FAILED.");
  }
  
  
  std::vector<std::string> tempPictures;
  
  if(this->loadPictures(picdir, tempPictures) == false){
    logging.error("loading picture filenames FAILED.");
    return false;
  }
  
  
  pictures = tempPictures;
  keywords = tempKeywords;
  
  for(unsigned int i=0;i<images.size();i++){
    if(images[i] != nullptr)
      SDL_FreeSurface(images[i]);
    images[i] = nullptr;
  }

  images.clear();
  imageFeatures.clear();
  
  if(loadData){
    images.resize(pictures.size());
    imageFeatures.resize(pictures.size());
    
    std::vector<float> synthParams;
    if(synth){
      synthParams.resize(synth->getNumberOfParameters());
      for(unsigned int i=0;i<synthParams.size();i++)
	synthParams[i] = 0.0f;
    }
    
    
    for(unsigned int i=0;i<images.size();i++){
      images[i] = nullptr;

      {
	char buffer[80];
	
	snprintf(buffer, 80,
		 "resonanz-engine: loading media files (%.1f%%)..",
		 100.0f*(((float)i)/((float)images.size())));
	engine_setStatus(buffer);
      }
      
      engine_showScreen("Loading..", i, synthParams); // loads picture if it is not loaded yet.

      // calculates feature vector from picture
      {
	std::vector<float> features;
	whiteice::math::vertex<> f;
	
	f.resize(PICFEATURES_SIZE);
	f.zero();

	calculatePicFeatureVector(images[i], features);

	for(unsigned int j=0;j<features.size() && j< f.size();j++)
	  f[j] = features[j];

	imageFeatures[i] = f;
      }
      
      engine_pollEvents();
      engine_updateScreen();
    }
    
    engine_pollEvents();
    engine_updateScreen();
  }
  else{
    images.clear();
  }
  
  return true;
}


bool ResonanzEngine::engine_loadDatabase(const std::string& modelDir)
{
  std::lock_guard<std::mutex> lock(database_mutex);

  latestModelDir = modelDir;
  
  keywordData.resize(keywords.size());
  pictureData.resize(pictures.size());
  
  std::string name1 = "input";
  std::string name2 = "output";

  float eeg_num_samples     = 0.0f;
  float keyword_num_samples = 0.0f;
  float picture_num_samples = 0.0f;
  float synth_num_samples   = 0.0f;

  // debugs eeg dimensions to log file at loadDatabase()
  {
    char buffer[80];
    sprintf(buffer, "engine_loadDatabase(): EEG DIMENSIONS: %d", eeg->getNumberOfSignals());
    logging.info(buffer);
  }

  // loads EEG stream values
  {
    std::string dbFilename = modelDir + "/" + calculateHashName("eegData" + eeg->getDataSourceName()) + ".ds";
    std::string eegName = "Pure EEG data";

    if(eegData.load(dbFilename) == false){
      logging.info("Couldn't load EEG data => creating empty database");
      eegData.clear();
      eegData.createCluster(eegName, eeg->getNumberOfSignals());
      eegData.createCluster("index", 1);
    }
    else if(eegData.getNumberOfClusters() != 2){
      logging.info("Couldn't load EEG data => creating empty database");
      eegData.clear();
      eegData.createCluster(eegName, eeg->getNumberOfSignals());
      eegData.createCluster("index", 1);
    }

    eeg_num_samples = eegData.size(0);

    {
      if(pcaPreprocess){
	if(eegData.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing EEG measurements [input]");
	  eegData.preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	// keywordData[i].convert(1);
      }
      else{
	if(eegData.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of EEG measurements [input]");
	  eegData.convert(0); // removes all preprocessings from input
	}
	
	eegData.preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
      }
    }
  }
    
  
  // loads databases into memory or initializes new ones
  for(unsigned int i=0;i<keywords.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".ds";
    
    keywordData[i].clear();
    
    if(keywordData[i].load(dbFilename) == false){
      logging.info("Couldn't load keyword data => creating empty database");
      
      keywordData[i].createCluster(name1, eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
      keywordData[i].createCluster(name2, eeg->getNumberOfSignals());
      keywordData[i].createCluster("index", 1);
    }
    else{
      if(keywordData[i].getNumberOfClusters() != 3){
	logging.error("Keyword data wrong number of clusters or data corruption => reset database");
	
	keywordData[i].clear();
	keywordData[i].createCluster(name1, eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS);
	keywordData[i].createCluster(name2, eeg->getNumberOfSignals());
	keywordData[i].createCluster("index", 1);
      }
    }
    
    
    const unsigned int datasize = keywordData[i].size(0);
    
    if(keywordData[i].removeBadData() == false)
      logging.warn("keywordData: bad data removal failed");
    
    if(datasize != keywordData[i].size(0)){
      char buffer[80];
      snprintf(buffer, 80, "Keyword %d: bad data removal reduced data: %d => %d\n",
	       i, datasize, keywordData[i].size(0));
      logging.warn(buffer);
    }
    
    keyword_num_samples += keywordData[i].size(0);
    
    {
      if(pcaPreprocess){
	if(keywordData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing keyword measurements [input]");
	  keywordData[i].preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	if(keywordData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing keyword measurements [output]");
	  keywordData[i].preprocess(1, whiteice::dataset<>::dnCorrelationRemoval);
	}
	// keywordData[i].convert(1);
      }
      else{
	if(keywordData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of keyword measurements [input]");
	  keywordData[i].convert(0); // removes all preprocessings from input
	}
	if(keywordData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of keyword measurements [output]");
	  keywordData[i].convert(1); // removes all preprocessings from output
	}
	
	keywordData[i].preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
	keywordData[i].preprocess(1, whiteice::dataset<>::dnMeanVarianceNormalization);
	
	// keywordData[i].convert(1);
      }
      
      // keywordData[i].convert(1); // removes all preprocessings from output
    }
    
  }
  
  logging.info("keywords measurement database loaded");
	
  
  
  for(unsigned int i=0;i<pictures.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(pictures[i] + eeg->getDataSourceName()) + ".ds";
    
    pictureData[i].clear();
    
    if(pictureData[i].load(dbFilename) == false){
      logging.info("Couldn't load picture data => creating empty database");
      
      pictureData[i].createCluster(name1, eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
      pictureData[i].createCluster(name2, eeg->getNumberOfSignals());
      pictureData[i].createCluster("index", 1);
      
    }
    else{
      if(pictureData[i].getNumberOfClusters() != 3){
	logging.error("Picture data wrong number of clusters or data corruption => reset database");
	
	pictureData[i].clear();
	pictureData[i].createCluster(name1, eeg->getNumberOfSignals() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
	pictureData[i].createCluster(name2, eeg->getNumberOfSignals());
	pictureData[i].createCluster("index", 1);
      }
    }
    
    const unsigned int datasize = pictureData[i].size(0);
    
    if(pictureData[i].removeBadData() == false)
      logging.warn("pictureData: bad data removal failed");
    
    if(datasize != pictureData[i].size(0)){
      char buffer[80];
      snprintf(buffer, 80, "Picture %d: bad data removal reduced data: %d => %d\n",
	       i, datasize, pictureData[i].size(0));
      logging.warn(buffer);
    }
    
    picture_num_samples += pictureData[i].size(0);
    
    
    {
      if(pcaPreprocess){
	if(pictureData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing picture measurements [input]");
	  pictureData[i].preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
	if(pictureData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing picture measurements [output]");
	  pictureData[i].preprocess(1, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
	// pictureData[i].convert(1);
      }
      else{
	if(pictureData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of picture measurements [input]");
	  pictureData[i].convert(0); // removes all preprocessings from input
	}
	if(pictureData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of picture measurements [output]");
	  pictureData[i].convert(1); // removes all preprocessings from output
	}
	
	pictureData[i].preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
	pictureData[i].preprocess(1, whiteice::dataset<>::dnMeanVarianceNormalization);
	
	// pictureData[i].convert(1);
      }
      
      // pictureData[i].convert(1); // removes all preprocessings from output
    }
  }
  
  logging.info("picture measurement database loaded");
  
  // loads synth parameters data into memory
  if(synth){
    std::string dbFilename = modelDir + "/" + calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".ds";
    
    synthData.clear();
    
    if(synthData.load(dbFilename) == false){
      synthData.createCluster(name1, eeg->getNumberOfSignals() + 2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
      synthData.createCluster(name2, eeg->getNumberOfSignals());
      synthData.createCluster("index", 1);
      logging.info("Couldn't load synth data => creating empty database");
      return false;
    }
    else{
      if(synthData.getNumberOfClusters() != 3){
	logging.error("Synth data wrong number of clusters or data corruption => reset database");
	synthData.clear();
	synthData.createCluster(name1, eeg->getNumberOfSignals() + 2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
	synthData.createCluster(name2, eeg->getNumberOfSignals());
	synthData.createCluster("index", 1);
	return false;
      }
    }
    
    const unsigned int datasize = synthData.size(0);
    
    if(synthData.removeBadData() == false)
      logging.warn("synthData: bad data removal failed");
    
    if(datasize != synthData.size(0)){
      char buffer[80];
      snprintf(buffer, 80, "Synth data: bad data removal reduced data: %d => %d\n",
	       datasize, synthData.size(0));
      logging.warn(buffer);
    }
    
    synth_num_samples += synthData.size(0);
    
    {
      if(pcaPreprocess){
	if(synthData.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing sound measurements [input]");
	  synthData.preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
	if(synthData.hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing sound measurements [output]");
	  synthData.preprocess(1, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
      }
      else{
	if(synthData.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of sound measurements [input]");
	  synthData.convert(0); // removes all preprocessings from input
	}
	
	if(synthData.hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of sound measurements [output]");
	  synthData.convert(1); // removes all preprocessings from input
	}
	
	synthData.preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
	synthData.preprocess(1, whiteice::dataset<>::dnMeanVarianceNormalization);
	
	// synthData.convert(1);
      }
    }
    
    logging.info("synth measurement database loaded");
  }
  
  
  // reports average samples in each dataset
  {
    if(keywordData.size() > 0){
      keyword_num_samples /= keywordData.size();
    }
    else{
      keyword_num_samples = 0.0f;
    }
    
    picture_num_samples /= pictureData.size();
    synth_num_samples   /= 1;
    
    char buffer[128];
    snprintf(buffer, 128, 
	     "measurements database loaded: %.1f EEG stream samples %.1f [samples/picture] %.1f [samples/keyword] %.1f [synth samples]", eeg_num_samples, picture_num_samples, keyword_num_samples, synth_num_samples);
    
    logging.info(buffer);
  }
  
  
  
  return true;
}


bool ResonanzEngine::engine_storeMeasurement(unsigned int pic, unsigned int key, 
					     const std::vector<float>& eegBefore, 
					     const std::vector<float>& eegAfter,
					     const std::vector<float>& synthBefore,
					     const std::vector<float>& synthAfter)
{
  if(eegBefore.size() != eegAfter.size()){
    logging.error("store measurement: eegBefore != eegAfter");
    return false;
  }

  // t5 is for pictures with picfeatures
  std::vector< whiteice::math::blas_real<float> > t1, t2, t3, t4, t5;
  
  t1.resize(eegBefore.size() + HMM_NUM_CLUSTERS);
  t2.resize(eegAfter.size());
  t3.resize(eegAfter.size());
  t4.resize(1);

  t5.resize(eegBefore.size() + HMM_NUM_CLUSTERS + PICFEATURES_SIZE);
  
  // initialize to zero [no bad data possible]
  for(auto& t : t1) t = 0.0f; 
  for(auto& t : t2) t = 0.0f;
  for(auto& t : t3) t = 0.0f;
  t4[0] = 0.0f;
  for(auto& t : t5) t = 0.0f;
  
  // heavy checks against correctness of the data because buggy code/hardware
  // seem to introduce bad measurment data into database..
  
  const whiteice::math::blas_real<float> delta = MEASUREMODE_DELAY_MS/1000.0f;
  
  for(unsigned int i=0;i<eegBefore.size();i++){
    auto& before = eegBefore[i];
    auto& after  = eegAfter[i];
    
    if(before < 0.0f){
      logging.error("store measurement. bad data: eegBefore < 0.0");
      return false;
    }
    else if(before > 1.0f){
      logging.error("store measurement. bad data: eegBefore > 1.0");
      return false;
    }
    else if(whiteice::math::isnan(before) || whiteice::math::isinf(before)){
      logging.error("store measurement. bad data: eegBefore is NaN or Inf");
      return false;
    }
    
    if(after < 0.0f){
      logging.error("store measurement. bad data: eegAfter < 0.0");
      return false;
    }
    else if(after > 1.0f){
      logging.error("store measurement. bad data: eegAfter > 1.0");
      return false;
    }
    else if(whiteice::math::isnan(after) || whiteice::math::isinf(after)){
      logging.error("store measurement. bad data: eegAfter is NaN or Inf");
      return false;
    }
    
	        
    t1[i] = eegBefore[i];
    t2[i] = (eegAfter[i] - eegBefore[i])/delta; // stores aprox "derivate": dEEG/dt
    t3[i] = eegAfter[i];
  }

  t4[0] = eegData.size(0);

  
  // updates HMM brain state model's state
  {
    std::lock_guard<std::mutex> lock(hmm_mutex);

    if(kmeans == NULL || hmm == NULL){
      logging.warn("WARN: engine_storeMeasurement(): K-Means or HMM model doesn't exist. Doesn't save HMM brain state with data!");

      HMMstate = 0; // DISABLE ADDING BRAINSTATE CLASSIFICATION TO DATA
    }
    else{
#if 0
      // HMMstate updated by main loop for now
      unsigned int dataCluster = kmeans->getClusterIndex(t1);
      unsigned int nextState = 0;
      hmm->next_state(HMMstate, nextState, dataCluster);
      HMMstate = nextState;
#endif
    }
  }
  


  for(unsigned int i=eegBefore.size();i<t1.size();i++){
    if(i-eegBefore.size() == HMMstate) t1[i] = 1.0f;
    else t1[i] = 0.0f;
  }
  
  if(key < keywordData.size()){
    bool b1 = true, b2 = true, b3 = true;
    
    if((b1 = keywordData[key].add(0, t1)) == false ||
       (b2 = keywordData[key].add(1, t2)) == false ||
       (b3 = keywordData[key].add(2, t4)) == false)
    {
      logging.error("Adding new keyword data FAILED");
      return false;
    }
  }
  
  if(pic < pictureData.size()){

    for(unsigned int i=0;i<t1.size();i++)
      t5[i] = t1[i];

    // picture feature vector to input of picture models (to store in cluster 0 input data)
    for(unsigned int i=t1.size();i<t5.size();i++)
      t5[i] = imageFeatures[pic][i-t1.size()].c[0];
    
    if(pictureData[pic].add(0, t5) == false || pictureData[pic].add(1, t2) == false || pictureData[pic].add(2, t4) == false){
      logging.error("Adding new picture data FAILED");
      return false;
    }
  }

  if(eegData.add(0, t3) == false || eegData.add(1, t4) == false){
    logging.error("Adding EEG measurement FAILED");
    return false;
  }

  // FIXME: don't handle HMM brain states at all
  if(synth){
    std::vector< whiteice::math::blas_real<float> > input, output;
    
    input.resize(synthBefore.size() + synthAfter.size() + eegBefore.size() + HMM_NUM_CLUSTERS);
    output.resize(eegAfter.size());
    
    // initialize to zero [no bad data possible]
    for(auto& t : input)  t = 0.0f; 
    for(auto& t : output) t = 0.0f;
    
    for(unsigned int i=0;i<synthBefore.size();i++){
      if(synthBefore[i] < 0.0f){
	logging.error("store measurement. bad data: synthBefore < 0.0");
	return false;
      }
      else if(synthBefore[i] > 1.0f){
	logging.error("store measurement. bad data: synthBefore > 1.0");
	return false;
      }
      else if(whiteice::math::isnan(synthBefore[i]) || whiteice::math::isinf(synthBefore[i])){
	logging.error("store measurement. bad data: synthBefore is NaN or Inf");
	return false;
      }
      
      input[i] = synthBefore[i];
    }
    
    for(unsigned int i=0;i<synthAfter.size();i++){
      if(synthAfter[i] < 0.0f){
	logging.error("store measurement. bad data: synthAfter < 0.0");
	return false;
      }
      else if(synthAfter[i] > 1.0f){
	logging.error("store measurement. bad data: synthAfter > 1.0");
	return false;
      }
      else if(whiteice::math::isnan(synthAfter[i]) || whiteice::math::isinf(synthAfter[i])){
	logging.error("store measurement. bad data: synthAfter is NaN or Inf");
	return false;
      }
      
      input[synthBefore.size() + i] = synthAfter[i];
    }
    
    for(unsigned int i=0;i<eegBefore.size();i++){
      input[synthBefore.size()+synthAfter.size() + i] = eegBefore[i];
    }

    // adds HMM state value
    input[synthBefore.size()+synthAfter.size()+eegBefore.size() + HMMstate] = 1.0f;
    
    for(unsigned int i=0;i<eegAfter.size();i++){
      // output[i] = (eegAfter[i] - eegBefore[i])/delta; // dEEG/dt
      output[i] = t2[i];
    }
    
    if(synthData.add(0, input) == false || synthData.add(1, output) == false || synthData.add(2, t4) == false){
      logging.error("Adding new synth data FAILED");
      return false;
    }
  }
  
  return true;
}


bool ResonanzEngine::engine_saveDatabase(const std::string& modelDir)
{
  std::lock_guard<std::mutex> lock(database_mutex);

  
  // saves eegData to files
  {
    std::string dbFilename = modelDir + "/" + calculateHashName("eegData" + eeg->getDataSourceName()) + ".ds";

    if(eegData.getNumberOfClusters() != 2) return false;

    eegData.convert(0);

    if(eegData.preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization) == false)
      return false;
    
    if(eegData.save(dbFilename) == false){
      logging.info("Couldn't save EEG data");
      return false;
    }
  }
  
  // saves databases from memory
  for(unsigned int i=0;i<keywordData.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".ds";
    
    if(keywordData[i].removeBadData() == false)
      logging.warn("keywordData: bad data removal failed");
    
    
    {
      if(pcaPreprocess){
	if(keywordData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing keyword measurements data [input]");
	  keywordData[i].preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	if(keywordData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing keyword measurements data [output]");
	  keywordData[i].preprocess(1, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
	// keywordData[i].convert(1); // removes preprocessigns from output
      }
      else{
	if(keywordData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA preprocessing from keyword measurements data [input]");
	  keywordData[i].convert(0); // removes all preprocessings from input
	}
	
	if(keywordData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA preprocessing from keyword measurements data [output]");
	  keywordData[i].convert(1); // removes all preprocessings from input
	}

	keywordData[i].convert(0);
	keywordData[i].convert(1);
	
	keywordData[i].preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
	keywordData[i].preprocess(1, whiteice::dataset<>::dnMeanVarianceNormalization);
	
	// keywordData[i].convert(1); // removes preprocessigns from output
      }
      
      // keywordData[i].convert(1); // removes all preprocessings from output
    }
    
    if(keywordData[i].save(dbFilename) == false){
      logging.error("Saving keyword data failed");
      return false;
    }
  }
  
  
  for(unsigned int i=0;i<pictureData.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(pictures[i] + eeg->getDataSourceName()) + ".ds";
    
    if(pictureData[i].removeBadData() == false)
      logging.warn("pictureData: bad data removal failed");
    
    
    {
      if(pcaPreprocess){
	if(pictureData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing picture measurements data [input]");
	  pictureData[i].preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	if(pictureData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing picture measurements data [output]");
	  pictureData[i].preprocess(1, whiteice::dataset<>::dnCorrelationRemoval);
	}
	// pictureData[i].convert(1); // removes preprocessigns from output
      }
      else{
	if(pictureData[i].hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA preprocessing from picture measurements data [input]");
	  pictureData[i].convert(0); // removes all preprocessings from input
	}
	if(pictureData[i].hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA preprocessing from picture measurements data [output]");
	  pictureData[i].convert(1); // removes all preprocessings from input
	}

	pictureData[i].convert(0);
	pictureData[i].convert(1);
	
	pictureData[i].preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
	pictureData[i].preprocess(1, whiteice::dataset<>::dnMeanVarianceNormalization);
	
	// pictureData[i].convert(1);
      }
      
      // pictureData[i].convert(1); // removes all preprocessings from output
    }
    
    if(pictureData[i].save(dbFilename) == false){
      logging.error("Saving picture data failed");
      return false;
    }
  }
  
  // stores sound synthesis measurements
  if(synth){
    std::string dbFilename = modelDir + "/" + calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".ds";
    
    if(synthData.removeBadData() == false)
      logging.warn("synthData: bad data removal failed");
    
    {
      if(pcaPreprocess){
	if(synthData.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing sound measurements [input]");
	  synthData.preprocess(0, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
	if(synthData.hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == false){
	  logging.info("PCA preprocessing sound measurements [output]");
	  synthData.preprocess(1, whiteice::dataset<>::dnCorrelationRemoval);
	}
	
	// synthData.convert(1);
      }
      else{
	if(synthData.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of sound measurements [input]");
	  synthData.convert(0); // removes all preprocessings from input
	}
	if(synthData.hasPreprocess(1, whiteice::dataset<>::dnCorrelationRemoval) == true){
	  logging.info("Removing PCA processing of sound measurements [output]");
	  synthData.convert(1); // removes all preprocessings from input
	}
	
	synthData.preprocess(0, whiteice::dataset<>::dnMeanVarianceNormalization);
	synthData.preprocess(1, whiteice::dataset<>::dnMeanVarianceNormalization);
	
	// synthData.convert(1);
      }
    }
    
    if(synthData.save(dbFilename) == false){
      logging.info("Saving synth data failed");
      return false;
    }
  }
  
  
  return true;
}


std::string ResonanzEngine::calculateHashName(const std::string& filename) const
{
  try{
    unsigned int N = strlen(filename.c_str()) + 1;
    unsigned char* data = (unsigned char*)malloc(sizeof(unsigned char)*N);
    unsigned char* hash160 = (unsigned char*)malloc(sizeof(unsigned char)*20);
    
    whiteice::crypto::SHA sha(160);
    
    memcpy(data, filename.c_str(), N);
    
    if(sha.hash(&data, N, hash160)){
      std::string result = "";
      for(unsigned int i=0;i<20;i++){
	char buffer[10];
	snprintf(buffer, 10, "%.2x", hash160[i]);
	result += buffer;
      }
      
      if(data) free(data);
      if(hash160) free(hash160);
      
      // printf("%s => %s\n", filename.c_str(), result.c_str());
      
      return result; // returns hex hash of the name
    }
    else{
      if(data) free(data);
      if(hash160) free(hash160);
      
      return "";
    }
  }
  catch(std::exception& e){
    return "";
  }
}


bool ResonanzEngine::engine_showScreen(const std::string& message, unsigned int picture,
				       const std::vector<float>& synthParams)
{
  SDL_Surface* surface = SDL_GetWindowSurface(window);
  if(surface == nullptr)
    return false;
  
  if(SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0)) != 0)
    return false;
  
  int bgcolor = 0;
  int elementsDisplayed = 0;
  
  {
    char buffer[256];
    snprintf(buffer, 256, "engine_showScreen(%s %d/%d dim(%d)) called",
	     message.c_str(), picture, (int)pictures.size(), (int)synthParams.size());
    logging.info(buffer);
  }
  
  if(picture < pictures.size()){ // shows a picture
    if(images[picture] == NULL){
      SDL_Surface* image = IMG_Load(pictures[picture].c_str());
      
      if(image == NULL){
	char buffer[120];
	snprintf(buffer, 120, "showscreen: loading image FAILED (%s): %s",
		 SDL_GetError(), pictures[picture].c_str());
	logging.warn(buffer);
      }
      
      SDL_Rect imageRect;
      SDL_Surface* scaled = 0;
      
      
      if(image != 0){
	if((image->w) > (image->h)){
	  double wscale = ((double)SCREEN_WIDTH)/((double)image->w);
	  // scaled = zoomSurface(image, wscale, wscale, SMOOTHING_ON);
	  
	  scaled = SDL_CreateRGBSurface(0, (int)(image->w*wscale), (int)(image->h*wscale), 32,
					0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
	  
	  if(SDL_BlitScaled(image, NULL, scaled, NULL) != 0)
	    return false;
	  
	}
	else{
	  double hscale = ((double)SCREEN_HEIGHT)/((double)image->h);
	  // scaled = zoomSurface(image, hscale, hscale, SMOOTHING_ON);
	  
	  scaled = SDL_CreateRGBSurface(0, (int)(image->w*hscale), (int)(image->h*hscale), 32,
					0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
	  
	  if(SDL_BlitScaled(image, NULL, scaled, NULL) != 0)
	    return false;
	}
	
	images[picture] = scaled;
      }
      
      if(scaled != NULL){
	SDL_Color averageColor;
	measureColor(scaled, averageColor);
	
	bgcolor = (int)(averageColor.r + averageColor.g + averageColor.b)/3;
	
	imageRect.w = scaled->w;
	imageRect.h = scaled->h;
	imageRect.x = (SCREEN_WIDTH - scaled->w)/2;
	imageRect.y = (SCREEN_HEIGHT - scaled->h)/2;
	
	if(SDL_BlitSurface(images[picture], NULL, surface, &imageRect) != 0)
	  return false;
	
	elementsDisplayed++;
      }
      
      if(image)
	SDL_FreeSurface(image);
    }
    else{
      SDL_Rect imageRect;
      SDL_Surface* scaled = 0;
      
      scaled = images[picture];
      
      SDL_Color averageColor;
      
      measureColor(scaled, averageColor);
      
      bgcolor = (int)(averageColor.r + averageColor.g + averageColor.b)/3;
      
      imageRect.w = scaled->w;
      imageRect.h = scaled->h;
      imageRect.x = (SCREEN_WIDTH - scaled->w)/2;
      imageRect.y = (SCREEN_HEIGHT - scaled->h)/2;
      
      if(SDL_BlitSurface(images[picture], NULL, surface, &imageRect) != 0)
	return false;
      
      elementsDisplayed++;
    }
  }

  logging.info("engine_showScreen(): picture shown.");

#if 0
  ///////////////////////////////////////////////////////////////////////
  // displays random curve (5 random points) [eye candy (for now)]
  
  
  if(showCurve && mic != NULL)
    {
      std::vector< math::vertex< math::blas_real<double> > > curve;
      std::vector< whiteice::math::vertex< whiteice::math::blas_real<double> > > points;
      const unsigned int NPOINTS = 5;
      const unsigned int DIMENSION = 2; // 3
      
      const double TICKSPERCURVE = CURVETIME/(TICK_MS/1000.0); // 0.5 second long buffer
      curveParameter = (tick - latestTickCurveDrawn)/TICKSPERCURVE;
      double stdev = 0.0;
      
      // estimates sound dbel variance during latest CURVETIME and 
      // scales curve noise/stddev according to distances from
      // mean dbel value
      {
	// assumes we dont miss any ticks..
	historyPower.push_back(mic->getInputPower());
	
	while(historyPower.size() > TICKSPERCURVE/5.0) // 1.0 sec long history
	  historyPower.pop_front();
	
	auto m = 0.0, v = 0.0;
	
	for(auto h : historyPower){
	  m += h/historyPower.size();
	  v += h*h/historyPower.size();
	}
	
	v -= m*m;
	v = sqrt(abs(v));
	
	double t = mic->getInputPower();
	stdev = 4.0;
	
	double limit = m+v;
	
	if(t > limit){
	  stdev += 50.0*(abs(limit) + v)/abs(limit);
	}
	else{
	}
	
	// printf("MIC : %f %f %f\n", mic->getInputPower(), limit, stdev); fflush(stdout);
	
      }
      
      
      if(curveParameter > 1.0)
      {
	points.resize(NPOINTS);
	
	for(auto& p : points){
	  p.resize(DIMENSION);
	  
	  for(unsigned int d=0;d<DIMENSION;d++){
	    whiteice::math::blas_real<float> value = rng.uniform()*2.0f - 1.0f; // [-1,1]
	    p[d] = value.c[0];
	  }
	  
	}
	
	startPoint = endPoint;
	endPoint = points;
	
	if(startPoint.size() == 0)
	  startPoint = points;
	
	latestTickCurveDrawn = tick;
	curveParameter = (tick - latestTickCurveDrawn)/TICKSPERCURVE;
      }
      
      {
	points.resize(NPOINTS);
	
	for(unsigned int j=0;j<points.size();j++){
	  points[j].resize(DIMENSION);
	  for(unsigned int d=0;d<DIMENSION;d++){
	    points[j][d] = (1.0 - curveParameter)*startPoint[j][d] + curveParameter*endPoint[j][d];
	  }
	  
	}
      }
      
      // creates curve that goes through points
      createHermiteCurve(curve, points, 0.001 + 0.01*stdev/4.0, 10000);
      
      
      for(const auto& p : curve){
	int x = 0;
	double scalingx = SCREEN_WIDTH/4;
	math::convert(x, scalingx*p[0] + SCREEN_WIDTH/2);
	int y = 0;
	double scalingy = SCREEN_HEIGHT/4;
	math::convert(y, scalingy*p[1] + SCREEN_HEIGHT/2);
	
	if(x>=0 && x<SCREEN_WIDTH && y>=0 && y<SCREEN_HEIGHT){
	  Uint8 * pixel = (Uint8*)surface->pixels;
	  pixel += (y * surface->pitch) + (x * sizeof(Uint32));
	  
	  if(bgcolor >= 160)
	    *((Uint32*)pixel) = 0x00000000; // black
	  else
	    *((Uint32*)pixel) = 0x00FFFFFF; // white
	}
	
      }
      
    }

  logging.info("engine_showScreen(): curve done.");

#endif
  
  ///////////////////////////////////////////////////////////////////////
  // displays a text
  
  {
    SDL_Color white = { 255, 255, 255 };
    //		SDL_Color red   = { 255, 0  , 0   };
    //		SDL_Color green = { 0  , 255, 0   };
    //		SDL_Color blue  = { 0  , 0  , 255 };
    SDL_Color black = { 0  , 0  , 0   };
    SDL_Color color;
    
    color = white;
    if(bgcolor > 160)
      color = black;
    
    if(font != NULL){
      
      SDL_Surface* msg = TTF_RenderUTF8_Blended(font, message.c_str(), color);
      
      if(msg != NULL)
	elementsDisplayed++;
      
      SDL_Rect messageRect;
      
      messageRect.x = (SCREEN_WIDTH - msg->w)/2;
      messageRect.y = (SCREEN_HEIGHT - msg->h)/2;
      messageRect.w = msg->w;
      messageRect.h = msg->h;
      
      if(SDL_BlitSurface(msg, NULL, surface, &messageRect) != 0)
	return false;
      
      SDL_FreeSurface(msg);
    }
    
  }

  logging.info("engine_showScreen(): text done.");
  
  ///////////////////////////////////////////////////////////////////////
  // video encoding (if activated)
  {
    if(video != NULL && programStarted > 0){
      auto t1 = std::chrono::system_clock::now().time_since_epoch();
      auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
      
      logging.info("adding frame to theora encoding queue");
      
      if(video->insertFrame((unsigned long long)(t1ms - programStarted),
			    surface) == false){
	
	logging.error("inserting frame FAILED");
      }
    }
  }
  
  
  ///////////////////////////////////////////////////////////////////////
  // plays sound

  logging.info("engine_showScreen(): synth start.");
  
  if(synth)
  {
    // changes synth parameters only as fast sound synthesis can generate
    // meaningful sounds (sound has time to evolve)
    
    auto t1 = std::chrono::system_clock::now().time_since_epoch();
    auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();
    unsigned long long now = (unsigned long long)t1ms;
    
    if(now - synthParametersChangedTime >= MEASUREMODE_DELAY_MS){
      synthParametersChangedTime = now;
      
      if(synth->setParameters(synthParams) == true){
	elementsDisplayed++;
      }
      else
	logging.warn("synth setParameters FAILED");
    }
  }
  
  {
    char buffer[256];
    snprintf(buffer, 256, "engine_showScreen(%s %d/%d dim(%d)) = %d. DONE",
	     message.c_str(), picture, (int)pictures.size(), (int)synthParams.size(), elementsDisplayed);
    logging.info(buffer);
  }

  
  
  return (elementsDisplayed > 0);
}


void ResonanzEngine::engine_pollEvents()
{
  std::lock_guard<std::mutex> lock(keypress_mutex);
  
  SDL_Event event;
  
  while(SDL_PollEvent(&event)){ // this is not thread-safe so must be protected by mutex lock???
    // currently ignores all incoming events
    // (should handle window close event somehow)
    
    if(event.type == SDL_KEYDOWN &&
       (event.key.keysym.sym == SDLK_ESCAPE ||
	event.key.keysym.sym == SDLK_RETURN)
       )
    {
      keypressed = true;
    }
  }
}


void ResonanzEngine::engine_updateScreen()
{
  if(window != nullptr){
    if(SDL_UpdateWindowSurface(window) != 0){
      printf("engine_updateScreen() failed: %s\n", SDL_GetError());
    }
  }
}


// initializes SDL libraries to be used (graphics, font, music)
bool ResonanzEngine::engine_SDL_init(const std::string& fontname)
{
  logging.info("Starting SDL init (0)..");
        
  SDL_Init(0);
  
  logging.info("Starting SDL subsystem init (events, video, audio)..");
  
  if(SDL_InitSubSystem(SDL_INIT_EVENTS) != 0){
    logging.error("SDL_Init(EVENTS) FAILED!");
    return false;
  }
  else
    logging.info("Starting SDL_Init(EVENTS) done..");
  
  if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0){
    logging.error("SDL_Init(VIDEO) FAILED!");
    return false;
  }
  else
    logging.info("Starting SDL_Init(VIDEO) done..");
  
  if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0){
    logging.error("SDL_Init(AUDIO) FAILED!");
    return false;
  }
  else
    logging.info("Starting SDL_Init(AUDIO) done..");
  
  
  SDL_DisplayMode mode;
  
  if(SDL_GetCurrentDisplayMode(0, &mode) == 0){
    SCREEN_WIDTH = mode.w;
    SCREEN_HEIGHT = mode.h;
  }
  
  logging.info("Starting SDL_GetCurrentDisplayMode() done..");
  
  if(TTF_Init() != 0){
    char buffer[80];
    snprintf(buffer, 80, "TTF_Init failed: %s\n", TTF_GetError());
    logging.error(buffer);
    throw std::runtime_error("TTF_Init() failed.");
  }
  
  logging.info("Starting TTF_Init() done..");
  
  int flags = IMG_INIT_JPG | IMG_INIT_PNG;
  
  if(IMG_Init(flags) != flags){
    char buffer[80];
    snprintf(buffer, 80, "IMG_Init failed: %s\n", IMG_GetError());
    logging.error(buffer);
    IMG_Quit();
    throw std::runtime_error("IMG_Init() failed.");
  }
  
  logging.info("Starting IMG_Init() done..");
  
  flags = MIX_INIT_OGG;
  
  audioEnabled = true;
  
  if(audioEnabled){
    synth = new IsochronicSoundSynthesis(); // curretly just supports single synthesizer type

    // valgrind says mic listener creation causes BUG in memory handling..
    // => DISABLED FOR NOW
    // mic   = new SDLMicListener();   // records single input channel
    mic = nullptr;

    synth->pause(); // no sounds
    
    logging.info("Created sound synthesizer and capture objects..");
  }
  else{
    synth = nullptr;
    mic = nullptr;
    
    logging.info("Created sound synthesizer and capture DISABLED.");
  }
  
  
  if(mic != nullptr)
    if(mic->listen() == false)      // tries to start recording audio
      logging.error("starting SDL sound capture failed");
  
  {
    // we get the mutex so eeg cannot change below us..
    std::lock_guard<std::mutex> lock(eeg_mutex);

    // FIXME: synth code doesn't use HMM brainstate!
    
    std::vector<unsigned int> nnArchitecture;
    
    if(synth){			  
      nnArchitecture.push_back(eeg->getNumberOfSignals() + 
			       2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
      
      for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*
				 (eeg->getNumberOfSignals() + 
				  2*synth->getNumberOfParameters() +
				  HMM_NUM_CLUSTERS));
	nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				 2*synth->getNumberOfParameters() + HMM_NUM_CLUSTERS);
      }
      
      nnArchitecture.push_back(eeg->getNumberOfSignals());
      
      if(nnsynth != nullptr) delete nnsynth;
      
      nnsynth = new whiteice::nnetwork<>(nnArchitecture);
      nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
      nnsynth->setNonlinearity(nnsynth->getLayers()-1,
			       whiteice::nnetwork<>::pureLinear);
      nnsynth->setResidual(true);
    }
    else{
      const int synth_number_of_parameters = 6;
      
      nnArchitecture.push_back(eeg->getNumberOfSignals() + 
			       2*synth_number_of_parameters +
			       HMM_NUM_CLUSTERS);
      
      for(int i=0;i<(NEURALNETWORK_DEPTH-1)/2;i++){
	nnArchitecture.push_back(NEURALNETWORK_COMPLEXITY*
				 (eeg->getNumberOfSignals() + 
				  2*synth_number_of_parameters +
				  HMM_NUM_CLUSTERS));
	nnArchitecture.push_back(eeg->getNumberOfSignals() + 
				 2*synth_number_of_parameters +
			       HMM_NUM_CLUSTERS);
      }
      
      nnArchitecture.push_back(eeg->getNumberOfSignals());
      
      if(nnsynth != nullptr) delete nnsynth;
      
      nnsynth = new whiteice::nnetwork<>(nnArchitecture);
      nnsynth->setNonlinearity(whiteice::nnetwork<>::rectifier);
      nnsynth->setNonlinearity(nnsynth->getLayers()-1,
			       whiteice::nnetwork<>::pureLinear);
      nnsynth->setResidual(true);
    }
  }
  
  //#if 0
  if(Mix_Init(flags) != flags){
    char buffer[80];
    snprintf(buffer, 80, "Mix_Init failed: %s\n", Mix_GetError());
    logging.warn(buffer);
    audioEnabled = false;
    /*
      IMG_Quit();
      Mix_Quit();
      throw std::runtime_error("Mix_Init() failed.");
    */
  }
  
  logging.info("Starting Mix_Init() done..");
  
  font = 0;
  
  if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) == -1){
    audioEnabled = false;
    char buffer[80];
    snprintf(buffer, 80, "ERROR: Cannot open SDL mixer: %s.\n", Mix_GetError());
    logging.warn(buffer);
  }
  else audioEnabled = true;
  //#endif
  
  logging.info("SDL initialization.. SUCCESSFUL");
  
  return true;
}


bool ResonanzEngine::engine_SDL_deinit()
{
  logging.info("SDL deinitialization..");
  
  if(synth){
    synth->pause();
    delete synth;
    synth = nullptr;	  
  }
  
  if(mic){
    delete mic;
    mic = nullptr;
  }
  
  if(audioEnabled)
    SDL_CloseAudio();
  
  if(font){
    TTF_CloseFont(font);
    font = NULL;
  }
  
  IMG_Quit();
  
  if(audioEnabled)
    Mix_Quit();
  
  TTF_Quit();
  SDL_Quit();
  
  logging.info("SDL deinitialization.. DONE");
  
  return true;
}


void ResonanzEngine::engine_stopHibernation()
{
#ifdef _WIN32
  // resets hibernation timers
  SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
#endif
}



bool ResonanzEngine::engine_playAudioFile(const std::string& audioFile)
{
  if(audioEnabled){
    music = Mix_LoadMUS(audioFile.c_str());
    
    if(music != NULL){
      if(Mix_PlayMusic(music, -1) == -1){
	Mix_FreeMusic(music);
	music = NULL;
	logging.warn("sdl-music: cannot start playing music");
	return false;
      }
      return true;
    }
    else{
      char buffer[80];
      snprintf(buffer, 80, "sdl-music: loading audio file failed: %s", audioFile.c_str());
      logging.warn(buffer);
      return false;
    }
  }
  else return false;
}


bool ResonanzEngine::engine_stopAudioFile()
{
  if(audioEnabled){
    Mix_FadeOutMusic(50);
    
    if(music == NULL){
      return false;
    }
    else{
      Mix_FreeMusic(music);
      music = NULL;
    }
    
    return true;
  }
  else return false;
}


bool ResonanzEngine::measureColor(SDL_Surface* image, SDL_Color& averageColor)
{
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  unsigned int N = 0;
  
  // SDL_Surface* im = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_Surface* im = image; // skip surface conversion/copy

  if(im == 0) return false;
  
  unsigned int* buffer = (unsigned int*)im->pixels;
  
  // instead of calculating value for the whole image, we just calculate sample from 1000 pixels
  
  for(unsigned int s=0;s<100;s++){
    int x = 0, y = 0;
    
    if(im){
      if(im->w)
	x = rand() % im->w;

      if(im->h)
	y = rand() % im->h;
    }

    if(image && buffer){
      int rr = (buffer[x + y*(image->pitch/4)] & 0xFF0000) >> 16;
      int gg = (buffer[x + y*(image->pitch/4)] & 0x00FF00) >> 8;
      int bb = (buffer[x + y*(image->pitch/4)] & 0x0000FF) >> 0;
      
      r += rr;
      g += gg;
      b += bb;
    }
    
    N++;
  }
  
  r /= N;
  g /= N;
  b /= N;
  
  averageColor.r = (Uint8)r;
  averageColor.g = (Uint8)g;
  averageColor.b = (Uint8)b;
  
  // SDL_FreeSurface(im);
  
  return true;
}




bool ResonanzEngine::loadWords(const std::string filename, std::vector<std::string>& words) const
{
  FILE* handle = fopen(filename.c_str(), "rt");
  
  if(handle == 0)
    return false;
  
  char buffer[256];
  
  while(fgets(buffer, 256, handle) == buffer){
    const int N = strlen(buffer);
    
    for(int i=0;i<N;i++)
      if(buffer[i] == '\n' || buffer[i] == '\r')
	buffer[i] = '\0';
    
    if(strlen(buffer) > 1)
      words.push_back(buffer);
  }
  
  fclose(handle);
  
  return true;
}


bool ResonanzEngine::loadPictures(const std::string directory, std::vector<std::string>& pictures) const
{
  // looks for pics/*.jpg and pics/*.png files
  
  DIR* dp;
  
  struct dirent *ep;
  
  dp = opendir(directory.c_str());
  
  if(dp == 0) return false;
  
  while((ep = readdir(dp)) != NULL){
    
    std::string name = ep->d_name;
    
    if(name.find(".jpg") == (name.size() - 4) ||
       name.find(".png") == (name.size() - 4) ||
       name.find(".JPG") == (name.size() - 4) ||
       name.find(".PNG") == (name.size() - 4))
      {
	std::string fullname = directory.c_str();
	fullname = fullname + "/";
	fullname = fullname + name;
	
	pictures.push_back(fullname);
      }
  }
  
  closedir(dp);
  
  return true;
}


std::string ResonanzEngine::analyzeModel(const std::string& modelDir) const
{
  // we go through database directory and load all *.ds files
  std::vector<std::string> databaseFiles;
  
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir (modelDir.c_str())) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir (dir)) != NULL) {
      const char* filename = ent->d_name;
      
      if(strlen(filename) > 3)
	if(strcmp(&(filename[strlen(filename)-3]),".ds") == 0)
	  databaseFiles.push_back(filename);
    }
    closedir (dir);
  }
  else { /* could not open directory */
    return "Cannot read directory";
  }
  
  unsigned int minDSSamples = (unsigned int)(-1);
  double avgDSSamples = 0;
  unsigned int N = 0;
  unsigned int failed = 0;
  unsigned int models = 0;
  
  float total_error = 0.0f;
  float total_N     = 0.0f;
  
  
  // std::lock_guard<std::mutex> lock(database_mutex);
  // (we do read only operations so these are relatively safe) => no mutex
  
  for(auto filename : databaseFiles){
    // calculate statistics
    whiteice::dataset<> ds;
    std::string fullname = modelDir + "/" + filename;
    if(ds.load(fullname) == false){
      failed++;
      continue; // couldn't load this dataset
    }
    
    if(ds.size(0) < minDSSamples) minDSSamples = ds.size(0);
    avgDSSamples += ds.size(0);
    N++;
    
    std::string modelFilename = fullname.substr(0, fullname.length()-3) + ".model";
    
    // check if there is a model file and load it into memory and TODO: calculate average error
    whiteice::bayesian_nnetwork<> nnet;
    
    if(nnet.load(modelFilename)){
      models++;
      
      if(ds.getNumberOfClusters() < 2)
	continue;
      
      if(ds.size(0) != ds.size(1))
	continue;
		  
      float error = 0.0f;
      float error_N = 0.0f;
      
      for(unsigned int i=0;i<ds.size(0);i++){
	math::vertex<> m;
	math::matrix<> cov;
	
	auto x = ds.access(0, i);
	
	if(nnet.calculate(x, m, cov, 1, 0) == false)
	  continue;
	
	auto y = ds.access(1, i);
	
	// converts data to real output values for meaningful comparision against cases WITHOUT preprocessing
	if(ds.invpreprocess(1, m) == false || ds.invpreprocess(1, y) == false)
	  continue; // skip these datapoints
	
	auto delta = y - m;
	
	// calculates per element error for easy comparision of different models
	error += delta.norm().c[0] / delta.size();
	error_N++;
      }
      
      if(error_N > 0.0f){
	error /= error_N; // average error for this stimulation element
	total_error += error;
	total_N++;
      }
    }
    
  }
  
  if(total_N > 0.0f)
    total_error /= total_N;
  
  
  if(N > 0){
    avgDSSamples /= N;
    double modelPercentage = 100*models/((double)N);
    
    char buffer[1024];
    snprintf(buffer, 1024, "%d entries (%.0f%% has a model). samples(avg): %.2f, samples(min): %d\nAverage model (per element) error: %f\n",
	     N, modelPercentage, avgDSSamples, minDSSamples, total_error);

    return buffer;
  }
  else{
    char buffer[1024];
    snprintf(buffer, 1024, "%d entries (0%% has a model). samples(avg): %.2f, samples(min): %d",
	     0, 0.0, 0);
    
    return buffer;
  }
}


// analyzes given measurements database and model performance more accurately
std::string ResonanzEngine::analyzeModel2(const std::string& pictureDir, 
					  const std::string& keywordsFile, 
					  const std::string& modelDir) const
{
  // 1. loads picture and keywords filename information into local memory
  std::vector<std::string> pictureFiles;
  std::vector<std::string> keywords;
  
  if(loadWords(keywordsFile, keywords) == false || 
     loadPictures(pictureDir, pictureFiles) == false)
    return "";
  
  // 2. loads dataset files (.ds) one by one if possible and calculates prediction error
  
  std::string report = "MODEL PREDICTION ERRORS:\n\n";
  
  // loads databases into memory
  for(unsigned int i=0;i<keywords.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".ds";
    std::string modelFilename = modelDir + "/" + calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".model";
    
    whiteice::dataset<> data;
    whiteice::bayesian_nnetwork<> bnn;
    
    if(data.load(dbFilename) == true && bnn.load(modelFilename)){
      if(data.getNumberOfClusters() >= 2){
	// calculates average error
	float error = 0.0f;
	float num   = 0.0f;
	
	for(unsigned int j=0;j<data.size(0);j++){
	  auto input = data.access(0, j);
	  
	  math::vertex<> m;
	  math::matrix<> C;
	  
	  if(bnn.calculate(input, m, C, 1, 0)){
	    auto output = data.access(1, j);
	    
	    // we must inv-postprocess data before calculation error
	    
	    data.invpreprocess(1, m);
	    data.invpreprocess(1, output);
	    
	    auto delta = output - m;
	    
	    error += delta.norm().c[0] / delta.size();
	    num++;
	  }
	}
	  
	error /= num;
	
	char buffer[256];
	snprintf(buffer, 256, "Keyword '%s' model error: %f (N=%d)\n", 
		 keywords[i].c_str(), error, (int)num);
	
	report += buffer;
      }
    }
  }
  
  report += "\n";
  
  for(unsigned int i=0;i<pictureFiles.size();i++){
    std::string dbFilename = 
      modelDir + "/" + calculateHashName(pictureFiles[i] + eeg->getDataSourceName()) + ".ds";
    std::string modelFilename = 
      modelDir + "/" + calculateHashName(pictureFiles[i] + eeg->getDataSourceName()) + ".model";
    
    whiteice::dataset<> data;
    whiteice::bayesian_nnetwork<> bnn;
    
    if(data.load(dbFilename) == true && bnn.load(modelFilename)){
      if(data.getNumberOfClusters() >= 2){
	// calculates average error
	float error = 0.0f;
	float num   = 0.0f;
	
	for(unsigned int j=0;j<data.size(0);j++){
	  auto input = data.access(0, j);
	  
	  math::vertex<> m;
	  math::matrix<> C;
	  
	  if(bnn.calculate(input, m, C, 1, 0)){
	    auto output = data.access(1, j);
	    
	    // we must inv-postprocess data before calculation error
	    
	    data.invpreprocess(1, m);
	    data.invpreprocess(1, output);
	    
	    auto delta = output - m;
	    
	    error += delta.norm().c[0] / delta.size();
	    num++;
	  }
	}
	
	error /= num;
	
	char buffer[256];
	snprintf(buffer, 256, "Picture '%s' model error: %f (N=%d)\n", 
		 pictureFiles[i].c_str(), error, (int)num);
	
	report += buffer;
      }
    }
  }
  
  report = report + "\n";
  
  unsigned int synth_N = 0;
  
  if(synth){
    std::string dbFilename = modelDir + "/" + 
      calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".ds";
    std::string modelFilename = modelDir + "/" + 
      calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".model";
    
    whiteice::dataset<> data;
    whiteice::bayesian_nnetwork<> bnn;    
    
    if(data.load(dbFilename) == true && bnn.load(modelFilename)){
      if(data.getNumberOfClusters() >= 2){
	// calculates average error
	float error = 0.0f;
	float num   = 0.0f;
	
	for(unsigned int j=0;j<data.size(0);j++){
	  auto input = data.access(0, j);
	  
	  math::vertex<> m;
	  math::matrix<> C;
	  
	  if(bnn.calculate(input, m, C, 1, 0)){
	    auto output = data.access(1, j);
	    
	    // we must inv-postprocess data before calculation error
	    
	    data.invpreprocess(1, m);
	    data.invpreprocess(1, output);
	    
	    auto delta = output - m;
	    
	    error += delta.norm().c[0] / delta.size();
	    num++;
	  }
	}
	
	error /= num;
	
	char buffer[256];
	snprintf(buffer, 256, "Synth %s model [dim(%d) -> dim(%d)] error: %f (N=%d)\n", 
		 synth->getSynthesizerName().c_str(), 
		 bnn.inputSize(), bnn.outputSize(), 
		 error, (int)num);
	
	report += buffer;	
      }
    }
  }

  return report;
}


// measured program functions
bool ResonanzEngine::invalidateMeasuredProgram()
{
  // invalidates currently measured program
  std::lock_guard<std::mutex> lock(measure_program_mutex);
  
  this->measuredProgram.resize(0);
  
  return true;
}


bool ResonanzEngine::getMeasuredProgram(std::vector< std::vector<float> >& program)
{
  // gets currently measured program
  std::lock_guard<std::mutex> lock(measure_program_mutex);
  
  if(measuredProgram.size() == 0)
    return false;
  
  program = this->measuredProgram;
  
  return true;
}


// calculates delta statistics from the measurements [with currently selected EEG]
std::string ResonanzEngine::deltaStatistics(const std::string& pictureDir, const std::string& keywordsFile, const std::string& modelDir) const
{
  // 1. loads picture and keywords files into local memory
  std::vector<std::string> pictureFiles;
  std::vector<std::string> keywords;
  
  if(loadWords(keywordsFile, keywords) == false || loadPictures(pictureDir, pictureFiles) == false)
    return "";
  
  std::multimap<float, std::string> keywordDeltas;
  std::multimap<float, std::string> pictureDeltas;
  float mean_delta_keywords = 0.0f;
  float var_delta_keywords  = 0.0f;
  float mean_delta_pictures = 0.0f;
  float var_delta_pictures  = 0.0f;
  float mean_delta_synth    = 0.0f;
  float var_delta_synth     = 0.0f;
  float num_keywords = 0.0f;
  float num_pictures = 0.0f;
  float pca_preprocess = 0.0f;
  
  unsigned int input_dimension = 0;
  unsigned int output_dimension = 0;
  
  // 2. loads dataset files (.ds) one by one if possible and calculates mean delta
  whiteice::dataset<> data;
  
  // loads databases into memory or initializes new ones
  for(unsigned int i=0;i<keywords.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".ds";
    
    data.clear();
    
    if(data.load(dbFilename) == true){
      if(data.getNumberOfClusters() >= 2){
	float delta = 0.0f;
	float delta2 = 0.0f;
	
	for(unsigned int j=0;j<data.size(0);j++){
	  auto d = data.access(1, j);
	  delta += d.norm().c[0] / data.size(0);
	  delta2 += d.norm().c[0]*d.norm().c[0] / data.size(0);
	}
	
	if(data.size(0) > 0){
	  input_dimension  = data.access(0, 0).size();
	  output_dimension = data.access(1, 0).size();
	}
	
	std::pair<float, std::string> p;
	p.first  = -delta;
	
	char buffer[128];
	snprintf(buffer, 128, "%s (N = %d)", 
		 keywords[i].c_str(), data.size(0));
	std::string msg = buffer;
	
	p.second = msg;
	keywordDeltas.insert(p);
	
	mean_delta_keywords += delta;
	var_delta_keywords  += delta2 - delta*delta;
	num_keywords++;
	
	if(data.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval))
	  pca_preprocess++;
      }
    }
  }
  
  mean_delta_keywords /= num_keywords;
  var_delta_keywords  /= num_keywords;
  
  for(unsigned int i=0;i<pictureFiles.size();i++){
    std::string dbFilename = modelDir + "/" + calculateHashName(pictureFiles[i] + eeg->getDataSourceName()) + ".ds";
    
    data.clear();
    
    if(data.load(dbFilename) == true){
      if(data.getNumberOfClusters() >= 2){
	float delta = 0.0f;
	float delta2 = 0.0f; 
	
	for(unsigned int j=0;j<data.size(0);j++){
	  auto d = data.access(1, j);
	  delta += d.norm().c[0] / data.size(0);
	  delta2 += d.norm().c[0]*d.norm().c[0] / data.size(0);
	}
	
	if(data.size(0) > 0){
	  input_dimension  = data.access(0, 0).size();
	  output_dimension = data.access(1, 0).size();
	}
	
	std::pair<float, std::string> p;
	p.first  = -delta;
	
	char buffer[128];
	snprintf(buffer, 128, "%s (N = %d)", 
		 pictureFiles[i].c_str(), data.size(0));
	std::string msg = buffer;
	
	p.second = msg;
	pictureDeltas.insert(p);
	
	mean_delta_pictures += delta;
	var_delta_pictures  += delta2 - delta*delta;
	num_pictures++;
	
	if(data.hasPreprocess(0, whiteice::dataset<>::dnCorrelationRemoval))
	  pca_preprocess++;
      }
    }
  }
  
  mean_delta_pictures /= num_pictures;
  var_delta_pictures  /= num_pictures;

  unsigned int synth_N = 0;
  
  if(synth){
    std::string dbFilename = modelDir + "/" + 
      calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".ds";
    
    data.clear();
    
    if(data.load(dbFilename) == true){

      synth_N = data.size(1);
      
      if(data.getNumberOfClusters() >= 2){
	
	float delta = 0.0f;
	float delta2 = 0.0f;
	
	for(unsigned int j=0;j<data.size(1);j++){
	  auto d = data.access(1, j);
	  delta += d.norm().c[0] / ((float)data.size(1));
	  delta2 += d.norm().c[0]*d.norm().c[0] / ((float)data.size(1));
	}
	
	mean_delta_synth += delta;
	var_delta_synth  += delta2;
      }
    }
    
  }
  
  var_delta_synth -= mean_delta_synth*mean_delta_synth;
  
  // 3. sorts deltas/keyword delta/picture (use <multimap> for automatic ordering) and prints the results
  
  std::string report = "";
  const unsigned int BUFSIZE = 512;
  char buffer[BUFSIZE];
  
  snprintf(buffer, BUFSIZE, "Picture delta: %.2f stdev(delta): %.2f\n", mean_delta_pictures, sqrt(var_delta_pictures));
  report += buffer;
  
  if(keywords.size() > 0){
    snprintf(buffer, BUFSIZE, "Keyword delta: %.2f stdev(delta): %.2f\n", mean_delta_keywords, sqrt(var_delta_keywords));
    report += buffer;
  }
  
  snprintf(buffer, BUFSIZE, "Synth delta: %.2f stdev(delta): %.2f (N = %d)\n", mean_delta_synth, sqrt(var_delta_synth), synth_N);
  report += buffer;
  
  snprintf(buffer, BUFSIZE, "PCA preprocessing: %.1f%% of elements\n", 100.0f*pca_preprocess/(num_pictures + num_keywords));
  report += buffer;
  snprintf(buffer, BUFSIZE, "Input dimension: %d Output dimension: %d\n", input_dimension, output_dimension);
  report += buffer;
  snprintf(buffer, BUFSIZE, "\n");
  report += buffer;
  
  snprintf(buffer, BUFSIZE, "PICTURE DELTAS\n");
  report += buffer;
  for(auto& a : pictureDeltas){
    snprintf(buffer, BUFSIZE, "%s: delta %.2f\n", a.second.c_str(), -a.first);
    report += buffer;
  }
  snprintf(buffer, BUFSIZE, "\n");
  report += buffer;
  
  snprintf(buffer, BUFSIZE, "KEYWORD DELTAS\n");
  report += buffer;
  for(auto& a : keywordDeltas){
    snprintf(buffer, BUFSIZE, "%s: delta %.2f\n", a.second.c_str(), -a.first);
    report += buffer;
  }
  snprintf(buffer, BUFSIZE, "\n");
  report += buffer;
  
  return report;
}


// returns collected program performance statistics [program weighted RMS]
std::string ResonanzEngine::executedProgramStatistics() const
{
  if(programRMS_N > 0){
    float rms = programRMS / programRMS_N;
    
    char buffer[80];
    snprintf(buffer, 80, "Program performance (average error): %.4f.\n", rms);
    std::string result = buffer;
    
    return result;
  }
  else{
    return "No program performance data available.\n";
  }
}


// exports data to ASCII format files (.txt files)
bool ResonanzEngine::exportDataAscii(const std::string& pictureDir, 
				     const std::string& keywordsFile, 
				     const std::string& modelDir) const
{
  // 1. loads picture and keywords files into local memory
  std::vector<std::string> pictureFiles;
  std::vector<std::string> keywords;
  
  if(loadWords(keywordsFile, keywords) == false || 
     loadPictures(pictureDir, pictureFiles) == false)
    return false;
  
  // 2. loads dataset files (.ds) one by one if possible and calculates mean delta
  whiteice::dataset<> data;

  // 0. loads and dumps eegData file
  {
    
    std::string dbFilename = modelDir + "/" + 
      calculateHashName("eegData" + eeg->getDataSourceName()) + ".ds";
    std::string txtFilename = modelDir + "/EEGDATA_" + eeg->getDataSourceName() + ".txt";
    
    data.clear();
    
    if(data.load(dbFilename) == true){
      if(data.exportAscii(txtFilename) == false)
	return false;
    }
    else return false;
  }
  
  
  // loads databases into memory or initializes new ones
  for(unsigned int i=0;i<keywords.size();i++){
    std::string dbFilename = modelDir + "/" + 
      calculateHashName(keywords[i] + eeg->getDataSourceName()) + ".ds";
    std::string txtFilename = modelDir + "/" + "KEYWORD_" + 
      keywords[i] + "_" + eeg->getDataSourceName() + ".txt";
    
    data.clear();
    
    if(data.load(dbFilename) == true){
      if(data.exportAscii(txtFilename) == false)
	return false;
    }
    else return false;
  }
  
  for(unsigned int i=0;i<pictureFiles.size();i++){
    std::string dbFilename = modelDir + "/" + 
      calculateHashName(pictureFiles[i] + eeg->getDataSourceName()) + ".ds";
    
    char filename[2048];
    snprintf(filename, 2048, "%s", pictureFiles[i].c_str());
    
    std::string txtFilename = modelDir + "/" + "PICTURE_" + 
      basename(filename) + "_" + eeg->getDataSourceName() + ".txt";
    
    data.clear();
    
    if(data.load(dbFilename) == true){
      if(data.exportAscii(txtFilename) == false)
	return false;
    }
    else return false;
  }
  
  
  if(synth){
    std::string dbFilename = modelDir + "/" + 
      calculateHashName(eeg->getDataSourceName() + synth->getSynthesizerName()) + ".ds";
    
    std::string sname = synth->getSynthesizerName();
    
    char synthname[2048];
    snprintf(synthname, 2048, "%s", sname.c_str());
	  
    for(unsigned int i=0;i<strlen(synthname);i++)
      if(isalnum(synthname[i]) == 0) synthname[i] = '_';
    
    std::string txtFilename = modelDir + "/" + "SYNTH_" +  
      synthname + "_" + eeg->getDataSourceName() + ".txt";
    
    data.clear();
    
    if(data.load(dbFilename) == true){
      // export data fails here for some reason => figure out why (dump seems to be ok)..
      if(data.exportAscii(txtFilename) == false){
	return false;
      }
    }
    else{
      return false;
    }
  }
  
  return true;
}





bool ResonanzEngine::deleteModelData(const std::string& modelDir)
{
  // we go through database directory and delete all *.ds and *.model files
  std::vector<std::string> databaseFiles;
  std::vector<std::string> modelFiles;
  
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir (modelDir.c_str())) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir (dir)) != NULL) {
      const char* filename = ent->d_name;
      
      if(strlen(filename) > 3)
	if(strcmp(&(filename[strlen(filename)-3]),".ds") == 0)
	  databaseFiles.push_back(filename);
    }
    closedir (dir);
  }
  else { /* could not open directory */
    return false;
  }
  
  dir = NULL;
  ent = NULL;
  if ((dir = opendir (modelDir.c_str())) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir (dir)) != NULL) {
      const char* filename = ent->d_name;
      
      if(strlen(filename) > 6)
	if(strcmp(&(filename[strlen(filename)-7]),".kmeans") == 0)
	  modelFiles.push_back(filename);
    }
    closedir (dir);
  }
  else { /* could not open directory */
    return false;
  }

  dir = NULL;
  ent = NULL;
  if ((dir = opendir (modelDir.c_str())) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir (dir)) != NULL) {
      const char* filename = ent->d_name;
      
      if(strlen(filename) > 6)
	if(strcmp(&(filename[strlen(filename)-4]),".hmm") == 0)
	  modelFiles.push_back(filename);
    }
    closedir (dir);
  }
  else { /* could not open directory */
    return false;
  }
  
  dir = NULL;
  ent = NULL;
  if ((dir = opendir (modelDir.c_str())) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir (dir)) != NULL) {
      const char* filename = ent->d_name;
      
      if(strlen(filename) > 6)
	if(strcmp(&(filename[strlen(filename)-6]),".model") == 0)
	  modelFiles.push_back(filename);
    }
    closedir (dir);
  }
  else { /* could not open directory */
    return false;
  }
  
  logging.info("about to delete models and measurements database..");
  
  // prevents access to database from other threads
  std::lock_guard<std::mutex> lock1(database_mutex);
  // operation locks so that other command cannot start
  std::lock_guard<std::mutex> lock2(command_mutex);
  
  if(currentCommand.command != ResonanzCommand::CMD_DO_NOTHING)
    return false;
  
  if(keywordData.size() > 0 || pictureData.size() > 0 ||
     keywordModels.size() > 0 || pictureModels.size() > 0)
    return false; // do not delete anything if there models/data is loaded into memory
  
  for(auto filename : databaseFiles){
    auto f = modelDir + "/" + filename;
    remove(f.c_str());
  }
  
  for(auto filename : modelFiles){
    auto f = modelDir + "/" + filename;
    remove(f.c_str());
  }
  
  logging.info("models and measurements database deleted");
  
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

ResonanzCommand::ResonanzCommand(){
  this->command = ResonanzCommand::CMD_DO_NOTHING;
  this->showScreen = false;
  this->pictureDir = "";
  this->keywordsFile = "";
  this->modelDir = "";
}

ResonanzCommand::~ResonanzCommand(){

}


} /* namespace resonanz */
} /* namespace whiteice */

