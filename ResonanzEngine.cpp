/*
 * ResonanzEngine.cpp
 *
 *  Created on: 13.6.2015
 *      Author: Tomas
 */

#include "ResonanzEngine.h"
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>

#include <math.h>
#include <dirent.h>

#include <dinrhiw/dinrhiw.h>

#include "Log.h"
#include "NMCFile.h"
#include "EmotivInsightStub.h"

namespace whiteice {
namespace resonanz {


ResonanzEngine::ResonanzEngine()
{
	std::lock_guard<std::mutex> lock(thread_mutex);

	engine_setStatus("resonanz-engine: starting..");

	workerThread = nullptr;
	thread_is_running = true;

	{
		std::lock_guard<std::mutex> lock(command_mutex);
		incomingCommand = nullptr;
		currentCommand.command = ResonanzCommand::CMD_DO_NOTHING;
		currentCommand.showScreen = false;
	}

	eeg = nullptr;

	// starts updater thread thread
	workerThread = new std::thread(std::bind(ResonanzEngine::engine_loop, this));
	workerThread->detach();
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
		delete eeg;
		eeg = nullptr;
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
		workerThread = new std::thread(std::bind(ResonanzEngine::engine_loop, this));
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


bool ResonanzEngine::cmdRandom(const std::string& pictureDir, const std::string& keywordsFile) throw()
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


bool ResonanzEngine::cmdExecuteProgram(const std::string& pictureDir, const std::string& keywordsFile, const std::string& modelDir,
		const std::vector<std::string>& targetSignal, const std::vector< std::vector<float> >& program)
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

	for(auto& p : program)
		NMCFile::interpolateProgram(p);

	incomingCommand->command = ResonanzCommand::CMD_DO_EXECUTE;
	incomingCommand->showScreen = true;
	incomingCommand->pictureDir = pictureDir;
	incomingCommand->keywordsFile = keywordsFile;
	incomingCommand->modelDir = modelDir;
	incomingCommand->signalName = targetSignal;
	incomingCommand->programValues = program;

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
		if(incomingCommand != nullptr)
			return true; // there is incoming work to be processed
		else
			return false;
	}
	else{
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
		return true;
	}
	else return false;
}



/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// main worker thread loop to execute commands

void ResonanzEngine::engine_loop()
{
	logging.info("engine_loop() started");

	// TODO autodetect good values based on windows screen resolution
	SCREEN_WIDTH  = 800;
	SCREEN_HEIGHT = 600;
	const std::string fontname = "Vera.ttf";

	eeg = new EmotivInsightStub();

	std::vector<unsigned int> nnArchitecture;
	nnArchitecture.push_back(eeg->getNumberOfSignals());
	nnArchitecture.push_back(4*eeg->getNumberOfSignals());
	nnArchitecture.push_back(4*eeg->getNumberOfSignals());
	nnArchitecture.push_back(eeg->getNumberOfSignals());

	nn = new whiteice::nnetwork<>(nnArchitecture);
	unsigned int currentPictureModel = 0;
	unsigned int currentKeywordModel = 0;

	// used to execute program [targetting target values]
	const float programHz = 1.0; // 1 program step means 1 second
	std::vector< std::vector<float> > program;
	std::vector< std::vector<float> > programVar;
	long long programStarted = 0LL; // program has not been started




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
			return;
		}
	}


	while(thread_is_running){

		ResonanzCommand prevCommand = currentCommand;
		if(engine_checkIncomingCommand() == true){
			logging.info("new engine command received");
			// we must make engine state transitions, state transfer from the previous command to the new command

			// state exit actions:
			if(prevCommand.command == ResonanzCommand::CMD_DO_MEASURE){
				engine_setStatus("resonanz-engine: saving database..");
				if(engine_saveDatabase(prevCommand.modelDir) == false){
					logging.error("saving database failed");
				}
				else{
					logging.error("saving database successful");
				}

				keywordData.clear();
				pictureData.clear();
			}
			else if(prevCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE){
				// removes unnecessarily data structures from memory (measurements database) [no need to save it because it was not changed]
				keywordData.clear();
				pictureData.clear();

				// also stops computation if needed
				if(optimizer != nullptr){
					if(optimizer->isRunning())
						optimizer->stopComputation();
					delete optimizer;
					optimizer = nullptr;
				}
			}
			else if(prevCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
				keywordData.clear();
				pictureData.clear();
				keywordModels.clear();
				pictureModels.clear();
			}

			// state exit/entry actions:

			// checks if we want to have open graphics window and opens one if needed
			if(currentCommand.showScreen == true && prevCommand.showScreen == false){
				if(window != nullptr) SDL_DestroyWindow(window);

				window = SDL_CreateWindow(windowTitle.c_str(),
							    SDL_WINDOWPOS_CENTERED,
							    SDL_WINDOWPOS_CENTERED,
							    SCREEN_WIDTH, SCREEN_HEIGHT,
							    SDL_WINDOW_SHOWN);

				if(window != nullptr){
					SDL_Surface* icon = IMG_Load(iconFile.c_str());
					if(icon != nullptr){
						SDL_SetWindowIcon(window, icon);
						SDL_FreeSurface(icon);
					}

					SDL_Surface* surface = SDL_GetWindowSurface(window);
					SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));
					SDL_UpdateWindowSurface(window);
				}

				SDL_RaiseWindow(window);
			}
			else if(currentCommand.showScreen == true && prevCommand.showScreen == true){
				// just empties current window with blank (black) screen
				if(window == nullptr){
					window = SDL_CreateWindow(windowTitle.c_str(),
								    SDL_WINDOWPOS_CENTERED,
								    SDL_WINDOWPOS_CENTERED,
								    SCREEN_WIDTH, SCREEN_HEIGHT,
								    SDL_WINDOW_SHOWN);
				}

				if(window != nullptr){
					SDL_Surface* icon = IMG_Load(iconFile.c_str());
					if(icon != nullptr){
						SDL_SetWindowIcon(window, icon);
						SDL_FreeSurface(icon);
					}

					SDL_Surface* surface = SDL_GetWindowSurface(window);
					SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));
					SDL_UpdateWindowSurface(window);
				}

				SDL_RaiseWindow(window);
			}
			else if(currentCommand.showScreen == false){
				if(window != nullptr) SDL_DestroyWindow(window);
				window = nullptr;
			}

			// state entry actions:

			// (re)loads media resources (pictures, keywords) if we want to do stimulation
			if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM || currentCommand.command == ResonanzCommand::CMD_DO_MEASURE ||
					currentCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE || currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
				engine_setStatus("resonanz-engine: loading media files..");

				bool loadData = (currentCommand.command != ResonanzCommand::CMD_DO_OPTIMIZE);

				if(engine_loadMedia(currentCommand.pictureDir, currentCommand.keywordsFile, loadData) == false)
					logging.error("loading media files failed");
				else
					logging.info("loading media files successful");
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
				currentPictureModel = 0;
				currentKeywordModel = 0;

				// checks there is enough data to do meaningful optimization
				bool aborted = false;

				for(unsigned int i=0;i<pictureData.size() && !aborted;i++){
					if(pictureData[i].size() < 10){
						engine_setStatus("resonanz-engine: less than 10 data points per picture/keyword => aborting optimization");
						logging.warn("aborting model optimization command because of too little data (less than 10 samples per case)");
						cmdDoNothing(false);
						aborted = true;
						break;
					}
				}

				for(unsigned int i=0;i<keywordData.size() && !aborted;i++){
					if(keywordData[i].size() < 10){
						engine_setStatus("resonanz-engine: less than 10 data points per picture/keyword => aborting optimization");
						logging.warn("aborting model optimization command because of too little data (less than 10 samples per case)");
						cmdDoNothing(false);
						aborted = true;
						break;
					}
				}

				if(aborted)
					continue; // do not start executing any commands [recheck command input buffer and move back to do nothing command]

			}

			if(currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
				try{
					engine_setStatus("resonanz-engine: loading prediction model..");

					if(engine_loadModels(currentCommand.modelDir) == false){
						logging.error("Couldn't load models from model dir: " + currentCommand.modelDir);
						this->cmdStopCommand();
						continue; // aborts initializing execute command
					}

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
							p[i] = 100000.0f; // 100.000 very large value (near infinite) => can take any value
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

					// starts measuring time for the execution of the program

					auto t0 = std::chrono::system_clock::now().time_since_epoch();
					auto t0ms = std::chrono::duration_cast<std::chrono::milliseconds>(t0).count();
					programStarted = t0ms;

					logging.info("Started executing neurostim program..");

				}
				catch(std::exception& e){

				}
			}

		}

		// executes current command
		if(currentCommand.command == ResonanzCommand::CMD_DO_NOTHING){
			engine_setStatus("resonanz-engine: sleeping..");
			engine_sleep(100); // 100ms

			engine_pollEvents(); // polls for events
			engine_updateScreen(); // always updates window if it exists
		}
		else if(currentCommand.command == ResonanzCommand::CMD_DO_RANDOM){
			engine_setStatus("resonanz-engine: showing random examples..");

			if(keywords.size() > 0 && pictures.size() > 0){
				unsigned int key = rand() % keywords.size();
				unsigned int pic = rand() % pictures.size();

				engine_showScreen(keywords[key], pic);
			}

			engine_pollEvents(); // polls for events
			engine_updateScreen(); // always updates window if it exists
		}
		else if(currentCommand.command == ResonanzCommand::CMD_DO_MEASURE){
			engine_setStatus("resonanz-engine: measuring eeg-responses..");

			if(keywords.size() > 0 && pictures.size() > 0){
				unsigned int key = rand() % keywords.size();
				unsigned int pic = rand() % pictures.size();

				engine_showScreen(keywords[key], pic);

				engine_pollEvents(); // polls for events
				engine_updateScreen(); // always updates window if it exists

				std::vector<float> eegBefore;
				std::vector<float> eegAfter;

				eeg->data(eegBefore);
				engine_sleep(MEASUREMODE_DELAY_MS);
				eeg->data(eegAfter);

				engine_pollEvents();

				engine_storeMeasurement(pic, key, eegBefore, eegAfter);
			}
			else{
				engine_pollEvents(); // polls for events
				engine_updateScreen(); // always updates window if it exists
			}
		}
		else if(currentCommand.command == ResonanzCommand::CMD_DO_OPTIMIZE){
			const float percentage = (currentPictureModel + currentKeywordModel)/((float)(pictureData.size()+keywordData.size()));
			{
				char buffer[80];
				sprintf(buffer, "resonanz-engine: optimizing prediction model (%.2f%%)..", 100.0f*percentage);

				engine_setStatus(buffer);
			}

			if(engine_optimizeModels(currentPictureModel, currentKeywordModel) == false)
				logging.warn("model optimization failure");

		}
		else if(currentCommand.command == ResonanzCommand::CMD_DO_EXECUTE){
			engine_setStatus("resonanz-engine: executing program..");

			auto t1 = std::chrono::system_clock::now().time_since_epoch();
			auto t1ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1).count();

			long long currentSecond = (long long)
					(programHz*(t1ms - programStarted)/1000.0f); // gets current second for the program value

			if(currentSecond < (signed)program[0].size()){
				// executes program
				std::vector<float> eegCurrent;
				std::vector<float> eegTarget;
				std::vector<float> eegTargetVariance;

				eeg->data(eegCurrent);

				eegTarget.resize(eegCurrent.size());
				eegTargetVariance.resize(eegCurrent.size());

				for(unsigned int i=0;i<eegTarget.size();i++){
					eegTarget[i] = program[i][currentSecond];
					eegTargetVariance[i] = programVar[i][currentSecond];
				}

				// shows picture/keyword which model predicts to give closest match to target
				// minimize(picture) ||f(picture,eegCurrent) - eegTarget||/eegTargetVariance
				engine_executeProgram(eegCurrent, eegTarget, eegTargetVariance);

				engine_sleep(20); // 20ms [25 fps] accuracy when showing pictures
			}
			else{
				// program has run to the end => stop
				logging.info("Executing the given program has stopped [program stop time].");
				cmdStopCommand();
			}
		}

		fflush(stdout); // updates eclipse etc console windows properly
	}

	if(window != nullptr)
		SDL_DestroyWindow(window);

	if(eeg != nullptr){
		delete eeg;
		eeg = nullptr;
	}

	if(nn != nullptr){
		delete nn;
		nn = nullptr;
	}

	engine_SDL_deinit();

}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

