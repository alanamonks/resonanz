/*
 * ResonanzEngine.h
 *
 *  Created on: 13.6.2015
 *      Author: Tomas Ukkonen
 */

#ifndef RESONANZENGINE_H_
#define RESONANZENGINE_H_

#include <string>
#include <thread>
#include <mutex>
#include <vector>
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

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <SDL_mixer.h>

#include <dinrhiw.h>

#include "DataSource.h"

#include "SDLSoundSynthesis.h"
#include "SDLMicrophoneListener.h"

#include "SDLTheora.h"
#include "SDLAVCodec.h"

#include "HMMStateUpdator.h"


namespace whiteice {
namespace resonanz {

/**
 * Resonanz command that is being executed or is given to the engine
 */
class ResonanzCommand
{
public:
  ResonanzCommand();
  virtual ~ResonanzCommand();
  
  static const unsigned int CMD_DO_NOTHING  = 0;
  static const unsigned int CMD_DO_RANDOM   = 1;
  static const unsigned int CMD_DO_MEASURE  = 2;
  static const unsigned int CMD_DO_OPTIMIZE = 3;
  static const unsigned int CMD_DO_EXECUTE  = 4;
  static const unsigned int CMD_DO_MEASURE_PROGRAM = 5;
  
  unsigned int command = CMD_DO_NOTHING;
  
  bool showScreen = false;
  std::string pictureDir;
  std::string keywordsFile;
  std::string modelDir;
  std::string audioFile;
  
  // does execute use EEG values or do Monte Carlo simulation
  bool blindMonteCarlo = false;
  bool saveVideo = false;
  
  std::vector<std::string> signalName;
  std::vector< std::vector<float> > programValues;
  
  unsigned int programLengthTicks = 0; // measured program length in ticks
};

/**
 * ResonanzEngine is singleton (You can have only SINGLE instance active at time)
 * (using multiple ResonanzEngine's at the same time is NOT thread-safe)
 */
class ResonanzEngine 
{
public:
	ResonanzEngine(const unsigned int numDeviceChannels = 7);
	virtual ~ResonanzEngine();

	// what resonanz is doing right now [especially interesting if we are optimizing model]
	std::string getEngineStatus() throw();

	// resets resonanz-engine (worker thread stop and recreation)
	bool reset() throw();

	bool cmdDoNothing(bool showScreen);

	bool cmdRandom(const std::string& pictureDir, const std::string& keywordsFile,
		       const std::string& audioFile,
		       bool saveVideo) throw();

	bool cmdMeasure(const std::string& pictureDir, const std::string& keywordsFile, const std::string& modelDir) throw();

	bool cmdOptimizeModel(const std::string& pictureDir, const std::string& keywordsFile, const std::string& modelDir) throw();

	bool cmdMeasureProgram(const std::string& mediaFile,
				const std::vector<std::string>& signalNames,
				const unsigned int programLengthTicks) throw();

	bool cmdExecuteProgram(const std::string& pictureDir, 
			       const std::string& keywordsFile, 
			       const std::string& modelDir,
			       const std::string& audioFile,
			       const std::vector<std::string>& targetSignal, 
			       const std::vector< std::vector<float> >& program,
			       bool blindMonteCarlo = false, bool saveVideo = false) throw();


	bool cmdStopCommand() throw();

	// returns true if resonaz-engine is executing some other command than do-nothing
	bool isBusy() throw();

	bool keypress(); // detects keypress from GUI

  
        bool workActive() const {
	  // returns true if there is active work going on and cannot stop..
	  if(video)
	    if(video->busy())
	      return true;
	  
	  return false;
	}

	// measured program functions
	bool invalidateMeasuredProgram(); // invalidates currently measured program
	bool getMeasuredProgram(std::vector< std::vector<float> >& program);

	// analyzes given measurements database and model performance
	std::string analyzeModel(const std::string& modelDir) const;
	
	// analyzes given measurements database and model performance more accurately
	std::string analyzeModel2(const std::string& pictureDir, 
				  const std::string& keywordsFile, 
				  const std::string& modelDir) const;

	// calculates delta statistics from the measurements [with currently selected EEG]
	std::string deltaStatistics(const std::string& pictureDir, 
				    const std::string& keywordsFile, 
				    const std::string& modelDir) const;

	// returns collected program performance statistics [program weighted RMS]
	std::string executedProgramStatistics() const;
	
	// exports data to ASCII format files (.txt files)
	bool exportDataAscii(const std::string& pictureDir, 
			     const std::string& keywordsFile, 
			     const std::string& modelDir) const;
	
	bool deleteModelData(const std::string& modelDir);

	// sets and gets EEG device information [note: engine must be in "doNothing" state
	// for the change of device to be successful]

