#ifndef FFAUDIOTRANSCODER_H
#define FFAUDIOTRANSCODER_H

#include <stdio.h>
#include <iostream>
#include <memory>
#include <istream>
#include <fstream>

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavformat/avio.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/audio_fifo.h"
    #include "libavutil/avassert.h"
    #include "libavutil/avstring.h"
    #include "libavutil/frame.h"
    #include "libavutil/opt.h"
    #include "libswresample/swresample.h"
}
#include <string>
/* The output bit rate in bit/s */
#define OUTPUT_BIT_RATE 96000
/* The number of output channels */
#define OUTPUT_CHANNELS 2

class FFAudioTranscoder
{
public:
    FFAudioTranscoder(const std::string& inputFile,
                      const std::string& outFile);
    int open();
    int transcode();

private:
    int openInputFile();
    int openOutFile();
    int initResampler();
    int initFifo();
    int writeOutputFileHeader();
    int readDecodeConvertAndStore(int* finished);
    int initInputFrame(AVFrame **frame);
    int decodeAudioFrame(AVFrame *frame, int *data_present, int *finished);
    int initPacket(AVPacket *packet);
    int initConvertedSamples(uint8_t ***converted_input_samples, int frame_size);
    int convertSamples(const uint8_t **input_data,
                       uint8_t **converted_data, const int frame_size);
    int addSamplesToFifo(uint8_t **converted_input_samples,
                         const int frame_size);
    int loadEncodeAndWrite();
    int initOutputFrame(AVFrame **frame, int frame_size);
    int encodeAudioFrame(AVFrame *frame, int *data_present);
    int writeOutputFileTrailer();
    std::string mInputFile;
    std::string mOutFile;
    //AVCodecContext* avctx;
    //AVCodec* input_codec;
    AVFormatContext* input_format_context = nullptr;
    AVCodecContext* input_codec_context = nullptr;
    AVFormatContext* output_format_context = nullptr;
    AVCodecContext* output_codec_context = nullptr;
    SwrContext* resample_context = nullptr;
    AVAudioFifo* fifo = nullptr;
    int64_t pts = 0;
    std::shared_ptr<unsigned char> buffer;
    std::shared_ptr<AVIOContext> avioContext;
    std::ifstream stream;
};

#endif // FFAUDIOTRANSCODER_H
