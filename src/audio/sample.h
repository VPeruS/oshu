/**
 * \file audio/sample.h
 * \ingroup audio_sample
 */

#pragma once

#include <stdint.h>

struct SDL_AudioSpec;

/**
 * \defgroup audio_sample Sample
 *
 * \brief
 * Load WAV files using SDL's WAV loader in order to play them over an audio
 * stream as sound effects.
 *
 * A sample is a short sound played when the user hits something. To be fast
 * and reactive, the samples are always stored in-memory. Do not confuse it
 * with a PCM sample.
 *
 * If need be, samples could be loaded using the #audio_stream module and
 * slurped into memory through #oshu_read_stream. However, since samples are
 * always WAV anyway, let's start with a naive implementation using SDL's
 * procedures.
 *
 * \{
 */

/**
 * An in-memory audio sample.
 *
 * For consistency accross the audio modules, samples are always stored as
 * packed 32-bit floats. The sample rate is defined when loading the sample.
 *
 * \sa oshu_load_sample
 * \sa oshu_destroy_sample
 */
struct oshu_sample {
	/**
	 * The PCM samples, as described in #oshu_sample.
	 *
	 * The array should contain exactly #size bytes.
	 */
	float *samples;
	/**
	 * The size of the #samples buffer.
	 *
	 * It must be at least 2 × #length × sizeof(float).
	 */
	uint32_t size;
	/**
	 * The length of the sample, in samples per channel.
	 */
	uint32_t length;
};

/**
 * Open a WAV file and store its samples into a dynamically allocated buffer.
 *
 * The specs of the SDL audio output device is required in order to convert the
 * samples into the apprioriate format, which is why the *spec* argument is
 * required. Note that this means the sample will be specific to that stream
 * only.
 *
 * \param path Path to the WAV file to load.
 * \param spec Target format of the sample. Must be stereo float samples.
 * \param sample An allocated #oshu_sample, initialized or not.
 *
 * \sa oshu_destroy_sample
 */
int oshu_load_sample(const char *path, struct SDL_AudioSpec *spec, struct oshu_sample *sample);

/**
 * Free the sample's PCM samples buffer.
 *
 * The sample object is left in an unspecified state.
 */
void oshu_destroy_sample(struct oshu_sample *sample);


/** \} */