// loads prediction models for program execution, returns false in case of failure
bool ResonanzEngine::engine_loadModels(const std::string& modelDir)
{
	if(keywords.size() <= 0 || pictures.size() <= 0)
		return false; // no loaded keywords of pictures

	pictureModels.resize(pictures.size());

	unsigned int pictureModelsLoaded = 0;
	unsigned int keywordModelsLoaded = 0;

	for(unsigned int i=0;i<pictureModels.size();i++){
		std::string filename = calculateHashName(pictures[i]) + ".model";
		filename = modelDir + "/" + filename;

		if(pictureModels[i].load(filename) == false){
			logging.error("Loading model file failed: " + filename);
			continue;
		}

		pictureModelsLoaded++;
	}

	keywordModels.resize(keywords.size());

	for(unsigned int i=0;i<keywordModels.size();i++){
		std::string filename = calculateHashName(keywords[i]) + ".model";
		filename = modelDir + "/" + filename;

		if(keywordModels[i].load(filename) == false){
			logging.error("Loading model file failed: " + filename);
			continue;
		}

		keywordModelsLoaded++;
	}

	// returns true if could load at least one model for keywords and models
	return (pictureModelsLoaded > 0 && keywordModelsLoaded > 0);
}


// shows picture/keyword which model predicts to give closest match to target
// minimize(picture) ||f(picture,eegCurrent) - eegTarget||/eegTargetVariance
bool ResonanzEngine::engine_executeProgram(const std::vector<float>& eegCurrent,
		const std::vector<float>& eegTarget, const std::vector<float>& eegTargetVariance)
{
	int bestKeyword = -1;
	int bestPicture = -1;
	float bestError = std::numeric_limits<double>::infinity();

	math::vertex<> target(eegTarget.size());
	math::vertex<> targetVariance(eegTargetVariance.size());

	for(unsigned int i=0;i<target.size();i++){
		target[i] = eegTarget[i];
		targetVariance[i] = eegTargetVariance[i];
	}


	for(unsigned int index=0;index<keywordModels.size();index++){
		whiteice::nnetwork<>& model = keywordModels[index];

		if(model.input_size() != eegCurrent.size() || model.output_size() != eegTarget.size())
			continue; // bad model/data => ignore

		math::vertex<> x(eegCurrent.size());
		for(unsigned int i=0;i<x.size();i++)
			x[i] = eegCurrent[i];

		if(keywordData[index].preprocess(0, x) == false)
			continue;

		model.input() = x;

		if(model.calculate(false) == false)
			continue;

		x = model.output();

		if(keywordData[index].invpreprocess(1, x) == false)
			continue;

		// now we have prediction x to the response to the given keyword
		// calculates error (weighted distance to the target state)

		auto delta = target - x;

		for(unsigned int i=0;i<delta.size();i++)
			delta[i] /= targetVariance[i];

		auto error = delta.norm();

		if(error < bestError){
			math::convert(bestError, error);
			bestKeyword = index;
		}

		engine_pollEvents(); // polls for incoming events in case there are lots of models
	}


	bestError = std::numeric_limits<double>::infinity();

	for(unsigned int index=0;index<pictureModels.size();index++){
		whiteice::nnetwork<>& model = pictureModels[index];

		if(model.input_size() != eegCurrent.size() || model.output_size() != eegTarget.size())
			continue; // bad model/data => ignore

		math::vertex<> x(eegCurrent.size());
		for(unsigned int i=0;i<x.size();i++)
			x[i] = eegCurrent[i];

		if(pictureData[index].preprocess(0, x) == false)
			continue;

		model.input() = x;

		if(model.calculate(false) == false)
			continue;

		x = model.output();

		if(pictureData[index].invpreprocess(1, x) == false)
			continue;

		// now we have prediction x to the response to the given keyword
		// calculates error (weighted distance to the target state)

		auto delta = target - x;

		for(unsigned int i=0;i<delta.size();i++)
			delta[i] /= targetVariance[i];

		auto error = delta.norm();

		if(error < bestError){
			math::convert(bestError, error);
			bestPicture = index;
		}

		engine_pollEvents(); // polls for incoming events in case there are lots of models
	}

	if(bestKeyword < 0 || bestPicture <=0){
		logging.error("Execute command couldn't find picture or keyword command to show (no models?)");
		engine_pollEvents();
		return false;
	}

	// now we have best picture and keyword that is predicted
	// to change users state to target value: show them

	std::string message = keywords[bestKeyword];

	engine_showScreen(message, bestPicture);
	engine_updateScreen();
	engine_pollEvents();

	return true;
}


