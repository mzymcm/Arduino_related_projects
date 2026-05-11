#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"
#include "display/oled_display.h"

#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#define TAG "MyESP32Board"

// ==================== 空背光 ====================
class NullBacklight : public Backlight {
protected:
    virtual void SetBrightnessImpl(uint8_t brightness) override {
        (void)brightness;
    }
};

// ==================== 开发板主类 ====================
class MyESP32Board : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    OledDisplay* display_ = nullptr;

    // ---------- 音频 I2C 初始化 ----------
    void InitializeAudioI2c() {
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

    // ---------- 显示屏 I2C 初始化 ----------
    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = OLED_SDA_PIN,
            .scl_io_num = OLED_SCL_PIN,
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

    // ---------- SSD1306 显示屏初始化 ----------
    void InitializeSsd1306Display() {
        // I2C 接口配置
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
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

        // SSD1306 驱动配置
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = OLED_RST_PIN;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = (uint8_t)OLED_RESOLUTION_HEIGHT,
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));

        // 复位并初始化显示屏
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        // 可选的镜像设置（根据实际屏幕方向决定）
        // ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, false, false));

        display_ = new OledDisplay(panel_io_, panel_, OLED_RESOLUTION_WIDTH, OLED_RESOLUTION_HEIGHT, false, false);
        ESP_LOGI(TAG, "SSD1306 display initialized");
    }

    // ---------- UART1 初始化 ----------
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
        // 初始化 LED GPIO
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << LED_CONTROL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(LED_CONTROL_GPIO, 0);

        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
        boot_button_.OnLongPress([this]() {
            EnterWifiConfigMode();
        });
    }

    // ---------- MCP 工具 ----------
    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.led.turn_on", "打开LED", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                gpio_set_level(LED_CONTROL_GPIO, 1);
                return "LED已打开";
            });

        mcp.AddTool("self.led.turn_off", "关闭LED", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                gpio_set_level(LED_CONTROL_GPIO, 0);
                return "LED已关闭";
            });

        mcp.AddTool("self.led.get_state", "查询LED状态", PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                int level = gpio_get_level(LED_CONTROL_GPIO);
                return level ? "LED已打开" : "LED已关闭";
            });

        mcp.AddTool("self.uart.send_to_arduino", "向Arduino发送文本",
            PropertyList({ Property("message", kPropertyTypeString, "消息内容") }),
            [this](const PropertyList& props) -> ReturnValue {
                std::string msg = props["message"].value<std::string>() + "\r\n";
                int len = uart_write_bytes(UART_NUM_1, msg.c_str(), msg.length());
                return len > 0 ? "已发送：" + props["message"].value<std::string>() : "发送失败";
            });
    }

public:
    MyESP32Board() : boot_button_(BUILTIN_BUTTON_GPIO) {
        InitializeAudioI2c();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeUart1();
        InitializeButtons();
        InitializeTools();
        ESP_LOGI(TAG, "MyESP32Board fully initialized");
    }

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

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static NullBacklight null_bl;
        return &null_bl;
    }
};

DECLARE_BOARD(MyESP32Board);