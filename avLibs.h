#pragma once
//{{{  includes
extern "C" {
  #include <libavutil/opt.h>
  #include <libavutil/mem.h>
  #include <libavutil/crc.h>
  #include <libavutil/fifo.h>
  #include <libavutil/avutil.h>

  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libswresample/swresample.h>
  }
//}}}

#define AVSEEK_FORCE 0x20000
#define AVFRAME_IN_LAVU

typedef int64_t offset_t;

//{{{
class cAvUtil {
public:
  virtual ~cAvUtil() {}

  void av_log_set_callback(void (*foo)(void*, int, const char*, va_list)) { ::av_log_set_callback(foo); }

  void *av_malloc(unsigned int size) { return ::av_malloc(size); }
  void *av_mallocz(unsigned int size) { return ::av_mallocz(size); }
  void *av_realloc(void *ptr, unsigned int size) { return ::av_realloc(ptr, size); }

  void av_free(void *ptr) { ::av_free(ptr); }
  void av_freep(void *ptr) { ::av_freep(ptr); }

  int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVRounding d) { return ::av_rescale_rnd(a, b, c, d); }
  int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq) { return ::av_rescale_q(a, bq, cq); }

  int av_crc_init(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size) { return ::av_crc_init(ctx, le, bits, poly, ctx_size); }
  const AVCRC* av_crc_get_table(AVCRCId crc_id) { return ::av_crc_get_table(crc_id); }
  uint32_t av_crc(const AVCRC *ctx, uint32_t crc, const uint8_t *buffer, size_t length) { return ::av_crc(ctx, crc, buffer, length); }

  int av_opt_set(void *obj, const char *name, const char *val, int search_flags) { return ::av_opt_set(obj, name, val, search_flags); }
  int av_opt_set_double(void *obj, const char *name, double val, int search_flags) { return ::av_opt_set_double(obj, name, val, search_flags); }
  int av_opt_set_int(void *obj, const char *name, int64_t val, int search_flags) { return ::av_opt_set_int(obj, name, val, search_flags); }

  AVFifoBuffer *av_fifo_alloc(unsigned int size) {return ::av_fifo_alloc(size); }
  void av_fifo_free(AVFifoBuffer *f) { ::av_fifo_free(f); }
  void av_fifo_reset(AVFifoBuffer *f) { ::av_fifo_reset(f); }
  int av_fifo_size(AVFifoBuffer *f) { return ::av_fifo_size(f); }
  int av_fifo_generic_read(AVFifoBuffer *f, void *dest, int buf_size, void (*func)(void*, void*, int))
    { return ::av_fifo_generic_read(f, dest, buf_size, func); }
  int av_fifo_generic_write(AVFifoBuffer *f, void *src, int size, int (*func)(void*, void*, int))
    { return ::av_fifo_generic_write(f, src, size, func); }
  char *av_strdup(const char *s) { return ::av_strdup(s); }
  int av_get_bytes_per_sample(enum AVSampleFormat p1)
    { return ::av_get_bytes_per_sample(p1); }

  AVDictionaryEntry *av_dict_get(AVDictionary *m, const char *key, const AVDictionaryEntry *prev, int flags){ return ::av_dict_get(m, key, prev, flags); }
  int av_dict_set(AVDictionary **pm, const char *key, const char *value, int flags) { return ::av_dict_set(pm, key, value, flags); }
  int av_dict_parse_string(AVDictionary **pm, const char *str, const char *key_val_sep, const char *pairs_sep, int flags) { return ::av_dict_parse_string(pm, str, key_val_sep, pairs_sep, flags); }
  void av_dict_free(AVDictionary **pm) { ::av_dict_free(pm); }

  int av_samples_get_buffer_size (int *linesize, int nb_channels, int nb_samples, enum AVSampleFormat sample_fmt, int align)
    { return ::av_samples_get_buffer_size(linesize, nb_channels, nb_samples, sample_fmt, align); }

  int64_t av_get_default_channel_layout(int nb_channels) { return ::av_get_default_channel_layout(nb_channels); }
  void av_log_set_level(int level) { ::av_log_set_level(level); };
  int av_samples_alloc(uint8_t **audio_data, int *linesize, int nb_channels, int nb_samples, enum AVSampleFormat sample_fmt, int align)
    { return ::av_samples_alloc(audio_data, linesize, nb_channels, nb_samples, sample_fmt, align); }

  int av_sample_fmt_is_planar(enum AVSampleFormat sample_fmt) { return ::av_sample_fmt_is_planar(sample_fmt); }
  int av_get_channel_layout_channel_index (uint64_t channel_layout, uint64_t channel) { return ::av_get_channel_layout_channel_index(channel_layout, channel); }
  int av_samples_fill_arrays(uint8_t **audio_data, int *linesize, const uint8_t *buf, int nb_channels, int nb_samples, enum AVSampleFormat sample_fmt, int align)
    { return ::av_samples_fill_arrays(audio_data, linesize, buf, nb_channels, nb_samples, sample_fmt, align); }
  int av_samples_copy(uint8_t **dst, uint8_t *const *src, int dst_offset, int src_offset, int nb_samples, int nb_channels, enum AVSampleFormat sample_fmt)
    { return ::av_samples_copy(dst, src, dst_offset, src_offset, nb_samples, nb_channels, sample_fmt); }

