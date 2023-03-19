/*
 * main.cpp
 *
 * FIXME: refactor the code to be simpler
 *
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <dirent.h>

#include<conio.h>

#include <dinrhiw.h>

#include "TranquilityEngine.h"
#include "NMCFile.h"

#include "timing.h"


bool parse_float_vector(std::vector<float>& v, const char* str);


void print_usage()
{
  printf("Usage: tranquility <mode> [options]\n");
  printf("Learn and activate brainwave entraintment stimulus (EEG).");
  printf("\n");
  printf("--random         display random stimulation\n");
  printf("--measure        measure brainwave responses to pictures/keywords\n");
  printf("--measure-music  measure response to media/music and save results to program file\n");
  printf("--optimize       optimize prediction model for targeted stimulation\n");
  printf("--program        programmed stimulation sequences towards target values\n");
  printf("--analyze        measurement database statistics and model performance analysis\n");
  printf("--dumpdata       dumps measurement database to ascii files\n");
  printf("--help           shows command line help\n");
  printf("\n");
  printf("--keyword-file=  source keywords file\n");
  printf("--model-dir=     model directory for measurements and prediction models\n");
  printf("--program-file=  sets NMC program file\n");
  printf("--music-file=    sets music (MP3) file for playback\n");
  printf("--target=        sets measurement program targets (comma separated numbers)\n");
  printf("--device=        sets measurement device: muse* (osc.udp://localhost:4545), muse4ch, random\n");
  printf("--method=        sets optimization method: rbf, lbfgs, bayes*\n");
  printf("--pca            preprocess input data with pca if possible\n");
  printf("--loop           loops program forever\n");
  printf("--program-len=   measured program length in seconds/ticks\n");
  printf("--fullscreen     fullscreen mode instead of windowed mode\n");
  printf("--savevideo      save video to neurostim.ogv file\n");
  printf("--optimize-synth only optimize synth model when optimizing\n");
  printf("--muse-port=     sets muse osc server port (localhost:<port-number>)\n");
  printf("-v               verbose mode\n");
  printf("\n");
  printf("This is alpha version. Report bugs to Tomas Ukkonen <nop@iki.fi>\n");
}

#define _GNU_SOURCE 1
#include <fenv.h>


//////////////////////////////////////////////////////////////////////



int main(int argc, char** argv)
{
  srand(time(0));

  printf("Tranquility v0.71\n");
  
  if(argc <= 1){
    print_usage();
    return -1;
  }
  
  // prints execution started timestamp (for calculating elapsed time)
  {
    time_t tm;
    time(&tm);
    printf("Execution started on %s", ctime(&tm));
  }
  
  // debugging enables floating point exceptions for NaNs
#if 0
  if(0){
    feenableexcept(FE_INVALID | FE_INEXACT);
  }
#endif

  whiteice::logging.setOutputFile("tranquility-engine.log");
  
  
  // process command line
  bool hasCommand = false;
  bool analyzeCommand = false;
  bool dumpAsciiCommand = false;
  whiteice::tranquility::TranquilityCommand cmd;	
  std::string device = "muse";
  std::string optimizationMethod = "bayes"; // was: lbfgs, rbf, bayes
  bool usepca  = false;
  bool fullscreen = false;
  bool loop = false;
  bool optimizeSynthOnly = false;
  bool randomPrograms = false;
  bool verbose = false;
  
  unsigned int programLength = 5*60; // 5 minutes default
  
  std::string programFile;
  std::vector<float> targets;
  
  std::string museServerPort = "4545";
  
  cmd.pictureDir = "pics";
  cmd.keywordsFile = "keywords.txt";
  cmd.modelDir = "model";
  
  
  for(int i=1;i<argc;i++){
    if(strcmp(argv[i], "--random") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_RANDOM;
      randomPrograms = true;
      hasCommand = true;
    }
    else if(strcmp(argv[i], "--measure") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_MEASURE;
      hasCommand = true;
    }
    else if(strcmp(argv[i], "--measure-music") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_MEASURE_PROGRAM;
      hasCommand = true;
    }
    else if(strcmp(argv[i], "--optimize") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_OPTIMIZE;
      hasCommand = true;
    }
    else if(strcmp(argv[i], "--program") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_EXECUTE;
      hasCommand = true;
    }
    
    else if(strcmp(argv[i], "--analyze") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_NOTHING;
      hasCommand = true;
      analyzeCommand = true;
    }
    else if(strcmp(argv[i], "--dumpdata") == 0){
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_NOTHING;
      hasCommand = true;
      dumpAsciiCommand = true;
    }
    else if(strcmp(argv[i], "--help") == 0){
      print_usage();
      
      cmd.command = whiteice::tranquility::TranquilityCommand::CMD_DO_NOTHING;
      hasCommand = true;
      
      return 0;
    }
    else if(strncmp(argv[i], "--picture-dir=", 14) == 0){
      char* p = &(argv[i][14]);
      if(strlen(p) > 0) cmd.pictureDir = p;
    }
    else if(strncmp(argv[i], "--program-len=", 14) == 0){
      char* p = &(argv[i][14]);
      if(strlen(p) > 0) programLength = atoi(p);
    }
    else if(strncmp(argv[i], "--model-dir=", 12) == 0){
      char* p = &(argv[i][12]);
      if(strlen(p) > 0) cmd.modelDir = p;
    }
    else if(strncmp(argv[i], "--keyword-file=", 15) == 0){
      char* p = &(argv[i][15]);
      if(strlen(p) > 0) cmd.keywordsFile = p;
    }
    else if(strncmp(argv[i], "--program-file=", 15) == 0){
      char* p = &(argv[i][15]);
      if(strlen(p) > 0) programFile = p;
    }
    else if(strncmp(argv[i], "--music-file=", 13) == 0){
      char* p = &(argv[i][13]);
      if(strlen(p) > 0) cmd.audioFile = p;
    }
    else if(strncmp(argv[i], "--device=", 9) == 0){
      char* p = &(argv[i][9]);
      if(strlen(p) > 0) device = p;
    }
    else if(strncmp(argv[i], "--method=", 9) == 0){
      char* p = &(argv[i][9]);
      if(strlen(p) > 0) optimizationMethod = p;
    }
    else if(strncmp(argv[i], "--target=", 9) == 0){
      char* p = &(argv[i][9]);
      parse_float_vector(targets, p);
    }
    else if(strncmp(argv[i], "--muse-port=", 12) == 0){
      char* p = &(argv[i][12]);
      if(strlen(p) > 0) museServerPort = p;
    }
    else if(strcmp(argv[i], "--optimize-synth") == 0){
      optimizeSynthOnly = true;
    }
    else if(strcmp(argv[i],"--fullscreen") == 0){
      fullscreen = true;
	    } 
    else if(strcmp(argv[i],"--loop") == 0){
      loop = true;
    } 
    else if(strcmp(argv[i],"--savevideo") == 0){
      cmd.saveVideo = true;
    } 
    else if(strcmp(argv[i],"--pca") == 0){
      usepca = true;
    }
    else if(strcmp(argv[i],"-v") == 0){
      verbose = true;
      whiteice::logging.setPrintOutput(true);
    }
    else{
      print_usage();
      printf("ERROR: bad parameters.\n");
      return -1;
    }
  }
  
  if(hasCommand == false){
    print_usage();
    printf("ERROR: bad command line\n");
    return -1;
  }
  
  unsigned int numChannels = 7;
  if(device == "muse4ch"){
    // converts target to all channels
    if(targets.size() == 7){
      
      auto copy = targets;
      copy.resize(25);
      
      for(unsigned int i=0;i<6;i++){
	copy[i] = targets[i];
	copy[1*6+i] = targets[i];
	copy[2*6+i] = targets[i];
	copy[3*6+i] = targets[i];
      }
      copy[24] = targets[6];
      
      targets = copy;
    }
    
    numChannels = 25;
  }
  
  std::cout << "TranquilityEngine NUMCHANNELS: " << numChannels << std::endl;
  
  // starts tranquility engine
  whiteice::tranquility::TranquilityEngine engine(numChannels);
  
  engine.setParameter("muse-port", museServerPort);
  
  
  
  
  // sets engine parameters
  {
    // sets measurement device
    if(device == "muse"){
      // listens UDP traffic at localhost:4545 (from muse-io)
      // osc.udp://localhost:4545
      if(engine.setEEGDeviceType(whiteice::tranquility::TranquilityEngine::RE_EEG_IA_MUSE_DEVICE))
	printf("Hardware: Interaxon Muse EEG\n");
      else{
	printf("Cannot connect to Interaxon Muse EEG device\n");
	exit(-1);
      }
    }
    else if(device == "muse4ch"){
      // listens UDP traffic at localhost:4545 (from muse-io)
      // osc.udp://localhost:4545
      if(engine.setEEGDeviceType(whiteice::tranquility::TranquilityEngine::RE_EEG_IA_MUSE_4CH_DEVICE))
	printf("Hardware: Interaxon Muse EEG [4 channels]\n");
      else{
	printf("Cannot connect to Interaxon Muse EEG device\n");
	exit(-1);
      }
    }
    else if(device == "insight"){
      if(engine.setEEGDeviceType(whiteice::tranquility::TranquilityEngine::RE_EEG_EMOTIV_INSIGHT_DEVICE))
	printf("Hardware: Emotiv Insight EEG\n");
      else{
	printf("Cannot connect to Emotiv Insight EEG device\n");
	exit(-1);
      }
    }
    else if(device == "random"){
      if(engine.setEEGDeviceType(whiteice::tranquility::TranquilityEngine::RE_EEG_RANDOM_DEVICE))
	printf("Hardware: Random EEG pseudodevice\n");
      else{
	printf("Cannot connect to Random EEG pseudodevice\n");
	exit(-1);
      }
    }
    else{
      printf("Hardware: unknown device (ERROR!)\n");
      exit(-1);
    }
    
    {
      const unsigned int SHOW_TOP_RESULTS = 2;
      
      char buffer[80];
      sprintf(buffer, "%d", SHOW_TOP_RESULTS);
      
      engine.setParameter("show-top-results", buffer);
    }
    
    engine.setParameter("use-bayesian-nnetwork", "true");
    engine.setParameter("use-data-rbf", "true");
    
    if(optimizationMethod == "rbf"){
      engine.setParameter("use-bayesian-nnetwork", "false");
      engine.setParameter("use-data-rbf", "true");
    }
    else if(optimizationMethod == "lbfgs"){
      engine.setParameter("use-bayesian-nnetwork", "false");
      engine.setParameter("use-data-rbf", "false");
    }
    else if(optimizationMethod == "bayes"){
      engine.setParameter("use-bayesian-nnetwork", "true");
      engine.setParameter("use-data-rbf", "false");	      
    }
    
    if(randomPrograms){
      engine.setParameter("random-programs", "true");
    }
    
    if(usepca){
      engine.setParameter("pca-preprocess", "true");
    }
    else{
      engine.setParameter("pca-preprocess", "false");
    }
    
    if(fullscreen){
      engine.setParameter("fullscreen", "true");
    }
    else{
      engine.setParameter("fullscreen", "false");
    }
    
    if(loop){
      engine.setParameter("loop", "true");
    }
    else{
      engine.setParameter("loop", "false");
    }
    
    if(optimizeSynthOnly){
      engine.setParameter("optimize-synth-only", "true");
    }
    else{
      engine.setParameter("optimize-synth-only", "false");
    }
  }
  
  
  if(cmd.command == cmd.CMD_DO_RANDOM){
    if(engine.cmdRandom(cmd.pictureDir, cmd.keywordsFile, cmd.audioFile, cmd.saveVideo) == false){
      printf("ERROR: bad parameters\n");
      return -1;
    }
  }
  else if(cmd.command == cmd.CMD_DO_MEASURE){
    if(engine.cmdMeasure(cmd.pictureDir, cmd.keywordsFile, cmd.modelDir) == false){
      printf("ERROR: bad parameters\n");
      return -1;
    }
  }
  else if(cmd.command == cmd.CMD_DO_MEASURE_PROGRAM){
    
    std::vector<std::string> names;
    
    const auto& eeg = engine.getDevice();
    
    eeg.getSignalNames(names);
    
    if(names.size() <= 0 || programLength <= 0){
      printf("ERROR: bad parameters\n");
      return -1;
    }
	  
    
    if(engine.cmdMeasureProgram(cmd.audioFile, names, programLength) == false){
      printf("ERROR: bad parameters\n");
      return -1;
    }
    
  }
  else if(cmd.command == cmd.CMD_DO_OPTIMIZE){
    if(engine.cmdOptimizeModel(cmd.pictureDir, cmd.keywordsFile, cmd.modelDir) == false){
      printf("ERROR: bad parameters\n");
      return -1;
    }
  }
  else if(cmd.command == cmd.CMD_DO_EXECUTE){
    whiteice::resonanz::NMCFile file;
    
    if(targets.size() == 0){
      if(file.loadFile(programFile) == false){
	std::cout << "Loading program file: " 
		  << programFile << " failed." << std::endl;
	return -1;
      }
    }
    else{
      
      if(targets.size() != engine.getDevice().getNumberOfSignals()){
	printf("Number of signals in target is wrong (%d != %d).\n",
	       (int)targets.size(), engine.getDevice().getNumberOfSignals());
	return -1;
      }
      
      if(file.createProgram(engine.getDevice(), targets, programLength) == false){
	std::cout << "Creating neurostim program failed." << std::endl;
	return -1;
      }
    }
    
    std::vector<std::string> signalNames;
    std::vector< std::vector<float> > signalPrograms;
    
    signalNames.resize(file.getNumberOfPrograms());
    signalPrograms.resize(file.getNumberOfPrograms());
    
    for(unsigned int i=0;i<signalNames.size();i++){
      file.getProgramSignalName(i, signalNames[i]);
      file.getRawProgram(i, signalPrograms[i]);
    }
    
    {
      printf("Signals selected:\n");
      
      for(unsigned int i=0;i<signalNames.size();i++){
	printf("%s\n", signalNames[i].c_str());
      }
    }
    
    
    if(engine.cmdExecuteProgram(cmd.pictureDir, cmd.keywordsFile, 
				cmd.modelDir, cmd.audioFile, 
				signalNames, signalPrograms,
				false, cmd.saveVideo) == false)
      {
	printf("ERROR: cmdExecuteProgram() bad parameters.\n");
	fflush(stdout);
	return -1;
      }
    
  }
  else if(analyzeCommand == true){
    millisleep(5000); // gives engine time to initialize synth object
    std::string msg = engine.analyzeModel(cmd.modelDir);
    std::cout << msg << std::endl;
    msg = engine.analyzeModel2(cmd.pictureDir, cmd.keywordsFile,
			       cmd.modelDir);
    std::cout << msg << std::endl;
    
    msg = engine.deltaStatistics(cmd.pictureDir, cmd.keywordsFile,
				 cmd.modelDir);
    std::cout << msg << std::endl;
    
    return 0;
  }
  else if(dumpAsciiCommand == true){
    millisleep(5000); // gives engine time to initialize synth object
    
    if(engine.exportDataAscii(cmd.pictureDir, cmd.keywordsFile,
			      cmd.modelDir)){
      std::cout << "Exporting measurements data to ascii format FAILED." << std::endl;
      return -1;
    }
    else{
      std::cout << "Measurements data exported to ascii format." << std::endl;
      return 0;
    }
    
  }
  
  
  millisleep(3000);
  
  
  while((!engine.keypress() && engine.isBusy()) ||
	engine.workActive())
    
    {
      std::cout << "Tranquility status: " << engine.getEngineStatus() << std::endl;
      
      fflush(stdout);
      millisleep(2000); // tranquility-engine thread is doing all the heavy work
    }
  
  {
    std::cout << "Tranquility status: " << engine.getEngineStatus() << std::endl;
    fflush(stdout);
  }
  
  
  engine.cmdStopCommand();
  millisleep(1000);
  
  // reports average RMS of executed program
  if(cmd.command == cmd.CMD_DO_MEASURE){
    std::string msg = engine.deltaStatistics(cmd.pictureDir,
					     cmd.keywordsFile,
					     cmd.modelDir);
    std::cout << msg << std::endl;
  }
  else if(cmd.command == cmd.CMD_DO_OPTIMIZE){
    std::string msg = engine.analyzeModel(cmd.modelDir);
    std::cout << msg << std::endl;
  }
  else if(cmd.command == cmd.CMD_DO_EXECUTE){
    printf("ABOUT TO SHOW PROGRAM EXECUTE STATISTICS\n"); 
    
    
    std::string msg = engine.executedProgramStatistics();
    std::cout << msg << std::endl;
  }
  else if(cmd.command == cmd.CMD_DO_MEASURE_PROGRAM){
    
    std::vector<std::string> names;
    
    const auto& eeg = engine.getDevice();
    
    eeg.getSignalNames(names);
    
    std::vector< std::vector<float> > program;
    
    if(engine.getMeasuredProgram(program) == false){
      printf("ERROR: Cannot retrieve measured program.\n");
      return -1;
    }
    
    if(program.size() <= 0){
      printf("ERROR: Cannot retrieve measured program.\n");
      return -1;
    }
    
    if(program.size() != names.size() || program[0].size() != programLength){
      printf("ERROR: Invalid measured program.\n");
      return -1;
    }
    
    whiteice::resonanz::NMCFile file;
    
    if(file.createProgram(eeg, program) == false){
      printf("ERROR: Cannot create program from measurements.\n");
      return -1;
    }
    
    if(file.saveFile(programFile) == false){
      printf("ERROR: Cannot save program to file.\n");
      return -1;
    }
    
  }
  
  
  return 0;
}





bool parse_float_vector(std::vector<float>& v, const char* str)
{
  std::vector<std::string> tokens;
  char* saveptr = NULL;
  unsigned int counter = 0;
  
  char* p = (char*)malloc(sizeof(char)*(strlen(str) + 1));
  if(p == NULL)
    return false;
  
  strcpy(p, str);
  
  char* token = strtok_r(p, ",", &saveptr);
  while(token != NULL){
    tokens.push_back(token);
    token = strtok_r(NULL, ",", &saveptr);
    counter++;
    
    if(counter >= 1000){ // sanity check
      free(p);
      return false;
    }
  }
  
  free(p);
  
  v.resize(tokens.size());
  
  for(unsigned int i=0;i<v.size();i++)
    v[i] = (float)atof(tokens[i].c_str());
  
  return true;
}
