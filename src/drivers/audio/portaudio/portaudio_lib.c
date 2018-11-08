/**
 * @file
 *
 * @date 14.05.2016
 * @author Anton Bondarev
 */
#include <stddef.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <util/log.h>

#include <kernel/thread.h>
#include <kernel/thread/thread_sched_wait.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <time.h>
#include <mem/misc/pool.h>
#include <framework/mod/options.h>

#include <drivers/audio/portaudio.h>
#include <drivers/audio/audio_dev.h>

enum pa_state {
	PA_STREAM_RUNNING,
	PA_STREAM_STOPPED,
	PA_STREAM_CLOSED
};

struct pa_strm {
	uint8_t devid;
	uint8_t number_of_chan;
	uint32_t sample_format;
	int rate;

	struct thread *pa_thread;
	enum pa_state state;

	PaStreamCallback *callback;
	void *user_data;

	int active;
};

#define MODOPS_PA_STREAM_COUNT OPTION_GET(NUMBER, pa_stream_count)

POOL_DEF(pa_strm_pool, struct pa_strm, MODOPS_PA_STREAM_COUNT);

#if 0
static void _buf_scale(void *buf, int len1, int len2) {
	uint16_t *b16 = buf;
	if (len2 > len1) {
		for (int i = len2 - 1; i >= 0; i--) {
			b16[i] = b16[i * len1 / len2];
		}
	}
}
#endif

/**
 * @brief Duplicate left channel for the buffer
 */
static void _mono_to_stereo(void *buf, int len) {
	/* Assume data is 16-bit */
	uint16_t *b16 = buf;
	for (int i = len * 2 - 1; i >= 0; i--) {
		b16[i] = b16[i / 2];
	}
}

/**
 * @brief Remove right channel from audio buffer
 */
static void _stereo_to_mono(void *buf, int len) {
	/* Assume data is 16-bit */
	uint16_t *b16 = buf;
	for (int i = 0; i < len; i++) {
		b16[i] = b16[i * 2];
	}
}

static void pa_do_rate_convertion(struct pa_strm *pa_stream,
		struct audio_dev *audio_dev, uint8_t *buf, int inp_frames) {
	int i, len, div;
	uint32_t *buf32 = (uint32_t *) buf;
	int audio_dev_rate = audio_dev->ad_ops->ad_ops_ioctl(audio_dev,
			ADIOCTL_GET_RATE, NULL);

	printk(">>>> pa_stream->rate = %d, audio_dev_rate = %d\n",
		pa_stream->rate, audio_dev_rate);

	if (pa_stream->rate == audio_dev_rate) {
		return;
	}

	len = inp_frames;

	if (audio_dev_rate > pa_stream->rate) {
		div = audio_dev_rate / pa_stream->rate;
		assert(div > 1);

		switch (audio_dev->dir) {
		case AUDIO_DEV_OUTPUT:
			for (i = len - 1; i >= 0; i--) {
				buf32[i] = buf32[i / div];
			}
			break;
		case AUDIO_DEV_INPUT:
			for (i = 0; i < len; i++) {
				buf32[i] = buf32[div * i];
			}
			break;
		default:
			break;
		}
	}
}

