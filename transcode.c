#include <stdio.h>
#include <stdint.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/mathematics.h>


void packet_dump(AVPacket *packet) {
    fprintf(stderr, "------------------------------------------------\n");
    fprintf(stderr, "packet->pts %ld\n", packet->pts);
    fprintf(stderr, "packet->dts %ld\n", packet->dts);
    fprintf(stderr, "packet->size %d\n", packet->size);
    fprintf(stderr, "packet->stream_index %d\n", packet->stream_index);
    fprintf(stderr, "packet->flags %d\n", packet->flags);
    fprintf(stderr, "packet->duration %d\n", packet->duration);
    fprintf(stderr, "packet->pos %ld\n", packet->pos);
    fprintf(stderr, "packet->convergence_duration %ld\n", packet->convergence_duration);
    fprintf(stderr, "------------------------------------------------\n");
}

void frame_dump(AVFrame *frame) {
    fprintf(stderr, "++++++++++++++++++++++++++++++++++++++++++++++++\n");
    fprintf(stderr, "frame->key_frame %d\n", frame->key_frame);
    fprintf(stderr, "frame->pict_type %c\n", av_get_picture_type_char(frame->pict_type));
    fprintf(stderr, "frame->pts %ld\n", frame->pts);
    fprintf(stderr, "frame->coded_picture_number %d\n", frame->coded_picture_number);
    fprintf(stderr, "frame->display_picture_number %d\n", frame->display_picture_number);
    fprintf(stderr, "frame->quality %d\n", frame->quality);
    fprintf(stderr, "frame->age %d\n", frame->age);
    fprintf(stderr, "frame->pkt_pts %ld\n", frame->pkt_pts);
    fprintf(stderr, "frame->pkt_dts %ld\n", frame->pkt_dts);
    fprintf(stderr, "frame->best_effort_timestamp %ld\n", frame->best_effort_timestamp);
    fprintf(stderr, "frame->pkt_pos %ld\n", frame->pkt_pos);
    fprintf(stderr, "frame->width %d\n", frame->width);
    fprintf(stderr, "frame->height %d\n", frame->height);
    fprintf(stderr, "frame->format %d\n", frame->format);
    fprintf(stderr, "++++++++++++++++++++++++++++++++++++++++++++++++\n");
}