bool ResonanzEngine::engine_optimizeModels(unsigned int& currentPictureModel, unsigned int& currentKeywordModel)
{
	if(currentPictureModel < pictureData.size()){

		if(optimizer == nullptr){ // first model to be optimized (no need to store the previous result)
			whiteice::math::vertex<> w;

			nn->randomize();
			nn->exportdata(w);

			optimizer = new whiteice::LBFGS_nnetwork<>(*nn, pictureData[currentPictureModel], false, false);

			{
				char buffer[1024];
				sprintf(buffer, "resonanz model optimization started: picture %d database size: %d",
						currentPictureModel, pictureData[currentPictureModel].size(0));
				logging.info(buffer);
			}

			optimizer->minimize(w);
		}
		else{
			whiteice::math::blas_real<float> error = 1000.0f;
			whiteice::math::vertex<> w;
			unsigned int iterations = 0;

			optimizer->getSolution(w, error, iterations);

			if(optimizer->isRunning() == false || optimizer->solutionConverged() == true || iterations >= 1000){
				// gets finished solution

				optimizer->stopComputation();
				optimizer->getSolution(w, error, iterations);

				{
					char buffer[1024];
					sprintf(buffer, "resonanz model optimization stopped. picture: %d iterations: %d error: %f",
							currentPictureModel, iterations, error.c[0]);
					logging.info(buffer);
				}

				// saves optimization results to a file
				std::string dbFilename = currentCommand.modelDir + "/" + calculateHashName(pictures[currentPictureModel]) + ".model";
				nn->importdata(w);
				if(nn->save(dbFilename) == false)
					logging.error("saving nn configuration file failed");

				delete optimizer;
				optimizer = nullptr;

				// starts new computation
				currentPictureModel++;
				if(currentPictureModel < pictures.size()){
					nn->randomize();
					nn->exportdata(w);

					{
						char buffer[1024];
						sprintf(buffer, "resonanz model optimization started: picture %d database size: %d",
								currentPictureModel, pictureData[currentPictureModel].size(0));
						logging.info(buffer);
					}

					optimizer = new whiteice::LBFGS_nnetwork<>(*nn, pictureData[currentPictureModel], false, false);
					optimizer->minimize(w);
				}
			}
		}
	}
	else if(currentKeywordModel < keywords.size()){

		if(optimizer == nullptr){ // first model to be optimized (no need to store the previous result)
			whiteice::math::vertex<> w;

			nn->randomize();
			nn->exportdata(w);

			optimizer = new whiteice::LBFGS_nnetwork<>(*nn, keywordData[currentKeywordModel], false, false);
			optimizer->minimize(w);

			{
				char buffer[1024];
				sprintf(buffer, "resonanz model optimization started: keyword %d database size: %d",
						currentKeywordModel, keywordData[currentKeywordModel].size(0));
				logging.info(buffer);
			}

		}
		else{
			whiteice::math::blas_real<float> error = 1000.0f;
			whiteice::math::vertex<> w;
			unsigned int iterations = 0;

			optimizer->getSolution(w, error, iterations);

			if(optimizer->isRunning() == false || optimizer->solutionConverged() == true || iterations >= 1000){
				// gets finished solution

				optimizer->stopComputation();

				optimizer->getSolution(w, error, iterations);


				{
					char buffer[1024];
					sprintf(buffer, "resonanz model optimization stopped. keyword: %d iterations: %d error: %f",
							currentKeywordModel, iterations, error.c[0]);
					logging.info(buffer);
				}

				// saves optimization results to a file
				std::string dbFilename = currentCommand.modelDir + "/" + calculateHashName(keywords[currentKeywordModel]) + ".model";
				nn->importdata(w);
				if(nn->save(dbFilename) == false)
					logging.error("saving nn configuration file failed");

				delete optimizer;
				optimizer = nullptr;

				// starts new computation
				currentKeywordModel++;
				if(currentKeywordModel < keywords.size()){
					nn->randomize();
					nn->exportdata(w);

					{
						char buffer[1024];
						sprintf(buffer, "resonanz model optimization started: keyword %d database size: %d",
								currentKeywordModel, keywordData[currentKeywordModel].size(0));
						logging.info(buffer);
					}

					optimizer = new whiteice::LBFGS_nnetwork<>(*nn, keywordData[currentKeywordModel], false, false);
					optimizer->minimize(w);
				}
			}
		}
	}
	else{ // both picture and keyword models has been computed
		cmdStopCommand();
	}

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

	if(incomingCommand == nullptr) return false; // incomingCommand changed while acquiring mutex..

	currentCommand = *incomingCommand;

	delete incomingCommand;
	incomingCommand = nullptr;

	return true;
}