/* Sort out problems related to the number of channels */
static void pa_do_channel_convertion(struct pa_strm *pa_stream,
		struct audio_dev *audio_dev, uint8_t *out_buf,
		uint8_t *in_buf, int inp_frames) {

	int num_of_chan = audio_dev->ad_ops->ad_ops_ioctl(audio_dev,
		audio_dev->dir == AUDIO_DEV_OUTPUT
		? ADIOCTL_OUT_SUPPORT
		: ADIOCTL_IN_SUPPORT,
		NULL);
	assert(num_of_chan > 0);
	num_of_chan = num_of_chan & AD_STEREO_SUPPORT ? 2 : 1;

	if (pa_stream->number_of_chan != num_of_chan) {
		if (pa_stream->number_of_chan == 1 && num_of_chan == 2) {
			switch (audio_dev->dir) {
			case AUDIO_DEV_OUTPUT:
				_mono_to_stereo(out_buf, inp_frames);
				break;
			case AUDIO_DEV_INPUT:
				_stereo_to_mono(in_buf, inp_frames);
				break;
			default:
				break;
			}
		} else if (pa_stream->number_of_chan == 2 && num_of_chan == 1) {
			switch (audio_dev->dir) {
			case AUDIO_DEV_OUTPUT:
				_stereo_to_mono(out_buf, inp_frames);
				break;
			case AUDIO_DEV_INPUT:
				_mono_to_stereo(in_buf, inp_frames);
				break;
			default:
				break;
			}
		} else {
			log_error("Audio configuration is broken!"
				  "Check the number of channels.\n");
		}
	}
}
#include <kernel/printk.h>
static void *pa_thread_hnd(void *arg) {
	struct pa_strm *pa_stream = (struct pa_strm *) arg;
	int err;
	struct audio_dev *audio_dev;
	uint8_t *out_buf;
	uint8_t *in_buf;
	int inp_frames;
	int out_frames;
	int buf_len;
	int audio_dev_rate;
	int div;

	audio_dev = audio_dev_get_by_idx(pa_stream->devid);
	assert(audio_dev);
	assert(audio_dev->ad_ops);
	assert(audio_dev->ad_ops->ad_ops_start);
	assert(audio_dev->ad_ops->ad_ops_resume);

	audio_dev_rate = audio_dev->ad_ops->ad_ops_ioctl(audio_dev,
			ADIOCTL_GET_RATE, NULL);
	buf_len = audio_dev->ad_ops->ad_ops_ioctl(audio_dev,
								ADIOCTL_BUFLEN, NULL);
	if (buf_len == -1) {
		log_error("Couldn't get audio device buf_len");
		return NULL;
	}

	if (!pa_stream->callback) {
		log_error("No callback provided for PA thread. "
				"That's probably not what you want.");
		return NULL;
	}

	SCHED_WAIT(pa_stream->active);
	audio_dev->ad_ops->ad_ops_start(audio_dev);

	out_frames = buf_len; /* This one is in bytes */

	switch (pa_stream->sample_format) {
	case paInt8:
	case paUInt8:
		/* Leave it as is */
		break;
	case paInt16:
		out_frames /= 2;
		break;
	case paInt24:
		out_frames /= 3;
	case paFloat32:
	case paInt32:
		out_frames /= 4;
		break;
	default:
		log_error("Unknown sample format: %d\n", pa_stream->sample_format);
	}

	/* Even if source is mono channel,
	 * we will anyway put twice as much data
	 * to fill right channel as well */
	out_frames /= 2; /* XXX work with mono-support devices */

	div = audio_dev_rate / pa_stream->rate;
	assert(div > 0);
	inp_frames = out_frames;
	inp_frames /= div;

	while (1) {
		SCHED_WAIT(pa_stream->active);

		if (pa_stream->state != PA_STREAM_RUNNING) {
			log_debug("Exit pa_thread");
			return NULL;
		}

		out_buf = audio_dev_get_out_cur_ptr(audio_dev);
		in_buf  = audio_dev_get_in_cur_ptr(audio_dev);

		log_debug("out_buf = 0x%X, buf_len %d", out_buf, buf_len);

		if (out_buf) {
			memset(out_buf, 0, buf_len);
		}

		if (audio_dev->dir == AUDIO_DEV_INPUT) {
			pa_do_rate_convertion(pa_stream, audio_dev, in_buf,
				inp_frames);
			pa_do_channel_convertion(pa_stream, audio_dev, out_buf, in_buf,
				inp_frames);
		}

		err = pa_stream->callback(in_buf,
			out_buf,
			inp_frames,
			NULL,
			0,
			pa_stream->user_data);

		switch (err) {
		case paContinue:
		/* Allright, just to continue the stream */
			break;
		/* Continue until all buffers generated by the callback
		 * have been played */
		case paComplete:
			Pa_StopStream(pa_stream);
			break;
		default:
			log_error("User callback error: %d", err);
			Pa_StopStream(pa_stream);
			break;
		}

		if (audio_dev->dir == AUDIO_DEV_OUTPUT) {
			pa_do_channel_convertion(pa_stream, audio_dev, out_buf, in_buf,
				inp_frames);
			pa_do_rate_convertion(pa_stream, audio_dev, out_buf,
				inp_frames * div);
		}
		pa_stream->active = 0;

		audio_dev->ad_ops->ad_ops_resume(audio_dev);
	}

	return NULL;
}

PaError Pa_Initialize(void) {
	return paNoError;
}

PaError Pa_Terminate(void) {
	return paNoError;
}

