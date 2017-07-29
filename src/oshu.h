#pragma once

#include <libavformat/avformat.h>
#include <SDL2/SDL.h>

#define oshu_log_verbose(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define oshu_log_debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define oshu_log_info(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define oshu_log_warn(...) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define oshu_log_error(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define oshu_log_critical(...) SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)

struct oshu_audio_stream {
	AVFormatContext *demuxer;
	AVCodec *codec;
	int stream_id;
	AVCodecContext *decoder;
	SDL_AudioDeviceID device_id;
	SDL_AudioSpec device_spec;
	AVFrame *frame;
	int64_t relative_timestamp;
	int sample_index;
	int finished;
};

void oshu_audio_init();
int oshu_audio_open(const char *url, struct oshu_audio_stream **stream);
void oshu_audio_play(struct oshu_audio_stream *stream);
double oshu_audio_position(struct oshu_audio_stream *stream);
void oshu_audio_close(struct oshu_audio_stream **stream);
