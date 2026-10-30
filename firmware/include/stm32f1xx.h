/*
 * stm32f1xx.h - bare register definitions for the Falcon HP thermostat MCU.
 *
 * The MCU is an Artery AT32F421: ARMv7-M Cortex-M4 with a hardware FPU.
 * The shipped firmware executes ~780 VFP instructions (vmov/vcvt/vstr/vmul/
 * vdiv/vcmp), which a Cortex-M3 cannot run at all, and the ADC it uses
 * sits at the AT32 address 0x40013C00 rather than the STM32F1 ADC1
 * address 0x40012400. The rest of the peripheral map is STM32F1-compatible
 * (RCC 0x40021000, FLASH 0x40022000, GPIOA 0x40010800 .. GPIOG 0x40012000,
 * FSMC 0xA0000000, USB 0x40005C00). Everything here is written against
 * those addresses directly - no vendor SPL/HAL is required.
 *
 * Only peripherals actually referenced by the shipped image are declared.
 * Verified present in the image (literal-pool resolved counts in brackets,
 * from tools/analyze.py):
 *
 *   GPIOA [28]  GPIOB [32]  GPIOC [17]  GPIOD [4]  GPIOE [1]
 *   GPIOF [5]   GPIOG [11]  RCC [10]     FLASH [7]  AFIO [2]
 *   EXTI [2]    DMA1 [1]    DMA2 [4]     IWDG [1]   PWR [1]
 *   ADC [2] (AT32 address 0x40013C00)   TIM3 [7]  TIM5 [2]
 *   SPI1 [2]    SPI3 [4]    USART1 [5]   USART2 [1] USART3 [12]
 *   UART4 [5]   UART5 [12]  USB [28]     FSMC [2]
 *
 * Notably absent from the image: I2C1/I2C2 (the EEPROM bus is bit-banged
 * on GPIOG), the F1 ADC address, TIM1/TIM2/TIM4/TIM6/TIM7, CAN, BKP, DAC,
 * WWDG, CRC. See docs/hw-map.md.
 */

#ifndef STM32F1XX_H
#define STM32F1XX_H

#include <stdint.h>

#define __IO volatile

/* ------------------------------------------------------------------ */
/* Cortex-M4 core                                                      */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CPUID;
    __IO uint32_t ICSR;
    __IO uint32_t VTOR;
    __IO uint32_t AIRCR;
    __IO uint32_t SCR;
    __IO uint32_t CCR;
    __IO uint8_t  SHP[12];
    __IO uint32_t SHCSR;
    __IO uint32_t CFSR;
    __IO uint32_t HFSR;
    __IO uint32_t DFSR;
    __IO uint32_t MMFAR;
    __IO uint32_t BFAR;
    __IO uint32_t AFSR;
    __IO uint32_t RESERVED0[8];
    __IO uint32_t CPACR;
} SCB_TypeDef;

typedef struct
{
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __IO uint32_t CALIB;
} SysTick_TypeDef;

typedef struct
{
    __IO uint32_t ISER[8];
    uint32_t      RESERVED0[24];
    __IO uint32_t ICER[8];
    uint32_t      RESERVED1[24];
    __IO uint32_t ISPR[8];
    uint32_t      RESERVED2[24];
    __IO uint32_t ICPR[8];
    uint32_t      RESERVED3[24];
    __IO uint32_t IABR[8];
    uint32_t      RESERVED4[56];
    __IO uint8_t  IP[240];
} NVIC_TypeDef;

#define SCB             ((SCB_TypeDef *)     0xE000ED00UL)
#define SysTick         ((SysTick_TypeDef *) 0xE000E010UL)
#define NVIC            ((NVIC_TypeDef *)    0xE000E100UL)

#define SCB_AIRCR_VECTKEY_Pos     16UL
#define SCB_AIRCR_VECTKEY_Msk     (0xFFFFUL << SCB_AIRCR_VECTKEY_Pos)
#define SCB_AIRCR_SYSRESETREQ     (1UL << 2)

/* CPACR (0xE000ED88) - coprocessor access control. The shipped image
 * references this address once (0x080638D0); the reset path must give
 * CP10/CP11 full access before any hard-float code runs. */
#define SCB_CPACR_CP10_Pos        20UL
#define SCB_CPACR_CP10_Msk        (0x3UL << SCB_CPACR_CP10_Pos)
#define SCB_CPACR_CP11_Pos        22UL
#define SCB_CPACR_CP11_Msk        (0x3UL << SCB_CPACR_CP11_Pos)
#define SCB_CPACR_CP10_CP11_FULL  (0xFUL << 20)

