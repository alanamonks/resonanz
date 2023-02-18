
#include "HMMStateUpdator.h"
#include <functional>


namespace whiteice
{
  namespace resonanz
  {

    
    HMMStateUpdatorThread::HMMStateUpdatorThread(whiteice::KMeans<>* kmeans,
						 whiteice::HMM* hmm,
						 whiteice::dataset<>* eegData,
						 std::vector< whiteice::dataset<> >* pictureData,
						 std::vector< whiteice::dataset<> >* keywordData,
						 whiteice::dataset<>* synthData)
    {
      this->kmeans = kmeans;
      this->hmm = hmm;

      this->eegData = eegData;
      this->pictureData = pictureData;
      this->keywordData = keywordData;
      this->synthData = synthData;

      updator_thread = nullptr;
    }
    
    
    HMMStateUpdatorThread::~HMMStateUpdatorThread()
    {
      this->stop();
    }
    
    
    bool HMMStateUpdatorThread::start()
    {
      if(kmeans == NULL || hmm == NULL ||
	 pictureData == NULL || keywordData == NULL)
	return false;

      std::lock_guard<std::mutex> lock(thread_mutex);
      
      if(thread_running){
	return false; // thread is already running
      }

      processingPicIndex = 0;
      processingKeyIndex = 0;
      processingSynthIndex = 0;
      thread_running = true;

      try{
	if(updator_thread){ delete updator_thread; updator_thread = nullptr; }
	updator_thread = new std::thread(std::bind(&HMMStateUpdatorThread::updator_loop, this));
      }
      catch(std::exception& e){
	thread_running = false;
	updator_thread = nullptr;
	return false;
      }

      return true;
    }
    
    bool HMMStateUpdatorThread::isRunning()
    {
      std::lock_guard<std::mutex> lock(thread_mutex);
      
      if(thread_running && updator_thread != nullptr)
	return true;
      else
	return false;
    }
    
    bool HMMStateUpdatorThread::stop()
    {
      std::lock_guard<std::mutex> lock(thread_mutex);
      
      if(thread_running == false)
	return false;
      
      thread_running = false;
      
      if(updator_thread){
	updator_thread->join();
	delete updator_thread;
      }
      
      updator_thread = nullptr;
      
      return true;
    }


    // does binary search of strictly increasing sequences of eeg_index numbers in
    // eegData and returns EEG DATA INDEX value for given eeg_index value
    unsigned int HMMStateUpdatorThread::find_eegData_index(unsigned int eeg_index)
    {
      try{
	
	// searches for the index starting position
	float start = 0;
	float end = eegData->size(0) - 1.0f;
	
	float guess = start;
	float guess_index = (float)(eegData->access(1, (unsigned int)guess)[0].c[0]);
	
	while(((unsigned int)guess_index) != ((unsigned int)eeg_index)){
	  
	  guess = start + (end-start)/2.0f;
	  guess_index = (float)(eegData->access(1, (unsigned int)guess)[0].c[0]);
	  
	  if(((unsigned int)guess_index) < eeg_index){
	    start = guess;
	  }
	  else{
	    end = guess;
	  }
	}
	
	return (unsigned int)guess;
      }
      catch(std::exception& e){
	std::cout << "HMMStateUpdatorThread::find_eegData_index(). Unexpected exception: " << e.what() << std::endl;

	exit(-1);
	
	return 0;
      }
    }
    

