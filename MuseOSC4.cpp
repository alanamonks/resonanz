/*
 * MuseOSC4.cpp
 *
 *  Created on: 18.2.2023
 *      Author: Tomas Ukkonen
 */

#include "MuseOSC4.h"
#include <math.h>
#include <unistd.h>
#include <chrono>

#include "spectral_entropy.h"


#include "oscpkt.hh"
#include "udp.hh"

#include <dinrhiw.h>


using namespace oscpkt;
using namespace std::chrono;

namespace whiteice {
namespace resonanz {

MuseOSC4::MuseOSC4(const unsigned int portNum) : 
  port(portNum)
{
  worker_thread = nullptr;
  running = false;
  hasConnection = false;
  quality = 0.0f;
  
  value.resize(this->getNumberOfSignals());
  for(auto& v : value) v = 0.0f;
  
  latest_sample_seen_t = 0LL;
  
  try{
    std::unique_lock<std::mutex> lock(connection_mutex);
    running = true;
    worker_thread = new std::thread(&MuseOSC4::muse_loop, this);

    // waits for up to 5 seconds for connection to be established
    // or silently fails (device has no connection when accessed)

    std::chrono::system_clock::time_point now =
      std::chrono::system_clock::now();
    auto end_time = now + std::chrono::seconds(5);

    while(hasConnection == true || std::chrono::system_clock::now() >= end_time)
      connection_cond.wait_until(lock, end_time);
  }
  catch(std::exception){
    running = false;
    throw std::runtime_error("MuseOSC4: couldn't create worker thread.");
  }
}


MuseOSC4::~MuseOSC4()
{
  running = false;
  if(worker_thread != nullptr){
    worker_thread->join();
    delete worker_thread;
  }
  
  worker_thread = nullptr;
}

/*
 * Returns unique DataSource name
 */
std::string MuseOSC4::getDataSourceName() const
{
  return "Interaxon Muse [4 channels]";
}

/**
 * Returns true if connection and data collection to device is currently working.
 */
bool MuseOSC4::connectionOk() const
{
  if(hasConnection == false)
    return false; // there is no connection
  
  long long ms_since_epoch = (long long)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  
  if((ms_since_epoch - latest_sample_seen_t) > 2000)
    return false; // latest sample is more than 2000ms second old => bad connection/data
  else
    return true; // sample within 2000ms => good
}

/**
 * returns current value
 */
bool MuseOSC4::data(std::vector<float>& x) const
{
  std::lock_guard<std::mutex> lock(data_mutex);
  
  if(this->connectionOk() == false)
    return false;
  
  x = value;
  
  return true;
}

bool MuseOSC4::getSignalNames(std::vector<std::string>& names) const
{
  names.resize(4*6+1);

  // delta, theta, alpha, beta, gamma
  
  names[0]  = "Muse 1: Delta";
  names[1]  = "Muse 1: Theta";
  names[2]  = "Muse 1: Alpha";
  names[3]  = "Muse 1: Beta";
  names[4]  = "Muse 1: Gamma";
  names[5]  = "Muse 1: Spectral Entropy";

  names[6]  = "Muse 2: Delta";
  names[7]  = "Muse 2: Theta";
  names[8]  = "Muse 2: Alpha";
  names[9]  = "Muse 2: Beta";
  names[10] = "Muse 2: Gamma";
  names[11] = "Muse 2: Spectral Entropy";

  names[12] = "Muse 3: Delta";
  names[13] = "Muse 3: Theta";
  names[14] = "Muse 3: Alpha";
  names[15] = "Muse 3: Beta";
  names[16] = "Muse 3: Gamma";
  names[17] = "Muse 3: Spectral Entropy";

  names[18] = "Muse 4: Delta";
  names[19] = "Muse 4: Theta";
  names[20] = "Muse 4: Alpha";
  names[21] = "Muse 4: Beta";
  names[22] = "Muse 4: Gamma";
  names[23] = "Muse 4: Spectral Entropy";

  names[24] = "Muse: Total Power";
  
  
  return true;
}

  
unsigned int MuseOSC4::getNumberOfSignals() const
{
  return (4*6+1); // entropy is calculated signal
}

  
void MuseOSC4::muse_loop() // worker thread loop
{
  // sets MuseOSC4 internal thread high priority thread
  // so that connection doesn't timeout
  {
    sched_param sch_params;
    int policy = SCHED_FIFO; // SCHED_RR
    
    pthread_getschedparam(pthread_self(), &policy, &sch_params);
    
    policy = SCHED_FIFO;
    sch_params.sched_priority = sched_get_priority_max(policy);
    
    if(pthread_setschedparam(pthread_self(),
			     policy, &sch_params) != 0){
    }
    
#ifdef WINOS
    SetThreadPriority(GetCurrentThread(),
		      THREAD_PRIORITY_HIGHEST);
    //SetThreadPriority(GetCurrentThread(),
    //THREAD_PRIORITY_TIME_CRITICAL);
#endif
  }
  
  hasConnection = false;
  
  UdpSocket sock;
  
  while(running){
    sock.bindTo(port);
    if(sock.isOk()) break;
    sock.close();
    sleep(1);
  }
  
  PacketReader pr;
  PacketWriter pw;
  
  std::vector<int> connectionQuality;
  
  std::vector<float> delta, theta, alpha, beta, gamma;
  bool hasNewData = false;
  
  while(running){
    
    if(hasConnection && hasNewData){ // updates data
      float q = 0.0f;
      for(auto qi : connectionQuality)
	if(qi > 0) q++;
      q = q / connectionQuality.size();
      
      quality = q;

      std::vector<float> w; // measurement
      

      for(unsigned int m=0;m<delta.size();m++){
	std::vector<float> v;
	
	// converts absolute power (logarithmic bels) to [0,1] value by saturating
	// values using tanh(t) this limits effective range of the values to [-0.1, 1.2]
	// TODO: calculate statistics of delta, theta, alpha, beta, gamma to optimally saturate values..
	if(m < delta.size()){
	  auto t = delta[m]; t = (1 + tanh(2*(t - 0.6)))/2.0;
	  v.push_back(t);
	}
	else
	  v.push_back(0.0f);

	if(m < theta.size()){
	  auto t = theta[m]; t = (1 + tanh(2*(t - 0.6)))/2.0;
	  v.push_back(t);
	}
	else
	  v.push_back(0.0f);

	if(m < alpha.size()){
	  auto t = alpha[m]; t = (1 + tanh(2*(t - 0.6)))/2.0;
	  v.push_back(t);
	}
	else
	  v.push_back(0.0f);

	if(m < beta.size()){
	  auto t = beta[m]; t = (1 + tanh(2*(t - 0.6)))/2.0;
	  v.push_back(t);
	}
	else
	  v.push_back(0.0f);

	if(m < gamma.size()){
	  auto t = gamma[m]; t = (1 + tanh(2*(t - 0.6)))/2.0;
	  v.push_back(t);
	}
	else
	  v.push_back(0.0f);
	
	// calculates spectral entropy
	std::vector<float> P;

	if(m < delta.size() && m < theta.size() &&
	   m < alpha.size() && m < beta.size() && m < gamma.size())
	{
	  // DO NOT USE RAW VALUES AS THEY GIVE BAD RESULTS ALTHOUGH SHOULD WORK OK
	  // 
	  //P.push_back(pow(10.0f,delta/10.0f));
	  //P.push_back(pow(10.0f,theta/10.0f));
	  //P.push_back(pow(10.0f,alpha/10.0f));
	  //P.push_back(pow(10.0f,beta/10.0f));
	  //P.push_back(pow(10.0f,gamma/10.0f));
	  
	  // use preprocessed values
	  
	  P.push_back(delta[m]);
	  P.push_back(theta[m]);
	  P.push_back(alpha[m]);
	  P.push_back(beta[m]);
	  P.push_back(gamma[m]);
	}

	if(P.size() > 0){
	  const float SPECTRAL_ENTROPY = spectral_entropy(P);
	  
	  // adds spectral entropy
	  v.push_back(SPECTRAL_ENTROPY);
	}
	else
	  v.push_back(0.0f);

	
	for(unsigned int i=0;i<v.size();i++)
	  w.push_back(v[i]);
      }

      // std::cout << "MUSE: SPECTRAL_ENTROPY: " << SPECTRAL_ENTROPY << std::endl;

      // calculates total power in decibels [sums power terms together]
      float total = 0.0f;
      float t = 0.0f;
      unsigned int N = 0;

      for(unsigned int m=0;m<delta.size();m++){

	if(m < delta.size() && m < theta.size() &&
	   m < alpha.size() && m < beta.size() && m < gamma.size()){
	  t = pow(10.0f, delta[m]/10.0f) + pow(10.0f, theta[m]/10.0f) + pow(10.0f, alpha[m]/10.0f) + pow(10.0f, beta[m]/10.0f) + pow(10.0f, gamma[m]/10.0f);
	  total += t;
	  N++;
	}
      }

      if(N > 0)
	total /= (float)N;
      
      if(total < 10e-10f)
	total = 10e-10f;
      
      total = 10.0f * log10(total);

      t = total; t = (1 + tanh(2*(t - 7.0)))/2.0;
      
      w.push_back(t);

      
      if(w.size() != this->getNumberOfSignals()){
	whiteice::logging.error("MuseOSC4: input data dimensions are WRONG!");

	char buffer[256];
	sprintf(buffer, "MUSEOSC4 ERROR: %d DATAPOINTS ONLY\n", (int)w.size());
	whiteice::logging.error(buffer);
      }
      else{
	// gets current time
	auto ms_since_epoch = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	{
	  std::lock_guard<std::mutex> lock(data_mutex);
	  value = w;
	  latest_sample_seen_t = (long long)ms_since_epoch;
	}
      }

      // printf("EEQ: D:%.2f T:%.2f A:%.2f B:%.2f G:%.2f [QUALITY: %.2f]\n", delta, theta, alpha, beta, gamma, q);
      // printf("EEQ POWER: %.2f [QUALITY %.2f]\n", log(exp(delta)+exp(theta)+exp(alpha)+exp(beta)+exp(gamma)), q);
      // fflush(stdout);
      
      hasNewData = false; // this data point has been processed
    }
    
    if(sock.receiveNextPacket(30)){
      
      pr.init(sock.packetData(), sock.packetSize());
      Message* msg = NULL;
      
      while(pr.isOk() && ((msg = pr.popMessage()) != 0)){
	Message::ArgReader r = msg->match("/muse/elements/is_good");
	
	if(r.isOk() == true){ // matched
	  // there are 4 ints telling connection quality
	  std::vector<int> quality;
	  
	  while(r.nbArgRemaining()){
	    if(r.isInt32()){
	      int32_t i;
	      r = r.popInt32(i);
	      quality.push_back((int)i);
	    }
	    else{
	      r = r.pop();
	    }
	  }
	  
	  if(quality.size() > 0){
	    connectionQuality = quality;
	  }
	  
	  bool connection = false;
	  
	  for(auto q : quality)
	    if(q > 0) connection = true;

	  {
	    std::lock_guard<std::mutex> lock(connection_mutex);
	    hasConnection = connection;
	    connection_cond.notify_all();
	  }
	}


	// always sets connection quality to at least 4 channels
	{
	  int n = connectionQuality.size();

	  if(n < 4){
	    while(n > 0){
	      connectionQuality.push_back(0);
	      n--;
	    }
	  }
	}
	
	// gets relative frequency bands powers..
	std::vector<float> f(connectionQuality.size());

	for(auto& ff : f) ff = 0.0f;
	
	r = msg->match("/muse/elements/delta_absolute");
	if(r.isOk()){
	  if(r.popFloat(f[0]).popFloat(f[1]).popFloat(f[2]).popFloat(f[3]).isOkNoMoreArgs()){
	    std::vector<float> samples;
	    for(unsigned int i=0;i<f.size();i++)
	      if(connectionQuality[i]) samples.push_back(f[i]);

	    delta = samples;

	    /*
	    float mean = 0.0f;
	    for(auto& si :samples) mean += pow(10.0f, si/10.0f);
	    mean /= samples.size();
	    
	    delta = 10.0f * log10(mean);
	    */
	    hasNewData = true;
	  }
	  
	}

	for(auto& ff : f) ff = 0.0f;
	
	r = msg->match("/muse/elements/theta_absolute");
	if(r.isOk()){
	  if(r.popFloat(f[0]).popFloat(f[1]).popFloat(f[2]).popFloat(f[3]).isOkNoMoreArgs()){
	    std::vector<float> samples;
	    for(unsigned int i=0;i<f.size();i++)
	      if(connectionQuality[i]) samples.push_back(f[i]);

	    theta = samples;

	    /*
	    float mean = 0.0f;
	    for(auto& si :samples) mean += pow(10.0f, si/10.0f);
	    mean /= samples.size();
	    
	    theta = 10.0f * log10(mean);
	    */
	    
	    hasNewData = true;
	  }
	}

	for(auto& ff : f) ff = 0.0f;
	
	r = msg->match("/muse/elements/alpha_absolute");
	if(r.isOk()){
	  if(r.popFloat(f[0]).popFloat(f[1]).popFloat(f[2]).popFloat(f[3]).isOkNoMoreArgs()){
	    std::vector<float> samples;
	    for(unsigned int i=0;i<f.size();i++)
	      if(connectionQuality[i]) samples.push_back(f[i]);

	    alpha = samples;

	    /*
	    float mean = 0.0f;
	    for(auto& si :samples) mean += pow(10.0f, si/10.0f);
	    mean /= samples.size();
	    
	    alpha = 10.0f * log10(mean);
	    */
	    hasNewData = true;
	  }
	}

	for(auto& ff : f) ff = 0.0f;
	
	r = msg->match("/muse/elements/beta_absolute");
	if(r.isOk()){
	  if(r.popFloat(f[0]).popFloat(f[1]).popFloat(f[2]).popFloat(f[3]).isOkNoMoreArgs()){
	    std::vector<float> samples;
	    for(unsigned int i=0;i<f.size();i++)
	      if(connectionQuality[i]) samples.push_back(f[i]);

	    beta = samples;

	    /*
	    float mean = 0.0f;
	    for(auto& si :samples) mean += pow(10.0f, si/10.0f);
	    mean /= samples.size();
	    
	    beta = 10.0f * log10(mean);
	    */
	    hasNewData = true;
	  }
	}

	for(auto& ff : f) ff = 0.0f;
	
	r = msg->match("/muse/elements/gamma_absolute");
	if(r.isOk()){
	  if(r.popFloat(f[0]).popFloat(f[1]).popFloat(f[2]).popFloat(f[3]).isOkNoMoreArgs()){
	    std::vector<float> samples;
	    for(unsigned int i=0;i<f.size();i++)
	      if(connectionQuality[i]) samples.push_back(f[i]);

	    gamma = samples;

	    /*
	    float mean = 0.0f;
	    for(auto& si :samples) mean += pow(10.0f, si/10.0f);
	    mean /= samples.size();
	    
	    gamma = 10.0f * log10(mean);
	    */
	    
	    hasNewData = true;
	  }
	}
	
      }
    }
    
    
    if(sock.isOk() == false){
      // tries to reconnect the socket to port
      sock.close();
      sleep(1);
      sock.bindTo(port);
    }
    
  }
  
  sock.close();
}
  
} /* namespace resonanz */
} /* namespace whiteice */
