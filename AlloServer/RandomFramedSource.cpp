

#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif

#include "RandomFramedSource.h"
#include <GroupsockHelper.hh> // for "gettimeofday()"
#include "config.h"
//#include "stdafx.h"
#include <stdint.h>
#include <time.h>

const float RandomFramedSource::encodeReportsPS = 0.5f;

void RandomFramedSource::logTimes(int64_t start, int64_t stop) {
  double start_d = (double) start / counter * .001;
  double stop_d = (double) stop / counter * .001;

  //printf("(milliseconds) start is: %.3lf, stop is: %.3lf, YUV/encoding took: %.3lf \n", start_d, stop_d, stop_d - start_d);
  printf("%s: encoding took: %.3lf \n", this->name, stop_d - start_d);
}
struct SwsContext *img_convert_ctx = NULL;

//int img_width = 960;
//int img_height = 540;

int rgb2yuv(AVFrame *frameRGB, AVFrame *frameYUV, AVCodecContext *c) {
  char *err;
  if (img_convert_ctx == NULL) {
    int w = image_width; //c->width;
    int h = image_height; //c->height;
    img_convert_ctx = sws_getContext(w, h, PIX_FMT_RGB24, w, h,
            c->pix_fmt, SWS_BICUBIC,
            NULL, NULL, NULL);
    if (img_convert_ctx == NULL) {
      sprintf(err, "Cannot initialize the conversion context!\n");
      return -1;
    }
  }
  if (frameRGB->linesize[0] > 0) {
    frameRGB->data[0] += frameRGB->linesize[0]*(c->height - 1);

    frameRGB->linesize[0] = -frameRGB->linesize[0];
  }
  return sws_scale(img_convert_ctx, frameRGB->data,
          frameRGB->linesize, 0, c->height,
          frameYUV->data, frameYUV->linesize);
}

RandomFramedSource*
RandomFramedSource::createNew(UsageEnvironment& env, char* name) {
  return new RandomFramedSource(env, name);
}

EventTriggerId RandomFramedSource::eventTriggerId = 0;

unsigned RandomFramedSource::referenceCount = 0;

struct timeval prevtime;

RandomFramedSource::RandomFramedSource(UsageEnvironment& env, char* name)
: FramedSource(env), name(name), encodeBarrier(2), shutDownEncode(false) {
  gettimeofday(&prevtime, NULL); // If you have a more accurate time - e.g., from an encoder - then use that instead.
  if (referenceCount == 0) {
    // Any global initialization of the device would be done here:
    //%%% TO BE WRITTEN %%%
  }
  ++referenceCount;
  myfile = fopen("/home/tibor/Desktop/Logs/deviceglxgears.log", "w");
  counter = 0;
  networkStartSum = 0;
  networkStopSum = 0;

  // initialize frame
  this->frame = avcodec_alloc_frame();
  if (!this->frame) {
    fprintf(stderr, "Could not allocate video frame\n");
    exit(1);
  }
  this->frame->format = AV_PIX_FMT_YUV420P;
  this->frame->width = image_width;
  this->frame->height = image_height;

  /* the image can be allocated by any means and av_image_alloc() is
   * just the most convenient way if av_malloc() is to be used */
  if (av_image_alloc(this->frame->data, this->frame->linesize, this->frame->width, this->frame->height,
          AV_PIX_FMT_YUV420P, 32) < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }

  // Initialize codec
  codecContext = avcodec_alloc_context3(codec);

  if (!codecContext) {
    fprintf(stderr, "%s: could not allocate video codec context\n", name);
    exit(1);
  }

  /* put sample parameters */
  codecContext->bit_rate = bit_rate;
  /* resolution must be a multiple of two */
  codecContext->width = image_width;
  codecContext->height = image_height;
  /* frames per second */
  codecContext->time_base = {1, FPS};
  codecContext->gop_size = 20; /* emit one intra frame every ten frames */
  codecContext->max_b_frames = 0;
  codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

  //if(codec_id == AV_CODEC_ID_H264){
  av_opt_set(codecContext->priv_data, "preset", preset_val, 0);
  av_opt_set(codecContext->priv_data, "tune", tune_val, 0);
  //av_opt_set(c->priv_data, "x264opts", x264_val, 0);


  //av_opt_set(c->priv_data, "profile", "high", 0);
  //av_opt_set(c->priv_data, "sliced-threads", "", 0);
  //}
  /* open it */
  if (avcodec_open2(codecContext, codec, NULL) < 0) {
    fprintf(stderr, "%s: could not open codec %d\n", name);
    exit(1);
  }

  this->randomPixels = new unsigned char[image_width * image_height * 3];

  // Any instance-specific initialization of the device would be done here:
  //%%% TO BE WRITTEN %%%

  // We arrange here for our "deliverFrame" member function to be called
  // whenever the next frame of data becomes available from the device.
  //
  // If the device can be accessed as a readable socket, then one easy way to do this is using a call to
  //     envir().taskScheduler().turnOnBackgroundReadHandling( ... )
  // (See examples of this call in the "liveMedia" directory.)
  //
  // If, however, the device *cannot* be accessed as a readable socket, then instead we can implement it using 'event triggers':
  // Create an 'event trigger' for this device (if it hasn't already been done):
  if (eventTriggerId == 0) {
    eventTriggerId = envir().taskScheduler().createEventTrigger(deliverFrame0);
  }

  boost::thread encodeThread(boost::bind(&RandomFramedSource::encodeLoop, this));
}

