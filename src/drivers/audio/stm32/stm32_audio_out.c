/**
 * @file
 * @brief Driver for STM32
 * @author Alex Kalmuk
 * @date 6.08.2018
 */

#include <unistd.h>
#include <errno.h>
#include <util/log.h>
#include <stdio.h>
#include <drivers/audio/portaudio.h>
#include <drivers/audio/audio_dev.h>
#include <kernel/panic.h>

#include <drivers/audio/stm32_audio.h>

static irq_return_t stm32_audio_i2s_dma_interrupt(unsigned int irq_num,
        void *dev_id);

static struct stm32_dev_priv stm32_dac;

struct stm32_dev_priv {
	int devid;
	uint8_t *out_buf;
	uint32_t out_buf_len;
};

static struct stm32_hw_out {
	struct audio_dev *out;
} stm32_hw_out;

/* Two buffers of STM32_MAX_BUF_LEN size */
static uint8_t dac_out_bufs[STM32_MAX_BUF_LEN * 2];

static void stm32_dev_start(struct audio_dev *dev) {
	if (0 != irq_attach(STM32_AUDIO_OUT_IRQ, stm32_audio_i2s_dma_interrupt,
				0, dev, "stm32_audio_out")) {
		log_error("irq_attach error");
	}

	if (0 != BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_HEADPHONE, DEFAULT_VOLUME,
				DEFAULT_SAMPLE_RATE)) {
		log_error("EVAL_AUDIO_Init error");
	}

	//BSP_AUDIO_OUT_SetFrequency(22500);

	if (0 != BSP_AUDIO_OUT_Play((uint16_t*) &dac_out_bufs[0], sizeof(dac_out_bufs))) {
		log_error("EVAL_AUDIO_Play error");
	}

	stm32_hw_out.out = dev;
}

static void stm32_dev_pause(struct audio_dev *dev) {
	if (BSP_AUDIO_OUT_Pause() != AUDIO_OK) {
		log_error("BSP_AUDIO_OUT_Pause error");
	}
}

static void stm32_dev_resume(struct audio_dev *dev) {
	BSP_AUDIO_OUT_ChangeBuffer((uint16_t *)stm32_dac.out_buf, STM32_MAX_BUF_LEN);
}

static void stm32_dev_stop(struct audio_dev *dev) {
	if (BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW) != AUDIO_OK) {
		log_error("BSP_AUDIO_OUT_Stop error");
	}

	irq_detach(STM32_AUDIO_OUT_IRQ, NULL);
}

static int stm32_dev_ioctl(struct audio_dev *dev, int cmd, void *args) {
	switch(cmd) {
	case ADIOCTL_IN_SUPPORT:
		return 0;
	case ADIOCTL_OUT_SUPPORT:
		return AD_STEREO_SUPPORT | AD_16BIT_SUPPORT;
	case ADIOCTL_BUFLEN:
		return STM32_MAX_BUF_LEN;
	case ADIOCTL_GET_RATE:
		return 0; /* There is no function to get rate in Cube...*/
	case ADIOCTL_SET_RATE:
	{
		int rate = *(int *) args;
		BSP_AUDIO_OUT_SetFrequency(rate);
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

static struct stm32_dev_priv stm32_dac = {
	.devid       = 1,
	.out_buf     = &dac_out_bufs[0],
	.out_buf_len = STM32_MAX_BUF_LEN,
};

AUDIO_DEV_DEF("stm32_dac", (struct audio_dev_ops *)&stm32_dev_ops, &stm32_dac);

uint8_t *audio_dev_get_out_cur_ptr(struct audio_dev *audio_dev) {
	struct stm32_dev_priv *priv = audio_dev->ad_priv;
	return priv->out_buf;
}

uint8_t *audio_dev_get_in_cur_ptr(struct audio_dev *audio_dev) {
	return NULL;
}

static void stm32_audio_irq_fill_buffer(int buf_index) {
	stm32_dac.out_buf = &dac_out_bufs[0] + buf_index * STM32_MAX_BUF_LEN;
	Pa_StartStream(stm32_hw_out.out->stream);
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void) {
	stm32_audio_irq_fill_buffer(0);
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void) {
	stm32_audio_irq_fill_buffer(1);
}

void BSP_AUDIO_OUT_Error_CallBack() {
	log_error("");
}

static irq_return_t stm32_audio_i2s_dma_interrupt(unsigned int irq_num,
		void *audio_dev) {
	audio_out_irq_handler();
	return IRQ_HANDLED;
}

STATIC_IRQ_ATTACH(STM32_AUDIO_OUT_IRQ, stm32_audio_i2s_dma_interrupt, NULL);