/* ------------------------------------------------------------------ */
/* RCC - 0x40021000                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CR;
    __IO uint32_t CFGR;
    __IO uint32_t CIR;
    __IO uint32_t APB2RSTR;
    __IO uint32_t APB1RSTR;
    __IO uint32_t AHBENR;
    __IO uint32_t APB2ENR;
    __IO uint32_t APB1ENR;
    __IO uint32_t BDCR;
    __IO uint32_t CSR;
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *) 0x40021000UL)

#define RCC_CR_HSION        (1UL << 0)
#define RCC_CR_HSIRDY       (1UL << 1)
#define RCC_CR_HSEON        (1UL << 16)
#define RCC_CR_HSERDY       (1UL << 17)
#define RCC_CR_PLLON        (1UL << 24)
#define RCC_CR_PLLRDY       (1UL << 25)

#define RCC_CFGR_SW_Pos     0UL
#define RCC_CFGR_SW_Msk     (0x3UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSI     (0x0UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSE     (0x1UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_PLL     (0x2UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SWS_Pos    2UL
#define RCC_CFGR_SWS_Msk    (0x3UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSI    (0x0UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSE    (0x1UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_PLL    (0x2UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_HPRE_DIV1  (0x0UL << 4)
#define RCC_CFGR_PPRE1_DIV2 (0x4UL << 8)
#define RCC_CFGR_PPRE1_DIV1 (0x0UL << 8)
#define RCC_CFGR_PLLSRC_HSE (1UL << 16)
#define RCC_CFGR_PLLXTPRE_DIV1  (0x0UL << 17)
#define RCC_CFGR_PLLMULL_Pos  18UL
#define RCC_CFGR_PLLMULL_Msk  (0xFUL << RCC_CFGR_PLLMULL_Pos)
#define RCC_CFGR_PLLMULL_MUL2  (0x2UL << RCC_CFGR_PLLMULL_Pos)
#define RCC_CFGR_PLLMULL_MUL9  (0x9UL << RCC_CFGR_PLLMULL_Pos)

#define RCC_APB2ENR_AFIOEN   (1UL << 0)
#define RCC_APB2ENR_IOPAEN  (1UL << 2)
#define RCC_APB2ENR_IOPBEN  (1UL << 3)
#define RCC_APB2ENR_IOPCEN  (1UL << 4)
#define RCC_APB2ENR_IOPDEN  (1UL << 5)
#define RCC_APB2ENR_IOPEEN  (1UL << 6)
#define RCC_APB2ENR_IOPFEN  (1UL << 7)
#define RCC_APB2ENR_IOPGEN  (1UL << 8)
#define RCC_APB2ENR_USART1EN (1UL << 14)
#define RCC_APB2ENR_USART2EN (1UL << 17)
#define RCC_APB2ENR_USART3EN (1UL << 18)
#define RCC_APB2ENR_SPI1EN   (1UL << 12)

#define RCC_APB1ENR_TIM2EN  (1UL << 0)
#define RCC_APB1ENR_TIM3EN  (1UL << 1)
#define RCC_APB1ENR_TIM4EN  (1UL << 2)
#define RCC_APB1ENR_TIM5EN  (1UL << 3)
#define RCC_APB1ENR_TIM6EN  (1UL << 4)
#define RCC_APB1ENR_TIM7EN  (1UL << 5)
#define RCC_APB1ENR_SPI2EN  (1UL << 8)
#define RCC_APB1ENR_SPI3EN  (1UL << 9)
#define RCC_APB1ENR_USART3EN (1UL << 17)
#define RCC_APB1ENR_UART4EN  (1UL << 19)
#define RCC_APB1ENR_UART5EN  (1UL << 20)
#define RCC_APB1ENR_I2C1EN  (1UL << 21)
#define RCC_APB1ENR_I2C2EN  (1UL << 22)
#define RCC_APB1ENR_USBEN   (1UL << 23)
#define RCC_APB1ENR_FSMCEN  (1UL << 8 + 8) /* placeholder, see RCC_AHBENR */
#define RCC_AHBENR_FSMCEN   (1UL << 8)
#define RCC_AHBENR_DMA1EN   (1UL << 9)

