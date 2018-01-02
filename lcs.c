/*
 * Copyright (c) 2017 Steven Liu <lq@Chinaffmpeg.org>
 * Copyright (c) 2017 OnVideo.cn
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: %lld, pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d KEY = %d\n",
           tag, pkt->pts,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index, pkt->flags);
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int i = 0;
    int init_open = 0;
    char *input_filename = NULL;
    char *output_filename = NULL;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
    static int64_t pts = 0;
    static int64_t dts = 0;
    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;
    AVOutputFormat *ofmt = NULL;
    AVPacket pkt;
    AVPacket opkt;

    if (argc < 3) {
        fprintf(stderr, "usage: %s [-refcount] input_file output_file\n", argv[0]);
        exit(1);
    }

    input_filename = argv[1];
    output_filename = argv[2];

    av_register_all();

    /* output AVFormatContext */
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_filename);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        return ret;
    }

    int a = 0;
    while(a < 10) {
        a++;
        /* input AVFormatContext  */
        if (avformat_open_input(&ifmt_ctx, input_filename, NULL, NULL) < 0) {
            fprintf(stderr, "Could not open source file %s\n", input_filename);
            exit(1);
        }

        if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
            fprintf(stderr, "Could not find stream information\n");
            exit(1);
        }
        av_dump_format(ifmt_ctx, 0, input_filename, 0);

        /* only open once, at the first time, map input stream and output stream */
        if (!init_open) {
            /* processing output streams */
            stream_mapping_size = ifmt_ctx->nb_streams;
            stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
            if (!stream_mapping) {
                ret = AVERROR(ENOMEM);
                return ret;;
            }
            ofmt = ofmt_ctx->oformat;
            for (i = 0; i < ifmt_ctx->nb_streams; i++) {
                AVStream *out_stream;
                AVCodecParameters *in_codecpar = ifmt_ctx->streams[i]->codecpar;

                if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
                    in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
                    in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
                    stream_mapping[i] = -1;
                    continue;
                }

                stream_mapping[i] = stream_index++;

                out_stream = avformat_new_stream(ofmt_ctx, NULL);
                if (!out_stream) {
                    fprintf(stderr, "Failed allocating output stream\n");
                    ret = AVERROR_UNKNOWN;
                    goto end;
                }

                ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
                if (ret < 0) {
                    fprintf(stderr, "Failed to copy codec parameters\n");
                    goto end;
                }
                out_stream->codecpar->codec_tag = 0;
            }
            av_dump_format(ofmt_ctx, 0, output_filename, 1);
            ret = avio_open(&ofmt_ctx->pb, output_filename, AVIO_FLAG_WRITE);
            if (ret < 0) {
                fprintf(stderr, "Could not open output file '%s'", output_filename);
                goto end;
            }
            ret = avformat_write_header(ofmt_ctx, NULL);
            if (ret < 0) {
                fprintf(stderr, "Error occurred when opening output file\n");
                goto end;
            }
            init_open++;
        }
        while (1) {
            AVStream *in_stream, *out_stream;

            ret = av_read_frame(ifmt_ctx, &pkt);
            if (ret < 0)
                goto error_close_input;

            in_stream  = ifmt_ctx->streams[pkt.stream_index];
            if (pkt.stream_index >= stream_mapping_size ||
                stream_mapping[pkt.stream_index] < 0) {
                av_packet_unref(&pkt);
                continue;
            }

            opkt.stream_index = stream_mapping[pkt.stream_index];
            out_stream = ofmt_ctx->streams[pkt.stream_index];
            log_packet(ifmt_ctx, &pkt, "in");

            if (pkt.stream_index == 1) {
                    pkt.pts = pts + pkt.duration;
                    pkt.dts = dts + pkt.duration;
            } else {
                    pkt.dts = dts + pkt.duration;
                    pkt.pts = pts + pkt.duration;
            }
            pts = pkt.pts;
            dts = pkt.dts;
            log_packet(ifmt_ctx, &pkt, "MOD");
            av_packet_rescale_ts(&pkt,
                                 ifmt_ctx->streams[pkt.stream_index]->time_base,
                                 ofmt_ctx->streams[pkt.stream_index]->time_base);
            log_packet(ofmt_ctx, &pkt, "out");

            ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
            if (ret < 0) {
                fprintf(stderr, "Error muxing packet\n");
                break;
            }
            av_packet_unref(&pkt);
        }

error_close_input:
        avformat_close_input(&ifmt_ctx);
    }

end:
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_close_input(&ofmt_ctx);
    avformat_free_context(ofmt_ctx);
    av_freep(&stream_mapping);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}

