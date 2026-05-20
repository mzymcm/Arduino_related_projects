#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ==================== 音频采样率 ====================
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// ==================== INMP441 I2S 麦克风引脚 ====================
#define I2S_MIC_BCLK_PIN         GPIO_NUM_26   // SCK
#define I2S_MIC_WS_PIN           GPIO_NUM_25   // WS / LRC
#define I2S_MIC_DIN_PIN          GPIO_NUM_32   // SD

// ==================== MAX98357A I2S 功放引脚 ====================
#define I2S_SPK_BCLK_PIN         GPIO_NUM_14   // BCLK
#define I2S_SPK_WS_PIN           GPIO_NUM_27   // LRC
#define I2S_SPK_DIN_PIN          GPIO_NUM_33   // DIN
#define AUDIO_PA_ENABLE_PIN      GPIO_NUM_12   // SD（高电平使能）

// ==================== 按键（BOOT键，按下低电平）====================
#define BUILTIN_BUTTON_GPIO      GPIO_NUM_0

// ==================== 板载 LED（仅用于状态指示）====================
#define LED_CONTROL_GPIO         GPIO_NUM_13

// ==================== 自定义UART 与 Arduino Nano 通信 ====================
#define UART1_TX_PIN             GPIO_NUM_19
#define UART1_RX_PIN             GPIO_NUM_18
#define UART1_BAUD_RATE          9600

// ==================== SSD1306 OLED 显示屏（I2C）====================
#define OLED_SDA_PIN             GPIO_NUM_21
#define OLED_SCL_PIN             GPIO_NUM_22
#define OLED_RST_PIN             GPIO_NUM_NC
#define OLED_RESOLUTION_WIDTH    128
#define OLED_RESOLUTION_HEIGHT   64
#define OLED_I2C_ADDR            0x3C

// 显示镜像（可选）
#define DISPLAY_MIRROR_X         true
#define DISPLAY_MIRROR_Y         true

// 音频I2S模式（使用双工或单工，根据实际硬件选择）
#define AUDIO_I2S_METHOD_SIMPLEX

#endif // _BOARD_CONFIG_H_