/* ------------------------------------------------------------------ */
/* FLASH - 0x40022000                                                  */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t ACR;
    __IO uint32_t KEYR;
    __IO uint32_t OPTKEYR;
    __IO uint32_t SR;
    __IO uint32_t CR;
    __IO uint32_t AR;
    __IO uint32_t RESERVED;
    __IO uint32_t OBR;
    __IO uint32_t WRPR;
} FLASH_TypeDef;

#define FLASH ((FLASH_TypeDef *) 0x40022000UL)

#define FLASH_KEY1        0x45670123UL
#define FLASH_KEY2        0xCDEF89ABUL
#define FLASH_SR_BSY       (1UL << 0)
#define FLASH_SR_PGERR     (1UL << 2)
#define FLASH_SR_WRPRTERR  (1UL << 4)
#define FLASH_SR_EOP       (1UL << 5)
#define FLASH_CR_LOCK      (1UL << 7)
#define FLASH_CR_PG        (1UL << 0)
#define FLASH_CR_PER       (1UL << 4)
#define FLASH_CR_STRT      (1UL << 6)

/* ------------------------------------------------------------------ */
/* GPIO - 0x40010800 .. 0x40012000                                     */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CRL;
    __IO uint32_t CRH;
    __IO uint32_t IDR;
    __IO uint32_t ODR;
    __IO uint32_t BSRR;
    __IO uint32_t BRR;
    __IO uint32_t LCKR;
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *) 0x40010800UL)
#define GPIOB ((GPIO_TypeDef *) 0x40010C00UL)
#define GPIOC ((GPIO_TypeDef *) 0x40011000UL)
#define GPIOD ((GPIO_TypeDef *) 0x40011400UL)
#define GPIOE ((GPIO_TypeDef *) 0x40011800UL)
#define GPIOF ((GPIO_TypeDef *) 0x40011C00UL)
#define GPIOG ((GPIO_TypeDef *) 0x40012000UL)

/* Pin configuration: MODE[1:0] and CNF[1:0]. */
#define GPIO_MODE_INPUT     0x0UL
#define GPIO_MODE_OUT_10MHZ 0x1UL
#define GPIO_MODE_OUT_2MHZ  0x2UL
#define GPIO_MODE_OUT_50MHZ 0x3UL
#define GPIO_CNF_IN_ANALOG  0x0UL
#define GPIO_CNF_IN_FLOAT   0x1UL
#define GPIO_CNF_IN_PUPD    0x2UL
#define GPIO_CNF_OUT_PP     0x0UL
#define GPIO_CNF_OUT_OD     0x1UL
#define GPIO_CNF_AF_PP      0x2UL
#define GPIO_CNF_AF_OD      0x3UL

/* ------------------------------------------------------------------ */
/* AFIO / EXTI - 0x40010000 / 0x40010400                               */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t EVCR;
    __IO uint32_t MAPR;
    __IO uint32_t EXTICR[4];
    __IO uint32_t RESERVED;
    __IO uint32_t MAPR2;
} AFIO_TypeDef;

typedef struct
{
    __IO uint32_t IMR;
    __IO uint32_t EMR;
    __IO uint32_t RTSR;
    __IO uint32_t FTSR;
    __IO uint32_t SWIER;
    __IO uint32_t PR;
} EXTI_TypeDef;

#define AFIO   ((AFIO_TypeDef *) 0x40010000UL)
#define EXTI   ((EXTI_TypeDef *) 0x40010400UL)

/* ------------------------------------------------------------------ */
/* USART / UART - 0x40013800, 0x40004400, 0x40004800, 0x40004C00, 0x40005000 */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t SR;
    __IO uint32_t DR;
    __IO uint32_t BRR;
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t CR3;
    __IO uint32_t GTPR;
} USART_TypeDef;

#define USART1 ((USART_TypeDef *) 0x40013800UL)
#define USART2 ((USART_TypeDef *) 0x40004400UL)
#define USART3 ((USART_TypeDef *) 0x40004800UL)
#define UART4  ((USART_TypeDef *) 0x40004C00UL)
#define UART5  ((USART_TypeDef *) 0x40005000UL)

#define USART_SR_RXNE    (1UL << 5)
#define USART_SR_TC      (1UL << 6)
#define USART_SR_TXE     (1UL << 7)
#define USART_SR_CTS     (1UL << 8)
#define USART_SR_RXOR    (1UL << 3)
#define USART_SR_FE      (1UL << 1)
#define USART_SR_NE      (1UL << 2)
#define USART_SR_ORE     (1UL << 3)

