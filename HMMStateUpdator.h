/*
 * HMMStateUpdatorThread
 *
 * reclassifies dataset<> classification field using K-Means and HMM model
 */

#ifndef HMMStateUpdator_h
#define HMMStateUpdator_h

//#include <dinrhiw/dinrhiw.h>
#include <dinrhiw.h>
#include <thread>
#include <mutex>

namespace whiteice {
  namespace resonanz {

    class HMMStateUpdatorThread
    {
    public:

      HMMStateUpdatorThread(whiteice::KMeans<>* kmeans,
			    whiteice::HMM* hmm,
			    whiteice::dataset<>* eegData,
			    std::vector< whiteice::dataset<> >* pictureData,
			    std::vector< whiteice::dataset<> >* keywordData,
			    whiteice::dataset<>* synthData);

      ~HMMStateUpdatorThread();

      bool start();
      
      bool isRunning();

      unsigned int getProcessedElements(){
	return (processingPicIndex + processingKeyIndex + processingSynthIndex);
      }

      bool stop();
      
    private:

      // does binary search of strictly increasing sequences of eeg_index numbers in
      // eegData and returns EEG DATA INDEX value for given eeg_index value
      unsigned int find_eegData_index(unsigned int eeg_index);
      
      void updator_loop();
      
      std::mutex thread_mutex;
      bool thread_running = false;
      std::thread* updator_thread = nullptr;
      
      whiteice::KMeans<>* kmeans;
      whiteice::HMM* hmm;

      whiteice::dataset<>* eegData;
      std::vector< whiteice::dataset<> >* pictureData;
      std::vector< whiteice::dataset<> >* keywordData;
      whiteice::dataset<>* synthData;

      unsigned int processingPicIndex = 0, processingKeyIndex = 0, processingSynthIndex = 0;
      
    };
    
  };
};


#endif
