/*
 * adc.c - the MCU's ADC (AT32 address 0x40013C00).
 *
 * The register offsets and bit positions are the ones the shipped
 * firmware touches; see adc.h and docs/hw-map.md.
 */

#include "adc.h"
#include "stm32f1xx.h"
#include "gpio.h"

typedef struct
{
    __IO uint32_t CTRL1;      /* 0x00 */
    __IO uint32_t CTRL2;      /* 0x04 */
    __IO uint32_t CTRL3;      /* 0x08 */
    __IO uint32_t CHSEL1;     /* 0x0C - eight three-bit fields */
    __IO uint32_t CHSEL2;     /* 0x10 - ten three-bit fields */
    __IO uint32_t RESERVED1[6];
    __IO uint32_t SMPR;       /* 0x2C */
    __IO uint32_t RESERVED2;  /* 0x30 */
    __IO uint32_t SEQ;        /* 0x34 - five-bit channel fields */
    __IO uint32_t RESERVED3[6];
    __IO uint16_t DATA;       /* 0x4C */
} ADC_TypeDef;

#define ADC ((ADC_TypeDef *) ADC_BASE_ADDR)

#define ADC_CTRL3_EOC        (1UL << 2)
#define ADC_CTRL3_START      (1UL << 3)
#define ADC_CTRL3_ENABLE     (1UL << 22)
#define ADC_CTRL2_DMA        (1UL << 8)

/* The analog inputs the original configures: PA6 and PA7 always, PA8
 * only in the variant whose configuration byte reads 'W' (0x57) - the
 * third NTC channel. */
#define ADC_PINS_MASK        0x00C0UL
#define ADC_PINS_MASK_WIDE   0x01C0UL

void adc_init(void)
{
    gpio_t pin;

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /* Analog inputs: floating input, no pull. */
    for (pin.pin = 6; pin.pin <= 8; pin.pin++)
    {
        gpio_init(pin, GPIO_MODE_IN_ANALOG, GPIO_SPEED_2MHZ);
    }

    ADC->CTRL3 |= ADC_CTRL3_ENABLE;
}

static void adc_select(uint8_t logical, uint8_t channel)
{
    uint32_t shift;
    uint32_t value;

    if (logical < 10)
    {
        shift = (uint32_t)logical * 3;
        value = ADC->CHSEL2;
        value &= ~(0x7UL << shift);
        value |= ((uint32_t)channel & 0x7UL) << shift;
        ADC->CHSEL2 = value;
    }
    else if (logical < 18)
    {
        shift = (uint32_t)(logical - 10) * 3;
        value = ADC->CHSEL1;
        value &= ~(0x7UL << shift);
        value |= ((uint32_t)channel & 0x7UL) << shift;
        ADC->CHSEL1 = value;
    }
}

uint16_t adc_read(uint8_t logical, uint8_t channel)
{
    uint32_t guard;

    adc_select(logical, channel);

    ADC->CTRL3 |= ADC_CTRL3_ENABLE;

    /* Clear the end-of-conversion flag before sampling. */
    ADC->CTRL1 &= ~(1UL << 1);

    guard = 0x10000UL;
    while ((ADC->CTRL3 & ADC_CTRL3_START) && guard--)
    {
    }

    /* The original returns the counts inverted: 0xFFF - data. */
    return (uint16_t)(ADC_FULL_SCALE - (uint16_t)ADC->DATA);
}