	static const int RE_EEG_NO_DEVICE = 0;
	static const int RE_EEG_RANDOM_DEVICE = 1;
	static const int RE_EEG_EMOTIV_INSIGHT_DEVICE = 2;
	static const int RE_EEG_IA_MUSE_DEVICE = 3;
	static const int RE_WD_LIGHTSTONE = 4;
        static const int RE_EEG_IA_MUSE_4CH_DEVICE = 5;
  

	bool setEEGDeviceType(int deviceNumber);
	int getEEGDeviceType();
	void getEEGDeviceStatus(std::string& status);
	const DataSource& getDevice() const;

	// sets special configuration parameter of resonanz-engine
	bool setParameter(const std::string& parameter, const std::string& value);


private:
	const std::string windowTitle = "Neuromancer NeuroStim";
	const std::string iconFile = "brain.png";

	volatile bool thread_is_running;
	volatile bool thread_initialized = false;
	std::thread* workerThread;
	std::mutex   thread_mutex;

	ResonanzCommand currentCommand; // what the engine should be doing right now

	ResonanzCommand* incomingCommand;
	std::mutex command_mutex;

	std::string engineState;
	std::mutex status_mutex;

	// main worker thread loop to execute commands
	void engine_loop();

	// functions used by updateLoop():

	void engine_setStatus(const std::string& msg) throw();

	void engine_sleep(int msecs); // sleeps for given number of milliseconds, updates engineState
	bool engine_checkIncomingCommand();

	bool engine_SDL_init(const std::string& fontname);
	bool engine_SDL_deinit();

	void engine_stopHibernation();

	bool measureColor(SDL_Surface* image, SDL_Color& averageColor);

	bool engine_loadMedia(const std::string& picdir, const std::string& keyfile, bool loadData);
	bool engine_showScreen(const std::string& message, 
			       unsigned int picture,
			       const std::vector<float>& synthparams);

	bool engine_playAudioFile(const std::string& audioFile);
	bool engine_stopAudioFile();

	void engine_pollEvents();

	void engine_updateScreen();

	SDL_Window* window = nullptr;
	int SCREEN_WIDTH, SCREEN_HEIGHT;
	TTF_Font* font = nullptr;
	bool audioEnabled = true; // false if using audiofiles is disabled
	Mix_Music* music = nullptr;
	bool fullscreen = false; // set to use fullscreen mode otherwise window

	bool keypressed = false;
	std::mutex keypress_mutex;

	long long tick = 0; // current engine tick (one tick is TICK_MS long)

	// set to 100ms (set tick back to 1000ms = 1 sec)
	static const unsigned int TICK_MS = 250;             // how fast engine runs: engine measures ticks and executes (one) command only when tick changes (was: 100)
	static const unsigned int MEASUREMODE_DELAY_MS = 500; // how long each screen is shown when measuring response (was: 200)

	// media resource
	std::vector<std::string> keywords;
	std::vector<std::string> pictures;
	std::vector<SDL_Surface*> images;

        const unsigned int PICFEATURES_SIZE = 20; // 5*(3+1)
        std::vector< whiteice::math::vertex<> > imageFeatures; // feature vectors of images
  
	SDLSoundSynthesis* synth = nullptr;
	SDLMicListener* mic = nullptr;

	// used currently by random image/picture viewer
	unsigned int currentKey = 0;
	unsigned int currentPic = 0;
	long long SHOWTIME_TICKS = (long long)(0.5 / (TICK_MS/1000.0));
        long long latestKeyPicChangeTick = -SHOWTIME_TICKS;

	bool loadWords(const std::string filename, std::vector<std::string>& words) const;
	bool loadPictures(const std::string directory, std::vector<std::string>& pictures) const;


	bool engine_loadDatabase(const std::string& modelDir);
	bool engine_storeMeasurement(unsigned int pic, unsigned int key, 
				     const std::vector<float>& eegBefore, 
				     const std::vector<float>& eegAfter,
				     const std::vector<float>& synthBefore,
				     const std::vector<float>& synthAfter);
	
	bool engine_saveDatabase(const std::string& modelDir);
	std::string calculateHashName(const std::string& filename) const;
        
        
        std::string latestModelDir;

        whiteice::dataset<> eegData; // EEG values data for KMeans and HMM brain state detection
        
        
	std::vector< whiteice::dataset<> > keywordData;
	std::vector< whiteice::dataset<> > pictureData;
	whiteice::dataset<>                synthData; // sound synthesis data
	
        mutable std::mutex database_mutex;  // mutex to synchronize I/O access to dataset files
	bool pcaPreprocess = false; // should measured data be preprocessed using PCA (no pca preprocessing as the default!)


	DataSource* eeg = nullptr;
	std::mutex eeg_mutex;
	int eegDeviceType = RE_EEG_NO_DEVICE;

        unsigned int musePort = 4545; // parameters when creating MuseOSC device/class for localhost
	