bool ResonanzEngine::engine_loadMedia(const std::string& picdir, const std::string& keyfile, bool loadData)
{
	std::vector<std::string> tempKeywords;

	// first we get picture names from directories and keywords from keyfile
	if(this->loadWords(keyfile, tempKeywords) == false)
		return false;

	std::vector<std::string> tempPictures;

	if(this->loadPictures(picdir, tempPictures) == false)
		return false;

	pictures = tempPictures;
	keywords = tempKeywords;

	for(unsigned int i=0;i<images.size();i++){
		if(images[i] != nullptr)
			SDL_FreeSurface(images[i]);
	}

	if(loadData){
		images.resize(pictures.size());

		for(unsigned int i=0;i<images.size();i++){
			images[i] = nullptr;
			engine_showScreen("Loading..", i);
			engine_pollEvents();
			engine_updateScreen();
		}
	}
	else{
		images.clear();
	}

	return true;
}


bool ResonanzEngine::engine_loadDatabase(const std::string& modelDir)
{
	std::lock_guard<std::mutex> lock(database_mutex);

	keywordData.resize(keywords.size());
	pictureData.resize(pictures.size());

	std::string name1 = "input";
	std::string name2 = "output";

	// loads databases into memory or initializes new ones
	for(unsigned int i=0;i<keywords.size();i++){
		std::string dbFilename = modelDir + "/" + calculateHashName(keywords[i]) + ".ds";

		if(keywordData[i].load(dbFilename) == false){
			keywordData[i].createCluster(name1, eeg->getNumberOfSignals());
			keywordData[i].createCluster(name2, eeg->getNumberOfSignals());
		}
	}


	for(unsigned int i=0;i<pictures.size();i++){
		std::string dbFilename = modelDir + "/" + calculateHashName(pictures[i]) + ".ds";

		if(pictureData[i].load(dbFilename) == false){
			pictureData[i].createCluster(name1, eeg->getNumberOfSignals());
			pictureData[i].createCluster(name2, eeg->getNumberOfSignals());
		}
	}

	return true;
}


