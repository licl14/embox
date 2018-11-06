/**
 * @file
 * @brief Simple audio player
 * @author Denis Deryugin <deryugin.denis@gmail.com>
 * @version 0.1
 * @date 2015-11-26
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <drivers/audio/portaudio.h>
#include <fs/file_format.h>
#include <util/math.h>
#include <framework/mod/options.h>

#define USE_LOCAL_BUFFER OPTION_GET(BOOLEAN, use_local_buffer)
#define FRAMES_PER_BUFFER OPTION_GET(NUMBER, frames_per_buffer)

static void print_usage(void) {
	printf("Usage: play [WAVAUDIOFILE]\n"
	       "       play -s\n"
	       "       play -m <audio address in memory (ROM, RAM, SDRAM)>\n"
	       "            (E.g. play -m 08080000. \n"
	       "            Use \"./st-flash write [WAV_FILE] 0x08080000\")\n");
}

double _sin(double x) {
	double m = 1.;
	while (x > 2. * 3.14)
		x -= 2. * 3.14;

	if (x > 3.14) {
		x -= 3.14;
		m = -1.;
	}
	return m * (x - x * x * x / 6. + x * x * x * x * x / 120.
		- x * x * x * x * x * x * x / (120 * 6 * 7)
		+ x * x * x * x * x * x * x * x * x/ (120 * 6 * 7 * 8 * 9));
}

static int _sin_w = 100;
static int _sin_h = 30000;
static int sin_callback(const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData) {
	uint16_t *data;

	data = outputBuffer;

	for (int i = 0; i < framesPerBuffer; i++) {
		double x = 2 * 3.14 * (i % _sin_w) / _sin_w;
		*data++ = (uint16_t) ((1. + _sin(x)) * _sin_h); /* Left channel  */
		*data++ = (uint16_t) ((1. + _sin(x)) * _sin_h); /* Right channel */
	}

	return 0;
}
#define FBUFFER_SIZE (64 * 1024 * 1024)
#define AUDIO_ADDR_UNINITIALIZED ((uint32_t) -1)

static uint32_t audio_memory_addr = AUDIO_ADDR_UNINITIALIZED;

#if USE_LOCAL_BUFFER
static uint8_t audio_buff[FBUFFER_SIZE];
static uint8_t *_fbuffer = audio_buff;
#else
static uint8_t *_fbuffer = NULL;
#endif

static int _bl = 0;
static int _fchan = 2;

static int fd_callback(const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData) {
	static int _ptr = 0;
        int read_bytes;

	read_bytes = min(_bl - _ptr, framesPerBuffer * _fchan * 2); /* Stereo 16-bit */
	memcpy(outputBuffer, _fbuffer + _ptr, read_bytes);
	_ptr += read_bytes;

	printf("|");
	fflush(stdout);
	if (_ptr < _bl) {
		return paContinue;
	} else {
		printf("\n");
		_ptr = 0;
		return paComplete;
	}
}

