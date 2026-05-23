#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// *********************** 音频采样率 ***********************
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// *********************** 使用 Simplex I2S 模式（独立 MIC / SPK）***********************
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

// INMP441 I2S 麦克风
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_25   // WS / LRC
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_26   // BCLK / SCK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_32   // SD / DIN

// MAX98357A I2S 功放
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_33   // DIN
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_14   // BCLK
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_27   // LRC

// 注意：没有连接 PA 使能引脚，因此不定义 AUDIO_PA_ENABLE_PIN

#else
// 如果使用 Duplex I2S 模式（未使用）
#endif

// *********************** 按键（低电平有效）***********************
#define BOOT_BUTTON_GPIO         GPIO_NUM_0   // 板载 BOOT 键

// *********************** 外接 LED 指示灯 ***********************
#define BUILTIN_LED_GPIO         GPIO_NUM_2   // 低电平点亮

// *********************** 0.96 英寸 OLED (SSD1306, I2C) ***********************
#define DISPLAY_SDA_PIN          GPIO_NUM_16
#define DISPLAY_SCL_PIN          GPIO_NUM_17
#define DISPLAY_WIDTH            128
#define DISPLAY_HEIGHT           64
#define DISPLAY_MIRROR_X         true
#define DISPLAY_MIRROR_Y         true
#define OLED_I2C_ADDR            0x3C

// *********************** 自定义 UART1（与 Arduino Nano 通信）***********************
#define UART1_TX_PIN             GPIO_NUM_21
#define UART1_RX_PIN             GPIO_NUM_22
#define UART1_BAUD_RATE          9600

// *********************** 新增 UART 缓冲区配置（原有功能不变） ***********************
#define UART1_BUFFER_SIZE        2048
#define UART1_RX_BUF_SIZE        256

#endif // _BOARD_CONFIG_H_