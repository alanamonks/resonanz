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

#include <dinrhiw.h>

#include "ResonanzEngine.h"
#include "NMCFile.h"

#ifdef WINNT
//#include <windows.h>
//#include "timing.h"
#endif

bool parse_float_vector(std::vector<float>& v, const char* str);

bool keypress();


void print_usage()
{
  printf("Usage: resonanz <mode> [options]\n");
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
  printf("--picture-dir=   use picture source directory\n");
  printf("--keyword-file=  source keywords file\n");
  printf("--model-dir=     model directory for measurements and prediction models\n");
  printf("--program-file=  sets NMC program file\n");
  printf("--music-file=    sets music (MP3) file for playback\n");
  printf("--target=        sets measurement program targets (comma separated numbers)\n");
  printf("--device=        sets measurement device: muse* (osc.udp://localhost:4545), [insight], random\n");
  printf("--method=        sets optimization method: rbf, lbfgs, bayes*\n");
  printf("--pca            preprocess input data with pca if possible\n");
  printf("--loop           loops program forever\n");
  printf("--program-len=   measured program length in seconds/ticks\n");
  printf("--fullscreen     fullscreen mode instead of windowed mode\n");
  printf("--savevideo      save video to neurostim.ogv file\n");
  printf("--optimize-synth only optimize synth model when optimizing\n");
  printf("-v               verbose mode\n");
  printf("\n");
  printf("This is alpha version. Report bugs to Tomas Ukkonen <nop@iki.fi>\n");
}

#define _GNU_SOURCE 1
#include <fenv.h>

