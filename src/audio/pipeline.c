/**
 * \file audio/pipeline.c
 * \ingroup audio
 *
 * \brief
 * Manage the audio pipeline, from ffmpeg input to SDL output.
 */

#include "audio/audio.h"
#include "log.h"

#include <assert.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/time.h>

/**
 * Size of the SDL audio buffer, in samples.
 * The smaller it is, the less lag.
 */
static const int sample_buffer_size = 1024;

void oshu_audio_init()
{
	av_register_all();
	avfilter_register_all();
}

/**
 * Log some helpful information about the decoded audio stream.
 * Meant for debugging more than anything else.
 */
static void dump_stream_info(struct oshu_audio *audio)
{
	struct oshu_stream *stream = &audio->source;
	oshu_log_info("============ Audio information ============");
	oshu_log_info("            Codec: %s.", stream->codec->long_name);
	oshu_log_info("      Sample rate: %d Hz.", stream->decoder->sample_rate);
	oshu_log_info(" Average bit rate: %ld kbps.", stream->decoder->bit_rate / 1000);
	oshu_log_info("    Sample format: %s.", av_get_sample_fmt_name(stream->decoder->sample_fmt));
	oshu_log_info("         Duration: %d seconds.", (int) (stream->stream->duration * stream->time_base));
}

/**
 * Decode a frame.
 *
 * Update #oshu_audio::current_timestamp and reset #oshu_audio::sample_index.
 *
 * Mark the stream as finished on EOF or on ERROR, meaning you must not call
 * this function anymore.
 */
static int next_frame(struct oshu_audio *audio)
{
	int rc = oshu_next_frame(&audio->source);
	if (rc < 0) {
		rc = av_buffersrc_write_frame(audio->pipeline.music, NULL);
		assert (rc == 0);
	} else {
		rc = av_buffersrc_write_frame(audio->pipeline.music, audio->source.frame);
		assert (rc == 0);
		int64_t ts = audio->source.frame->best_effort_timestamp;
		if (ts > 0)
			audio->current_timestamp = audio->source.time_base * ts;
	}
	return 0;
}

static void feed(struct oshu_audio *audio)
{
	if (av_buffersrc_get_nb_failed_requests(audio->pipeline.music) > 0) {
		next_frame(audio);
	}
	if (av_buffersrc_get_nb_failed_requests(audio->pipeline.effect) > 0) {
		if (audio->overlay) {
			/*av_buffersrc_write_frame(audio->pipeline.effect, audio->overlay->frame);*/
			audio->overlay = NULL;
		} else {
			av_buffersrc_write_frame(audio->pipeline.effect, NULL);
		}
	}
}

/**
 * Fill the audio buffer with the song data, then optionally add a sample.
 */
static void audio_callback(void *userdata, Uint8 *buffer, int len)
{
	struct oshu_audio *audio;
	audio = (struct oshu_audio*) userdata;
	int rc;
	while (!audio->finished) {
		rc = av_buffersink_get_frame(audio->pipeline.sink, audio->pipeline.output);
		if (rc == AVERROR(EAGAIN)) {
			feed(audio);
			continue;
		} else if (rc == AVERROR_EOF) {
			audio->finished = 1;
			break;
		} else if (rc == 0) {
			memcpy(buffer, audio->pipeline.output->data[0], len);
			return;
		} else {
			oshu_av_error(rc);
			break;
		}
	}
	memset(buffer, len, audio->device_spec.silence);
}

/**
 * Initialize the SDL audio device.
 * \return 0 on success, -1 on error.
 */
static int open_device(struct oshu_audio *audio)
{
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = audio->source.decoder->sample_rate;
	want.format = AUDIO_F32;
	want.channels = audio->source.decoder->channels;
	want.samples = sample_buffer_size * want.channels;
	want.callback = audio_callback;
	want.userdata = (void*) audio;
	audio->device_id = SDL_OpenAudioDevice(NULL, 0, &want, &audio->device_spec, 0);
	if (audio->device_id == 0) {
		oshu_log_error("failed to open the audio device: %s", SDL_GetError());
		return -1;
	}
	return 0;
}

