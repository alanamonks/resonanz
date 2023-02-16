/*
 * SDLAVCodec.h
 *
 * libavcodec based video encoding class
 *
 *  Created on: 16.2.2023
 *      Author: Tomas
 */

#ifndef SDLAVCODEC_H_
#define SDLAVCODEC_H_

#include <SDL.h>

extern "C" {
  
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
  
};

#include <list>
#include <string>
#include <thread>
#include <mutex>


namespace whiteice {
  namespace resonanz {
    
    /**
     * Class to help encoding (and decoding) SDL_Surface
     * frames into
     *
     */
    class SDLAVCodec {
    public:
      SDLAVCodec(float q = 0.8f); // encoding quality between 0 and 1
      virtual ~SDLAVCodec();
      
      // setups encoding structure
      bool startEncoding(const std::string& filename, unsigned int width, unsigned int height);

      void setupEncoder(); // helper function..
      
      // inserts SDL_Surface picture frame into video at msecs
      // onwards since the start of the encoding (msecs = 0 is the first frame)
      // [nullptr means black empty frame]
      bool insertFrame(unsigned int msecs, SDL_Surface* surface = nullptr);
      
      // stops encoding with a final frame [nullptr means black empty frame]
      bool stopEncoding(unsigned int msecs, SDL_Surface* surface = nullptr);
      
      // error was detected during encoding: restart encoding to try again
      bool error() const { return error_flag; }
      
    private:
      bool __insert_frame(unsigned int msecs, SDL_Surface* surface, bool last);
      
      struct frame {
	//th_ycbcr_buffer buffer; // picture information
	AVFrame* frame;
	
	// msecs since the start of the [encoded] video
	unsigned int msecs;
	
	// last frame in video: instructs encoder loop to shutdown after this one
	bool last;
      };
      
      float quality;
      
      const int FPS; // video frames per second
      const int MSECS_PER_FRAME;
      int latest_frame_encoded;
      
      std::mutex incoming_mutex;
      SDLAVCodec::frame* prev;
      
      const unsigned int MAX_QUEUE_LENGTH = 60*FPS; // maximum of 1 minute (60 seconds) of frames..
      std::list<SDLAVCodec::frame*> incoming; // incoming frames for the encoder (loop)
      
      bool running;
      bool error_flag;
      
      // thread to do all encoding communication between theora and
      // writing resulting frames into disk
      void encoder_loop();
      
      // encodes single video frame
      bool encode_frame(AVFrame* buffer, bool last=false);
      
      int frameHeight, frameWidth; // divisable by 16..
      
      std::mutex start_lock;
      std::thread* encoder_thread;
      //th_enc_ctx* handle;
      FILE* outputFile;

      int m_frameId = 1;
      
      AVCodecContext* m_encoder = nullptr;
      AVFormatContext* m_muxer = nullptr;
      AVStream* m_avStream = nullptr;
      AVBufferRef* m_device = nullptr;
      
      AVFrame* m_hwFrame = nullptr;
    };
    
    
  }
} /* namespace whiteice */

#endif /* SDLAVCODEC_H_ */