/* XXX Now we support either only input of ouput streams, bot not both at the same time */
PaError Pa_OpenStream(PaStream** stream,
		const PaStreamParameters *inputParameters,
		const PaStreamParameters *outputParameters,
		double sampleRate, unsigned long framesPerBuffer,
		PaStreamFlags streamFlags, PaStreamCallback *streamCallback,
		void *userData) {
	struct pa_strm *pa_stream;
	struct audio_dev *audio_dev;
	int prev_rate;
	int rate = (int) sampleRate;

	assert(stream != NULL);
	assert(streamFlags == paNoFlag || streamFlags == paClipOff);
	assert(streamCallback != NULL);

	log_debug("stream %p input %p output %p rate %f"
			" framesPerBuffer %lu flags %lu callback %p user_data %p",
			stream, inputParameters, outputParameters, sampleRate,
			framesPerBuffer, streamFlags, streamCallback, userData);

	pa_stream = pool_alloc(&pa_strm_pool);
	if (!pa_stream) {
		log_error("Cannot allocate pa_strm");
		return paInternalError;
	}

	if (outputParameters != NULL) {
		pa_stream->active          = 1;
		pa_stream->number_of_chan  = outputParameters->channelCount;
		pa_stream->devid           = outputParameters->device;
		pa_stream->sample_format   = outputParameters->sampleFormat;
	} else {
		pa_stream->active          = 0;
		pa_stream->number_of_chan  = inputParameters->channelCount;
		pa_stream->devid           = inputParameters->device;
		pa_stream->sample_format   = inputParameters->sampleFormat;
	}

	pa_stream->callback       = streamCallback;
	pa_stream->user_data      = userData;

	*stream = pa_stream;

	audio_dev = audio_dev_get_by_idx(pa_stream->devid);
	if (audio_dev == NULL)
		return paInvalidDevice;

	assert(audio_dev->ad_ops);
	assert(audio_dev->ad_ops->ad_ops_ioctl);
	assert(audio_dev->ad_ops->ad_ops_start);

	prev_rate = audio_dev->ad_ops->ad_ops_ioctl(audio_dev,
		ADIOCTL_GET_RATE, NULL);
	if ((prev_rate != -1) && (prev_rate != rate)) {
		audio_dev->ad_ops->ad_ops_ioctl(audio_dev, ADIOCTL_SET_RATE, &rate);
		pa_stream->rate = rate;
	}

	audio_dev->dir == AUDIO_DEV_OUTPUT
		? audio_dev_open_out_stream(audio_dev, pa_stream)
		: audio_dev_open_in_stream(audio_dev, pa_stream);

	pa_stream->state = PA_STREAM_RUNNING;
	pa_stream->pa_thread = thread_create(THREAD_FLAG_SUSPENDED,
								pa_thread_hnd, pa_stream);

	return paNoError;
}

static void pa_thread_wakeup(struct pa_strm *strm,
		enum pa_state state) {
	assert(strm && strm->pa_thread);
	strm->state = state;
	strm->active = 1;
	sched_wakeup(&strm->pa_thread->schedee);
}

PaError Pa_IsStreamStopped(PaStream *stream) {
	struct pa_strm *strm = (struct pa_strm *)stream;
	return strm->state == PA_STREAM_STOPPED;
}

PaError Pa_CloseStream(PaStream *stream) {
	struct pa_strm *pa_stream = stream;

	if (!Pa_IsStreamStopped(stream)) {
		pa_thread_wakeup(pa_stream, PA_STREAM_CLOSED);
		/* Wait until thread finished and only after that free the stream */
		thread_join(pa_stream->pa_thread, NULL);
	}
	pool_free(&pa_strm_pool, pa_stream);

	return paNoError;
}

PaError Pa_StartStream(PaStream *stream) {
	pa_thread_wakeup((struct pa_strm *) stream, PA_STREAM_RUNNING);
	return paNoError;
}

PaError Pa_StopStream(PaStream *stream) {
	struct pa_strm *strm;
	struct audio_dev *audio_dev;

	assert(stream);
	strm = (struct pa_strm *)stream;

	audio_dev = audio_dev_get_by_idx(strm->devid);
	assert(audio_dev);
	assert(audio_dev->ad_ops);

	if (audio_dev->ad_ops->ad_ops_pause)
		audio_dev->ad_ops->ad_ops_pause(audio_dev);
	else
		log_error("Stream pause not supported!\n");

	if (audio_dev->ad_ops->ad_ops_stop)
		audio_dev->ad_ops->ad_ops_stop(audio_dev);
	else
		log_error("Stream stop not supported!\n");

	pa_thread_wakeup(strm, PA_STREAM_STOPPED);
	/* Wait until thread finished and only after that free the stream */
	thread_join(strm->pa_thread, NULL);

	return paNoError;
}

void Pa_Sleep(long msec) {
	usleep(msec * 1000);
}
