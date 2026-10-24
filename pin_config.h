#pragma once

// Pines oficiales de la Waveshare ESP32-S3-Touch-AMOLED-1.75 y -2.16 (mismos pines)
// Fuentes: github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75 y ...-2.16
//   (libraries/Mylibrary/pin_config.h)
//
// Placa: por defecto la 1.75 (redonda, 466x466). Para la 2.16 (cuadrada, 480x480)
// descomenta la linea siguiente o compila con -DBOARD_AMOLED_216.
// #define BOARD_AMOLED_216

#define XPOWERS_CHIP_AXP2101

// Pantalla AMOLED 466x466, driver CO5300 por QSPI
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET 39
// -DLCD_WIDTH/-DLCD_HEIGHT sustituyen el tamano (solo el renderer del host lo usa)
#ifdef BOARD_AMOLED_216
#define LCD_SIZE_DEFAULT 480
#define LCD_ROUND_DEFAULT 0
#define LCD_OFF_X 0  // columna/fila del CO5300 donde empieza el lienzo
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
// 1 = pantalla redonda (menus sobre un disco), 0 = cuadrada (menus a pantalla completa)
#ifndef LCD_ROUND
#define LCD_ROUND LCD_ROUND_DEFAULT
#endif

// Táctil capacitivo CST9217 (1.75) / CST9220 (2.16) por I2C
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT 11
#define TP_RESET 40

// Audio ES8311. NOTA: el MCLK real es GPIO42 (verificado en placa con el
// proyecto PlaneRadar2.0); el 16 que figuraba era erroneo. Aun asi el codec se
// configura con reloj derivado del BCLK, asi que el MCLK apenas importa.
#define I2S_MCK_IO 42
#define I2S_BCK_IO 9
#define I2S_DI_IO 10
#define I2S_WS_IO 45
#define I2S_DO_IO 8
#define PA 46

// Ranura TF (no usada todavía)
#define SDMMC_CLK 2
#define SDMMC_CMD 1
#define SDMMC_DATA 3
#define SDMMC_CS 41
