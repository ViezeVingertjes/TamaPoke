#pragma once

// Official pins of the Waveshare ESP32-S3-Touch-AMOLED-1.75 and -2.16 (same pins)
// Sources: github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75 y ...-2.16
//   (libraries/Mylibrary/pin_config.h)
//
// Board: the 1.75 by default (round, 466x466). For the 2.16 (square, 480x480)
// uncomment the next line or build with -DBOARD_AMOLED_216.
// #define BOARD_AMOLED_216

#define XPOWERS_CHIP_AXP2101

// AMOLED display 466x466, CO5300 driver over QSPI
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET 39
// -DLCD_WIDTH/-DLCD_HEIGHT override the size (only the host renderer uses it)
#ifdef BOARD_AMOLED_216
#define LCD_SIZE_DEFAULT 480
#define LCD_ROUND_DEFAULT 0
#define LCD_OFF_X 0  // CO5300 column/row where the canvas starts
#define LCD_OFF_Y 0
#else
#define LCD_SIZE_DEFAULT 466
#define LCD_ROUND_DEFAULT 1
#define LCD_OFF_X 6
#define LCD_OFF_Y 0
#endif
#ifndef LCD_WIDTH
#define LCD_WIDTH LCD_SIZE_DEFAULT
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT LCD_SIZE_DEFAULT
#endif
// 1 = round display (menus on a disc), 0 = square (full-screen menus)
#ifndef LCD_ROUND
#define LCD_ROUND LCD_ROUND_DEFAULT
#endif

// CST9217 (1.75) / CST9220 (2.16) capacitive touch over I2C
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT 11
#define TP_RESET 40

// ES8311 audio. NOTE: the real MCLK is GPIO42 (verified on the board with the
// PlaneRadar2.0 project); the 16 that was listed was wrong. Even so the codec is
// configured with a clock derived from BCLK, so MCLK hardly matters.
#define I2S_MCK_IO 42
#define I2S_BCK_IO 9
#define I2S_DI_IO 10
#define I2S_WS_IO 45
#define I2S_DO_IO 8
#define PA 46

// TF slot (not used yet)
#define SDMMC_CLK 2
#define SDMMC_CMD 1
#define SDMMC_DATA 3
#define SDMMC_CS 41