        bool engine_optimizeModels(unsigned int& currentHMMModel,
				   unsigned int& currentPictureModel, 
				   unsigned int& currentKeywordModel, 
				   bool& soundModelCalculated);

        mutable std::mutex hmm_mutex; // synchronized manipulation of HMM params
        whiteice::KMeans<>* kmeans = nullptr;
        whiteice::HMM* hmm = nullptr;
        unsigned int HMMstate = 0; // current HMM state
        HMMStateUpdatorThread* hmmUpdator = nullptr;
  
        const unsigned int KMEANS_NUM_CLUSTERS = 15;
        const unsigned int HMM_NUM_CLUSTERS = 20; // number of HMM hidden brain states
	
	// whiteice::pLBFGS_nnetwork<>* optimizer = nullptr;
	whiteice::math::NNGradDescent<>* optimizer = nullptr;
	
	const unsigned int NUM_OPTIMIZER_THREADS = 2;
	const unsigned int NUM_OPTIMIZER_ITERATIONS = 500; // was: 150, 1000
	bool optimizeSynthOnly = false;

  	whiteice::nnetwork<>* nn = nullptr;
        whiteice::nnetwork<>* nnkey = nullptr; // key data neural network 
	whiteice::nnetwork<>* nnsynth = nullptr; // synth data neural network
        
	whiteice::bayesian_nnetwork<>* bnn = nullptr;
	whiteice::UHMC<>* bayes_optimizer = nullptr;

	const int NEURALNETWORK_COMPLEXITY = 10; // values above 10 seem to make sense (was: 25, 10)
	const int NEURALNETWORK_DEPTH = 2; // how many layers neural network have (was: 3, 6, *10*)

        bool use_bayesian_nnetwork = false;
	const unsigned int BAYES_NUM_SAMPLES = 250; // number of samples collected from "bayesian posterior" (what we really sample is MLE likelihood thought..) [reduced from 1000 to 500 because HMC now don't add samples until epsilon is properly learnt]

	bool engine_loadModels(const std::string& modelDir); // loads prediction models for program execution, returns false in case of failure
	bool engine_executeProgram(const std::vector<float>& eegCurrent,
			const std::vector<float>& eegTarget, const std::vector<float>& eegTargetVariance, float timedelta);

	// executes program blindly based on Monte Carlo sampling and prediction models
	bool engine_executeProgramMonteCarlo(const std::vector<float>& eegTarget,
			const std::vector<float>& eegTargetVariance, float timedelta);
	
	bool loopMode = false; // loop program forever
  
        unsigned int SHOW_TOP_RESULTS = 3; // how many results show in executeProgram from top results

	std::vector< whiteice::bayesian_nnetwork<> > keywordModels;
	std::vector< whiteice::bayesian_nnetwork<> > pictureModels;
	whiteice::bayesian_nnetwork<>                synthModel;
	bool dataRBFmodel = true; // don't calculate neural networks but use simple model to directly predict response from stimulus

	// number of parameters to test with synthModel before selecting the optimium one 
	const unsigned int SYNTH_NUM_GENERATED_PARAMS = 200; // (was 400, 100, 2000) reduced to 50 because of slowness(?)

        // number of pictures to test per iteration for stimulus response before selecting the optimum one
        const unsigned int PIC_DATASET_SIZE = 100;
  	
	unsigned long long synthParametersChangedTime = 0ULL;

	// estimate output value N(m,cov) for x given dataset data uses nearest neighbourhood estimation
	bool engine_estimateNN(const whiteice::math::vertex<>& x, const whiteice::dataset<>& data,
			whiteice::math::vertex<>& m, whiteice::math::matrix<>& cov);

	// for calculating program performance: RMS statistic
	float programRMS = 0.0f;
	int programRMS_N = 0;

	// for blind Monte Carlo sampling mode
	std::vector< math::vertex<> > mcsamples;
	const unsigned int MONTE_CARLO_SIZE = 1000; // number of samples used

	long long programStarted; // 0 = program has not been started
        //SDLTheora* video = nullptr; // used to encode program into video
        SDLAVCodec* video = nullptr; // used to encode program into video


	std::mutex measure_program_mutex;
	std::vector< std::vector<float> > measuredProgram;
	std::vector< std::vector<float> > rawMeasuredSignals; // used internally

	// display curve parameters (only works in random mode??)
	bool showCurve = false;
	double CURVETIME = 5.0; // show single curve for 1.0 seconds (interpolation time)
	std::vector< whiteice::math::vertex< whiteice::math::blas_real<double> > > startPoint;
	std::vector< whiteice::math::vertex< whiteice::math::blas_real<double> > > endPoint;
	double curveParameter = 1.0;
	long long latestTickCurveDrawn = -100000000;
	std::list<double> historyPower;

	bool randomPrograms = false;

	
	whiteice::RNG<> rng;
};



} /* namespace resonanz */
} /* namespace whiteice */

#endif /* RESONANZENGINE_H_ */