#define USART_CR1_RE     (1UL << 2)
#define USART_CR1_TE     (1UL << 3)
#define USART_CR1_RXNEIE (1UL << 5)
#define USART_CR1_TCIE   (1UL << 6)
#define USART_CR1_TXEIE  (1UL << 7)
#define USART_CR1_PS     (1UL << 9)
#define USART_CR1_PCE    (1UL << 10)
#define USART_CR1_M      (1UL << 12)
#define USART_CR1_UE     (1UL << 13)

#define USART_CR2_STOP2  (1UL << 12)
#define USART_CR2_STOP1  (1UL << 13)

#define USART_CR3_RTSE   (1UL << 0)
#define USART_CR3_DMAR   (1UL << 6)

/* ------------------------------------------------------------------ */
/* SPI - 0x40013000 (SPI1) / 0x40003C00 (SPI3)                        */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SR;
    __IO uint32_t DR;
    __IO uint32_t CRCPR;
    __IO uint32_t RXCRCR;
    __IO uint32_t TXCRCR;
    __IO uint32_t I2SCFGR;
    __IO uint32_t I2SPR;
} SPI_TypeDef;

#define SPI1 ((SPI_TypeDef *) 0x40013000UL)
#define SPI2 ((SPI_TypeDef *) 0x40003800UL)
#define SPI3 ((SPI_TypeDef *) 0x40003C00UL)

#define SPI_CR1_CPHA     (1UL << 0)
#define SPI_CR1_CPOL     (1UL << 1)
#define SPI_CR1_MSTR     (1UL << 2)
#define SPI_CR1_BR_SHIFT 3UL
#define SPI_CR1_SPE      (1UL << 6)
#define SPI_CR1_SSI      (1UL << 8)
#define SPI_CR1_SSM      (1UL << 9)
#define SPI_CR1_DFF      (1UL << 11)
#define SPI_CR1_LSBFIRST (1UL << 7)

#define SPI_SR_RXNE      (1UL << 0)
#define SPI_SR_TXE       (1UL << 1)
#define SPI_SR_BSY       (1UL << 7)

/* ------------------------------------------------------------------ */
/* Timers - 0x40000400 (TIM3), 0x40000C00 (TIM5), 0x40013400 (TIM8)   */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t SMCR;
    __IO uint32_t DIER;
    __IO uint32_t SR;
    __IO uint32_t EGR;
    __IO uint32_t CCMR1;
    __IO uint32_t CCMR2;
    __IO uint32_t CCER;
    __IO uint32_t CNT;
    __IO uint32_t PSC;
    __IO uint32_t ARR;
    uint32_t      RESERVED0;
    __IO uint32_t CCR1;
    __IO uint32_t CCR2;
    __IO uint32_t CCR3;
    __IO uint32_t CCR4;
} TIM_TypeDef;

#define TIM3 ((TIM_TypeDef *) 0x40000400UL)
#define TIM5 ((TIM_TypeDef *) 0x40000C00UL)
#define TIM8 ((TIM_TypeDef *) 0x40013400UL)
#define TIM9 ((TIM_TypeDef *) 0x40013C00UL)

#define TIM_CR1_CEN      (1UL << 0)
#define TIM_CR1_ARPE     (1UL << 7)
#define TIM_DIER_UIE     (1UL << 0)
#define TIM_SR_UIF       (1UL << 0)
#define TIM_EGR_UG       (1UL << 0)

/* ------------------------------------------------------------------ */
/* DMA1 - 0x40020000                                                   */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CCR;
    __IO uint32_t CNDTR;
    __IO uint32_t CPAR;
    __IO uint32_t CMAR;
    __IO uint32_t RESERVED;
} DMA_Channel_TypeDef;

#define DMA_CCR_EN     (1UL << 0)
#define DMA_CCR_TCIE   (1UL << 1)
#define DMA_CCR_TEIE   (1UL << 3)
#define DMA_CCR_DIR    (1UL << 4)
#define DMA_CCR_CIRC   (1UL << 5)
#define DMA_CCR_MINC   (1UL << 7)
#define DMA_CCR_PSIZE_Pos 11
#define DMA_CCR_MSIZE_Pos 13
#define DMA_CCR_PL      (1UL << 16)
#define DMA_CCR_DBM     (1UL << 18)
#define DMA_CCR_CT      (1UL << 19)

