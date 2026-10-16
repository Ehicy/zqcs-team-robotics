#include "motor_hal.h"
#include "stm32f10x.h"

static void fault_stop(void)
{
    /* Stop the bridge immediately, then let the independent watchdog reset
     * the CPU. No exception handler feeds it or restarts motion. */
    __disable_irq();
    lower_motor_emergency_stop();
    for (;;) __NOP();
}

void NMI_Handler(void) { fault_stop(); }
void HardFault_Handler(void) { fault_stop(); }
void MemManage_Handler(void) { fault_stop(); }
void BusFault_Handler(void) { fault_stop(); }
void UsageFault_Handler(void) { fault_stop(); }

/* These exceptions have no legitimate producer in this bare-metal target. */
void SVC_Handler(void) { fault_stop(); }
void DebugMon_Handler(void) { fault_stop(); }
void PendSV_Handler(void) { fault_stop(); }

/* SysTick_Handler is implemented in timing.c; EXTI15_10_IRQHandler is in
 * motor_hal.c. The USART2 IRQ belongs to the target's main transport module. */