#if defined(AVFRAME_IN_LAVU)
  void av_frame_free(AVFrame **frame) { return ::av_frame_free(frame); }
  AVFrame *av_frame_alloc() { return ::av_frame_alloc(); }
  void av_frame_unref(AVFrame *frame) { return ::av_frame_unref(frame); }
  void av_frame_move_ref(AVFrame *dst, AVFrame *src) { return ::av_frame_move_ref(dst,src); }
#endif
  };
//}}}
//{{{
class cAvFormat {
public:
  virtual ~cAvFormat() {}

  //{{{
  void av_register_all()
  {
    return ::av_register_all();
  }
  //}}}
  void av_register_all_dont_call() { *(volatile int* )0x0 = 0; }
  AVInputFormat *av_find_input_format(const char *short_name) { return ::av_find_input_format(short_name); }
  int url_feof(AVIOContext *s) { return ::url_feof(s); }
  void avformat_close_input(AVFormatContext **s) { ::avformat_close_input(s); }
  int av_read_frame(AVFormatContext *s, AVPacket *pkt) { return ::av_read_frame(s, pkt); }
  int av_read_play(AVFormatContext *s) { return ::av_read_play(s); }
  int av_read_pause(AVFormatContext *s) { return ::av_read_pause(s); }
  int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp, int flags) { return ::av_seek_frame(s, stream_index, timestamp, flags); }
  //{{{
  int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options)
  {
    return ::avformat_find_stream_info(ic, options);
  }
  //}}}
  //{{{
  int avformat_open_input(AVFormatContext **ps, const char *filename, AVInputFormat *fmt, AVDictionary **options)
  { return ::avformat_open_input(ps, filename, fmt, options); }
  //}}}
  //{{{
  AVIOContext *avio_alloc_context(unsigned char *buffer, int buffer_size, int write_flag, void *opaque,
                            int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                            int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                            offset_t (*seek)(void *opaque, offset_t offset, int whence)) { return ::avio_alloc_context(buffer, buffer_size, write_flag, opaque, read_packet, write_packet, seek); }
  //}}}
  AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened) {return ::av_probe_input_format(pd, is_opened); }
  AVInputFormat *av_probe_input_format2(AVProbeData *pd, int is_opened, int *score_max) {*score_max = 100; return ::av_probe_input_format(pd, is_opened); } // Use av_probe_input_format, this is not exported by ffmpeg's headers
  int av_probe_input_buffer(AVIOContext *pb, AVInputFormat **fmt, const char *filename, void *logctx, unsigned int offset, unsigned int max_probe_size) { return ::av_probe_input_buffer(pb, fmt, filename, logctx, offset, max_probe_size); }
  void av_dump_format(AVFormatContext *ic, int index, const char *url, int is_output) { ::av_dump_format(ic, index, url, is_output); }
  int avio_open(AVIOContext **s, const char *filename, int flags) { return ::avio_open(s, filename, flags); }
  int avio_close(AVIOContext *s) { return ::avio_close(s); }
  int avio_open_dyn_buf(AVIOContext **s) { return ::avio_open_dyn_buf(s); }
  int avio_close_dyn_buf(AVIOContext *s, uint8_t **pbuffer) { return ::avio_close_dyn_buf(s, pbuffer); }
  offset_t avio_seek(AVIOContext *s, offset_t offset, int whence) { return ::avio_seek(s, offset, whence); }
  int avio_read(AVIOContext *s, unsigned char *buf, int size) { return ::avio_read(s, buf, size); }
  void avio_w8(AVIOContext *s, int b) { ::avio_w8(s, b); }
  void avio_write(AVIOContext *s, const unsigned char *buf, int size) { ::avio_write(s, buf, size); }
  void avio_wb24(AVIOContext *s, unsigned int val) { ::avio_wb24(s, val); }
  void avio_wb32(AVIOContext *s, unsigned int val) { ::avio_wb32(s, val); }
  void avio_wb16(AVIOContext *s, unsigned int val) { ::avio_wb16(s, val); }
  AVFormatContext *avformat_alloc_context() { return ::avformat_alloc_context(); }
  //{{{
  int av_set_options_string(AVFormatContext *ctx, const char *opts,
            const char *key_val_sep, const char *pairs_sep) { return ::av_set_options_string(ctx, opts, key_val_sep, pairs_sep); }
  //}}}
  AVStream *avformat_new_stream(AVFormatContext *s, AVCodec *c) { return ::avformat_new_stream(s, c); }
  AVOutputFormat *av_guess_format(const char *short_name, const char *filename, const char *mime_type) { return ::av_guess_format(short_name, filename, mime_type); }
  int avformat_write_header (AVFormatContext *s, AVDictionary **options) { return ::avformat_write_header (s, options); }
  int av_write_trailer(AVFormatContext *s) { return ::av_write_trailer(s); }
  int av_write_frame  (AVFormatContext *s, AVPacket *pkt) { return ::av_write_frame(s, pkt); }
  int avformat_network_init  (void) { return ::avformat_network_init(); }
  int avformat_network_deinit  (void) { return ::avformat_network_deinit(); }
  };