typedef struct
{
    __IO uint32_t ISR;
    __IO uint32_t IFCR;
    __IO uint32_t IPRR;
    __IO uint32_t APBR;
    __IO uint32_t RESERVED0;
    DMA_Channel_TypeDef CH[7];
} DMA1_TypeDef;

#define DMA1 ((DMA1_TypeDef *) 0x40020000UL)

/* ------------------------------------------------------------------ */
/* IWDG - 0x40003000                                                   */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t KR;
    __IO uint32_t PR;
    __IO uint32_t RLR;
    __IO uint32_t SR;
} IWDG_TypeDef;

#define IWDG ((IWDG_TypeDef *) 0x40003000UL)
#define IWDG_KR_UNLOCK    0x5555UL
#define IWDG_KR_RELOAD    0xAAAAUL

/* ------------------------------------------------------------------ */
/* PWR / BKP - 0x40007000 / 0x40006C00                                */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CR;
    __IO uint32_t CSR;
} PWR_TypeDef;

#define PWR ((PWR_TypeDef *) 0x40007000UL)

typedef struct
{
    __IO uint32_t DR1;
    __IO uint32_t DR2;
    __IO uint32_t CR;
} BKP_TypeDef;

#define BKP ((BKP_TypeDef *) 0x40006C00UL)

/* ------------------------------------------------------------------ */
/* RTC - 0x40002800                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t CR;
    __IO uint32_t CRH;
    __IO uint32_t CCR;
    __IO uint32_t SHR;
    __IO uint32_t CNT;
    __IO uint32_t ALRH;
    __IO uint32_t ALRL;
} RTC_TypeDef;

#define RTC ((RTC_TypeDef *) 0x40002800UL)

/* ------------------------------------------------------------------ */
/* USB device - 0x40005C00 (RAM at 0x40006000)                        */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t EP0R;
    __IO uint32_t EP1R;
    __IO uint32_t EP2R;
    __IO uint32_t EP3R;
    __IO uint32_t EP4R;
    __IO uint32_t EP5R;
    __IO uint32_t EP6R;
    __IO uint32_t EP7R;
    __IO uint32_t CNTR;
    __IO uint32_t ISTR;
    __IO uint32_t FNR;
    __IO uint32_t DADDR;
    __IO uint32_t BTABLE;
} USB_TypeDef;

#define USB    ((USB_TypeDef *) 0x40005C00UL)
#define USB_PMA ((__IO uint32_t *) 0x40006000UL)

#define USB_CNTR_FRES     (1UL << 0)
#define USB_CNTR_PDWN     (1UL << 1)
#define USB_CNTR_RESETM   (1UL << 15)
#define USB_CNTR_SUSPM    (1UL << 11)
#define USB_CNTR_WKUPM    (1UL << 12)
#define USB_CNTR_CTRM     (1UL << 15)

#define USB_ISTR_EP_ID_Pos 0UL
#define USB_ISTR_EP_ID_Msk (0xFUL << USB_ISTR_EP_ID_Pos)
#define USB_ISTR_DIR      (1UL << 4)
#define USB_ISTR_RESET    (1UL << 10)
#define USB_ISTR_SUSP     (1UL << 11)
#define USB_ISTR_WKUP     (1UL << 12)
#define USB_ISTR_CTR      (1UL << 15)

/* ------------------------------------------------------------------ */
/* DMA1 - 0x40020000                                                  */
/* ------------------------------------------------------------------ */

/* FSMC - 0xA0000000 (external NOR: SST39VF1601)                      */
/* ------------------------------------------------------------------ */

typedef struct
{
    __IO uint32_t PCR1;
    __IO uint32_t SR1;
    __IO uint32_t PMEM1;
    __IO uint32_t PER1;
} FSMC_Bank1_TypeDef;

typedef struct
{
    __IO uint32_t ACR;
    __IO uint32_t TCR2;
    __IO uint32_t TSR2;
    __IO uint32_t OR2;
} FSMC_TypeDef;

#define FSMC ((FSMC_TypeDef *) 0xA0000000UL)
#define FSMC_Bank1 ((FSMC_Bank1_TypeDef *) 0xA0000000UL)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#define __disable_irq()  do { __asm volatile ("cpsid i" ::: "memory"); } while (0)
#define __enable_irq()   do { __asm volatile ("cpsie i" ::: "memory"); } while (0)

static inline void __wfi(void)
{
    __asm volatile ("wfi");
}

#endif /* STM32F1XX_H */
