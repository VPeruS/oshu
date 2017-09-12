/**
 * \file stream/stream.c
 * \ingroup audio
 *
 * \brief
 * Open and decode an stream file.
 */

#include "audio/audio.h"
#include "log.h"

void oshu_av_error(int rc)
{
	char errbuf[256];
	av_strerror(rc, errbuf, sizeof(errbuf));
	oshu_log_error("ffmpeg error: %s", errbuf);
}

/**
 * Read a page for the demuxer and feed it to the decoder.
 *
 * When reaching EOF, feed the decoder a NULL packet to flush it.
 *
 * This function is meant to be called exclusively from #next_frame,
 * because a single page may yield many codec frames.
 *
 * \return 0 on success, -1 on error.
 */
static int next_page(struct oshu_stream *stream)
{
	int rc;
	for (;;) {
		rc = av_read_frame(stream->demuxer, &stream->packet);
		if (rc == AVERROR_EOF) {
			oshu_log_debug("reached the last page, flushing");
			rc = avcodec_send_packet(stream->decoder, NULL);
			break;
		} else if (rc < 0) {
			break;
		}
		if (stream->packet.stream_index == stream->stream->index) {
			rc = avcodec_send_packet(stream->decoder, &stream->packet);
			break;
		}
		av_packet_unref(&stream->packet);
	}
	av_packet_unref(&stream->packet);
	if (rc < 0) {
		oshu_av_error(rc);
		return -1;
	}
	return 0;
}

int oshu_next_frame(struct oshu_stream *stream)
{
	for (;;) {
		int rc = avcodec_receive_frame(stream->decoder, stream->frame);
		if (rc == 0) {
			return 0;
		} else if (rc == AVERROR(EAGAIN)) {
			if (next_page(stream) < 0) {
				oshu_log_warn("abrupt end of stream");
				return -1;
			}
		} else if (rc == AVERROR_EOF) {
			oshu_log_debug("reached the last frame");
			return AVERROR_EOF;
		} else {
			oshu_av_error(rc);
			return -1;
		}
	}
}

/**
 * Open the libavformat demuxer, and find the best stream stream.
 *
 * Fill #oshu_stream::demuxer, #oshu_stream::codec, #oshu_stream::stream and
 * #oshu_stream::time_base.
 *
 * \return 0 on success, -1 on error.
 */
static int open_demuxer(const char *url, struct oshu_stream *stream)
{
	int rc = avformat_open_input(&stream->demuxer, url, NULL, NULL);
	if (rc < 0) {
		oshu_log_error("failed opening the stream file");
		goto fail;
	}
	rc = avformat_find_stream_info(stream->demuxer, NULL);
	if (rc < 0) {
		oshu_log_error("error reading the stream headers");
		goto fail;
	}
	rc = av_find_best_stream(
		stream->demuxer,
		AVMEDIA_TYPE_AUDIO,
		-1, -1,
		&stream->codec,
		0
	);
	if (rc < 0 || stream->codec == NULL) {
		oshu_log_error("error finding the best stream stream");
		goto fail;
	}
	stream->stream = stream->demuxer->streams[rc];
	stream->time_base = av_q2d(stream->stream->time_base);
	return 0;
fail:
	oshu_av_error(rc);
	return -1;
}

/**
 * Open the libavcodec decoder.
 *
 * You must call this function after #open_demuxer.
 *
 * \return 0 on success, and a negative ffmpeg error code on failure.
 */
static int open_decoder(struct oshu_stream *stream)
{
	stream->decoder = avcodec_alloc_context3(stream->codec);
	int rc = avcodec_parameters_to_context(
		stream->decoder,
		stream->stream->codecpar
	);
	if (rc < 0) {
		oshu_log_error("error copying the codec context");
		goto fail;
	}
	rc = avcodec_open2(stream->decoder, stream->codec, NULL);
	if (rc < 0) {
		oshu_log_error("error opening the codec");
		goto fail;
	}
	stream->frame = av_frame_alloc();
	if (stream->frame == NULL) {
		oshu_log_error("could not allocate the codec frame");
		goto fail;
	}
	return 0;
fail:
	oshu_av_error(rc);
	return -1;
}

int oshu_open_stream(const char *url, struct oshu_stream *stream)
{
	if (open_demuxer(url, stream) < 0)
		goto fail;
	if (open_decoder(stream) < 0)
		goto fail;
	if (oshu_next_frame(stream) < 0)
		goto fail;
	return 0;
fail:
	oshu_close_stream(stream);
	return -1;
}

void oshu_close_stream(struct oshu_stream *stream)
{
	if (stream->frame)
		av_frame_free(&stream->frame);
	if (stream->decoder)
		avcodec_free_context(&stream->decoder);
	if (stream->demuxer)
		avformat_close_input(&stream->demuxer);
}