//}}}
//{{{
class cAvCodec {
public:
  virtual ~cAvCodec() {}

  //{{{
  void avcodec_register_all() {
    ::avcodec_register_all();
  }
  //}}}
  void avcodec_flush_buffers(AVCodecContext *avctx) { ::avcodec_flush_buffers(avctx); }
  //{{{
  int avcodec_open2(AVCodecContext *avctx, AVCodec *codec, AVDictionary **options) {
    return ::avcodec_open2(avctx, codec, options);
  }
  //}}}
  int avcodec_open2_dont_call(AVCodecContext *avctx, AVCodec *codec, AVDictionary **options) { *(int *)0x0 = 0; return 0; }
  int avcodec_close_dont_call(AVCodecContext *avctx) { *(int *)0x0 = 0; return 0; }
  AVCodec *avcodec_find_decoder(enum AVCodecID id) { return ::avcodec_find_decoder(id); }
  AVCodec *avcodec_find_encoder(enum AVCodecID id) { return ::avcodec_find_encoder(id); }
  //{{{
  int avcodec_close(AVCodecContext *avctx) {
    return ::avcodec_close(avctx);
  }
  //}}}
  AVFrame *av_frame_alloc() { return ::av_frame_alloc(); }
  int avpicture_fill(AVPicture *picture, uint8_t *ptr, AVPixelFormat pix_fmt, int width, int height) { return ::avpicture_fill(picture, ptr, pix_fmt, width, height); }
  int avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture, int *got_picture_ptr, AVPacket *avpkt) { return ::avcodec_decode_video2(avctx, picture, got_picture_ptr, avpkt); }
  int avcodec_decode_audio4(AVCodecContext *avctx, AVFrame *frame, int *got_frame_ptr, AVPacket *avpkt) { return ::avcodec_decode_audio4(avctx, frame, got_frame_ptr, avpkt); }
  int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub, int *got_sub_ptr, AVPacket *avpkt) { return ::avcodec_decode_subtitle2(avctx, sub, got_sub_ptr, avpkt); }
  int avcodec_encode_audio2(AVCodecContext *avctx, AVPacket *avpkt, const AVFrame *frame, int *got_packet_ptr) { return ::avcodec_encode_audio2(avctx, avpkt, frame, got_packet_ptr); }
  int avpicture_get_size(AVPixelFormat pix_fmt, int width, int height) { return ::avpicture_get_size(pix_fmt, width, height); }
  AVCodecContext *avcodec_alloc_context3(AVCodec *codec) { return ::avcodec_alloc_context3(codec); }
  void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode) { ::avcodec_string(buf, buf_size, enc, encode); }
  void avcodec_get_context_defaults3(AVCodecContext *s, AVCodec *codec) { ::avcodec_get_context_defaults3(s, codec); }

  AVCodecParserContext *av_parser_init(int codec_id) { return ::av_parser_init(codec_id); }
  //{{{
  int av_parser_parse2(AVCodecParserContext *s,AVCodecContext *avctx, uint8_t **poutbuf, int *poutbuf_size,
                    const uint8_t *buf, int buf_size,
                    int64_t pts, int64_t dts, int64_t pos)
  {
    return ::av_parser_parse2(s, avctx, poutbuf, poutbuf_size, buf, buf_size, pts, dts, pos);
  }
  //}}}
  void av_parser_close(AVCodecParserContext *s) { ::av_parser_close(s); }

  AVBitStreamFilterContext *av_bitstream_filter_init(const char *name) { return ::av_bitstream_filter_init(name); }
  //{{{
  int av_bitstream_filter_filter(AVBitStreamFilterContext *bsfc,
    AVCodecContext *avctx, const char *args,
    uint8_t **poutbuf, int *poutbuf_size,
    const uint8_t *buf, int buf_size, int keyframe) { return ::av_bitstream_filter_filter(bsfc, avctx, args, poutbuf, poutbuf_size, buf, buf_size, keyframe); }
  //}}}
  void av_bitstream_filter_close(AVBitStreamFilterContext *bsfc) { ::av_bitstream_filter_close(bsfc); }

  void avpicture_free(AVPicture *picture) { ::avpicture_free(picture); }
  void av_free_packet(AVPacket *pkt) { ::av_free_packet(pkt); }
  int avpicture_alloc(AVPicture *picture, AVPixelFormat pix_fmt, int width, int height) { return ::avpicture_alloc(picture, pix_fmt, width, height); }
  int avcodec_default_get_buffer2(AVCodecContext *s, AVFrame *pic, int flags) { return ::avcodec_default_get_buffer2(s, pic, flags); }
  enum AVPixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum AVPixelFormat *fmt) { return ::avcodec_default_get_format(s, fmt); }
  AVCodec *av_codec_next(AVCodec *c) { return ::av_codec_next(c); }

  int av_dup_packet(AVPacket *pkt) { return ::av_dup_packet(pkt); }
  void av_init_packet(AVPacket *pkt) { return ::av_init_packet(pkt); }
  };
//}}}
//{{{
class cSwResample {
public:
  virtual ~cSwResample() {}

  virtual struct SwrContext *swr_alloc_set_opts (struct SwrContext *s, int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate, int64_t in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate, int log_offset, void *log_ctx) { return ::swr_alloc_set_opts(s, out_ch_layout, out_sample_fmt, out_sample_rate, in_ch_layout, in_sample_fmt, in_sample_rate, log_offset, log_ctx); }
  virtual int swr_init (struct SwrContext *s) { return ::swr_init(s); }
  virtual void swr_free (struct SwrContext **s){ return ::swr_free(s); }
  virtual int swr_convert (struct SwrContext *s, uint8_t **out, int out_count, const uint8_t **in , int in_count){ return ::swr_convert(s, out, out_count, in, in_count); }
  };
//}}}
