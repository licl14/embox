/**
 * @file
 * @brief Driver for STM32
 * @author Alex Kalmuk
 * @date 07.11.2018
 */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <util/log.h>
#include <drivers/audio/portaudio.h>
#include <drivers/audio/audio_dev.h>
#include <kernel/panic.h>

#include <drivers/audio/portaudio.h>
#include <fs/file_format.h>

#include <drivers/audio/stm32_audio.h>

static irq_return_t stm32_audio_in_dma_interrupt(unsigned int irq_num,
        void *dev_id);
static irq_return_t stm32_audio_in_interrupt(unsigned int irq_num,
		void *audio_dev);

static struct stm32_dev_priv stm32_adc;

struct stm32_dev_priv {
	int devid;
	uint8_t *in_buf;
	uint32_t in_buf_len;
	int rate;
};

static struct stm32_hw_in {
	void *stream;
} stm32_hw_in;

/* Two buffers of STM32_MAX_BUF_LEN size */
static uint8_t adc_in_bufs[STM32_MAX_BUF_LEN * 2];

static uint8_t audio_in_buffer[STM32_MAX_BUF_LEN];

uint8_t *get_audio_in_buffer(void) {
	return audio_in_buffer;
}

static void stm32_dev_start(struct audio_dev *dev) {
	struct stm32_dev_priv *priv = dev->ad_priv;

	if (0 != irq_attach(STM32_AUDIO_IN_DMA_IRQ, stm32_audio_in_dma_interrupt,
				0, dev, "stm32_audio_dma_in")) {
		log_error("irq_attach error");
	}
	if (0 != irq_attach(STM32_AUDIO_IN_IRQ, stm32_audio_in_interrupt,
				0, dev, "stm32_audio_in")) {
		log_error("irq_attach error");
	}

	if (0 != BSP_AUDIO_IN_Init(INPUT_DEVICE_DIGITAL_MICROPHONE_2, 100, priv->rate)) {
		log_error("BSP_AUDIO_IN_Init error");
	}

	if (0 != BSP_AUDIO_IN_Record((uint16_t *) &adc_in_bufs[0], STM32_MAX_BUF_LEN)) {
		log_error("BSP_AUDIO_IN_Record error");
	}
}

static void stm32_dev_pause(struct audio_dev *dev) {
	if (BSP_AUDIO_IN_Pause() != AUDIO_OK) {
		log_error("BSP_AUDIO_OUT_Pause error");
	}
}

static void stm32_dev_resume(struct audio_dev *dev) {
}

static void stm32_dev_stop(struct audio_dev *dev) {
	if (BSP_AUDIO_IN_Stop(CODEC_PDWN_HW) != AUDIO_OK) {
		log_error("BSP_AUDIO_OUT_Stop error");
	}

	irq_detach(STM32_AUDIO_IN_DMA_IRQ, NULL);
}

static int stm32_dev_ioctl(struct audio_dev *dev, int cmd, void *args) {
	switch(cmd) {
	case ADIOCTL_IN_SUPPORT:
		return AD_STEREO_SUPPORT | AD_16BIT_SUPPORT;
	case ADIOCTL_OUT_SUPPORT:
		return 0;
	case ADIOCTL_BUFLEN:
		return STM32_MAX_BUF_LEN;
	case ADIOCTL_GET_RATE:
		return 0; /* There is no function to get rate in Cube...*/
	case ADIOCTL_SET_RATE:
	{
		struct stm32_dev_priv *priv = dev->ad_priv;
		priv->rate = *(int *) args;
		return 0;
	}
	default:
		log_error("Unsupported ioctl cmd %d", cmd);
	}
	SET_ERRNO(EINVAL);
	return -1;
}

static const struct audio_dev_ops stm32_dev_ops = {
	.ad_ops_start  = stm32_dev_start,
	.ad_ops_pause  = stm32_dev_pause,
	.ad_ops_resume = stm32_dev_resume,
	.ad_ops_stop   = stm32_dev_stop,
	.ad_ops_ioctl  = stm32_dev_ioctl
};

static struct stm32_dev_priv stm32_adc = {
	.in_buf_len = STM32_MAX_BUF_LEN,
};

AUDIO_DEV_DEF("stm32_adc", (struct audio_dev_ops *)&stm32_dev_ops, &stm32_adc, AUDIO_DEV_INPUT);

uint8_t *audio_dev_get_in_cur_ptr(struct audio_dev *audio_dev) {
	return stm32_adc.in_buf;
}

void audio_dev_open_in_stream(struct audio_dev *audio_dev, void *stream) {
	stm32_hw_in.stream = stream;
}

static void stm32_audio_irq_fill_buffer(int buf_index) {
	stm32_adc.in_buf = &adc_in_bufs[0] + buf_index * STM32_MAX_BUF_LEN;
	memcpy(audio_in_buffer, stm32_adc.in_buf, STM32_MAX_BUF_LEN);
	Pa_StartStream(stm32_hw_in.stream);
}

void BSP_AUDIO_IN_HalfTransfer_CallBack(void) {
	stm32_audio_irq_fill_buffer(0);
}

void BSP_AUDIO_IN_TransferComplete_CallBack(void) {
	stm32_audio_irq_fill_buffer(1);
}

void BSP_AUDIO_IN_Error_CallBack() {
	log_error("");
}

static irq_return_t stm32_audio_in_dma_interrupt(unsigned int irq_num,
		void *audio_dev) {
	//log_info("");
	audio_in_dma_irq_handler();
	return IRQ_HANDLED;
}

static irq_return_t stm32_audio_in_interrupt(unsigned int irq_num,
		void *audio_dev) {
	//log_info("");
	audio_in_irq_handler();
	return IRQ_HANDLED;
}

STATIC_IRQ_ATTACH(STM32_AUDIO_IN_DMA_IRQ, stm32_audio_in_dma_interrupt, NULL);
STATIC_IRQ_ATTACH(STM32_AUDIO_IN_IRQ, stm32_audio_in_interrupt, NULL);