int main(int argc, const char *argv[]) {
    int len, got_frame, frame_bytes_output, sample_fmt_output_size;
    unsigned int stream_idx;
    uint8_t *tmp;
    uint8_t buffer[409600];
    int16_t samples[409600], resamples[409600];
    AVFormatContext *format_input_context, *format_output_context;
    AVStream *stream_input, *stream_output;
    AVCodec *codec;
    AVFrame *frame;
    AVPacket packet;
    AVFifoBuffer *audio_fifo;
    ReSampleContext *resample_context = NULL; // TODO: audio resample ctx per stream

    if (argc != 3) {
        fprintf(stderr, "Usage: %s input output\n", argv[0]);
        return 1;
    }

    // Initialize libavformat and register all the muxers, demuxers and protocols
    av_register_all();

    // Register all the codecs, parsers and bitstream filters which were enabled at configuration time
    avcodec_register_all();


    // Allocate an AVFormatContext
    format_input_context = avformat_alloc_context();
    if (format_input_context == NULL) {
        fprintf(stderr, "Cannot alloc format input context");
        return 1;
    }

    // Open input file
    if (avformat_open_input(&format_input_context, argv[1], NULL, NULL) < 0) {
        fprintf(stderr, "Cannot open input file\n");
        return 1;
    }

    // Read packets of a media file to get stream information
    if (av_find_stream_info(format_input_context) < 0) {
        fprintf(stderr, "Cannot find stream information");
        return 1;
    }

    // Open all stream decoders
    for (stream_idx = 0; stream_idx < format_input_context->nb_streams; stream_idx++) {
        stream_input = format_input_context->streams[stream_idx];
        // Find a registered decoder with a matching codec ID.
        if ((codec = avcodec_find_decoder(stream_input->codec->codec_id)) == NULL) {
            fprintf(stderr, "Cannot find default decoder\n");
            return 1;
        }

        // Initialize the AVCodecContext to use the given AVCodec.
        if (avcodec_open(stream_input->codec, codec) < 0) {
            fprintf(stderr, "Cannot open default decoder\n");
            return 1;
        }
    }

    // Print video information to stdout
    av_dump_format(format_input_context, 0, argv[1], 0);




    // Allocate an AVFormatContext for an output format.
    if (avformat_alloc_output_context2(&format_output_context, NULL, NULL, argv[2]) < 0) {
        fprintf(stderr, "Cannot alloc format output context\n");
        return 1;
    }

    // For each input stream, create an output stream
    for (stream_idx = 0; stream_idx < format_input_context->nb_streams; stream_idx++) {
        stream_input = format_input_context->streams[stream_idx];
        // Add a new stream to a media file.
        if ((stream_output = av_new_stream(format_output_context, stream_idx)) == NULL) {
            fprintf(stderr, "Cannot create new stream\n");
            return 1;
        }
        switch (stream_input->codec->codec_type) {
            case CODEC_TYPE_VIDEO:
                // Find a registered encoder with a matching codec ID.
                if ((codec = avcodec_find_encoder(format_output_context->oformat->video_codec)) == NULL) {
                    fprintf(stderr, "Cannot find default video encoder\n");
                    return 1;
                }
                stream_output->codec->codec_id = codec->id;
                stream_output->codec->codec_type = codec->type;
                stream_output->codec->width = stream_input->codec->width;
                stream_output->codec->height = stream_input->codec->height;
                stream_output->codec->time_base = stream_input->codec->time_base;
                stream_output->codec->bit_rate = stream_input->codec->bit_rate;
                stream_output->codec->bit_rate_tolerance = stream_input->codec->bit_rate_tolerance;
                stream_output->codec->pix_fmt = codec->pix_fmts[0];

                // Some formats want stream headers to be separate (ie. mov)
                if (format_output_context->oformat->flags & AVFMT_GLOBALHEADER)
                    stream_output->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

                // Initialize the AVCodecContext to use the given AVCodec.
                if (avcodec_open(stream_output->codec, codec) < 0) {
                    fprintf(stderr, "Cannot open default video encoder\n");
                    return 1;
                }
                break;
            case CODEC_TYPE_AUDIO:
                // Find a registered encoder with a matching codec ID.
                if ((codec = avcodec_find_encoder(format_output_context->oformat->audio_codec)) == NULL) {
                    fprintf(stderr, "Cannot find default audio encoder\n");
                    return 1;
                }
                stream_output->codec->codec_id = codec->id;
                stream_output->codec->codec_type = codec->type;
                stream_output->codec->channels = stream_input->codec->channels;
                stream_output->codec->sample_fmt = SAMPLE_FMT_S16; //stream_input->codec->sample_fmt;
                stream_output->codec->sample_rate = stream_input->codec->sample_rate;
                stream_output->codec->time_base = stream_input->codec->time_base;
                stream_output->codec->bit_rate = stream_input->codec->bit_rate;
                stream_output->codec->bit_rate_tolerance = stream_input->codec->bit_rate_tolerance;

                // Some formats want stream headers to be separate (ie. mov)
                if (format_output_context->oformat->flags & AVFMT_GLOBALHEADER)
                    stream_output->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

                // Initialize the AVCodecContext to use the given AVCodec.
                if (avcodec_open(stream_output->codec, codec) < 0) {
                    fprintf(stderr, "Cannot open default audio encoder\n");
                    return 1;
                }

                if (stream_output->codec->channels != stream_input->codec->channels
                    || stream_output->codec->sample_rate != stream_input->codec->sample_rate
                    || stream_output->codec->sample_fmt != stream_input->codec->sample_fmt) {

                    resample_context = av_audio_resample_init(stream_output->codec->channels, stream_input->codec->channels,
                                                            stream_output->codec->sample_rate, stream_input->codec->sample_rate,
                                                            stream_output->codec->sample_fmt, stream_input->codec->sample_fmt,
                                                            16, 10, 0, 0.8);
                }
                break;
            default:
                break;
        }
    }

    // Open output file
    if (avio_open(&format_output_context->pb, argv[2], URL_WRONLY) < 0) {
        fprintf(stderr, "Cannot open output file\n");
        return 1;
    }

    // Allocate the stream private data and write the stream header to an output media file.
    if (avformat_write_header(format_output_context, NULL) < 0) {
        fprintf(stderr, "Cannot write format headers\n");
        return 1;
    }

    av_dump_format(format_output_context, 0, argv[2], 1);




    // TRANSCODE //
    // Initialize an AVFifoBuffer.
    audio_fifo = av_fifo_alloc(65536);

    // Allocate an AVFrame and set its fields to default values.
    frame = avcodec_alloc_frame();

    // Initialize optional fields of a packet with default values.
    av_init_packet(&packet);



    // Return the next frame of a stream.
    while (av_read_frame(format_input_context, &packet) >= 0) {
        stream_input = format_input_context->streams[packet.stream_index];
        stream_output = format_output_context->streams[packet.stream_index];

        switch (stream_input->codec->codec_type) {
            case CODEC_TYPE_VIDEO:
                // Decode the video frame of size avpkt->size from avpkt->data into picture.
                len = avcodec_decode_video2(stream_input->codec, frame, &got_frame, &packet);
                if (len < 0) {
                    fprintf(stderr, "Cannot decode video packet\n");
                    return 1;
                }
                if (!got_frame) {
                    av_free_packet(&packet);
                    continue;
                }

                // TODO: format and size conversion -> sws context
                // TODO: frame init set by user:
                //          interlaced_frame
                //          mb_type
                //          motion_val
                //          pan_scan
                //          pts (MUST)
                //          ref_index
                //          top_field_first

                if (packet.pts == AV_NOPTS_VALUE) {
                    fprintf(stderr, "Video packet.pts == AV_NOPTS_VALUE");
                    return 1;
                }
                frame->pts = av_rescale_q(packet.pts, stream_input->time_base, stream_output->codec->time_base);

                // Free a packet.
                av_free_packet(&packet);


                // Encode a video frame into buffer.
                len = avcodec_encode_video(stream_output->codec, buffer, sizeof(buffer), frame);
                if (len < 0) {
                    fprintf(stderr, "Cannot encode video packet\n");
                    return 1;
                }

                av_init_packet(&packet);
                // Calculate packet presentation timestamp
                if (stream_output->codec->coded_frame->pts != AV_NOPTS_VALUE)
                    packet.pts = av_rescale_q(stream_output->codec->coded_frame->pts, stream_output->codec->time_base, stream_output->time_base);
                if(stream_output->codec->coded_frame->key_frame)
                    packet.flags |= AV_PKT_FLAG_KEY;
                packet.stream_index = stream_output->index;
                packet.size = len;
                packet.data = buffer;
                // Write a packet to an output media file ensuring correct interleaving.
                if (av_interleaved_write_frame(format_output_context, &packet) < 0) {
                    fprintf(stderr, "Cannot write video frame\n");
                    return 1;
                }
                break;
            case CODEC_TYPE_AUDIO:
                // Calculate encoder frame bytes
                sample_fmt_output_size = av_get_bytes_per_sample(stream_output->codec->sample_fmt);
                frame_bytes_output = stream_output->codec->frame_size * sample_fmt_output_size * stream_output->codec->channels;

                got_frame = sizeof(samples);
                // Decode the audio frame of size avpkt->size from avpkt->data into samples.
                len = avcodec_decode_audio3(stream_input->codec, samples, &got_frame, &packet);
                if (!got_frame || len < 0) {
                    fprintf(stderr, "Cannot decode audio packet\n");
                    return 1;
                }
                // Free a packet.
                av_free_packet(&packet);

                // Audio resample
                if (resample_context != NULL) {
                    len = audio_resample(resample_context, resamples, samples, got_frame / (stream_input->codec->channels * av_get_bytes_per_sample(stream_input->codec->sample_fmt))); // got_frame/2 = number of frames, depends on format *channels: got_frames / (dec->channels * sample size)
                    got_frame = len * stream_output->codec->channels * sample_fmt_output_size;
                    tmp = (uint8_t *)resamples;
                }
                else
                    tmp = (uint8_t *)samples;

                // Write all samples to an audio fifo
                av_fifo_generic_write(audio_fifo, tmp, got_frame, NULL);


                // Encode all available frames
                while (av_fifo_size(audio_fifo) >= frame_bytes_output) {
                    // Read a frame from the fifo
                    av_fifo_generic_read(audio_fifo, samples, frame_bytes_output, NULL);

                    // Encode an audio frame from samples into buffer.
                    len = avcodec_encode_audio(stream_output->codec, buffer, sizeof(buffer), samples);
                    if (len < 0) {
                        fprintf(stderr, "Cannot encode audio packet");
                        return 1;
                    }

                    av_init_packet(&packet);
                    // Calculate packet presentation timestamp
                    if (stream_output->codec->coded_frame->pts != AV_NOPTS_VALUE)
                        packet.pts = av_rescale_q(stream_output->codec->coded_frame->pts, stream_output->codec->time_base, stream_output->time_base);
                    packet.flags |= AV_PKT_FLAG_KEY;
                    packet.stream_index = stream_output->index;
                    packet.size = len;
                    packet.data = buffer;
                    // Write a packet to an output media file ensuring correct interleaving.
                    if (av_interleaved_write_frame(format_output_context, &packet) < 0) {
                        fprintf(stderr, "Cannot write audio frame\n");
                        return 1;
                    }
                }
                break;
            default:
                break;
        }

    }

    // Free a memory block.
    av_free(frame);


    // Write the stream trailer to an output media file and free the file private data.
    if (av_write_trailer(format_output_context) < 0) {
        fprintf(stderr, "Cannot write format trailer\n");
        return 1;
    }

    // Close output file
    if (avio_close(format_output_context->pb) < 0) {
        fprintf(stderr, "Cannot close output file\n");
        return 1;
    }

    // Close all output codecs
    for (stream_idx = 0; stream_idx < format_output_context->nb_streams; stream_idx++) {
        stream_output = format_output_context->streams[stream_idx];
        av_freep(&stream_output->codec->stats_in);
        avcodec_close(stream_output->codec);
    }

    // Free an AVFormatContext and all its streams.
    avformat_free_context(format_output_context);

    // Close all input codecs
    for (stream_idx = 0; stream_idx < format_input_context->nb_streams; stream_idx++) {
        stream_input = format_input_context->streams[stream_idx];
        av_freep(&stream_input->codec->stats_in);
        avcodec_close(stream_input->codec);
    }

    // Close a media file (but not its codecs).
    av_close_input_file(format_input_context);

    return 0;
}
