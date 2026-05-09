#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// 音频采样率
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 音频 I2S 引脚（请根据实际硬件修改）
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_8
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_7
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_6
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_5

// 音频 Codec I2C
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_1
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_2
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_4
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR   // 通常是 0x18

// 按键（ESP32-C3 开发板 BOOT 键一般接 GPIO9）
#define BUILTIN_BUTTON_GPIO      GPIO_NUM_9

// LED 控制引脚（请根据您的接线设置）
#define LED_CONTROL_GPIO         GPIO_NUM_3

// UART1 与 Arduino Nano 通信
#define UART1_TX_PIN             GPIO_NUM_11
#define UART1_RX_PIN             GPIO_NUM_12

#endif