bool ResonanzEngine::engine_storeMeasurement(unsigned int pic, unsigned int key, const std::vector<float>& eegBefore, const std::vector<float>& eegAfter)
{
	std::vector< whiteice::math::blas_real<float> > t1, t2;
	t1.resize(eegBefore.size());
	t2.resize(eegAfter.size());

	for(unsigned int i=0;i<t1.size();i++){
		t1[i] = eegBefore[i];
		t2[i] = eegAfter[i];
	}

	keywordData[key].add(0, t1);
	keywordData[key].add(1, t2);

	pictureData[pic].add(0, t1);
	pictureData[pic].add(1, t2);

	return true;
}


bool ResonanzEngine::engine_saveDatabase(const std::string& modelDir)
{
	std::lock_guard<std::mutex> lock(database_mutex);

	// saves databases from memory
	for(unsigned int i=0;i<keywordData.size();i++){
		std::string dbFilename = modelDir + "/" + calculateHashName(keywords[i]) + ".ds";

		if(keywordData[i].save(dbFilename) == false)
			return false;
	}


	for(unsigned int i=0;i<pictureData.size();i++){
		std::string dbFilename = modelDir + "/" + calculateHashName(pictures[i]) + ".ds";

		if(pictureData[i].save(dbFilename) == false)
			return false;
	}

	return true;
}


