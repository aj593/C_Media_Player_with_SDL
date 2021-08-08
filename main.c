#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

int main(int argc, char *argv[]) {
    //need two arguments, second one being given the file name to open
    if(argc == 1){
        return 1;
    }

    av_register_all(); //initializing the ffmpeg library, registers all available file formats


    AVFormatContext* pFormatCtx = NULL;

    //Read file header and store file format info into pFormatCtx
    //NULL arguments will be initialized automatically
    if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0){
        return -1; //file couldn't be opened
    }

    //Check file's stream info (in pFormatCtx->streams array of pointers)
    if(avformat_find_stream_info(pFormatCtx, NULL) < 0){
        return -1; //stream info couldn't be found
    }

    // Dump file info, used for debugging
    av_dump_format(pFormatCtx, 0, argv[1], 0);

    AVCodecContext* pCodecCtxOriginal = NULL;
    AVCodecContext* pCodecCtx = NULL;

    int videoStream = -1;
    for(int i = 0; i < pFormatCtx->nb_streams; i++){
        //If the codec type is of type video, assign it to the videoStream var and break
        if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            videoStream = i;
            break;
        }
    }

    //Check if video stream found, exit if not found
    if(videoStream == -1){
        return -1;
    }

    //Get pointer to codec context for stream
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    //Getting the actual codec and opening the file:
    AVCodec* pCodec = NULL;
    
    //Find decoder for video stream
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec == NULL){
        fprintf(stderr, "Unsupported codec!\n");
        return -1; //Codec not found
    }

    //Allocate an avcodec
    pCodecCtx = avcodec_alloc_context3(pCodec); //TODO: free this
    //Copy the context from the original to another AVCodecContext*
    if(avcodec_copy_context(pCodecCtx, pCodecCtxOriginal) != 0){
        fprintf(stderr, "Couldn't copy codec context");
        return -1; //error copying codec context
    }

    //We shouldn't use AVCodecContext from the video stream directly, which is why we copied the context after allocating mem for it

    //Open codec
    if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0){
        return -1; //Could not open codec
    }

    AVFrame* pFrameRGB; //allocating the frame
    if((pFrameRGB = av_frame_alloc()) == NULL){
        return -1; //unable to allocate
    }

    uint8_t* buffer; //will be allocated for array of integers
    int num_bytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

    //allocating integer array of size based on calculated num_bytes for picture
    if((buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t))) == NULL){
        return -1; //unable to allocate
    }

    //Associate the frame with newly allocated buffer
    avpicture_fill((AVPicture*)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

    struct SwsContext* sws_ctx = NULL;
    int is_frame_finished;
    AVPacket packet;

    //Initialize SWS context
    sws_ctx = sws_getContext(pCodecCtx->width,
    pCodecCtx->height,
    pCodecCtx->pix_fmt,
    pCodecCtx->width,
    pCodecCtx->height,
    PIX_FMT_RGB24,
    SWS_BILINEAR,
    NULL,
    NULL,
    NULL
    );


    return 0;
}