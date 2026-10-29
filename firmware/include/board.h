/*
 * board.h - board-level pin and peripheral assignment.
 *
 * Everything that is specific to the Falcon HP PCB lives here so the
 * drivers stay portable. Facts below are marked with how they were
 * established:
 *
 *  [F]  from the shipped firmware (address or string)
 *  [P]  from the PCB photos
 *  [I]  inferred - needs schematic confirmation
 *
 * Flash layout [F]:
 *   0x08000000-0x08001257  bootloader   (at32_bootloader_6M.hex)
 *   0x08005000-0x080057FF  shared parameter block
 *   0x08005800-0x08094237  application  (Falcon-HP-144pin-8M-Mark-261001.hex)
 *
 * Terminals [P] from the board silkscreen, [F] matched against the HVAC
 * terminal-definition strings in the firmware:
 *
 *   B A G W2 W Y2 | Y1 O R Opd Gnd St
 *
 *   W   heat stage 1        [F] "Heating--W_OB"
 *   W2  aux heat stage 2    [F] "Aux Heating--W2_AUX"
 *   Y   cooling stage 1     [F] "1st compressor--Y"
 *   Y2  cooling stage 2     [F] "2nd compressor--Y2"
 *   G   fan                 [F] "Fan--G"
 *   O/B reversing valve     [F] "O/B Valve--W_OB"
 */

#ifndef BOARD_H
#define BOARD_H

#include "gpio.h"

/* ---- relays ------------------------------------------------------- */

/* The firmware exposes relay state as W1/W2/Y1/Y2/G in the Modbus and
 * BACnet point tables:
 *   0x080906C8  W1relaystatus | Heating relay W1 status | fn 0x08042674
 *   0x080906D8  W2relaystatus | Heating relay W2 status | fn 0x080426A8
 *   0x080906E8  Y1relaystatus | Cooling relay Y1 status | fn 0x08013268
 *   0x080906F8  Y2relaystatus | Cooling relay Y2 status | fn 0x0801329C
 *   0x08090708  Fanrelaystatus | Fan relay G status     | fn 0x0803EA9C
 *
 * Which physical GPIO drives each relay is [I]: the firmware only names
 * the logical relay. The assignment below is a placeholder matching the
 * common convention of grouping the outputs on one port; it must be
 * corrected from the schematic.
 */
extern const gpio_t board_relay_w1;
extern const gpio_t board_relay_w2;
extern const gpio_t board_relay_y1;
extern const gpio_t board_relay_y2;
extern const gpio_t board_relay_g;
extern const gpio_t board_relay_ob;   /* O/B reversing valve */

/* ---- serial ports -------------------------------------------------- */

/* [I] The image uses five UARTs. The ESP-07S modem speaks the Espressif
 * AT command set (AT+CWJAP, AT+BLEINIT, AT+BLUFI, ...) and the LoRa
 * module answers AT+DTRX / AT+CJOIN. Which port carries which is not
 * recorded in the firmware and must come from the schematic.
 */
extern USART_TypeDef *const board_uart_esp;
extern USART_TypeDef *const board_uart_lora;
extern USART_TypeDef *const board_uart_rs485;
extern USART_TypeDef *const board_uart_debug;

/* ---- display ------------------------------------------------------- */

/* [P] An FPC ribbon connects a 480x320 colour panel. bg.bmp in the
 * supplied asset set is exactly 480x320, which fixes the panel geometry.
 * The asset set is 32bpp BGRA with 24bpp on 12 files (digitbg.bmp and
 * friends), so the framebuffer is BGRA / RGB565.
 */
#define BOARD_LCD_WIDTH        480
#define BOARD_LCD_HEIGHT       320

/* [F] SPI3 (0x40003C00) is referenced 20 times and SPI1 47 times; the
 * display is on SPI. Which of the two carries the panel is [I].
 */
extern SPI_TypeDef *const board_lcd_spi;

/* ---- external NOR flash (UI assets) --------------------------------- */

/* [F] The firmware names the part "Norflash SST39VF1601" and drives it
 * through FSMC (0xA0000000, references at 0x08083790 use the classic
 * F1 "subtract 0x60000000" bank-address trick), not SPI. The SST39VF1601
 * is a 16 Mbit (2 MiB) x8/x16 asynchronous NOR, which matches the FAT32
 * volume label ("MSDOS5.0", "NO NAME    FAT32", volume "AT32") that the
 * firmware parses at 0x080703C7.
 */
#define BOARD_NOR_BASE_ADDR    0x60000000UL
#define BOARD_NOR_SIZE_BYTES   (2UL * 1024UL * 1024UL)

/* ---- asset store ---------------------------------------------------- */

/* [F] The firmware's bitmap paths look like "\fan\10.bmp",
 * "\nfan\10.bmp", "\White\105.bmp", "\Gray\10.bmp", "\Heatbar\10.bmp" and
 * the supplied asset tree has fan/, nfan/, White/, Gray/ - matching.
 */
#define BOARD_ASSET_ROOT       "\\"
#define BOARD_ASSET_SUBDIRS    "fan", "nfan", "White", "Gray"

void board_init(void);

/* Relay helpers: relays are active-level, driven through board_relay_*. */
void board_relay_set(int index, int on);

#endif /* BOARD_H */