std::string ResonanzEngine::calculateHashName(std::string& filename)
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


bool ResonanzEngine::engine_showScreen(const std::string& message, unsigned int picture)
{
	SDL_Surface* surface = SDL_GetWindowSurface(window);
	if(surface == nullptr)
		return false;

	SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));

	int bgcolor = 0;

	if(picture < pictures.size()){ // shows a picture
		if(images[picture] == NULL){
			SDL_Surface* image = IMG_Load(pictures[picture].c_str());
			SDL_Rect imageRect;
			SDL_Surface* scaled = 0;

			if(image != 0){
				if((image->w) > (image->h)){
					double wscale = ((double)SCREEN_WIDTH)/((double)image->w);
					// scaled = zoomSurface(image, wscale, wscale, SMOOTHING_ON);

					scaled = SDL_CreateRGBSurface(0, (int)(image->w*wscale), (int)(image->h*wscale), 32,
							0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

					SDL_BlitScaled(image, NULL, scaled, NULL);

				}
				else{
					double hscale = ((double)SCREEN_HEIGHT)/((double)image->h);
					// scaled = zoomSurface(image, hscale, hscale, SMOOTHING_ON);

					scaled = SDL_CreateRGBSurface(0, (int)(image->w*hscale), (int)(image->h*hscale), 32,
							0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

					SDL_BlitScaled(image, NULL, scaled, NULL);
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

				SDL_BlitSurface(images[picture], NULL, surface, &imageRect);
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

			SDL_BlitSurface(images[picture], NULL, surface, &imageRect);
		}
	}

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

		SDL_Surface* msg = TTF_RenderUTF8_Blended(font, message.c_str(), color);

		SDL_Rect messageRect;

		messageRect.x = (SCREEN_WIDTH - msg->w)/2;
		messageRect.y = (SCREEN_HEIGHT - msg->h)/2;
		messageRect.w = msg->w;
		messageRect.h = msg->h;


		SDL_BlitSurface(msg, NULL, surface, &messageRect);
		SDL_FreeSurface(msg);
	}

	return true;
}


void ResonanzEngine::engine_pollEvents()
{
	SDL_Event event;

	while(SDL_PollEvent(&event)){
		// currently ignores all incoming events
		// (should handle window close event somehow)

		if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP){
			std::lock_guard<std::mutex> lock(keypress_mutex);
			keypressed = true;
		}
	}
}


void ResonanzEngine::engine_updateScreen()
{
	if(window != nullptr)
		SDL_UpdateWindowSurface(window);
}


// initializes SDL libraries to be used (graphics, font, music)
bool ResonanzEngine::engine_SDL_init(const std::string& fontname)
{
	SDL_Init(SDL_INIT_EVERYTHING);

	if(TTF_Init() != 0){
		printf("TTF_Init failed: %s\n", TTF_GetError());
		fflush(stdout);
		throw std::runtime_error("TTF_Init() failed.");
	}

	int flags = IMG_INIT_JPG | IMG_INIT_PNG;

	if(IMG_Init(flags) != flags){
		printf("IMG_Init failed: %s\n", IMG_GetError());
		fflush(stdout);
		IMG_Quit();
		throw std::runtime_error("IMG_Init() failed.");
	}

	flags = MIX_INIT_OGG;

	if(Mix_Init(flags) != flags){
		printf("Mix_Init failed: %s\n", Mix_GetError());
		fflush(stdout);
		IMG_Quit();
		Mix_Quit();
		throw std::runtime_error("Mix_Init() failed.");
	}

	double fontSize = 100.0*sqrt(((float)(SCREEN_WIDTH*SCREEN_HEIGHT))/(640.0*480.0));
	unsigned int fs = (unsigned int)fontSize;
	if(fs <= 0) fs = 10;

	font = 0;
	font = TTF_OpenFont(fontname.c_str(), fs);

	if(font == 0){
		printf("TTF_OpenFont failure (%s): %s\n", fontname.c_str() , TTF_GetError());
		fflush(stdout);
		IMG_Quit();
		Mix_Quit();
		throw std::runtime_error("TTF_OpenFont() failed.");
	}

	if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) == -1){
		IMG_Quit();
		Mix_Quit();
		printf("ERROR: Cannot open SDL mixer: %s.\n", Mix_GetError());

		throw std::runtime_error("Mix_OpenAudio() failed.");
	}

	return true;
}