RandomFramedSource::~RandomFramedSource() {
  // Any instance-specific 'destruction' (i.e., resetting) of the device would be done here:
  //%%% TO BE WRITTEN %%%
  encodeBarrier.wait();
  this->shutDownEncode = true;
  encodeBarrier.wait();
  
  counter++;

  --referenceCount;
  if (referenceCount == 0) {
    // Any global 'destruction' (i.e., resetting) of the device would be done here:
    //%%% TO BE WRITTEN %%%

    // Reclaim our 'event trigger'
    envir().taskScheduler().deleteEventTrigger(eventTriggerId);
    eventTriggerId = 0;
  }
  fclose(myfile);
}

void RandomFramedSource::doGetNextFrame() {
  //fMaxSize = 200000;
  // This function is called (by our 'downstream' object) when it asks for new data.

  // Note: If, for some reason, the source device stops being readable (e.g., it gets closed), then you do the following:
  if (0 /* the source stops being readable */ /*%%% TO BE WRITTEN %%%*/) {
    handleClosure(this);
    return;
  }

  // If a new frame of data is immediately available to be delivered, then do this now:
  if (1 /* a new frame of data is immediately available to be delivered*/ /*%%% TO BE WRITTEN %%%*/) {
    deliverFrame();
  }

  // No new data is immediately available to be delivered.  We don't do anything more here.
  // Instead, our event trigger must be called (e.g., from a separate thread) when new data becomes available.

}

void RandomFramedSource::deliverFrame0(void* clientData) {
  ((RandomFramedSource*) clientData)->deliverFrame();
}

void RandomFramedSource::encodeLoop() {

  while (!this->shutDownEncode) {

    /*if (counter % 15 == 0) {
    for (int i = 0; i < (image_width * image_height * 3) - sizeof(int); i += 10) {
     *((int*)(this->randomPixels + i)) = rand();
    }
    }*/

    
    encodeStartSumBuffer += av_gettime();
    /*for (int i = 0; i < image_width * image_height * 3; i += sizeof (int)) {
      
      int rnd = rand_r(&encodeSeed);
      for (int j = 0; i < image_width * image_height * 3 ||
              j < (3 * 500); j++, i += sizeof (int)) {
        *((int*) (this->randomPixels + i)) = rnd;
      }
      
    }*/
    encodeStopSumBuffer += av_gettime();
    
    encodeStartSumFrame += av_gettime();
    avpicture_fill((AVPicture *)this->frame, this->randomPixels, AV_PIX_FMT_YUV420P, image_width, image_height);
    encodeStopSumFrame += av_gettime();
    
    encodeStartSumEncode += av_gettime();
    av_init_packet(&this->pkt);
    this->pkt.data = NULL; // packet data will be allocated by the encoder
    this->pkt.size = 0;
    int got_output = 0;
    int ret = avcodec_encode_video2(codecContext, &this->pkt, ::frame, &got_output);
    
    fprintf(myfile, "packet size: %i \n", this->pkt.size);
    fflush(myfile);

    if (ret < 0) {
      fprintf(stderr, "Error encoding frame\n");
      exit(1);
    }
    encodeStopSumEncode += av_gettime();
    
    encodeStartSumSync += av_gettime();
    encodeBarrier.wait();
    encodeBarrier.wait();
    encodeStopSumSync += av_gettime();
    
    int frequency = FPS/encodeReportsPS;
    
    if (counter % frequency == 0) {
      float bufferTime =  (encodeStopSumBuffer - encodeStartSumBuffer) * 0.001f / frequency;
      float frameTime =  (encodeStopSumFrame - encodeStartSumFrame) * 0.001f / frequency;
      float encodeTime =  (encodeStopSumEncode - encodeStartSumEncode) * 0.001f / frequency;
      float syncTime =  (encodeStopSumSync - encodeStartSumSync) * 0.001f / frequency;
      encodeStopSumBuffer = encodeStartSumBuffer = encodeStopSumFrame = encodeStartSumFrame =
              encodeStopSumEncode = encodeStartSumEncode = encodeStopSumSync = encodeStartSumSync = 0;
              
      printf("%s: buffer %.3f, frame %.3f, encode %.3f, sync %.3f \n", this->name, bufferTime, frameTime, encodeTime, syncTime);
    }
  }
}

