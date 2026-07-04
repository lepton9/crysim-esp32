#pragma once

#include <sdkconfig.h>

/*ESP32S3*/
#define PIN_LCD_BL                   38

#define PIN_LCD_D0                   39
#define PIN_LCD_D1                   40
#define PIN_LCD_D2                   41
#define PIN_LCD_D3                   42
#define PIN_LCD_D4                   45
#define PIN_LCD_D5                   46
#define PIN_LCD_D6                   47
#define PIN_LCD_D7                   48

#define PIN_POWER_ON                 15

#define PIN_LCD_RES                  5
#define PIN_LCD_CS                   6
#define PIN_LCD_DC                   7
#define PIN_LCD_WR                   8
#define PIN_LCD_RD                   9

#define PIN_BUTTON_1                 0
#define PIN_BUTTON_2                 14
#define PIN_BAT_VOLT                 4

#define PIN_IIC_SCL                  17
#define PIN_IIC_SDA                  18

#define PIN_TOUCH_INT                16
#define PIN_TOUCH_RES                21


#define BOARD_NONE_PIN      (-1)

#if CONFIG_LILYGO_T_DISPLAY_S3

#define BOARD_POWERON        (gpio_num_t)(15)
#define BOARD_TFT_BL         (38)
#define BOARD_TFT_DATA0      (39)
#define BOARD_TFT_DATA1      (40)
#define BOARD_TFT_DATA2      (41)
#define BOARD_TFT_DATA3      (42)
#define BOARD_TFT_DATA4      (45)
#define BOARD_TFT_DATA5      (46)
#define BOARD_TFT_DATA6      (47)
#define BOARD_TFT_DATA7      (48)
#define BOARD_TFT_RST        (5)
#define BOARD_TFT_CS         (6)
#define BOARD_TFT_DC         (7)
#define BOARD_TFT_WR         (8)
#define BOARD_TFT_RD         (9)
#define BOARD_I2C_SCL        (17)
#define BOARD_I2C_SDA        (18)
#define BOARD_TOUCH_IRQ      (16)
#define BOARD_TOUCH_RST      (21)
#define AMOLED_WIDTH         (170)
#define AMOLED_HEIGHT        (320)

#define BOARD_HAS_TOUCH      0
#define DISPLAY_BUFFER_SIZE  (AMOLED_WIDTH * 20)
#define DISPLAY_FULLRESH     false

#define DISPLAY_HOR_RES      (AMOLED_HEIGHT)
#define DISPLAY_VER_RES      (AMOLED_WIDTH)

#endif
