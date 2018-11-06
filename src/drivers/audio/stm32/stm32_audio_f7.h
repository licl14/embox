/**
 * @file
 *
 * @data 6.11.2018
 * @author Alex Kalmuk
 */

#ifndef SRC_DRIVERS_AUDIO_STM32_STM32_AUDIO_F7_H_
#define SRC_DRIVERS_AUDIO_STM32_STM32_AUDIO_F7_H_

#include <assert.h>
#include <kernel/irq.h>
#include <framework/mod/options.h>

#include "stm32746g_discovery.h"
#include "stm32746g_discovery_audio.h"

#define DEFAULT_VOLUME           OPTION_GET(NUMBER, volume)
#define DEFAULT_SAMPLE_RATE      OPTION_GET(NUMBER, sample_rate)

#define MAX_BUF_LEN     2048
#define OUTPUT_CHAN_N   2
#define STM32_MAX_BUF_LEN  (MAX_BUF_LEN * OUTPUT_CHAN_N)

#define STM32_AUDIO_OUT_IRQ    (AUDIO_OUT_SAIx_DMAx_IRQ + 16)
#define STM32_AUDIO_IN_DMA_IRQ (AUDIO_IN_SAIx_DMAx_IRQ + 16)
#define STM32_AUDIO_IN_IRQ     (AUDIO_IN_INT_IRQ + 16)

static_assert(76 == STM32_AUDIO_OUT_IRQ);
static_assert(86 == STM32_AUDIO_IN_DMA_IRQ);
static_assert(56 == STM32_AUDIO_IN_IRQ);

extern SAI_HandleTypeDef haudio_out_sai;
extern SAI_HandleTypeDef haudio_in_sai;

static inline void audio_out_irq_handler(void) {
  HAL_DMA_IRQHandler(haudio_out_sai.hdmatx);
}

static inline void audio_in_dma_irq_handler(void) {
  HAL_DMA_IRQHandler(haudio_in_sai.hdmarx);
}

static inline void audio_in_irq_handler(void) {
	/* Interrupt handler shared between SD_DETECT pin, USER_KEY button and touch screen interrupt */
	if (__HAL_GPIO_EXTI_GET_IT(SD_DETECT_PIN) != RESET) {
		/* SD detect event or touch screen interrupt */
		HAL_GPIO_EXTI_IRQHandler(SD_DETECT_PIN | TS_INT_PIN | AUDIO_IN_INT_GPIO_PIN);
	}
	else
	{	/* User button event or Touch screen interrupt */
		HAL_GPIO_EXTI_IRQHandler(KEY_BUTTON_PIN);
	}
}

#endif /* SRC_DRIVERS_AUDIO_STM32_STM32_AUDIO_F7_H_ */
