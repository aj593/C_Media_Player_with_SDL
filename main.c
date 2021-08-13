// gcc -o main main.c -lavutil -lavformat -lavcodec -lswscale -lz -lm  `sdl2-config --cflags --libs`

//#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

void save_frame(AVFrame* av_frame, int width, int height, int frame_index);

int main(int argc, char* argv[]){
  if(argc != 3){
    printf("Need exactly 3 args, you provided %d args", argc);
    return -1; //exit with error b/c of wrong number of args
  }

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0){
    printf("Could not initialize SDL - %s\n", SDL_GetError());
    return -1;
  }

  AVFormatContext* p_format_context = NULL;

  //Look at the video file's header/URL by opening it
  if(avformat_open_input(&p_format_context, argv[1], NULL, NULL) < 0){
    printf("Couldn't open file %s\n", argv[1]);
    return -1;
  }

  if(avformat_find_stream_info(p_format_context, NULL) < 0){
    printf("Couldn't find stream information for file %s\n", argv[1]);
    return -1;
  }

  //Dump info about file onto standard error for debugging purposes
  av_dump_format(p_format_context, 0, argv[1], 0);

  int video_stream = -1;
  //p_format_context now has loaded in it a streams array of length nb_streams
  for(int i = 0; i < p_format_context->nb_streams; i++){
    if(p_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
      video_stream = i;
      break;
    }
  }

  if(video_stream == -1){ return -1; } //video stream unable to be found

  AVCodec* p_codec = NULL;

  if((p_codec = avcodec_find_decoder(p_format_context->streams[video_stream]->codecpar->codec_id)) == NULL){
    printf("Video has unsupported codec!\n");
    return -1;
  }

  //Codec context = info about the codec that the stream uses
  AVCodecContext* p_codec_context_original = avcodec_alloc_context3(p_codec);
  if(avcodec_parameters_to_context(p_codec_context_original, p_format_context->streams[video_stream]->codecpar) != 0){
    printf("Could not copy codec context\n");
    return -1;
  }

  AVCodecContext* p_codec_context = avcodec_alloc_context3(p_codec);
  if(avcodec_parameters_to_context(p_codec_context, p_format_context->streams[video_stream]->codecpar) != 0){
    printf("Could not copy codec context\n");
    return -1;
  }

  if(avcodec_open2(p_codec_context, p_codec, NULL) < 0){
    printf("Could not open codec\n");
    return -1;
  }

  AVFrame* p_frame = NULL; //Place to store the frame
  if((p_frame = av_frame_alloc()) == NULL){
    printf("Couldn't allocate frame");
    return -1;
  }

  SDL_Window* screen = SDL_CreateWindow(
    "SDL Video Player",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    p_codec_context->width/2,
    p_codec_context->height/2,
    SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
  );

  if(!screen){
    printf("SDL: could not set video mode - exiting");
    return -1;
  }

  SDL_GL_SetSwapInterval(1);
  SDL_Renderer* renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
  SDL_Texture* texture = SDL_CreateTexture(  // [4]
    renderer,
    SDL_PIXELFORMAT_YV12,
    SDL_TEXTUREACCESS_STREAMING,
    p_codec_context->width,
    p_codec_context->height
  );

  int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, p_codec_context->width, p_codec_context->height, 32);
  uint8_t* buffer = NULL; //array buffer that will hold raw data when converting frame
  if((buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t))) == NULL){
    printf("Could not allocate buffer for raw data");
    return -1;
  }

  SDL_Event event;

  //Will convert frame from native format to RGB later
  //Convert initial frame to a specific format, first allocate AVFrame struct
  AVFrame* p_frame_RGB = NULL;
  if((p_frame_RGB = av_frame_alloc()) == NULL){
    printf("Could not allocate frame\n");
    return -1;
  }

  //Assign buffer values to p_frame_RGB so we're ready to read from the stream
  av_image_fill_arrays(
    p_frame_RGB->data,
    p_frame_RGB->linesize,
    buffer,
    AV_PIX_FMT_RGB24,
    p_codec_context->width,
    p_codec_context->height,
    32
  );

  //Read through entire video stream by reading in the packet, decoding it into our frame, and convert and save it once it's complete
  struct SwsContext* sws_context = 
    sws_getContext( 
      p_codec_context->width,
      p_codec_context->height,
      p_codec_context->pix_fmt,
      p_codec_context->width,
      p_codec_context->height,
      AV_PIX_FMT_RGB24,   // sws_scale destination color scheme
      SWS_BILINEAR,
      NULL,
      NULL,
      NULL
    );

  AVPacket* p_packet = NULL;
  if((p_packet = av_packet_alloc()) == NULL){
    printf("Could not allocate packet");
    return -1;
  }

  int max_frames_to_decode = atoi(argv[2]); //@TODO: non threadsafe atoi??

  int i = 0;
  while(av_read_frame(p_format_context, p_packet) >= 0){
    if(p_packet->stream_index == video_stream){ //is this a packet from the video stream?
      if(avcodec_send_packet(p_codec_context, p_packet) < 0){
        printf("Error sending packet for decoding");
        return -1;
      }
      int receive_frame_ret_val;
      while((receive_frame_ret_val = avcodec_receive_frame(p_codec_context, p_frame)) >= 0){
        if(receive_frame_ret_val == AVERROR(EAGAIN) || receive_frame_ret_val == AVERROR_EOF){
          break;
        }
        if(receive_frame_ret_val < 0){
          printf("Error while decoding");
          return -1;
        }
        sws_scale(
          sws_context,
          (uint8_t const * const *)p_frame->data,
          p_frame->linesize,
          0,
          p_codec_context->height,
          p_frame_RGB->data,
          p_frame_RGB->linesize
        );
        if(++i <= max_frames_to_decode){
          //save_frame(p_frame_RGB, p_codec_context->width, p_codec_context->height, i);

          //getting clip time and fps
          double fps = av_q2d(p_format_context->streams[video_stream]->r_frame_rate);
          double sleep_time = 1.0/fps; //@TODO: cast fps to double?
          SDL_Delay((1000 * sleep_time) - 10); //give time for CPU scheduling
          
          //SDL_Rect struct holding the window's position and height
          SDL_Rect rect = {0, 0, p_codec_context->width, p_codec_context->height}; //@TODO: right way to initialize this struct?

          //Update a rectangle within a planar
          SDL_UpdateYUVTexture(
            texture,            // the texture to update
            &rect,              // a pointer to the rectangle of pixels to update, or NULL to update the entire texture
            p_frame_RGB->data[0],      // the raw pixel data for the Y plane
            p_frame_RGB->linesize[0],  // the number of bytes between rows of pixel data for the Y plane
            p_frame_RGB->data[1],      // the raw pixel data for the U plane
            p_frame_RGB->linesize[1],  // the number of bytes between rows of pixel data for the U plane
            p_frame_RGB->data[2],      // the raw pixel data for the V plane
            p_frame_RGB->linesize[2]   // the number of bytes between rows of pixel data for the V plane
          );

          SDL_RenderClear(renderer); //Clear current rendering target with drawing color
          SDL_RenderCopy(renderer, texture, NULL, NULL); //Copy portion of the texture to the current rendering target
          SDL_RenderPresent(renderer); //Update the screen with any rendering performed since previous call
        }
        else break;
      }
      if(i > max_frames_to_decode) break;
    }
    av_packet_unref(p_packet); //Free packet allocated by av_read_frame

    SDL_PollEvent(&event); //Handling the ctrl + C event to exit
      switch(event.type){
        case SDL_QUIT:{
          SDL_Quit();
          exit(0);
        } break;
        default: {} break;
      }
  }

  //Clean up operaxtions
  av_free(buffer);
  av_frame_free(&p_frame_RGB);
  av_free(p_frame_RGB);
  av_frame_free(&p_frame);
  av_free(p_frame);
  avcodec_close(p_codec_context);
  avcodec_close(p_codec_context_original);
  avformat_close_input(&p_format_context);

  SDL_DestroyRenderer(renderer);
  SDL_Quit();

  return 0;
}

/**void save_frame(AVFrame* av_frame, int width, int height, int frame_index){
  FILE* p_file;
  char filename_size[32];
  sprintf(filename_size, "frame%d.ppm", frame_index);
  p_file = fopen(filename_size, "wb");
  if(p_file == NULL) return;

  fprintf(p_file, "P6\n%d %d\n255\n", width, height); //write header

  for(int y_axis = 0; y_axis < height; y_axis++){
    fwrite(av_frame->data[0] + y_axis * av_frame->linesize[0], 1, width * 3, p_file);
  }

  fclose(p_file);
}*/