static int create_graph(struct oshu_audio *audio)
{
	int rc;
	struct oshu_pipeline *p = &audio->pipeline;
	p->graph = avfilter_graph_alloc();
	assert (p->graph);

	AVFilter *abuffer = avfilter_get_by_name("abuffer");
	assert (abuffer);
	p->music = avfilter_graph_alloc_filter(p->graph, abuffer, "music");
	assert (p->music);
	AVBufferSrcParameters *music_params = av_buffersrc_parameters_alloc();
	assert (music_params);
	music_params->format = audio->source.decoder->sample_fmt;
	music_params->time_base = audio->source.stream->time_base;
	music_params->channel_layout = audio->source.decoder->channel_layout;
	music_params->sample_rate = audio->source.decoder->sample_rate;
	if ((rc = av_buffersrc_parameters_set(p->music, music_params)))
		goto fail;
	av_free(music_params);
	if ((rc = avfilter_init_str(p->music, NULL)) < 0)
		goto fail;
	oshu_log_debug("music source ready");

	p->effect = avfilter_graph_alloc_filter(p->graph, abuffer, "effect");
	assert (p->effect);
	AVBufferSrcParameters *effect_params = av_buffersrc_parameters_alloc();
	assert (effect_params);
	effect_params->format = AV_SAMPLE_FMT_FLT;
	music_params->time_base = audio->source.stream->time_base;
	effect_params->channel_layout = AV_CH_LAYOUT_STEREO;
	effect_params->sample_rate = audio->device_spec.freq;
	if ((rc = av_buffersrc_parameters_set(p->effect, effect_params)))
		goto fail;
	av_free(effect_params);
	if ((rc = avfilter_init_str(p->effect, NULL)) < 0)
		goto fail;
	oshu_log_debug("effect source ready");

	AVFilter *amix = avfilter_get_by_name("amix");
	assert (amix);
	oshu_log_debug("got the amix filter");
	p->mixer = avfilter_graph_alloc_filter(p->graph, amix, "mixer");
	assert (p->mixer);
	oshu_log_debug("allocated the mixer");
	if ((rc = avfilter_init_str(p->mixer, "inputs=2:duration=first")) < 0)
		goto fail;
	oshu_log_debug("mixer ready");

	AVFilter *aformat = avfilter_get_by_name("aformat");
	assert (aformat);
	p->converter = avfilter_graph_alloc_filter(p->graph, aformat, "converter");
	assert (p->converter);
	if ((rc = avfilter_init_str(p->converter, "sample_fmts=flt")) < 0)
		goto fail;
	oshu_log_debug("converter ready");

	AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
	assert (abuffersink);
	p->sink = avfilter_graph_alloc_filter(p->graph, abuffersink, "sink");
	assert (p->sink);
	if ((rc = avfilter_init_str(p->sink, NULL)) < 0)
		goto fail;
	oshu_log_debug("sink ready");

	if ((rc = avfilter_link(p->music, 0, p->mixer, 0)) < 0)
		goto fail;
	if ((rc = avfilter_link(p->effect, 0, p->mixer, 1)) < 0)
		goto fail;
	if ((rc = avfilter_link(p->mixer, 0, p->converter, 0)) < 0)
		goto fail;
	if ((rc = avfilter_link(p->converter, 0, p->sink, 0)) < 0)
		goto fail;
	oshu_log_debug("links ready");

	if ((rc = avfilter_graph_config(p->graph, NULL)) < 0)
		goto fail;
	oshu_log_debug("graph ready");

	av_buffersink_set_frame_size(p->sink, sample_buffer_size);

	p->output = av_frame_alloc();
	assert (p->output);

	return 0;

fail:
	oshu_av_error(rc);
	return -1;
}

int oshu_audio_open(const char *url, struct oshu_audio **audio)
{
	*audio = calloc(1, sizeof(**audio));
	if (*audio == NULL) {
		oshu_log_error("could not allocate the audio context");
		return -1;
	}
	if (oshu_open_stream(url, &(*audio)->source) < 0)
		goto fail;
	if (open_device(*audio) < 0)
		goto fail;
	if (create_graph(*audio) < 0)
		goto fail;
	dump_stream_info(*audio);
	return 0;
fail:
	oshu_audio_close(audio);
	return -1;
}

void oshu_audio_play(struct oshu_audio *audio)
{
	SDL_PauseAudioDevice(audio->device_id, 0);
}

void oshu_audio_pause(struct oshu_audio *audio)
{
	SDL_PauseAudioDevice(audio->device_id, 1);
}

void oshu_audio_close(struct oshu_audio **audio)
{
	if (*audio == NULL)
		return;
	if ((*audio)->device_id)
		SDL_CloseAudioDevice((*audio)->device_id);
	oshu_close_stream(&(*audio)->source);
	free(*audio);
	*audio = NULL;
}
