#pragma once

// Pines oficiales de la Waveshare ESP32-S3-Touch-AMOLED-1.75 y -2.16 (mismos pines)
// Fuentes: github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75 y ...-2.16
//   (libraries/Mylibrary/pin_config.h)
//
// Placa: por defecto la 1.75 (redonda). Para la 2.16 (cuadrada 480x480) descomenta
// la linea siguiente o compila con -DBOARD_AMOLED_216.
// #define BOARD_AMOLED_216

#define XPOWERS_CHIP_AXP2101

// Pantalla AMOLED, driver CO5300 por QSPI
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET 39
// El juego se dibuja siempre en 466x466 (UI_SIZE). LCD_OFF_* es donde cae ese
// cuadro dentro del panel: en la 2.16 (480x480) queda centrado con 7 px negros
// alrededor (shortcut: sin reescalar la UI; si se quiere usar los 480 completos,
// hay que reajustar las coordenadas fijas de TamaPoke.ino).
#define UI_SIZE 466
#ifdef BOARD_AMOLED_216
#define LCD_PANEL_SIZE 480
#define LCD_OFF_X 7
#define LCD_OFF_Y 7
#else
#define LCD_PANEL_SIZE 466
#define LCD_OFF_X 6
#define LCD_OFF_Y 0
#endif
#define LCD_WIDTH UI_SIZE
#define LCD_HEIGHT UI_SIZE

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

// Ranura TF (en la 2.16 igual: GPIO1 CMD, GPIO2 CLK, GPIO3 D0, GPIO41 CS, segun esquema)
#define SDMMC_CLK 2
#define SDMMC_CMD 1
#define SDMMC_DATA 3
#define SDMMC_CS 41