bool ResonanzEngine::engine_SDL_deinit()
{
  SDL_CloseAudio();
  TTF_CloseFont(font);
  IMG_Quit();
  Mix_Quit();
  TTF_Quit();
  SDL_Quit();

  return true;
}


bool ResonanzEngine::measureColor(SDL_Surface* image, SDL_Color& averageColor)
{
	double r = 0.0;
	double g = 0.0;
	double b = 0.0;
	unsigned int N = 0;

	// SDL_Surface* im = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_Surface* im = image; // skip surface conversion/copy

	// if(im == 0) return false;

	unsigned int* buffer = (unsigned int*)im->pixels;

	// instead of calculating value for the whole image, we just calculate sample from 1000 pixels

	for(unsigned int s=0;s<1000;s++){
		const int x = rand() % im->w;
		const int y = rand() % im->h;

		int rr = (buffer[x + y*(image->pitch/4)] & 0xFF0000) >> 16;
		int gg = (buffer[x + y*(image->pitch/4)] & 0x00FF00) >> 8;
		int bb = (buffer[x + y*(image->pitch/4)] & 0x0000FF) >> 0;

		r += rr;
		g += gg;
		b += bb;

		N++;
	}

#if 0
	for(int y=0;y<(im->h);y++){
		for(int x=0;x<(im->w);x++){
			int rr = (buffer[x + y*(image->pitch/4)] & 0xFF0000) >> 16;
			int gg = (buffer[x + y*(image->pitch/4)] & 0x00FF00) >> 8;
			int bb = (buffer[x + y*(image->pitch/4)] & 0x0000FF) >> 0;

			r += rr;
			g += gg;
			b += bb;

			N++;
		}
	}
#endif

	r /= N;
	g /= N;
	b /= N;

	averageColor.r = (Uint8)r;
	averageColor.g = (Uint8)g;
	averageColor.b = (Uint8)b;

	// SDL_FreeSurface(im);

	return true;
}




