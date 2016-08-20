/**
  ******************************************************************************
  * File Name          : main.c
  * Description        : Main program body
  ******************************************************************************
  *
  * COPYRIGHT(c) 2015 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  *
  * Modifications by Robert Fisk
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "usb_host.h"
#include "board_config.h"
#include "downstream_statemachine.h"
#include "downstream_spi.h"
#include "led.h"
#include "interrupts.h"


/* Private function prototypes -----------------------------------------------*/
static void SystemClock_Config(void);
static void GPIO_Init(void);
static void DisableFlashWrites(void);
static void CheckFirmwareMatchesHardware(void);

volatile uint8_t    UsbInterruptHasHappened = 0;
uint8_t             IterationCount = 0;


int main(void)
{
    //First things first!
    DisableFlashWrites();
    CheckFirmwareMatchesHardware();

    /* Configure the system clock */
    SystemClock_Config();

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Initialize all configured peripherals */
    GPIO_Init();
    LED_Init();
    USB_Host_Init();

    Downstream_InitStateMachine();

    while (1)
    {
        USB_Host_Process();
        Downstream_SPIProcess();
        Downstream_PacketProcessor_CheckNotifyDisconnectReply();

        //Count number of main loops since last USB interrupt
        if (UsbInterruptHasHappened)
        {
            UsbInterruptHasHappened = 0;
            IterationCount = 0;
        }

        //Some USB host state transitions take 3 iterations to fully apply.
        //We'll be generous and give it 5 before sleeping.
        if (IterationCount++ > 4)
        {
            __WFI();                //sleep time!
        }
    }
}


void DisableFlashWrites(void)
{
    //Disable flash writes until the next reset
    //This will cause a bus fault interrupt, so allow one now.
    EnableOneBusFault();
    FLASH->KEYR = 999;

    //Confirm that flash cannot be unlocked
    //This unlock attempt will also cause two bus faults.
    if ((FLASH->CR & FLASH_CR_LOCK) == 0) while(1);
    EnableOneBusFault();
    FLASH->KEYR = FLASH_KEY1;
    EnableOneBusFault();
    FLASH->KEYR = FLASH_KEY2;
    if ((FLASH->CR & FLASH_CR_LOCK) == 0) while(1);
}


void CheckFirmwareMatchesHardware(void)
{
    //Check we are running on the expected hardware:
    //STM32F407 on an Olimex dev board

    GPIO_InitTypeDef GPIO_InitStruct;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    if ((*(uint32_t*)DBGMCU_BASE & DBGMCU_IDCODE_DEV_ID) == DBGMCU_IDCODE_DEV_ID_405_407_415_417)
    {
        //The H407 board has a STAT LED on PC13. If there is no pullup on this pin,
        //then we are probably running on another board.
        GPIO_InitStruct.Pin = FAULT_LED_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        GPIO_InitStruct.Speed = GPIO_SPEED_LOW;
        GPIO_InitStruct.Alternate = 0;
        HAL_GPIO_Init(FAULT_LED_PORT, &GPIO_InitStruct);
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(FAULT_LED_PORT, &GPIO_InitStruct);

        if (FAULT_LED_PORT->IDR & FAULT_LED_PIN)
        {
            //Pin pulls up, so this is an H407 board :)
            return;
        }
    }

    //This is not the hardware we expected, so turn on our fault LED(s) and die in a heap.
    GPIO_InitStruct.Pin = FAULT_LED_PIN | H405_FAULT_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(FAULT_LED_PORT, &GPIO_InitStruct);
    FAULT_LED_ON;
    H405_FAULT_LED_ON;
    while (1);
}


/** System Clock Configuration
*/
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;

  __PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 12;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) while (1);

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_HCLK |
                                RCC_CLOCKTYPE_PCLK1 |
                                RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) while (1);

  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
}




void GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* GPIO Ports Clock Enable */
    //__GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    //Bulk initialise all ports as inputs with pullups active,
    //excluding JTAG pins which must remain as AF0!
    GPIO_InitStruct.Pin = (GPIO_PIN_All & ~(PA_JTMS | PA_JTCK | PA_JTDI));
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_LOW;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = (GPIO_PIN_All & ~(PB_JTDO | PB_NJTRST));
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_All;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    //USB VBUS pins are analog input
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pin = USB_FS_VBUS_PIN;
    HAL_GPIO_Init(USB_FS_VBUS_PORT, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = USB_HS_VBUS_PIN;
    HAL_GPIO_Init(USB_HS_VBUS_PORT, &GPIO_InitStruct);

    //Enable USB_FS power
    USB_FS_VBUSON_PORT->BSRR = (USB_FS_VBUSON_PIN << BSRR_SHIFT_HIGH);
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Pin = USB_FS_VBUSON_PIN;
    HAL_GPIO_Init(USB_FS_VBUSON_PORT, &GPIO_InitStruct);

    //Disable USB_HS power
    USB_HS_VBUSON_PORT->BSRR = (USB_HS_VBUSON_PIN << BSRR_SHIFT_LOW);
    GPIO_InitStruct.Pin = USB_HS_VBUSON_PIN;
    HAL_GPIO_Init(USB_HS_VBUSON_PORT, &GPIO_InitStruct);

    //STAT_LED is output
    FAULT_LED_OFF;
    GPIO_InitStruct.Pin = FAULT_LED_PIN;
    HAL_GPIO_Init(FAULT_LED_PORT, &GPIO_InitStruct);

    //SPI_INT_ACTIVE indicator
    GPIO_InitStruct.Pin = INT_ACTIVE_PIN;
    HAL_GPIO_Init(INT_ACTIVE_PORT, &GPIO_InitStruct);
    INT_ACTIVE_OFF;
}



#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */

}

#endif

/**
  * @}
  */ 

/**
  * @}
*/ 

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
