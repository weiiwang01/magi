#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"

#define TIMEOUT_TIMER_HZ 64000000U
#define TIMEOUT_NANOSECONDS_TO_CYCLES(ns) \
  ((uint32_t)(((uint64_t)(ns) * TIMEOUT_TIMER_HZ + 999999999ULL) / 1000000000ULL))

typedef struct
{
  uint32_t start;
  uint32_t duration;
} timeout_t;

static inline void timeout_timer_init(void)
{
  uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE) != RCC_HCLK_DIV1)
  {
    timer_clock *= 2U;
  }
  if (timer_clock < TIMEOUT_TIMER_HZ || timer_clock % TIMEOUT_TIMER_HZ != 0U)
  {
    Error_Handler();
  }

  __HAL_RCC_TIM16_CLK_ENABLE();
  __HAL_RCC_TIM16_FORCE_RESET();
  __HAL_RCC_TIM16_RELEASE_RESET();
  TIM16->PSC = timer_clock / TIMEOUT_TIMER_HZ - 1U;
  TIM16->ARR = UINT16_MAX;
  TIM16->EGR = TIM_EGR_UG;
  TIM16->SR = 0U;
  TIM16->CNT = 0U;
  TIM16->CR1 = TIM_CR1_CEN;
}

static inline void timeout_init(timeout_t *timeout, uint32_t duration)
{
  timeout->start = HAL_GetTick();
  timeout->duration = duration;
}

static inline bool timeout_expired(const timeout_t *timeout)
{
  return (uint32_t)(HAL_GetTick() - timeout->start) >= timeout->duration;
}

static inline void timeout_sleep_cycles(uint64_t cycles)
{
  if (cycles == 0U)
  {
    return;
  }

  uint16_t previous = (uint16_t)TIM16->CNT;
  for (;;)
  {
    uint16_t current = (uint16_t)TIM16->CNT;
    uint16_t elapsed = (uint16_t)(current - previous);
    if (elapsed > cycles)
    {
      return;
    }
    cycles -= elapsed;
    previous = current;
  }
}

#endif
