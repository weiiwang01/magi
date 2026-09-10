//
// Created by weiiwang01 on 8/31/26.
//

#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32g0xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define TIMEOUT_TIMER_HZ 64000000U
#define TIMEOUT_TICKS_PER_US (TIMEOUT_TIMER_HZ / 1000000U)
#define TIMEOUT_NANOSECONDS_TO_CYCLES(ns)                                     \
  ((uint32_t)(((uint64_t)(ns) * TIMEOUT_TIMER_HZ + 999999999ULL)              \
              / 1000000000ULL))

typedef struct
{
  uint32_t start;
  uint32_t duration;
} timeout_t;

static void
timeout_timer_init()
{
  uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE) != RCC_HCLK_DIV1)
    {
      timer_clock *= 2U;
    }
  configASSERT(timer_clock >= TIMEOUT_TIMER_HZ
               && timer_clock % TIMEOUT_TIMER_HZ == 0U);

  __HAL_RCC_TIM16_CLK_ENABLE();
  __HAL_RCC_TIM16_FORCE_RESET();
  __HAL_RCC_TIM16_RELEASE_RESET();

  TIM16->PSC = timer_clock / TIMEOUT_TIMER_HZ - 1U;
  TIM16->ARR = UINT16_MAX;
  TIM16->EGR = TIM_EGR_UG;
  TIM16->SR = 0;
  TIM16->CNT = 0;
  TIM16->CR1 = TIM_CR1_CEN;
}

static void
timeout_init(timeout_t *timeout, const uint32_t duration)
{
  timeout->start = HAL_GetTick();
  timeout->duration = duration;
}

static bool
timeout_expired(const timeout_t *timeout)
{
  const uint32_t now = HAL_GetTick();
  return (now - timeout->start) >= timeout->duration;
}

static uint32_t
timeout_remaining(const timeout_t *timeout)
{
  const uint32_t now = HAL_GetTick();
  const uint32_t elapsed = now - timeout->start;
  return elapsed >= timeout->duration ? 0 : timeout->duration - elapsed;
}

static void
timeout_sleep_cycles(uint64_t cycles)
{
  if (cycles == 0)
    {
      return;
    }

  uint16_t previous = (uint16_t)TIM16->CNT;
  while (true)
    {
      const uint16_t current = (uint16_t)TIM16->CNT;
      const uint16_t elapsed = (uint16_t)(current - previous);
      if (elapsed > cycles)
        {
          return;
        }
      cycles -= elapsed;
      previous = current;
    }
}

static void
timeout_sleep_us(uint32_t us)
{
  timeout_sleep_cycles((uint64_t)us * TIMEOUT_TICKS_PER_US);
}

static void
timeout_sleep_ms(uint32_t ms)
{
  if ((__get_IPSR() == 0U)
      && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
      TickType_t ticks = pdMS_TO_TICKS(ms);

      if (ticks == 0 && ms != 0)
        {
          ticks = 1;
        }

      vTaskDelay(ticks);
    }
  else
    {
      HAL_Delay(ms);
    }
}

#endif // TIMEOUT_H