int main(int argc, char **argv) {
	int opt;
	int err;
	FILE *fd = NULL;
	static uint8_t fmt_buf[128];
	int chan_n = 2;
	int sample_rate = 44100;
	int bits_per_sample = 16;
	int fdata_len = 0x100000;
	int sleep_msec;

	PaStreamCallback *callback;
	PaStream *stream = NULL;

	struct PaStreamParameters out_par;

	if (argc < 2) {
		print_usage();
		return 0;
	}

	callback = &fd_callback;

	while (-1 != (opt = getopt(argc, argv, "nshm:"))) {
		switch (opt) {
		case 'h':
			print_usage();
			return 0;
		case 's':
			callback = &sin_callback;
			break;
		case 'm':
			if ((optarg == NULL) || (!sscanf(optarg, "%x", &audio_memory_addr))) {
				print_usage();
				return -1;
			}
			printf("Audio file memory address is 0x%x\n", audio_memory_addr);
			break;
		default:
			printf("Unknown argument: %c", opt);
			return 0;
		}
	}

	if (callback == fd_callback) {
		if ((audio_memory_addr == AUDIO_ADDR_UNINITIALIZED) && !_fbuffer) {
			return -1;
		}

		if (audio_memory_addr != AUDIO_ADDR_UNINITIALIZED) {
			/* Get audio info from memory */
			memcpy(fmt_buf, (void*) audio_memory_addr, 44);
			_fbuffer = (uint8_t*) audio_memory_addr;
		} else if (_fbuffer) {
			/* Get audio info from file */
			if (NULL == (fd = fopen(argv[argc - 1], "r"))) {
				printf("Can't open file %s\n", argv[argc - 1]);
				return 0;
			}

			fread(fmt_buf, 1, 44, fd);
			if (raw_get_file_format(fmt_buf) != RIFF_FILE) {
				printf("%s is not a RIFF audio file\n", argv[argc - 1]);
				return 0;
			}
			_bl = min(fread(_fbuffer, 1, FBUFFER_SIZE, fd), FBUFFER_SIZE);
		}

		chan_n          = *((uint16_t*) &fmt_buf[22]);
		sample_rate     = *((uint32_t*) &fmt_buf[24]);
		bits_per_sample = *((uint16_t*) &fmt_buf[34]);
		fdata_len       = *((uint32_t*) &fmt_buf[40]);
		printf("File size:             %d bytes\n",
		       *((uint32_t*) &fmt_buf[4]));
		printf("File type header:      %c%c%c%c\n",
		        fmt_buf[8], fmt_buf[9], fmt_buf[10], fmt_buf[11]);
		printf("Length of format data: %d\n", *((uint32_t*) &fmt_buf[16]));
		printf("Type format:           %d\n", *((uint16_t*) &fmt_buf[20]));
		printf("Number of channels:    %d\n", chan_n);
		printf("Sample rate:           %d\n", sample_rate);
		printf("Bits per sample:       %d\n", bits_per_sample);
		printf("Size of data section:  %d\n", fdata_len);

		if (bits_per_sample * sample_rate * chan_n == 0) {
			printf("Check bps, sample rate and channel number, they should not be zero!\n");
			goto err_close_fd;
		}

		printf("Progress:\n");

		if (audio_memory_addr != AUDIO_ADDR_UNINITIALIZED) {
			_bl = fdata_len;
		}

		_fchan = chan_n;
	}


	/* Initialize PA */
	if (paNoError != (err = Pa_Initialize())) {
		printf("Portaudio error: could not initialize!\n");
		goto err_close_fd;
	}

	out_par = (PaStreamParameters) {
		.device                    = 0,
		.channelCount              = chan_n,
		.sampleFormat              = paInt16,
		.suggestedLatency          = 10,
		.hostApiSpecificStreamInfo = NULL,
	};

	err = Pa_OpenStream(&stream,
			NULL,
			&out_par,
			sample_rate,
			FRAMES_PER_BUFFER,
			0,
			callback,
			NULL);

	if (err != paNoError) {
		printf("Portaudio error: could not open stream!\n");
		goto err_terminate_pa;
	}

	if (paNoError != (err = Pa_StartStream(stream))) {
		printf("Portaudio error: could not start stream!\n");
		goto err_terminate_pa;
	}

	sleep_msec = 1000 * (fdata_len /
	           (bits_per_sample / 8 * sample_rate * chan_n));
	Pa_Sleep(sleep_msec);

	if (paNoError != (err = Pa_StopStream(stream))) {
		printf("Portaudio error: could not stop stream!\n");
		goto err_terminate_pa;
	}

	if (paNoError != (err = Pa_CloseStream(stream))) {
		printf("Portaudio error: could not close stream!\n");
		goto err_terminate_pa;
	}

err_terminate_pa:
	if (paNoError != (err = Pa_Terminate()))
		printf("Portaudio error: could not terminate!\n");

err_close_fd:
	if (fd)
		fclose(fd);
	return 0;
}
