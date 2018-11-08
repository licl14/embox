/**
 * @file
 * @brief Driver for STM32
 * @author Alex Kalmuk
 * @date 07.11.2018
 */

#include <embox/unit.h>
#include <util/log.h>
#include <kernel/thread/sync/mutex.h>

#include <drivers/audio/stm32_audio.h>

EMBOX_UNIT_INIT(stm32_audio_init);

static int audio_init_started = 0;
static struct mutex audio_init_mutex;

static int stm32_audio_init(void) {
	if (0 != irq_attach(STM32_AUDIO_IN_DMA_IRQ, stm32_audio_in_dma_interrupt,
				0, NULL, "stm32_audio_dma_in")) {
		log_error("irq_attach error");
	}
	if (0 != irq_attach(STM32_AUDIO_IN_IRQ, stm32_audio_in_interrupt,
				0, NULL, "stm32_audio_in")) {
		log_error("irq_attach error");
	}

	if (0 != irq_attach(STM32_AUDIO_OUT_IRQ, stm32_audio_i2s_dma_interrupt,
				0, NULL, "stm32_audio_out")) {
		log_error("irq_attach error");
	}

	mutex_init(&audio_init_mutex);

	return 0;
}

void stm32_audio_init_start(void) {
	extern void stm32_audio_out_start(void);
	extern void stm32_audio_in_start(void);

	mutex_lock(&audio_init_mutex);
	{
		if (audio_init_started) {
			goto out;
		}
		if (0 != BSP_AUDIO_IN_OUT_Init(
					INPUT_DEVICE_DIGITAL_MICROPHONE_2,
					OUTPUT_DEVICE_HEADPHONE,
					100, 16000)) {
			log_error("EVAL_AUDIO_Init error");
			goto out;
		}

		stm32_audio_out_start();
		stm32_audio_in_start();

		audio_init_started = 1;
	}
out:
	mutex_unlock(&audio_init_mutex);
}
