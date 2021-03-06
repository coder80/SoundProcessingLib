#include "FFAudioTranscoder.h"

static int readFunction(void* opaque, uint8_t* buf, int buf_size) {
    auto& me = *reinterpret_cast<std::istream*>(opaque);
    me.read(reinterpret_cast<char*>(buf), buf_size);
    return me.gcount();
}

FFAudioTranscoder::FFAudioTranscoder(const std::string& inputFile,
                                     const std::string& outFile)
    : mInputFile(inputFile), mOutFile(outFile)
{

}

int FFAudioTranscoder::open() {
    openInputFile();
    openOutFile();
    initResampler();
    initFifo();
    writeOutputFileHeader();
    return 0;
}

int FFAudioTranscoder::openInputFile() {
    AVCodecContext *avctx;
    AVCodec *input_codec;
    int error;

    /*stream.open(mInputFile, std::ios::binary);
    buffer.reset(reinterpret_cast<unsigned char*>(av_malloc(8192)), &av_free);
    avioContext.reset(avio_alloc_context(buffer.get(),
                                         8192, 0, reinterpret_cast<void*>(static_cast<std::istream*>(&stream)),
                                         &readFunction, nullptr, nullptr), &av_free);

    input_format_context = avformat_alloc_context();
    input_format_context->pb = avioContext.get();
    avformat_open_input(&input_format_context, "memory buffer", nullptr, nullptr);*/

    /* Open the input file to read from it. */
    if ((error = avformat_open_input(&mInputFormatContext, mInputFile.c_str(), nullptr,
                                     nullptr)) < 0) {
        std::cerr << "Could not open input file " <<
                mInputFile << std::endl;
        mInputFormatContext = nullptr;
        return error;
    }
    /* Get information on the input file (number of streams etc.). */
    if ((error = avformat_find_stream_info(mInputFormatContext, nullptr)) < 0) {
        std::cerr <<  "Could not open find stream info"
                << std::endl;
        avformat_close_input(&mInputFormatContext);
        return error;
    }
    /* Make sure that there is only one stream in the input file. */
    if ((mInputFormatContext)->nb_streams != 1) {
        fprintf(stderr, "Expected one audio input stream, but found %d\n",
                (mInputFormatContext)->nb_streams);
        avformat_close_input(&mInputFormatContext);
        return AVERROR_EXIT;
    }
    /* Find a decoder for the audio stream. */
    if (!(input_codec = avcodec_find_decoder((mInputFormatContext)->streams[0]->codecpar->codec_id))) {
        fprintf(stderr, "Could not find input codec\n");
        avformat_close_input(&mInputFormatContext);
        return AVERROR_EXIT;
    }
    /* Allocate a new decoding context. */
    avctx = avcodec_alloc_context3(input_codec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate a decoding context\n");
        avformat_close_input(&mInputFormatContext);
        return AVERROR(ENOMEM);
    }
    /* Initialize the stream parameters with demuxer information. */
    error = avcodec_parameters_to_context(avctx, (mInputFormatContext)->streams[0]->codecpar);
    if (error < 0) {
        avformat_close_input(&mInputFormatContext);
        avcodec_free_context(&avctx);
        return error;
    }
    /* Open the decoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, input_codec, nullptr)) < 0) {
        throw std::runtime_error("Could not open input codec");
        avcodec_free_context(&avctx);
        avformat_close_input(&mInputFormatContext);
        return error;
    }
    /* Save the decoder context for easier access later. */
    mInputCodecContext = avctx;
    return 0;
}

