/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for g_os_startup */
osThreadId_t g_os_startupHandle;
const osThreadAttr_t g_os_startup_attributes = {
  .name = "g_os_startup",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for g_gamepad_task */
osThreadId_t g_gamepad_taskHandle;
const osThreadAttr_t g_gamepad_task_attributes = {
  .name = "g_gamepad_task",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for g_transmit_task */
osThreadId_t g_transmit_taskHandle;
const osThreadAttr_t g_transmit_task_attributes = {
  .name = "g_transmit_task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for g_gamepad_queue */
osMessageQueueId_t g_gamepad_queueHandle;
const osMessageQueueAttr_t g_gamepad_queue_attributes = {
  .name = "g_gamepad_queue"
};
/* Definitions for g_link_tick */
osTimerId_t g_link_tickHandle;
const osTimerAttr_t g_link_tick_attributes = {
  .name = "g_link_tick"
};
/* Definitions for g_uart_tx_mutex */
osMutexId_t g_uart_tx_mutexHandle;
const osMutexAttr_t g_uart_tx_mutex_attributes = {
  .name = "g_uart_tx_mutex"
};
/* Definitions for g_uart_tx_done */
osSemaphoreId_t g_uart_tx_doneHandle;
const osSemaphoreAttr_t g_uart_tx_done_attributes = {
  .name = "g_uart_tx_done"
};
/* Definitions for g_system_events */
osEventFlagsId_t g_system_eventsHandle;
const osEventFlagsAttr_t g_system_events_attributes = {
  .name = "g_system_events"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void Os_startup_entry(void *argument);
void Os_gamepad_entry(void *argument);
void Os_transmit_entry(void *argument);
void Os_link_tick_callback(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of g_uart_tx_mutex */
  g_uart_tx_mutexHandle = osMutexNew(&g_uart_tx_mutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of g_uart_tx_done */
  g_uart_tx_doneHandle = osSemaphoreNew(1, 0, &g_uart_tx_done_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* Create the timer(s) */
  /* creation of g_link_tick */
  g_link_tickHandle = osTimerNew(Os_link_tick_callback, osTimerPeriodic, NULL, &g_link_tick_attributes);

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of g_gamepad_queue */
  g_gamepad_queueHandle = osMessageQueueNew (16, 80, &g_gamepad_queue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of g_os_startup */
  g_os_startupHandle = osThreadNew(Os_startup_entry, NULL, &g_os_startup_attributes);

  /* creation of g_gamepad_task */
  g_gamepad_taskHandle = osThreadNew(Os_gamepad_entry, NULL, &g_gamepad_task_attributes);

  /* creation of g_transmit_task */
  g_transmit_taskHandle = osThreadNew(Os_transmit_entry, NULL, &g_transmit_task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* creation of g_system_events */
  g_system_eventsHandle = osEventFlagsNew(&g_system_events_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_Os_startup_entry */
/**
  * @brief  Function implementing the g_os_startup thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_Os_startup_entry */
__weak void Os_startup_entry(void *argument)
{
  /* USER CODE BEGIN Os_startup_entry */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Os_startup_entry */
}

/* USER CODE BEGIN Header_Os_gamepad_entry */
/**
* @brief Function implementing the g_gamepad_task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Os_gamepad_entry */
__weak void Os_gamepad_entry(void *argument)
{
  /* USER CODE BEGIN Os_gamepad_entry */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Os_gamepad_entry */
}

/* USER CODE BEGIN Header_Os_transmit_entry */
/**
* @brief Function implementing the g_transmit_task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Os_transmit_entry */
__weak void Os_transmit_entry(void *argument)
{
  /* USER CODE BEGIN Os_transmit_entry */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Os_transmit_entry */
}

/* Os_link_tick_callback function */
__weak void Os_link_tick_callback(void *argument)
{
  /* USER CODE BEGIN Os_link_tick_callback */

  /* USER CODE END Os_link_tick_callback */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

