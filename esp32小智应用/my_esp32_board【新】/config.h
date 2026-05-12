#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// 音频采样率（ESP32 双核 4MB Flash 建议 16000 以节省 RAM）
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// 音频 I2S 引脚
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_NC      // 不使用 MCLK
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_26
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_25
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_33
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_32

// 音频 Codec I2C
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_21
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_22
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_27
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR   // 0x18

// 按键（ESP32 开发板 BOOT 键一般接 GPIO0，按下为低电平）
#define BUILTIN_BUTTON_GPIO      GPIO_NUM_0

// LED 控制引脚（使用安全 GPIO13，高电平点亮）
#define LED_CONTROL_GPIO         GPIO_NUM_13

// UART1 与 Arduino Nano 通信
#define UART1_TX_PIN             GPIO_NUM_17
#define UART1_RX_PIN             GPIO_NUM_16

// ==================== SSD1306 OLED 显示屏配置 ====================
#define OLED_SDA_PIN             GPIO_NUM_19
#define OLED_SCL_PIN             GPIO_NUM_18
#define OLED_RST_PIN             GPIO_NUM_NC      // 无复位引脚
#define OLED_RESOLUTION_WIDTH    128
#define OLED_RESOLUTION_HEIGHT   64

#endif