bool ResonanzEngine::loadWords(const std::string filename, std::vector<std::string>& words)
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


bool ResonanzEngine::loadPictures(const std::string directory, std::vector<std::string>& pictures)
{
	// looks for pics/*.jpg and pics/*.png files

	DIR* dp;

	struct dirent *ep;

	dp = opendir(directory.c_str());

	if(dp == 0) return false;

	while((ep = readdir(dp)) != NULL){
		if(ep->d_name == NULL)
			continue;

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


std::string ResonanzEngine::analyzeModel(const std::string& modelDir)
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

	std::lock_guard<std::mutex> lock(database_mutex);

	for(auto filename : databaseFiles){
		// calculate statistics
		whiteice::dataset<> ds;
		std::string fullname = modelDir + "/" + filename;
		if(ds.load(fullname) == false){
			failed++;
			continue; // couldn't load this dataset
		}

		if(ds.size() < minDSSamples) minDSSamples = ds.size();
		avgDSSamples += ds.size();
		N++;

		std::string modelFilename = fullname.substr(0, fullname.length()-3) + ".model";

		// check if there is a model file and load it into memory and calculate average error
		whiteice::nnetwork<> nnet;
		if(nnet.load(modelFilename))
			models++;

	}


	if(N > 0){
		avgDSSamples /= N;
		double modelPercentage = 100*models/((double)N);

		char buffer[1024];
		sprintf(buffer, "%d entries (%.0f%% has a model). samples(avg): %.2f, samples(min): %d",
				N, modelPercentage, avgDSSamples, minDSSamples);

		return buffer;
	}
	else{
		char buffer[1024];
		sprintf(buffer, "%d entries (0%% has a model). samples(avg): %.2f, samples(min): %d",
				0, 0.0, 0);

		return buffer;
	}
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
