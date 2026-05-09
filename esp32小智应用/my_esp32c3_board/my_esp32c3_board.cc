#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"

#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MyESP32C3Board"

// ==================== 空背光实现（无显示屏） ====================
class NullBacklight : public Backlight {
protected:
    virtual void SetBrightnessImpl(uint8_t brightness) override {
        (void)brightness;   // 不做任何实际控制
    }
};

// ==================== 开发板主类 ====================
class MyESP32C3Board : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t codec_i2c_bus_;

    // ---------- I2C 初始化（音频编解码器）----------
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 }
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    // ---------- UART1 初始化（与 Arduino Nano 通信）----------
    void InitializeUart1() {
        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT
        };
        ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART1_TX_PIN, UART1_RX_PIN,
                                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 2048, 2048, 20, NULL, 0));

        // 接收任务：打印来自 Arduino 的数据
        xTaskCreate([](void* arg) {
            uint8_t buffer[256];
            while (1) {
                int len = uart_read_bytes(UART_NUM_1, buffer, sizeof(buffer)-1, pdMS_TO_TICKS(100));
                if (len > 0) {
                    buffer[len] = '\0';
                    ESP_LOGI(TAG, "Received from Arduino: %s", (char*)buffer);
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }, "uart_rx_task", 4096, this, 5, NULL);

        ESP_LOGI(TAG, "UART1 initialized (TX=%d, RX=%d)", UART1_TX_PIN, UART1_RX_PIN);
    }

    // ---------- 按键初始化 ----------
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
        boot_button_.OnLongPress([this]() {
            EnterWifiConfigMode();
        });
    }

    // ---------- MCP 工具注册（语音控制接口）----------
    void InitializeTools() {
        // 配置 LED 引脚为推挽输出
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << LED_CONTROL_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(LED_CONTROL_GPIO, 0);   // 初始关闭

        auto& mcp = McpServer::GetInstance();

        // 1. 打开 LED
        mcp.AddTool("self.led.turn_on", "打开LED", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                gpio_set_level(LED_CONTROL_GPIO, 1);
                ESP_LOGI(TAG, "LED ON");
                return "LED已打开";
            });

        // 2. 关闭 LED
        mcp.AddTool("self.led.turn_off", "关闭LED", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                gpio_set_level(LED_CONTROL_GPIO, 0);
                ESP_LOGI(TAG, "LED OFF");
                return "LED已关闭";
            });

        // 3. 查询 LED 状态
        mcp.AddTool("self.led.get_state", "查询LED状态", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                int level = gpio_get_level(LED_CONTROL_GPIO);
                bool is_on = (level == 1);
                ESP_LOGI(TAG, "LED state: %s", is_on ? "ON" : "OFF");
                return is_on ? "LED已打开" : "LED已关闭";
            });

        // 4. 向 Arduino Nano 发送文本
        mcp.AddTool("self.uart.send_to_arduino", "向Arduino发送文本",
            PropertyList({ Property("message", kPropertyTypeString, "消息内容") }),
            [this](const PropertyList& props) -> ReturnValue {
                std::string msg = props["message"].value<std::string>();
                msg += "\r\n";   // Arduino 的 Serial.readStringUntil('\n') 会以此结束
                int len = uart_write_bytes(UART_NUM_1, msg.c_str(), msg.length());
                if (len > 0) {
                    ESP_LOGI(TAG, "Sent to Arduino: %s", msg.c_str());
                    return "已发送：" + props["message"].value<std::string>();
                } else {
                    return "发送失败";
                }
            });
    }

public:
    MyESP32C3Board() : boot_button_(BUILTIN_BUTTON_GPIO) {
        InitializeI2c();
        InitializeUart1();
        InitializeButtons();
        InitializeTools();
        ESP_LOGI(TAG, "MyESP32C3Board fully initialized");
    }

    // 音频编解码器（ES8311）
    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR
        );
        return &audio_codec;
    }

    // 无显示屏
    virtual Display* GetDisplay() override {
        return nullptr;
    }

    // 背光：返回空实现（不会导致崩溃）
    virtual Backlight* GetBacklight() override {
        static NullBacklight null_bl;
        return &null_bl;
    }
};

// 注册开发板
DECLARE_BOARD(MyESP32C3Board);