    void HMMStateUpdatorThread::updator_loop()
    {
      try{
	
	// pictureData
	for(unsigned int p=0;p<pictureData->size();p++, processingPicIndex++)
	{
	  auto& pic = (*pictureData)[p];
	  
	  std::vector< whiteice::dataset<>::data_normalization > norms;
	  
	  pic.getPreprocessings(0, norms);
	  pic.convert(0);
	  
	  for(unsigned int i=0;i<pic.size(0);i++){
	    
	    whiteice::math::vertex<> eeg;
	    
	    auto w = eegData->access(0, 0);
	    eeg.resize(w.size());
	    eeg.zero();

	    
	    // finds historical EEG values (10 last) and approximates current HMM state from them
	    unsigned int HMMstate = 0;
	    
	    {
	      const unsigned int cindex = (unsigned int)(pic.access(2, i)[0].c[0]);

	      int index = ((int)cindex) - 10;
	      if(index < 0) index = 0;
	      
	      unsigned int eeg_index = find_eegData_index((unsigned int)index);
	      
	      HMMstate = hmm->sample(hmm->getPI());
	      
	      while(((unsigned int)(eegData->access(1, eeg_index)[0].c[0])) < cindex){
		
		auto v = eegData->access(0, eeg_index);
		
		eeg.resize(v.size());
		
		for(unsigned int j=0;j<eeg.size();j++){
		  eeg[j] = v[j];
		}
		
		unsigned int kcluster = kmeans->getClusterIndex(eeg);
		
		unsigned int nextState = 0;
		hmm->next_state(HMMstate, nextState, kcluster);
		HMMstate = nextState;
		
		eeg_index++;
	      }
	      
	    }
	    
	    
	    auto v = pic.access(0, i);

	    for(unsigned int j=eeg.size();j<(eeg.size()+(hmm->getNumHiddenStates()));j++){
	      unsigned int index = j-eeg.size();
	      
	      if(index == HMMstate) v[j] = 1.0f;
	      else v[j] = 0.0f;
	    }
	    
	    pic.access(0, i) = v;
	  }
	  
	  for(const auto& p : norms)
	    pic.preprocess(0, p);

	  pic.preprocess(0); // always do mean-variance normalization
	}
	
	
	
	// keywordData
	for(unsigned int p=0;p<keywordData->size();p++, processingKeyIndex++)
	{
	  auto& key = (*keywordData)[p];
	  
	  std::vector< whiteice::dataset<>::data_normalization > norms;
	  
	  key.getPreprocessings(0, norms);
	  key.convert(0);
	  
	  
	  for(unsigned int i=0;i<key.size(0);i++){
	    
	    whiteice::math::vertex<> eeg;
	    
	    auto w = eegData->access(0, 0);
	    eeg.resize(w.size());
	    eeg.zero();

	    // finds historical EEG values (10 last) and approximates current HMM state from them
	    unsigned int HMMstate = 0;
	    
	    {
	      const unsigned int cindex = (unsigned int)(key.access(2, i)[0].c[0]);
	      
	      int index = ((int)cindex) - 10;
	      if(index < 0) index = 0;
	      
	      unsigned int eeg_index = find_eegData_index((unsigned int)index);
	      
	      HMMstate = hmm->sample(hmm->getPI());
	      
	      while(((unsigned int)(eegData->access(1, eeg_index)[0].c[0])) < cindex){
		
		auto v = eegData->access(0, eeg_index);
		
		eeg.resize(v.size());
		
		for(unsigned int j=0;j<eeg.size();j++){
		  eeg[j] = v[j];
		}
		
		unsigned int kcluster = kmeans->getClusterIndex(eeg);
		
		unsigned int nextState = 0;
		hmm->next_state(HMMstate, nextState, kcluster);
		HMMstate = nextState;
		
		eeg_index++;
	      }
	      
	    }
	    
	    
	    auto v = key.access(0, i);
	    
	    for(unsigned int j=eeg.size();j<v.size();j++){
	      unsigned int index = j-eeg.size();
	      
	      if(index == HMMstate) v[j] = 1.0f;
	      else v[j] = 0.0f;
	    }
	    
	    key.access(0, i) = v;
	  }
	  
	  for(const auto& p : norms)
	    key.preprocess(0, p);
	  
	  key.preprocess(0); // always do mean-variance normalization
	}
	
	
	
	// synthData
	{
	  auto& synth = (*synthData);
	  
	  std::vector< whiteice::dataset<>::data_normalization > norms;
	  
	  synth.getPreprocessings(0, norms);
	  synth.convert(0);
	  
	  for(unsigned int i=0;i<synth.size(0);i++){
	    
	    
	    whiteice::math::vertex<> eeg;
	    
	    auto w = eegData->access(0, 0);
	    eeg.resize(w.size());
	    eeg.zero();
	    
	    
	    // finds historical EEG values (10 last) and approximates current HMM state from them
	    unsigned int HMMstate = 0;
	    
	    {
	      const unsigned int cindex = (unsigned int)(synth.access(2, i)[0].c[0]);
	      
	      int index = ((int)cindex) - 10;
	      if(index < 0) index = 0;
	      
	      unsigned int eeg_index = find_eegData_index((unsigned int)index);
	      
	      HMMstate = hmm->sample(hmm->getPI());
	      
	      while(((unsigned int)(eegData->access(1, eeg_index)[0].c[0])) < cindex){
		
		auto v = eegData->access(0, eeg_index);
		
		eeg.resize(v.size());
		
		for(unsigned int j=0;j<eeg.size();j++){
		  eeg[j] = v[j];
		}
		
		unsigned int kcluster = kmeans->getClusterIndex(eeg);
		
		unsigned int nextState = 0;
		hmm->next_state(HMMstate, nextState, kcluster);
		HMMstate = nextState;
		
		eeg_index++;
	      }
	    
	    }
	    
	    
	    auto v = synth.access(0, i);
	    
	    for(unsigned int j=0;j<(hmm->getNumHiddenStates());j++){
	      unsigned int index = (v.size() - (hmm->getNumHiddenStates())) + j;
	      
	      if(j == HMMstate) v[index] = 1.0f;
	      else v[index] = 0.0f;
	    }
	    
	    synth.access(0, i) = v;
	  }
	  
	  
	  for(const auto& p : norms)
	    synth.preprocess(0, p);
	  
	  synth.preprocess(0); // always do mean-variance normalization

	  processingSynthIndex++;
	}

      }
      catch(std::exception& e){
	std::cout << "HMMStateUpdatorThread::updator_loop(). Unexpected exception: " << e.what() << std::endl;
      }
	
      
      thread_running = false;
    }
    
  };
};
