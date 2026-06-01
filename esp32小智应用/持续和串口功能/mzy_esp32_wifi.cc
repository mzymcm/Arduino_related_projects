#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "display/oled_display.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

// ==================== 新增 UART 相关头文件 ====================
#include <driver/uart.h>
#include <cstring>
#include <string>

#define TAG "ESP32-MarsbearSupport"

// ==================== 新增 UART 通信函数（模块化，不修改任何原有代码） ====================
static void uart1_init() {
    uart_config_t uart_config = {
        .baud_rate = UART1_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, UART1_BUFFER_SIZE, UART1_BUFFER_SIZE, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void uart1_send_string(const std::string& str) {
    uart_write_bytes(UART_NUM_1, str.c_str(), str.length());
}

static void uart1_rx_task(void *arg) {
    uint8_t *buffer = (uint8_t*)malloc(UART1_RX_BUF_SIZE);
    if (!buffer) {
        ESP_LOGE("UART", "Failed to allocate rx buffer");
        vTaskDelete(NULL);
        return;
    }
    std::string line;
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, buffer, UART1_RX_BUF_SIZE - 1, portMAX_DELAY);
        if (len > 0) {
            buffer[len] = '\0';
            for (int i = 0; i < len; ++i) {
                char c = buffer[i];
                if (c == '\n') {
                    if (!line.empty()) {
                        if (line.back() == '\r') line.pop_back();
                        if (!line.empty()) {
                            // 将收到的文本转给 Application 处理
                            Application::GetInstance().SendTextToAI(line);
                        }
                        line.clear();
                    }
                } else {
                    line.push_back(c);
                }
            }
        }
    }
    free(buffer);
    vTaskDelete(NULL);
}

// 提供给其他模块调用的发送接口
extern "C" void send_to_arduino(const char* str) {
    if (str) uart1_send_string(std::string(str));
}
// ==================== UART 函数结束 ====================

class CompactWifiBoard : public WifiBoard {
private:
    Button boot_button_;

    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = OLED_I2C_ADDR,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        // 配置 LED 引脚（低电平点亮）
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << BUILTIN_LED_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(BUILTIN_LED_GPIO, 0);  // 初始熄灭

        // BOOT 按键（GPIO0）
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            // LED 闪烁一下反馈
            gpio_set_level(BUILTIN_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(BUILTIN_LED_GPIO, 0);
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            EnterWifiConfigMode();
        });
    }

    void InitializeTools() {
        // 无额外工具
    }

public:
    CompactWifiBoard() : WifiBoard(), boot_button_(BOOT_BUTTON_GPIO)
    {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();

        // ==================== 新增：初始化 UART 并启动接收任务 ====================
        uart1_init();
        xTaskCreate(uart1_rx_task, "uart_rx", 4096, NULL, 5, NULL);
        // ==================== 新增结束 ====================
    }

    virtual AudioCodec* GetAudioCodec() override 
    {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);