int FFAudioTranscoder::openOutFile() {
    AVCodecContext *avctx          = nullptr;
    AVIOContext *output_io_context = nullptr;
    AVStream *stream               = nullptr;
    AVCodec *output_codec          = nullptr;
    int error;
    /* Open the output file to write to it. */
    if ((error = avio_open(&output_io_context, mOutFile.c_str(),
                           AVIO_FLAG_WRITE)) < 0) {
        //fprintf(stderr, "Could not open output file '%s' (error '%s')\n",
                //mOutFile.c_str(), av_err2str(error));
        return error;
    }
    /* Create a new format context for the output container format. */
    if (!(mOutputFormatContext = avformat_alloc_context())) {
        fprintf(stderr, "Could not allocate output format context\n");
        return AVERROR(ENOMEM);
    }
    /* Associate the output file (pointer) with the container format context. */
    (mOutputFormatContext)->pb = output_io_context;
    /* Guess the desired container format based on the file extension. */
    if (!((mOutputFormatContext)->oformat = av_guess_format(nullptr, mOutFile.c_str(),
                                                              nullptr))) {
        fprintf(stderr, "Could not find output file format\n");
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    if (!((mOutputFormatContext)->url = av_strdup(mOutFile.c_str()))) {
        fprintf(stderr, "Could not allocate url.\n");
        error = AVERROR(ENOMEM);
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    /* Find the encoder to be used by its name. */
    if (!(output_codec = avcodec_find_encoder(AV_CODEC_ID_AAC))) {
        fprintf(stderr, "Could not find an AAC encoder.\n");
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    /* Create a new audio stream in the output file container. */
    if (!(stream = avformat_new_stream(mOutputFormatContext, nullptr))) {
        fprintf(stderr, "Could not create new stream\n");
        error = AVERROR(ENOMEM);
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    avctx = avcodec_alloc_context3(output_codec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate an encoding context\n");
        error = AVERROR(ENOMEM);
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    /* Set the basic encoder parameters.
     * The input file's sample rate is used to avoid a sample rate conversion. */
    avctx->channels       = OUTPUT_CHANNELS;
    avctx->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
    avctx->sample_rate    = mInputCodecContext->sample_rate;
    avctx->sample_fmt     = output_codec->sample_fmts[0];
    avctx->bit_rate       = OUTPUT_BIT_RATE;
    /* Allow the use of the experimental AAC encoder. */
    avctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    /* Set the sample rate for the container. */
    stream->time_base.den = mInputCodecContext->sample_rate;
    stream->time_base.num = 1;
    /* Some container formats (like MP4) require global headers to be present.
     * Mark the encoder so that it behaves accordingly. */
    if ((mOutputFormatContext)->oformat->flags & AVFMT_GLOBALHEADER)
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    /* Open the encoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, output_codec, nullptr)) < 0) {
        //fprintf(stderr, "Could not open output codec (error '%s')\n",
                //av_err2str(error));
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    error = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (error < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        avcodec_free_context(&avctx);
        return error < 0 ? error : AVERROR_EXIT;
    }
    /* Save the encoder context for easier access later. */
    mOutputCodecContext = avctx;
    return 0;
}

int FFAudioTranscoder::initResampler() {
    int error;
    /*
     * Create a resampler context for the conversion.
     * Set the conversion parameters.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity (they are sometimes not detected
     * properly by the demuxer and/or decoder).
     */
    mResampleContext = swr_alloc_set_opts(nullptr,
                                          av_get_default_channel_layout(mOutputCodecContext->channels),
                                          mOutputCodecContext->sample_fmt,
                                          mOutputCodecContext->sample_rate,
                                          av_get_default_channel_layout(mInputCodecContext->channels),
                                          mInputCodecContext->sample_fmt,
                                          mInputCodecContext->sample_rate,
                                          0, nullptr);
    if (!mResampleContext) {
        fprintf(stderr, "Could not allocate resample context\n");
        return AVERROR(ENOMEM);
    }
    /*
    * Perform a sanity check so that the number of converted samples is
    * not greater than the number of samples to be converted.
    * If the sample rates differ, this case has to be handled differently
    */
    av_assert0(mOutputCodecContext->sample_rate == mInputCodecContext->sample_rate);
    /* Open the resampler with the specified parameters. */
    if ((error = swr_init(mResampleContext)) < 0) {
        fprintf(stderr, "Could not open resample context\n");
        swr_free(&mResampleContext);
        return error;
    }

    return 0;
}

int FFAudioTranscoder::initFifo() {
    /* Create the FIFO buffer based on the specified output sample format. */
    if (!(mFifo = av_audio_fifo_alloc(mOutputCodecContext->sample_fmt,
                                      mOutputCodecContext->channels, 1))) {
        fprintf(stderr, "Could not allocate FIFO\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int FFAudioTranscoder::writeOutputFileHeader() {
    int error;
    if ((error = avformat_write_header(mOutputFormatContext, nullptr)) < 0) {
        //fprintf(stderr, "Could not write output file header (error '%s')\n",
                //av_err2str(error));
        return error;
    }
    return 0;
}

int FFAudioTranscoder::initInputFrame(AVFrame **frame) {
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int FFAudioTranscoder::initPacket(AVPacket *packet) {
    av_init_packet(packet);
    /* Set the packet data and size so that it is recognized as being empty. */
    packet->data = NULL;
    packet->size = 0;
    return 0;
}

int FFAudioTranscoder::decodeAudioFrame(AVFrame *frame, int *data_present, int *finished) {
    /* Packet used for temporary storage. */
    AVPacket input_packet;
    int error;
    initPacket(&input_packet);
    /* Read one audio frame from the input file into a temporary packet. */
    if ((error = av_read_frame(mInputFormatContext, &input_packet)) < 0) {
        /* If we are at the end of the file, flush the decoder below. */
        if (error == AVERROR_EOF)
            *finished = 1;
        else {
            std::cerr << "Could not read frame" << std::endl;
            return error;
        }
    }
    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(mInputCodecContext, &input_packet)) < 0) {
        std::cerr <<  "Could not send packet for decoding (error '%s')\n" << std::endl;
        return error;
    }
    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(mInputCodecContext, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        av_packet_unref(&input_packet);
        return error;
    /* If the end of the input file is reached, stop decoding. */
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
        av_packet_unref(&input_packet);
        return error;
    } else if (error < 0) {
        std::cerr << "Could not decode frame" << std::endl;
        av_packet_unref(&input_packet);
        return error;
    /* Default case: Return decoded data. */
    } else {
        *data_present = 1;
        av_packet_unref(&input_packet);
        return error;
    }

    av_packet_unref(&input_packet);
    return error;
}

int FFAudioTranscoder::initConvertedSamples(uint8_t ***converted_input_samples, int frame_size) {
    int error;
    /* Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*converted_input_samples = reinterpret_cast<uint8_t**>(calloc(mOutputCodecContext->channels,
                                            sizeof(**converted_input_samples))))) {
        fprintf(stderr, "Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }
    /* Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                  mOutputCodecContext->channels,
                                  frame_size,
                                  mOutputCodecContext->sample_fmt, 0)) < 0) {
                std::cerr << "Could not allocate converted input samples" << std::endl;
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        return error;
    }
    return 0;
}

int FFAudioTranscoder::convertSamples(const uint8_t **input_data,
                   uint8_t **converted_data, const int frame_size) {
    int error;
    /* Convert the samples using the resampler. */
    if ((error = swr_convert(mResampleContext,
                             converted_data, frame_size,
                             input_data    , frame_size)) < 0) {
        std::cerr << "Could not convert input samples (error '%s')\n" << std::endl;
        return error;
    }
    return 0;
}

int FFAudioTranscoder::addSamplesToFifo(uint8_t **converted_input_samples,
                     const int frame_size) {
    int error;
    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(mFifo, av_audio_fifo_size(mFifo) + frame_size)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }
    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(mFifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

int FFAudioTranscoder::readDecodeConvertAndStore(int* finished) {
    /* Temporary storage of the input samples of the frame read from the file. */
    AVFrame *input_frame = NULL;
    /* Temporary storage for the converted input samples. */
    uint8_t **converted_input_samples = NULL;
    int data_present = 0;
    int ret = AVERROR_EXIT;
    /* Initialize temporary storage for one input frame. */
    if (initInputFrame(&input_frame)) {
        if (converted_input_samples) {
            av_freep(&converted_input_samples[0]);
            free(converted_input_samples);
        }
        av_frame_free(&input_frame);
        return ret;
    }
    /* Decode one frame worth of audio samples. */
    if (decodeAudioFrame(input_frame, &data_present, finished)) {
        if (converted_input_samples) {
            av_freep(&converted_input_samples[0]);
            free(converted_input_samples);
        }
        av_frame_free(&input_frame);
        return ret;
    }
    /* If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error. */
    if (*finished) {
        ret = 0;
        if (converted_input_samples) {
            av_freep(&converted_input_samples[0]);
            free(converted_input_samples);
        }
        av_frame_free(&input_frame);
        return ret;
    }
    /* If there is decoded data, convert and store it. */
    if (data_present) {
        /* Initialize the temporary storage for the converted input samples. */
        if (initConvertedSamples(&converted_input_samples,
                                   input_frame->nb_samples)) {
            if (converted_input_samples) {
                av_freep(&converted_input_samples[0]);
                free(converted_input_samples);
            }
            av_frame_free(&input_frame);
            return ret;
        }
        /* Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples. */
        if (convertSamples((const uint8_t**)input_frame->extended_data, converted_input_samples,
                            input_frame->nb_samples)) {
            if (converted_input_samples) {
                av_freep(&converted_input_samples[0]);
                free(converted_input_samples);
            }
            av_frame_free(&input_frame);
            return ret;
        }
        /* Add the converted input samples to the FIFO buffer for later processing. */
        if (addSamplesToFifo(converted_input_samples,
                                input_frame->nb_samples)) {
            if (converted_input_samples) {
                av_freep(&converted_input_samples[0]);
                free(converted_input_samples);
            }
            av_frame_free(&input_frame);
            return ret;
        }
        ret = 0;
    }

    ret = 0;
    if (converted_input_samples) {
        av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }
    av_frame_free(&input_frame);
    return ret;
}

int FFAudioTranscoder::initOutputFrame(AVFrame **frame, int frame_size) {
    int error;
    /* Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }
    /* Set the frame's parameters, especially its size and format.
     * av_frame_get_buffer needs this to allocate memory for the
     * audio samples of the frame.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity. */
    (*frame)->nb_samples     = frame_size;
    (*frame)->channel_layout = mOutputCodecContext->channel_layout;
    (*frame)->format         = mOutputCodecContext->sample_fmt;
    (*frame)->sample_rate    = mOutputCodecContext->sample_rate;
    /* Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified. */
    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        std::cerr <<  "Could not allocate output frame samples" << std::endl;
        av_frame_free(frame);
        return error;
    }
    return 0;
}

int FFAudioTranscoder::encodeAudioFrame(AVFrame *frame, int *data_present) {
    /* Packet used for temporary storage. */
    AVPacket output_packet;
    int error;
    initPacket(&output_packet);
    /* Set a timestamp based on the sample rate for the container. */
    if (frame) {
        frame->pts = mPts;
        mPts += frame->nb_samples;
    }
    /* Send the audio frame stored in the temporary packet to the encoder.
     * The output audio stream encoder is used to do this. */
    error = avcodec_send_frame(mOutputCodecContext, frame);
    /* The encoder signals that it has nothing more to encode. */
    if (error == AVERROR_EOF) {
        error = 0;
        av_packet_unref(&output_packet);
        return error;
    } else if (error < 0) {
        std::cerr <<  "Could not send packet for encoding" << std::endl;
        return error;
    }
    /* Receive one encoded frame from the encoder. */
    error = avcodec_receive_packet(mOutputCodecContext, &output_packet);
    /* If the encoder asks for more data to be able to provide an
     * encoded frame, return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        av_packet_unref(&output_packet);
        return error;
    /* If the last frame has been encoded, stop encoding. */
    } else if (error == AVERROR_EOF) {
        error = 0;
        av_packet_unref(&output_packet);
        return error;
    } else if (error < 0) {
        std::cerr <<  "Could not encode frame" << std::endl;
        av_packet_unref(&output_packet);
        return error;
    /* Default case: Return encoded data. */
    } else {
        *data_present = 1;
    }
    /* Write one audio frame from the temporary packet to the output file. */
    if (*data_present &&
        (error = av_write_frame(mOutputFormatContext, &output_packet)) < 0) {
        std::cerr <<  "Could not write frame" << std::endl;
        av_packet_unref(&output_packet);
        return error;
    }

    return error;
}

int FFAudioTranscoder::loadEncodeAndWrite() {
    /* Temporary storage of the output samples of the frame written to the file. */
    AVFrame *output_frame;
    /* Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size. */
    const int frame_size = FFMIN(av_audio_fifo_size(mFifo),
                                 mOutputCodecContext->frame_size);
    int data_written;
    /* Initialize temporary storage for one output frame. */
    if (initOutputFrame(&output_frame, frame_size))
        return AVERROR_EXIT;
    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(mFifo, (void **)output_frame->data, frame_size) < frame_size) {
        fprintf(stderr, "Could not read data from FIFO\n");
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    /* Encode one frame worth of audio samples. */
    if (encodeAudioFrame(output_frame, &data_written)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    av_frame_free(&output_frame);
    return 0;
}

int FFAudioTranscoder::writeOutputFileTrailer() {
    int error;
    if ((error = av_write_trailer(mOutputFormatContext)) < 0) {
        std::cerr << "Could not write output file trailer" << std::endl;
        return error;
    }
    return 0;
}

int FFAudioTranscoder::transcode() {
    int ret = AVERROR_EXIT;
    while (true) {
        /* Use the encoder's desired frame size for processing. */
        const int output_frame_size = mOutputCodecContext->frame_size;
        int finished                = 0;
        /* Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples. */
        while (av_audio_fifo_size(mFifo) < output_frame_size) {
            /* Decode one frame worth of audio samples, convert it to the
             * output sample format and put it into the FIFO buffer. */
            if (readDecodeConvertAndStore(&finished))
                close();
            /* If we are at the end of the input file, we continue
             * encoding the remaining audio samples to the output file. */
            if (finished)
                break;
        }
        /* If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder. */
        while (av_audio_fifo_size(mFifo) >= output_frame_size ||
               (finished && av_audio_fifo_size(mFifo) > 0))
            /* Take one frame worth of audio samples from the FIFO buffer,
             * encode it and write it to the output file. */
            if (loadEncodeAndWrite())
                close();
        /* If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish. */
        if (finished) {
            int data_written;
            /* Flush the encoder as it may have delayed frames. */
            do {
                data_written = 0;
                if (encodeAudioFrame(nullptr, &data_written))
                    close();
            } while (data_written);
            break;
        }
    }
    /* Write the trailer of the output file container. */
    if (writeOutputFileTrailer())
        close();
    ret = 0;
    return ret;
}

void FFAudioTranscoder::close() {
    if (mFifo)
        av_audio_fifo_free(mFifo);
    swr_free(&mResampleContext);
    if (mOutputCodecContext)
        avcodec_free_context(&mOutputCodecContext);
    if (mOutputFormatContext) {
        avio_closep(&mOutputFormatContext->pb);
        avformat_free_context(mOutputFormatContext);
    }
    if (mInputCodecContext)
        avcodec_free_context(&mInputCodecContext);
    if (mInputFormatContext)
        avformat_close_input(&mInputFormatContext);
    if (mOutputFormatContext->pb) {
        avio_closep(&(mOutputFormatContext)->pb);
    }

    if (mOutputFormatContext) {
        avformat_free_context(mOutputFormatContext);
    }

    mOutputFormatContext = nullptr;
}

FFAudioTranscoder::~FFAudioTranscoder() {
    close();
}
