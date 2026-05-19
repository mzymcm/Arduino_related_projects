#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "display/oled_display.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include <map>
#include <vector>
#include <cstring>
#include "assets/lang_config.h"

#define TAG "MyESP32Board"

class MyEsp32Board : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;

    std::map<std::vector<std::string>, std::string> command_map_ = {
        {{"开灯", "打开灯"}, "open_led"},
        {{"关灯", "关闭灯", "关上灯"}, "close_led"},
        {{"打开电视", "开电视", "看电视"}, "open_tv"},
        {{"关上电视", "关闭电视", "关电视", "不看电视"}, "close_tv"},
        {{"拉开窗帘", "打开窗帘", "天亮了"}, "open_cut"},
        {{"拉上窗帘", "关闭窗帘", "天黑了"}, "close_cut"}
    };

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = OLED_SDA_PIN,
            .scl_io_num = OLED_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = OLED_I2C_ADDR,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = { .dc_low_on_data = 0, .disable_control_phase = 0 },
            .scl_speed_hz = 400 * 1000,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = OLED_RST_PIN;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = OLED_RESOLUTION_HEIGHT,
        };
        panel_config.vendor_config = &ssd1306_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
        display_ = new OledDisplay(panel_io_, panel_, OLED_RESOLUTION_WIDTH, OLED_RESOLUTION_HEIGHT, true, true);
    }

    void InitializeUart() {
        uart_config_t uart_config = {
            .baud_rate = UART1_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 1024 * 2, 0, 0, NULL, 0));
        xTaskCreate([](void* arg) {
            uint8_t buffer[512];
            while (true) {
                int len = uart_read_bytes(UART_NUM_1, buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));
                if (len > 0) {
                    buffer[len] = '\0';
                    std::string text((char*)buffer, len);
                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                    ESP_LOGI(TAG, "Received from Nano: %s", text.c_str());
                    Application::GetInstance().InjectUserText(text);
                }
            }
        }, "uart_rx", 4096, nullptr, 5, NULL);
    }

    void InitializeGpio() {
        gpio_config_t pa_enable = {
            .pin_bit_mask = 1ULL << AUDIO_PA_ENABLE_PIN,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_enable);
        gpio_set_level(AUDIO_PA_ENABLE_PIN, 1);
        ESP_LOGI(TAG, "Audio PA enabled (pin %d)", AUDIO_PA_ENABLE_PIN);

        gpio_config_t led_conf = {
            .pin_bit_mask = 1ULL << LED_CONTROL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&led_conf);
        gpio_set_level(LED_CONTROL_GPIO, 0);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().ToggleChatState();
        });
        boot_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Long press, reboot");
            Application::GetInstance().Reboot();
        });
    }

    void SendToNano(const std::string& data) {
        uart_write_bytes(UART_NUM_1, data.c_str(), data.size());
        uart_write_bytes(UART_NUM_1, "\n", 1);
    }

public:
    MyEsp32Board() : WifiBoard(), boot_button_(BOOT_BUTTON_GPIO) {
        InitializeGpio();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeUart();
        InitializeButtons();
        ESP_LOGI(TAG, "Board initialized, auto-listen will be triggered when system is idle");
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            I2S_SPK_BCLK_PIN, I2S_SPK_WS_PIN, I2S_SPK_DIN_PIN,
            I2S_MIC_BCLK_PIN, I2S_MIC_WS_PIN, I2S_MIC_DIN_PIN);
#else
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            I2S_SPK_BCLK_PIN, I2S_SPK_WS_PIN, I2S_SPK_DIN_PIN, I2S_MIC_DIN_PIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual void OnSttText(const std::string& text) override {
        ESP_LOGI(TAG, "STT text: %s", text.c_str());
        for (const auto& entry : command_map_) {
            for (const auto& phrase : entry.first) {
                if (text.find(phrase) != std::string::npos) {
                    SendToNano(entry.second);
                    Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                    return;
                }
            }
        }
    }
};

DECLARE_BOARD(MyEsp32Board);