void RandomFramedSource::deliverFrame() {
  // This function is called when new frame data is available from the device.
  // We deliver this data by copying it to the 'downstream' object, using the following parameters (class members):
  // 'in' parameters (these should *not* be modified by this function):
  //     fTo: The frame data is copied to this address.
  //         (Note that the variable "fTo" is *not* modified.  Instead,
  //          the frame data is copied to the address pointed to by "fTo".)
  //     fMaxSize: This is the maximum number of bytes that can be copied
  //         (If the actual frame is larger than this, then it should
  //          be truncated, and "fNumTruncatedBytes" set accordingly.)
  // 'out' parameters (these are modified by this function):
  //     fFrameSize: Should be set to the delivered frame size (<= fMaxSize).
  //     fNumTruncatedBytes: Should be set iff the delivered frame would have been
  //         bigger than "fMaxSize", in which case it's set to the number of bytes
  //         that have been omitted.
  //     fPresentationTime: Should be set to the frame's presentation time
  //         (seconds, microseconds).  This time must be aligned with 'wall-clock time' - i.e., the time that you would get
  //         by calling "gettimeofday()".
  //     fDurationInMicroseconds: Should be set to the frame's duration, if known.
  //         If, however, the device is a 'live source' (e.g., encoded from a camera or microphone), then we probably don't need
  //         to set this variable, because - in this case - data will never arrive 'early'.
  // Note the code below.

  if (!isCurrentlyAwaitingData()) return; // we're not ready for the data yet


  int64_t start = av_gettime();

  encodeBarrier.wait();

  fprintf(myfile, "fMaxSize at beginning of function: %i \n", fMaxSize);
  fflush(myfile);

  // Has no effect. FPS encoded in H.264 codec
  this->fDurationInMicroseconds = 1000000 / 30; // 30 fps



  counter++;
  int ret;
  


  gettimeofday(&fPresentationTime, NULL); // If you have a more accurate time - e.g., from an encoder - then use that instead.
  //int64_t start = fPresentationTime.tv_sec *1000000 +fPresentationTime.tv_usec;



  //  if(got_output) printf("got output%d\n",this->pkt.size);
  u_int8_t* newFrameDataStart = (u_int8_t*) this->pkt.data; //%%% TO BE WRITTEN %%%
  unsigned newFrameSize = this->pkt.size; //%%% TO BE WRITTEN %%%

  // Deliver the data here:
  if (newFrameSize > fMaxSize) {
    fFrameSize = fMaxSize;
    /*rest =*/ fNumTruncatedBytes = newFrameSize - fMaxSize;
    //printf("truncated\n");

    fprintf(myfile, "frameSize %i larger than maxSize %i\n", this->pkt.size, fMaxSize);
    fflush(myfile);
  } else {
    fFrameSize = newFrameSize;
    /*rest =*/ fNumTruncatedBytes = 0;

  }


  // If the device is *not* a 'live source' (e.g., it comes instead from a file or buffer), then set "fDurationInMicroseconds" here.
  memmove(fTo, newFrameDataStart, fFrameSize);
  //if (fNumTruncatedBytes == 0)
  av_free_packet(&this->pkt);
  // printf("got output%d %d\n",got_output,pkt.data);
  // After delivering the data, inform the reader that it is now available:
  FramedSource::afterGetting(this);

  int64_t stop = av_gettime(); //fPresentationTime.tv_sec *1000000 +fPresentationTime.tv_usec;
  networkStartSum += start;
  networkStopSum += stop;
  //  
  
  int frequency = FPS/encodeReportsPS;
  
  if (counter % frequency == 0) {
    networkTotalStop = stop;
    
    float availableTime = (float)frequency / FPS;
    
    printf("%s: network %.3f (theoretical max FPS: %.3f) \n", name, (networkStopSum-networkStartSum) * 0.001f / frequency,
            FPS * availableTime / ((networkTotalStop - networkTotalStart) * 0.000001));
    //printf("time for %d frames: %.3f secs, %.3f\n", frequency, (networkTotalStop - networkTotalStart) * 0.000001, availableTime);
    //logTimes(start_sum, stop_sum);
    networkStopSum = networkStartSum = networkTotalStop = 0;
    networkTotalStart = stop;
  }

  encodeBarrier.wait();
}


// The following code would be called to signal that a new frame of data has become available.
// This (unlike other "LIVE555 Streaming Media" library code) may be called from a separate thread.
// (Note, however, that "triggerEvent()" cannot be called with the same 'event trigger id' from different threads.
// Also, if you want to have multiple device threads, each one using a different 'event trigger id', then you will need
// to make "eventTriggerId" a non-static member variable of "DeviceSource".)

void signalNewFrameData() {
  TaskScheduler* ourScheduler = NULL; //%%% TO BE WRITTEN %%%
  RandomFramedSource* ourDevice = NULL; //%%% TO BE WRITTEN %%%

  if (ourScheduler != NULL) { // sanity check
    ourScheduler->triggerEvent(RandomFramedSource::eventTriggerId, ourDevice);
  }
}