int main(int argc, char** argv)
{
  srand(time(0));

  printf("Resonanz engine v0.61\n");
  
  if(argc <= 1){
    print_usage();
    return -1;
  }
  
  // debugging enables floating point exceptions for NaNs
#if 0
  if(0){
    feenableexcept(FE_INVALID | FE_INEXACT);
  }
#endif

	whiteice::logging.setOutputFile("resonanz-engine.log");

	
	// process command line
	bool hasCommand = false;
	bool analyzeCommand = false;
	bool dumpAsciiCommand = false;
	whiteice::resonanz::ResonanzCommand cmd;	
	std::string device = "muse";
	std::string optimizationMethod = "bayes"; // was: lbfgs, rbf, bayes
	bool usepca  = false;
	bool fullscreen = false;
	bool loop = false;
	bool optimizeSynthOnly = false;
	bool randomPrograms = false;
	bool verbose = false;

	unsigned int programLength = 120; // 2 minutes default
	
	std::string programFile;
	std::vector<float> targets;
	
	cmd.pictureDir = "pics";
	cmd.keywordsFile = "keywords.txt";
	cmd.modelDir = "model";
	
	
	for(int i=1;i<argc;i++){
		if(strcmp(argv[i], "--random") == 0){
			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_RANDOM;
			randomPrograms = true;
			hasCommand = true;
		}
		else if(strcmp(argv[i], "--measure") == 0){
			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_MEASURE;
			hasCommand = true;
		}
		else if(strcmp(argv[i], "--measure-music") == 0){
			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_MEASURE_PROGRAM;
			hasCommand = true;
		}
		else if(strcmp(argv[i], "--optimize") == 0){
			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_OPTIMIZE;
			hasCommand = true;
		}
		else if(strcmp(argv[i], "--program") == 0){
			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_EXECUTE;
			hasCommand = true;
		}

		else if(strcmp(argv[i], "--analyze") == 0){
			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_NOTHING;
			hasCommand = true;
			analyzeCommand = true;
		}
		else if(strcmp(argv[i], "--dumpdata") == 0){
		        cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_NOTHING;
			hasCommand = true;
			dumpAsciiCommand = true;
		}
		else if(strcmp(argv[i], "--help") == 0){
			print_usage();

			cmd.command = whiteice::resonanz::ResonanzCommand::CMD_DO_NOTHING;
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

	// starts resonanz engine
	whiteice::resonanz::ResonanzEngine engine;	

	// sets engine parameters
	{
	    // sets measurement device
	    if(device == "muse"){
	      // listens UDP traffic at localhost:4545 (from muse-io)
	      // osc.udp://localhost:4545
	      if(engine.setEEGDeviceType(whiteice::resonanz::ResonanzEngine::RE_EEG_IA_MUSE_DEVICE))
		printf("Hardware: Interaxon Muse EEG\n");
	      else{
		printf("Cannot connect to Interaxon Muse EEG device\n");
		exit(-1);
	      }
	    }
	    else if(device == "insight"){
	      if(engine.setEEGDeviceType(whiteice::resonanz::ResonanzEngine::RE_EEG_EMOTIV_INSIGHT_DEVICE))
		printf("Hardware: Emotiv Insight EEG\n");
	      else{
		printf("Cannot connect to Emotiv Insight EEG device\n");
		exit(-1);
	      }
	    }
	    else if(device == "random"){
	      if(engine.setEEGDeviceType(whiteice::resonanz::ResonanzEngine::RE_EEG_RANDOM_DEVICE))
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
		  
		  if(file.createProgram(engine.getDevice(), targets, 120) == false){
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
		
		
		std::string audioFile = cmd.audioFile;
		
		
		if(engine.cmdExecuteProgram(cmd.pictureDir, cmd.keywordsFile, 
					    cmd.modelDir, audioFile, 
					    signalNames, signalPrograms,
					    false, cmd.saveVideo) == false){
			printf("ERROR: bad parameters\n");
			return -1;
		}

	}
	else if(analyzeCommand == true){
	        sleep(5); // gives engine time to initialize synth object
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
	        sleep(5); // gives engine time to initialize synth object
		
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
	
	sleep(1);

	while(!engine.keypress() && engine.isBusy()){
	  std::cout << "Resonanz status: " << engine.getEngineStatus() << std::endl;
	  
	  fflush(stdout);
	  sleep(2); // resonanz-engine thread is doing all the heavy work
	}
	
	{
	  std::cout << "Resonanz status: " << engine.getEngineStatus() << std::endl;
	  fflush(stdout);
	}
	  
	
	engine.cmdStopCommand();
	sleep(1);
	
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



#if 0
  printf("Usage: resonanz <mode> [options]\n");
  printf("Resonanz v0.421. Learn and activate brainwave entraintment stimulus.\n\n");
  
  printf("--video            display video stimulus\n");
  printf("--audio            play audio stimulus\n");
  printf("--measure-pictures measure brainwave responses to picture stimulus\n");
  printf("--measure-keywords measure brainwave responses to keyword stimulus\n");
  printf("--measure-audio    measure brainwave responses to audio stimulus\n");
  printf("--learn            [disabled - use script for now]\n");
  printf("\n");
  printf("--help             shows command line help\n");
  printf("--picture-dir=     use pictures from the specified directories\n");
  printf("--keyword-file=    use keywords loaded from keyword files\n");
  printf("--music-dir=       use music from the specified directories\n");
  printf("--target=          sets target for emotiv insight values: 0.2,0.9,0.1 etc.\n");
  printf("--target-error=    sets target error function (target error variances)\n");
  printf("--simulate         simulation of emotiv insight values [blind mode]\n");
  printf("--random           shows random stimulation\n");
  printf("-v                 verbose mode\n");
  printf("\n");
  
  printf("This is pre-alpha version. Report bugs to <nop@iki.fi>\n");
#endif


#if 0
int main(int argc, char** argv)
{
  srand(time(0));
  
  if(argc > 1){
    printf("Resonanz v0.421 <http://resonanz.sourceforge.net>\n");
  }
  else{
    print_usage();
    return -1;
  }
      
  // process command line
  bool showPictures = false;
  bool showKeywords = false;
  bool playAudio    = false;
  bool playVideo    = false;
  bool verbose      = false;
  bool hasCommand   = false;
  bool simulation   = false;
  bool randomStim   = false;
  bool measure_eeg_responses = false;
  
  std::vector<std::string> keywordFiles;
  std::vector<std::string> pictureDirs;
  std::vector<std::string> musicDirs;
  
  // target state to be moved towards [emotiv insight metastate]
  whiteice::math::vertex<> target;
  whiteice::math::vertex<> var_error;
  
  
  {
    
    for(unsigned int i=1;i<argc;i++){
      if(strcmp(argv[i], "--measure-pictures") == 0){
	measure_eeg_responses = true;
	showPictures = true;
	printf("Recording EEG-meta signal responses..\n");
	hasCommand = true;
      }
      else if(strcmp(argv[i], "--measure-keywords") == 0){
	measure_eeg_responses = true;
	showKeywords = true;
	printf("Recording EEG-meta signal responses..\n");
	hasCommand = true;
      }
      else if(strcmp(argv[i], "--measure-audio") == 0){
	measure_eeg_responses = true;
	playAudio = true;
	printf("Recording EEG-meta signal responses..\n");
	hasCommand = true;
      }
      else if(strcmp(argv[i], "--video") == 0){
	measure_eeg_responses = false;
	playVideo = true;
	printf("Showing stimulus..\n");
	hasCommand = true;
      }
      else if(strcmp(argv[i], "--audio") == 0){
	measure_eeg_responses = false;
	playAudio = true;
	printf("Showing stimulus..\n");
	hasCommand = true;
      }
      else if(strncmp(argv[i], "--picture-dir=", 14) == 0){
	char* p = &(argv[i][14]);
	if(strlen(p) > 0) pictureDirs.push_back(p);
      }
      else if(strncmp(argv[i], "--music-dir=", 12) == 0){
	char* p = &(argv[i][14]);
	if(strlen(p) > 0) musicDirs.push_back(p);
      }
      else if(strncmp(argv[i], "--keyword-file=", 15) == 0){
	char* p = &(argv[i][15]);
	if(strlen(p) > 0) keywordFiles.push_back(p);
      }
      else if(strncmp(argv[i], "--target=", 9) == 0){
	char* p = &(argv[i][9]);
	if(strlen(p) > 0){
	  std::vector<float> f;
	  if(parse_float_vector(f, p)){
	    target.resize(f.size());
	    for(unsigned int i=0;i<target.size();i++)
	      target[i] = f[i];
	  }
	}
      }
      else if(strncmp(argv[i], "--target-error=", 15) == 0){
	char* p = &(argv[i][15]);
	if(strlen(p) > 0){
	  std::vector<float> f;
	  if(parse_float_vector(f, p)){
	    var_error.resize(f.size());
	    for(unsigned int i=0;i<var_error.size();i++)
	      var_error[i] = f[i];
	  }
	}
      }
      else if(strcmp(argv[i], "--simulate") == 0){
	simulation = true;
      }
      else if(strcmp(argv[i], "--random") == 0){
	printf("Random stimulus activated..\n");
	randomStim = true;
      }
      else if(strcmp(argv[i], "-v") == 0){
	verbose = true;
      }
      else if(strcmp(argv[i], "--help") == 0){
	print_usage();
	return 0;
      }
      else{
	print_usage();
	printf("ERROR: bad parameters.\n");
	return -1;
      }
    }
    
    if(hasCommand == false){
      print_usage();
      printf("ERROR: bad parameters.\n");
      return -1;
    }
    
  }
  
  
  if(target.size() > 0 && var_error.size() < target.size()){
    var_error.resize(target.size());
    for(unsigned int i=0;i<var_error.size();i++){
      var_error[i] = 1.0f;
    }
  }
  
  
  if(keywordFiles.size() == 0)
    keywordFiles.push_back("keywords.txt");
  
  if(pictureDirs.size() == 0)
    pictureDirs.push_back("pics");

  if(musicDirs.size() == 0)
    musicDirs.push_back("music");
  
  
  std::vector<std::string> words;
  
  for(unsigned int i=0;i<keywordFiles.size();i++){
    if(!loadWords(keywordFiles[i], words)){
      printf("Couldn't load keywords from file: %s.\n", keywordFiles[i].c_str());
      return -1;
    }
  }

  if(verbose)
    printf("%d keywords loaded.\n", words.size());
  
  std::vector<std::string> pictures;
  
  for(unsigned int i=0;i<pictureDirs.size();i++){
    if(!loadPictures(pictureDirs[i], pictures)){
      printf("Couldn't find any pictures from directory: %s.\n", pictureDirs[i].c_str());
      return -1;
    }
  }
  
  if(verbose)
    printf("%d pictures loaded.\n", pictures.size());
  
  std::vector<std::string> sounds;
  
  for(unsigned int i=0;i<musicDirs.size();i++){
    if(!loadMusic(musicDirs[i], sounds)){
      printf("Couldn't find any music from directory: %s.\n", musicDirs[i].c_str());
      return -1;
    }
  }
  
  if(verbose)
    printf("%d tracks loaded.\n", sounds.size());  
  
  /////////////////////////////////////////////////////////////////////////////
  
  DataSource* eeg = new EmotivInsightStub();
  // ResonanzShow* sdl = new ResonanzShow(640, 480);
  ResonanzShow* sdl = new ResonanzShow(800, 600);
  
  
  if(measure_eeg_responses == false && randomStim == false){
    std::cout << std::endl;
    std::cout << "target    = " << target << std::endl;
    std::cout << "var_error = " << var_error << std::endl;

    if(eeg->getNumberOfSignals() != target.size() || 
       eeg->getNumberOfSignals() != var_error.size()){
      printf("ERROR: Number of EEG signals is %d but target/error dimensions mismatch (%d/%d).\n",
	     eeg->getNumberOfSignals(), target.size(), var_error.size());
      
      delete sdl;
      delete eeg;
      
      return -1;
    }
  }
  
  
  if(sdl->init(words, pictures, sounds) == false){
    printf("ERROR: cannot initialize SDL.\n");
    delete sdl;
    delete eeg;
    
    return -1;
  }
  
  
  bool keypress = false;
  time_t music_change_time = 0;
  time_t MUSIC_TIME_LIMIT = 10;
  
  // used by simulation code, instead of using Emotiv measurements we blindly
  // trust to nnetwork<> predictions and create stimulation sequence based on
  // predicted next state after 
  whiteice::math::vertex<> expectedState;
  expectedState.resize(eeg->getNumberOfSignals());
  for(unsigned int i=0;i<expectedState.size();i++)
    expectedState[i] = 0.5f; // initial starting state

  std::vector< whiteice::dataset<>* > keywordData;
  std::vector< whiteice::dataset<>* > pictureData;
  std::vector< whiteice::dataset<>* > soundData;

  keywordData.resize(words.size());
  for(unsigned int i=0;i<words.size();i++)
    keywordData[i] = NULL;
  
  pictureData.resize(pictures.size());
  for(unsigned int i=0;i<pictures.size();i++)
    pictureData[i] = NULL;
  
  soundData.resize(sounds.size());
  for(unsigned int i=0;i<sounds.size();i++)
    soundData[i] = NULL;
  
  // for stimulation
  std::vector< prediction* > keywordPredict;
  std::vector< prediction* > picturePredict;
  std::vector< prediction* > soundPredict;

  // we load all neural network and dataset configurations into memory we can find
  if(measure_eeg_responses == false){ // stimulus mode
    keywordPredict.resize(words.size());
    for(unsigned int i=0;i<words.size();i++){
      std::string ds_filename = calculateHashName(words[i]) + ".ds";
      ds_filename = "datasets/" + ds_filename;
      
      std::string nn_filename = calculateHashName(words[i]) + ".cfg";
      nn_filename = "nnetworks/" + nn_filename;
      
      keywordPredict[i] = new prediction;
      
      if(keywordPredict[i]->ds.load(ds_filename) == false || 
	 keywordPredict[i]->nn.load(nn_filename) == false){
	delete keywordPredict[i];
	keywordPredict[i] = NULL;
      }
      else{
	keywordPredict[i]->ds.clearData(0);
	keywordPredict[i]->ds.clearData(1);
      }
    }
    
    picturePredict.resize(pictures.size());
    for(unsigned int i=0;i<pictures.size();i++){
      std::string ds_filename = calculateHashName(pictures[i]) + ".ds";
      ds_filename = "datasets/" + ds_filename;
      
      std::string nn_filename = calculateHashName(pictures[i]) + ".cfg";
      nn_filename = "nnetworks/" + nn_filename;
      
      picturePredict[i] = new prediction;
      
      if(picturePredict[i]->ds.load(ds_filename) == false || 
	 picturePredict[i]->nn.load(nn_filename) == false){
	delete picturePredict[i];
	picturePredict[i] = NULL;
      }
      else{
	picturePredict[i]->ds.clearData(0);
	picturePredict[i]->ds.clearData(1);
      }
    }
    
    soundPredict.resize(sounds.size());
    for(unsigned int i=0;i<sounds.size();i++){
      std::string ds_filename = calculateHashName(sounds[i]) + ".ds";
      ds_filename = "datasets/" + ds_filename;
      
      std::string nn_filename = calculateHashName(sounds[i]) + ".cfg";
      nn_filename = "nnetworks/" + nn_filename;
      
      soundPredict[i] = new prediction;
      
      if(soundPredict[i]->ds.load(ds_filename) == false || 
	 soundPredict[i]->nn.load(nn_filename) == false){
	delete soundPredict[i];
	soundPredict[i] = NULL;
      }
      else{
	soundPredict[i]->ds.clearData(0);
	soundPredict[i]->ds.clearData(1);
      }
    }
    
  }

  
  // loads all pictures into memory
  {
    for(unsigned int i=0;i<pictures.size();i++)
      sdl->showScreen(0, i);
  }
  
  while(!sdl->keypress())
  {
    if(measure_eeg_responses){
      std::vector<float> prev, after;
      std::vector<float> soundPrev, soundAfter;
      eeg->data(prev);
      
      unsigned int keyword = rand() % words.size();
      unsigned int picture = rand() % pictures.size();
      unsigned int track   = rand() % sounds.size();
      
      if(showPictures == false)
	picture = (unsigned int)(-1);
      if(showKeywords == false)
	keyword = (unsigned int)(-1);
      if(playAudio == false)
	track = (unsigned int)(-1);
      
      sdl->showScreen(keyword, picture);
      
      if((time(0) - music_change_time) >= MUSIC_TIME_LIMIT && playAudio){
	eeg->data(soundAfter);
	
	////////////////////////////////////////////////////////
	// add data to soundsData datasets
	{
	  if(soundData[track] == NULL){
	    std::string filename = calculateHashName(sounds[track]) + ".ds";
	    filename = "datasets/" + filename;
	    
	    soundData[track] = new whiteice::dataset<>();
	    
	    if(soundData[track]->load(filename) == false){
	      std::string name = "input";
	      soundData[track]->createCluster(name, eeg->getNumberOfSignals());
	      name = "output";
	      soundData[track]->createCluster(name, eeg->getNumberOfSignals());
	    }
	  }
	  
	  if(soundAfter.size() > 0 && soundPrev.size() > 0){
	    std::vector< whiteice::math::blas_real<float> > t;
	    t.resize(soundPrev.size());
	    for(unsigned int i=0;i<t.size();i++)
	      t[i] = soundPrev[i];
	    
	    soundData[track]->add(0, t);
	    
	    t.resize(soundAfter.size());
	    for(unsigned int i=0;i<t.size();i++)
	      t[i] = soundAfter[i];
	    
	    soundData[track]->add(1, t);
	  }
	  
	}
	
	////////////////////////////////////////////////////////

	music_change_time = time(0);
	sdl->playMusic(track);
	
	eeg->data(soundPrev);
      }
      
      sdl->delay(1000); // 1 seconds is maybe ok when measuring responses
      
      eeg->data(after);

      if(showPictures){
	if(pictureData[picture] == NULL){
	  std::string filename = calculateHashName(pictures[picture]) + ".ds";
	  filename = "datasets/" + filename;
	  
	  pictureData[picture] = new whiteice::dataset<>();
	  
	  if(pictureData[picture]->load(filename) == false){
	    std::string name = "input";
	    pictureData[picture]->createCluster(name, eeg->getNumberOfSignals());
	    name = "output";
	    pictureData[picture]->createCluster(name, eeg->getNumberOfSignals());
	  }
	}

	std::vector< whiteice::math::blas_real<float> > t;
	t.resize(prev.size());
	for(unsigned int i=0;i<t.size();i++)
	  t[i] = prev[i];
	
	pictureData[picture]->add(0, t);

	t.resize(after.size());
	for(unsigned int i=0;i<t.size();i++)
	  t[i] = after[i];

	pictureData[picture]->add(1, t);
      }
      
      
      if(showKeywords){
	if(keywordData[keyword] == NULL){
	  std::string filename = calculateHashName(words[keyword]) + ".ds";
	  filename = "datasets/" + filename;
	  
	  keywordData[keyword] = new whiteice::dataset<>();
	  
	  if(keywordData[keyword]->load(filename) == false){
	    std::string name = "input";
	    keywordData[keyword]->createCluster(name, eeg->getNumberOfSignals());
	    name = "output";
	    keywordData[keyword]->createCluster(name, eeg->getNumberOfSignals());
	  }
	}
	
	std::vector< whiteice::math::blas_real<float> > t;
	t.resize(prev.size());
	for(unsigned int i=0;i<t.size();i++)
	  t[i] = prev[i];
	
	keywordData[keyword]->add(0, t);

	t.resize(after.size());
	for(unsigned int i=0;i<t.size();i++)
	  t[i] = after[i];

	keywordData[keyword]->add(1, t);
      }
      
    }
    else{
      // currently the stimulus code just displays random keywords and pictures
      // as there is no Emotiv Insight signal source to guide stimulus selection
      
      unsigned int keyword = rand() % words.size();
      unsigned int picture = rand() % pictures.size();
      unsigned int track   = rand() % sounds.size();
      
      // pseudo random guess for the randomly 
      // selected initial stimulus sequence response
      whiteice::math::vertex<> initialResultWords;
      whiteice::math::vertex<> initialResultPictures;
      whiteice::math::vertex<> initialResultSounds;
      
      initialResultWords.resize(eeg->getNumberOfSignals());
      initialResultPictures.resize(eeg->getNumberOfSignals());
      initialResultSounds.resize(eeg->getNumberOfSignals());
      
      for(unsigned int i=0;i<eeg->getNumberOfSignals();i++){
	initialResultWords[i]    = ((float)rand())/((float)RAND_MAX);
	initialResultPictures[i] = ((float)rand())/((float)RAND_MAX);
	initialResultSounds[i]   = ((float)rand())/((float)RAND_MAX);
      }

      whiteice::math::vertex<> currentState;
      std::vector<float> state;
      
      if(simulation == false){
	eeg->data(state);
	currentState.resize(state.size());
	for(unsigned int i=0;i<state.size();i++)
	  currentState[i] = state[i];
      }
      else{
	currentState = expectedState;
      }
      
      if(target.size() < currentState.size())
	target.resize(currentState.size());
      
      // currently we use generic error function for all cases
      if(var_error.size() < currentState.size()){
	var_error.resize(currentState.size());
	for(unsigned int i=0;i<state.size();i++)
	  var_error[i] = 1.0f;
      }
      
      // used by simulation code
      whiteice::math::vertex<> keywordNewState;
      whiteice::math::vertex<> pictureNewState;
      whiteice::math::vertex<> trackNewState;
      
      // shows keywords and pictures
      if(playVideo){
	if(randomStim == false){
	  keyword = findBestStimulus(keyword, initialResultWords, keywordPredict, 
				     currentState, target, var_error, keywordNewState);
	  
	  picture = findBestStimulus(picture, initialResultPictures, picturePredict, 
				     currentState, target, var_error, pictureNewState);
	}
	
	
	if(verbose){
	  std::cout << "picture = " << pictures[picture] << std::endl;
	  std::cout << "keyword = " << words[keyword] << std::endl;
	}
	
	sdl->showScreen(keyword, picture);
      }
      
      
      // plays music [much slower change rate]
      if((time(0) - music_change_time) >= MUSIC_TIME_LIMIT && playAudio){
	if(randomStim == false){
	  track   = findBestStimulus(track,   initialResultSounds, soundPredict, 
				     currentState, target, var_error, trackNewState);
	}

	if(verbose){
	  std::cout << "track = " << sounds[track] << std::endl;
	}
	
	music_change_time = time(0);
	sdl->playMusic(track);
      }
      
      
      if(simulation == true)
      {
	expectedState.resize(eeg->getNumberOfSignals());
	expectedState.zero();
	unsigned int N = 0;
	
	if(keywordNewState.size() == eeg->getNumberOfSignals()){
	  expectedState += keywordNewState; N++; 
	}
	if(pictureNewState.size() == eeg->getNumberOfSignals()){ 
	  expectedState += pictureNewState; N++; 
	}
	if(trackNewState.size() == eeg->getNumberOfSignals()){ 
	  expectedState += trackNewState; N++; 
	}
	
	expectedState = expectedState / N;
      }
      
      
      // 250ms might be good
      // TODO we measure time spend in a loop wait only needed msecs in order to keep
      //      framerate constant
      sdl->delay(500); // [we keep updating image as fast as we can?]
    }
        
  }
  

  // saves datasets to back to files if there is something save (non-NULLs)
  if(measure_eeg_responses){
    for(unsigned int i=0;i<words.size();i++){
      if(keywordData[i] != NULL){
	std::string filename = calculateHashName(words[i]) + ".ds";
	filename = "datasets/" + filename;
	
	if(keywordData[i]->save(filename) == false)
	  printf("ERROR: Cannot save dataset for (%s): %s\n", 
		 words[i].c_str(), filename.c_str());
	
	delete keywordData[i];
      }
      keywordData[i] = NULL;
    }
    
    for(unsigned int i=0;i<pictures.size();i++){
      if(pictureData[i] != NULL){
	std::string filename = calculateHashName(pictures[i]) + ".ds";
	filename = "datasets/" + filename;
	
	if(pictureData[i]->save(filename) == false)
	  printf("ERROR: Cannot save dataset for (%s): %s\n", 
		 pictures[i].c_str(), filename.c_str());
	
	delete pictureData[i];
      }
      pictureData[i] = NULL;
    }
    
    for(unsigned int i=0;i<sounds.size();i++){
      if(soundData[i] != NULL){
	std::string filename = calculateHashName(sounds[i]) + ".ds";
	filename = "datasets/" + filename;
	
	if(soundData[i]->save(filename) == false)
	  printf("ERROR: Cannot save dataset for (%s): %s\n", 
		 sounds[i].c_str(), filename.c_str());
	
	delete soundData[i];
      }
      soundData[i] = NULL;
    }
  }
  else{
    // video stimulus mode: clean up prediction engine variables
    
    for(unsigned int i=0;i<words.size();i++)
      if(keywordPredict[i]) delete keywordPredict[i];
    
    for(unsigned int i=0;i<pictures.size();i++)
      if(picturePredict[i]) delete picturePredict[i];
    
    for(unsigned int i=0;i<sounds.size();i++)
      if(soundPredict[i]) delete soundPredict[i];
  }
  
  
  if(sdl) delete sdl;
  if(eeg) delete eeg;
  
  return 0;
}



bool loadWords(std::string filename, std::vector<std::string>& words)
{
  FILE* handle = fopen(filename.c_str(), "rt");
  
  if(handle == 0)
    return false;
  
  char buffer[256];
  
  while(fgets(buffer, 256, handle) == buffer){
    const int N = strlen(buffer);
    
    for(int i=0;i<N;i++)
      if(buffer[i] == '\n')
	buffer[i] = '\0';
    
    if(strlen(buffer) > 1)
      words.push_back(buffer);
  }
  
  fclose(handle);
  
  return true;
}


bool loadPictures(std::string directory, std::vector<std::string>& pictures)
{
  // looks for pics/*.jpg and pics/*.png files
  
  DIR* dp;
  struct dirent *ep;
  
  dp = opendir(directory.c_str());
  
  if(dp == 0) return false;
  
  while((ep = readdir(dp)) != NULL){
    if(ep->d_name == NULL) continue;    
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


bool loadMusic(std::string directory, std::vector<std::string>& tracks)
{
  // looks for pics/*.jpg and pics/*.png files
  
  DIR* dp;
  struct dirent *ep;
  
  dp = opendir(directory.c_str());
  
  if(dp == 0) return false;
  
  while((ep = readdir(dp)) != NULL){
    if(ep->d_name == NULL) continue;    
    std::string name = ep->d_name;
    
    if(name.find(".mp3") == (name.size() - 4) || 
       name.find(".ogg") == (name.size() - 4))
    {
      std::string fullname = directory.c_str();
      fullname = fullname + "/";
      fullname = fullname + name;
      
      tracks.push_back(fullname);
    }
  }
  
  closedir(dp);
  
  return true;
}

#endif

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

#if 0

std::string calculateHashName(std::string& filename)
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
	sprintf(buffer, "%.2x", hash160[i]);
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


float distanceError(const whiteice::math::vertex<>& initialResult, 
		    const whiteice::math::vertex<>& target, 
		    const whiteice::math::vertex<>& varError)
{
  float error = 0.0f;
  
  for(unsigned int i=0;i<target.size();i++){
    float e = 0.0f;
    
    convert(e, ((initialResult[i] - target[i])/varError[i]));
    
    error += e;
  }
}


/*
 * finds the best stimulus to reach the target state with 
 * the prediction engine/system
 */
unsigned int findBestStimulus(const unsigned int initialStimulationIndex,
			      const whiteice::math::vertex<>& initialResult,
			      const std::vector<prediction*>& preds,
			      const whiteice::math::vertex<>& currentState,
			      const whiteice::math::vertex<>& target,
			      const whiteice::math::vertex<>& varError,
			      whiteice::math::vertex<>& predictedNewState)
{
  unsigned int index = initialStimulationIndex;
  float theBestResult = distanceError(initialResult, target, varError);
  predictedNewState = initialResult;
  
  for(unsigned int i=0;i<preds.size();i++){
    if(preds[i] == NULL) continue; // skip this one
    
    if(preds[i]->ds.dimension(0) != currentState.size() ||
       preds[i]->ds.dimension(1) != currentState.size())
      continue; // ignore bad data
    
    if(preds[i]->nn.input_size() != currentState.size() || 
       preds[i]->nn.output_size() != currentState.size())
      continue; // ignore bad data
    
    whiteice::math::vertex<> x = currentState;
    
    if(preds[i]->ds.preprocess(0, x) == false) continue;
    
    preds[i]->nn.input() = x;
    if(preds[i]->nn.calculate(false) == false) continue;
    x = preds[i]->nn.output();
    
    if(preds[i]->ds.invpreprocess(1, x) == false) continue;
    
    // now we have predicted output response state
    // selection is probabilistic: 
    // better solution is selected only 80% of the time
    
    float error = distanceError(x, target, varError);
    if(error < theBestResult){
      
      if( (((float)rand())/((float)RAND_MAX)) <= 0.80 ){
	predictedNewState = x;
	theBestResult = error;
	index = i;
      }
    }
  }
  
  return index;
}

#endif
