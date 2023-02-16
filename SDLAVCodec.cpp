/*
 * SDLAVCodec.cpp
 *
 *  Created on: 16.2.2023
 *      Author: Tomas Ukkonen
 */

#include "SDLAVCodec.h"
#include <string.h>

#include <ogg/ogg.h>
#include <math.h>
#include <assert.h>


#include <chrono>
#include <thread>

#include "Log.h"


namespace whiteice {
namespace resonanz {

  // FPS was 25, now 100

SDLAVCodec::SDLAVCodec(float q) :
  FPS(100), MSECS_PER_FRAME(1000/100) // currently saves at 25 frames per second, now 100
{
  if(q >= 0.0f && q <= 1.0f)
    quality = q;
  else
    quality = 0.5f;
  
  running = false;
  encoder_thread = nullptr;
}

  
SDLAVCodec::~SDLAVCodec()
{
  std::lock_guard<std::mutex> lock1(incoming_mutex);
  
  for(auto& i : incoming){
    av_frame_free(&(i->frame));
    delete i;
  }
  
  incoming.clear();
  
  std::lock_guard<std::mutex> lock2(start_lock);
  
  running = false;
  if(encoder_thread){
    encoder_thread->detach();
    delete encoder_thread; // shutdown using force
  }

  if(running){
    encode_frame(nullptr ,true);
    av_write_trailer(m_muxer);
  }
  
}


void SDLAVCodec::setupEncoder()
{
    const char* encoderName = "libx264";
    
    AVCodec* videoCodec = avcodec_find_encoder_by_name(encoderName);
    m_encoder = avcodec_alloc_context3(videoCodec);
    m_encoder->bit_rate = frameWidth * frameHeight * FPS * 2;
    m_encoder->width = frameWidth;
    m_encoder->height = frameHeight;
    m_encoder->time_base = (AVRational){1, FPS};
    m_encoder->framerate = (AVRational){FPS, 1};

    m_encoder->gop_size = FPS;  // have at least 1 I-frame per second
    m_encoder->max_b_frames = 1;
    m_encoder->pix_fmt = AV_PIX_FMT_YUV420P;

    assert(avcodec_open2(m_encoder, videoCodec, nullptr) == 0);  // <-- returns -22 (EINVAL) for hardware encoder

    m_muxer->video_codec_id = videoCodec->id;
    m_muxer->video_codec = videoCodec;
}
  

// setups encoding structure
bool SDLAVCodec::startEncoding(const std::string& filename,
			       unsigned int width, unsigned int height)
{
  std::lock_guard<std::mutex> lock(start_lock);
  
  if(width <= 0 || height <= 0)
    return false;
  
  if(running)
    return false;
  
  error_flag = false;
  
  
  frameHeight = height;
  frameWidth = width; 
  
  assert(avformat_alloc_output_context2(&m_muxer, nullptr, "matroska", nullptr) == 0);
  assert(m_muxer != nullptr);
  
  setupEncoder();
  
  m_avStream = avformat_new_stream(m_muxer, nullptr);
  assert(m_avStream != nullptr);
  m_avStream->id = m_muxer->nb_streams-1;
  m_avStream->time_base = m_encoder->time_base;
  
  // Some formats want stream headers to be separate.
  if(m_muxer->oformat->flags & AVFMT_GLOBALHEADER)
    m_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  assert(avcodec_parameters_from_context(m_avStream->codecpar,
					 m_encoder) == 0);
  assert(avio_open(&m_muxer->pb,
		   filename.c_str(), AVIO_FLAG_WRITE) == 0);
  assert(avformat_write_header(m_muxer, nullptr) == 0);

  
  try{
    latest_frame_encoded = -1;
    encoder_thread = new std::thread(&SDLAVCodec::encoder_loop, this);
    
    if(encoder_thread == nullptr){
      running = false;
      return false;
    }
  }
  catch(std::exception& e){
    running = false;
    return false;
  }
  
  return true;
  
	
#if 0 
	
	th_info format;

	th_info_init(&format);

	frameHeight = ((height + 15)/16)*16;
	frameWidth  = ((width + 15)/16)*16;

	format.frame_width = frameWidth;
	format.frame_height = frameHeight;
	format.pic_width = width;
	format.pic_height = height;
	format.pic_x = 0;
	format.pic_y = 0;
	format.quality = (int)(63*quality);
	format.target_bitrate = 0;
	format.pixel_fmt = TH_PF_444;
	format.colorspace = TH_CS_UNSPECIFIED;
	format.fps_numerator = FPS; // frames per second!
	format.fps_denominator = 1;

	handle = th_encode_alloc(&format);

	if(handle == NULL)
		return false;

	// sets encoding speed to the maximum
	{
		int splevel = 100;
		th_encode_ctl(handle, TH_ENCCTL_GET_SPLEVEL_MAX, &splevel, sizeof(int));
		int result = th_encode_ctl(handle, TH_ENCCTL_SET_SPLEVEL, &splevel, sizeof(int));

		if(result == TH_EFAULT || result == TH_EINVAL || result == TH_EIMPL)
			logging.warn("couldn't get theora to maximum encoding speed");
	}

	outputFile = fopen(filename.c_str(), "wb");
	if(outputFile == NULL){
		th_encode_free(handle);
		return false;
	}

	if(ferror(outputFile)){
		fclose(outputFile);
		th_encode_free(handle);
		return false;
	}


	try{
		latest_frame_encoded = -1;
		encoder_thread = new std::thread(&SDLAVCodec::encoder_loop, this);

		if(encoder_thread == nullptr){
			running = false;
			fclose(outputFile);
			th_encode_free(handle);
			outputFile = NULL;
			handle = nullptr;
			return false;
		}
	}
	catch(std::exception& e){
		running = false;
		fclose(outputFile);
		th_encode_free(handle);
		outputFile = NULL;
		handle = nullptr;
		return false;
	}

	return true;
#endif
}


// inserts SDL_Surface picture frame into video at msecs
// onwards since the start of the encoding (msecs = 0 is the first frame)
bool SDLAVCodec::insertFrame(unsigned int msecs, SDL_Surface* surface)
{
	// very quick skipping of frames [without conversion] when picture for the current frame has been already inserted
	const int frame = msecs/MSECS_PER_FRAME;
	if(frame <= latest_frame_encoded)
		return false;

	if(running){
		if(__insert_frame(msecs, surface, false)){
			latest_frame_encoded = frame;
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}
}

// inserts last frame and stops encoding (and saves and closes file when encoding has stopped)
bool SDLAVCodec::stopEncoding(unsigned int msecs, SDL_Surface* surface)
{
  if(running){
    if(__insert_frame(msecs, surface, true) == false){
      logging.fatal("sdl-theora: inserting LAST frame failed");
      return false;
    }
  }
  else{
    return false;
  }

  std::lock_guard<std::mutex> lock(start_lock);

  running = false;
  
  if(encoder_thread){
    // may block forever... [should do some kind of timed wait instead?]
    encoder_thread->join();
    delete encoder_thread;
  }
  encoder_thread = nullptr;

  encode_frame(nullptr, true);
  av_write_trailer(m_muxer);
  
  running = false; // it is safe to do because we have start lock?
  
  return true; // everything went correctly
}


bool SDLAVCodec::__insert_frame(unsigned int msecs, SDL_Surface* surface, bool last)
{
  // converts SDL into YUV format [each plane separatedly and have full width and height]
  // before sending it to the encoder thread
  
  SDL_Surface* frame = SDL_CreateRGBSurface(0, frameWidth, frameHeight, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
  
  if(frame == NULL){
    logging.error("sdl-theora::__insert_frame failed [1]");
    return false;
  }
  
  if(surface != NULL){
    SDL_BlitSurface(surface, NULL, frame, NULL);
  }
  else{ // just fills the frame with black
    SDL_FillRect(frame, NULL, SDL_MapRGB(frame->format, 0, 0, 0));
  }
  
  // assumes yuv pixels format is full plane for each component:
  // Y plane (one byte per pixel), U plane (one byte per pixel), V plane (one byte per pixel)
  
  SDLAVCodec::frame* f = new SDLAVCodec::frame;
  
  f->msecs = msecs;
  
  f->frame = av_frame_alloc();
  
  f->frame->format = AV_PIX_FMT_YUV420P;
  f->frame->width = frameWidth;
  f->frame->height = frameHeight;
  
  assert(av_frame_get_buffer(frame, 0) == 0);
  assert(av_frame_make_writable(frame) == 0);
  
  
  // perfect opportunity for parallelization: pixel conversions are independet from each other
#pragma omp parallel for
  for(int y=0; y<f->frame->height; y++) {
    for(int x=0; x<f->frame->width; x++) {
      
      const unsigned int index = x + frameWidth*y;
      unsigned int* source = (unsigned int*)(frame->pixels);
      
      const unsigned int r = (source[index] & 0x00FF0000)>>16;
      const unsigned int g = (source[index] & 0x0000FF00)>> 8;
      const unsigned int b = (source[index] & 0x000000FF)>> 0;
      
      auto Y  =  (0.257*r) + (0.504*g) + (0.098*b) + 16.0;
      auto Cr =  (0.439*r) - (0.368*g) - (0.071*b) + 128.0;
      auto Cb = -(0.148*r) - (0.291*g) + (0.439*b) + 128.0;
      
      if(Y < 0.0) Y = 0.0;
      else if(Y > 255.0) Y = 255.0;
      
      if(Cr < 0.0) Cr = 0.0;
      else if(Cr > 255.0) Cr = 255.0;
      
      if(Cb < 0.0) Cb = 0.0;
      else if(Cb > 255.0) Cb = 255.0;
      
      f->frame->data[0][y * f->frame->linesize[0] + x] =
	(unsigned char)round(Y);  // Y
      f->frame->data[1][y * f->frame->linesize[1] + x] =
	(unsigned char)round(Cb);  // Cb
      f->frame->data[2][y * f->frame->linesize[2] + x] =
	(unsigned char)round(Cr);  // Cr
    }
  }
  
  
  f->last = last; // IMPORTANT!
  
  {
    std::lock_guard<std::mutex> lock1(start_lock);
    std::lock_guard<std::mutex> lock2(incoming_mutex);
    
    // always processes special LAST frames
    if((running == false || incoming.size() >= MAX_QUEUE_LENGTH) && f->last != true){
      logging.error("sdl-theora::__insert_frame failed [3]");

      av_frame_free(&(f->frame));
      delete f;

      SDL_FreeSurface(frame);
      return false;
    }
    else
      incoming.push_back(f);
  }
  
  SDL_FreeSurface(frame);
  
  return true;
}


// thread to do all encoding communication between theora and
// writing resulting frames into disk
void SDLAVCodec::encoder_loop()
{
  running = true;
  
  logging.info("sdl-theora: encoder thread started..");
  
  prev = nullptr;
  
  logging.info("sdl-theora: theora video headers written..");
  
  // keeps encoding incoming frames
  SDLAVCodec::frame* f = nullptr;
  int latest_frame_generated = -1;
  prev = nullptr;
  
  while(1)
  {
    {
      char buffer[80];
      snprintf(buffer, 80, "sdl-theora: incoming frame buffer size: %d", (int)incoming.size());
      logging.info(buffer);
    }
    
    {
      incoming_mutex.lock();
      if(incoming.size() > 0){ // has incoming picture data
	f = incoming.front();
	incoming.pop_front();
	incoming_mutex.unlock();
      }
      else{
	incoming_mutex.unlock();
	
	// sleep here ~10ms [time between frames 40ms]
	std::this_thread::sleep_for(std::chrono::milliseconds(MSECS_PER_FRAME/4));
	
	continue;
      }
    }
    
    // converts milliseconds field to frame number
    int f_frame = (f->msecs / MSECS_PER_FRAME);
    
    // if there has been no frames between:
    // last_frame_generated .. f_frame
    // fills them with latest_frame_generated (prev)
    
    if(latest_frame_generated < 0 && f_frame >= 0){
      // writes f frame
      latest_frame_generated = 0;
      
      logging.info("sdl-theora: writing initial f-frames");

      for(int i=latest_frame_generated;i<f_frame;i++){
	if(encode_frame(f->frame) == false)
	  logging.error("sdl-theora: encoding frame failed");
	else{
	  char buffer[80];
	  snprintf(buffer, 80, "sdl-theora: encoding frame: %d/%d", i, FPS);
	  logging.info(buffer);
	}
	
      }
    }
    else if((latest_frame_generated+1) < f_frame){
      // writes prev frames
      
      logging.info("sdl-theora: writing prev-frames");
      
      for(int i=(latest_frame_generated+1);i<f_frame;i++){
	if(encode_frame(prev->frame) == false)
	  logging.error("sdl-theora: encoding frame failed");
	else{
	  char buffer[80];
	  snprintf(buffer, 80, "sdl-theora: encoding frame: %d/%d", i, FPS);
	  logging.info(buffer);
	}
	
      }
    }
    
    // writes f-frame once (f_frame) if it is a new frame for this msec
    // OR if it is a last frame [stream close frame]
    if(latest_frame_generated < f_frame || f->last)
    {
      logging.info("sdl-theora: writing current frame");
      
      if(encode_frame(f->frame, f->last) == false)
	logging.error("sdl-theora: encoding frame failed");
      else{
	char buffer[80];
	snprintf(buffer, 80, "sdl-theora: encoding frame: %d/%d", f_frame, FPS);
	logging.info(buffer);
      }
      
    }
    
    latest_frame_generated = f_frame;

    if(prev != nullptr){

      av_frame_free(&(prev->frame));
      delete prev;
      prev = nullptr;
    }
    
    prev = f;
    
    if(f->last == true){
      logging.info("sdl-theora: special last frame seen => exit");
      break;
    }
  }
  
  logging.info("sdl-theora: theora encoder thread shutdown sequence..");
  
  logging.info("sdl-theora: encoder thread shutdown: ogg_stream_destroy");
  logging.info("sdl-theora: encoder thread shutdown: ogg_stream_destroy.. done");

  
  // all frames has been written
  if(prev != nullptr){
    av_frame_free(&(prev->frame));
    delete prev;
    prev = nullptr;
  }
  
  logging.info("sdl-theora: encoder thread shutdown: incoming buffer clear");
  
  {
    std::lock_guard<std::mutex> lock1(incoming_mutex);
    for(auto i : incoming){
      av_frame_free(&(i->frame));
      delete i;
    }
    
    incoming.clear();
  }
  
  {
    logging.info("sdl-theora: encoder thread halt. running = false");
    running = false;
  }
  
}


bool SDLAVCodec::encode_frame(AVFrame* buffer,
			      bool last)
{
  assert(avcodec_send_frame(m_encoder, buffer) == 0);
  
  AVPacket packet;
  av_init_packet(&packet);
  int ret = 0;
  while(ret >= 0) {
    ret = avcodec_receive_packet(m_encoder, &packet);
    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return true;  // nothing to write
    }
    assert(ret >= 0);
    
    av_packet_rescale_ts(&packet,
			 m_encoder->time_base,
			 m_avStream->time_base);
    
    packet.stream_index = m_avStream->index;
    av_interleaved_write_frame(m_muxer, &packet);
    av_packet_unref(&packet);
  }
	  
  return true;
}



}
} /* namespace whiteice */
