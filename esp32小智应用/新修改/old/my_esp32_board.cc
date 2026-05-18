#include "wifi_board.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"
#include "display/oled_display.h"
#include "esp_wifi.h"

#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <vector>
#include <unordered_map>

#define TAG "MyESP32Board"

// ==================== 空背光 ====================
class NullBacklight : public Backlight {
protected:
    virtual void SetBrightnessImpl(uint8_t brightness) override { (void)brightness; }
};

// ==================== 虚拟显示器 ====================
class DummyDisplay : public Display {
public:
    virtual void SetupUI() override {}
    virtual void SetChatMessage(const char* role, const char* message) override {}
    virtual void SetEmotion(const char* emotion) override {}
    virtual void SetStatus(const char* status) override {}
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override {}
    virtual void ClearChatMessages() override {}
    virtual void UpdateStatusBar(bool force = false) override {}
    virtual bool Lock(int timeout_ms = 0) override { return true; }
    virtual void Unlock() override {}
};

// ==================== I2S 音频编解码器（来自旧版，已验证可用）====================
class I2SAudioCodec : public AudioCodec {
public:
    I2SAudioCodec() {
        ESP_LOGI(TAG, "Initializing I2S Audio Codec");

        input_sample_rate_ = AUDIO_INPUT_SAMPLE_RATE;
        output_sample_rate_ = AUDIO_OUTPUT_SAMPLE_RATE;

        // 配置功放使能引脚（初始关闭）
        gpio_config_t pa_conf = {
            .pin_bit_mask = 1ULL << AUDIO_PA_ENABLE_PIN,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&pa_conf);
        gpio_set_level(AUDIO_PA_ENABLE_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(10));

        // ---------- 麦克风通道 (RX) ----------
        i2s_chan_config_t mic_chan_cfg = {
            .id = I2S_NUM_0,
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = 8,
            .dma_frame_num = 1024,
            .auto_clear = true,
        };
        esp_err_t err = i2s_new_channel(&mic_chan_cfg, NULL, &rx_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create mic channel: %s", esp_err_to_name(err));
            return;
        }

        i2s_std_config_t mic_std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_INPUT_SAMPLE_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = GPIO_NUM_NC,
                .bclk = I2S_MIC_BCLK_PIN,
                .ws = I2S_MIC_WS_PIN,
                .dout = GPIO_NUM_NC,
                .din = I2S_MIC_DIN_PIN,
                .invert_flags = { false, false, false }
            },
        };
        err = i2s_channel_init_std_mode(rx_handle_, &mic_std_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init mic I2S: %s", esp_err_to_name(err));
            return;
        }
        err = i2s_channel_enable(rx_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable mic I2S: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Mic enabled (BCLK=%d, WS=%d, DIN=%d)", I2S_MIC_BCLK_PIN, I2S_MIC_WS_PIN, I2S_MIC_DIN_PIN);
        }

        // ---------- 扬声器通道 (TX) ----------
        i2s_chan_config_t spk_chan_cfg = {
            .id = I2S_NUM_1,
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = 8,
            .dma_frame_num = 1024,
            .auto_clear = true,
        };
        err = i2s_new_channel(&spk_chan_cfg, &tx_handle_, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create speaker channel: %s", esp_err_to_name(err));
            return;
        }

        i2s_std_config_t spk_std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_OUTPUT_SAMPLE_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = GPIO_NUM_NC,
                .bclk = I2S_SPK_BCLK_PIN,
                .ws = I2S_SPK_WS_PIN,
                .dout = I2S_SPK_DIN_PIN,
                .din = GPIO_NUM_NC,
                .invert_flags = { false, false, false }
            },
        };
        err = i2s_channel_init_std_mode(tx_handle_, &spk_std_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init speaker I2S: %s", esp_err_to_name(err));
            return;
        }
        err = i2s_channel_enable(tx_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable speaker I2S: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Speaker enabled (BCLK=%d, WS=%d, DIN=%d)", I2S_SPK_BCLK_PIN, I2S_SPK_WS_PIN, I2S_SPK_DIN_PIN);
        }

        // 强制开启麦克风输入（重要！）
        EnableInput(true);
        ESP_LOGI(TAG, "I2SAudioCodec ready, input enabled");
    }

    virtual ~I2SAudioCodec() {
        if (rx_handle_) {
            i2s_channel_disable(rx_handle_);
            i2s_del_channel(rx_handle_);
        }
        if (tx_handle_) {
            i2s_channel_disable(tx_handle_);
            i2s_del_channel(tx_handle_);
        }
        gpio_set_level(AUDIO_PA_ENABLE_PIN, 0);
    }

    virtual int Read(int16_t* dest, int samples) override {
        if (!rx_handle_) return 0;
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(rx_handle_, dest, samples * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(err));
            return 0;
        }
        return bytes_read / sizeof(int16_t);
    }

    virtual int Write(const int16_t* src, int samples) override {
        if (!tx_handle_) {
            ESP_LOGW(TAG, "Speaker handle is null");
            return 0;
        }

        // 首次写入时使能功放
        if (!pa_enabled_) {
            gpio_set_level(AUDIO_PA_ENABLE_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            pa_enabled_ = true;
            ESP_LOGI(TAG, "PA enabled (first write)");
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(tx_handle_, src, samples * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(err));
            return 0;
        }
        return bytes_written / sizeof(int16_t);
    }

private:
    bool pa_enabled_ = false;
};

// ==================== 命令映射器（支持多短语→标准指令）====================
class CommandMapper {
private:
    std::unordered_map<std::string, std::string> phrase_to_cmd_;
public:
    CommandMapper() {
        AddMapping({"开灯", "打开灯", "把灯打开", "亮灯"}, "open_led");
        AddMapping({"关灯", "关闭灯", "关上灯", "熄灯"}, "close_led");
        AddMapping({"打开电视", "开电视", "看电视", "启动电视"}, "open_tv");
        AddMapping({"关上电视", "关闭电视", "关电视", "不看电视"}, "close_tv");
        AddMapping({"拉开窗帘", "打开窗帘", "天亮了", "升起窗帘"}, "open_curtain");
        AddMapping({"拉上窗帘", "关闭窗帘", "天黑了", "降下窗帘"}, "close_curtain");
    }

    void AddMapping(const std::vector<std::string>& phrases, const std::string& cmd) {
        for (const auto& p : phrases) phrase_to_cmd_[p] = cmd;
        ESP_LOGI(TAG, "Mapping: %s -> %s", phrases.front().c_str(), cmd.c_str());
    }

    std::string FindCommand(const std::string& phrase) const {
        auto it = phrase_to_cmd_.find(phrase);
        return (it != phrase_to_cmd_.end()) ? it->second : "";
    }
};

// ==================== 开发板主类 ====================
class MyESP32Board : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    OledDisplay* display_ = nullptr;
    DummyDisplay dummy_display_;
    CommandMapper cmd_mapper_;
    TaskHandle_t uart_rx_task_ = nullptr;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = OLED_SDA_PIN,
            .scl_io_num = OLED_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 }
        };
        esp_err_t err = i2c_new_master_bus(&bus_config, &display_i2c_bus_);
        if (err != ESP_OK) ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        else ESP_LOGI(TAG, "I2C bus created (SDA=%d, SCL=%d)", OLED_SDA_PIN, OLED_SCL_PIN);
    }

    void InitializeSsd1306Display() {
        if (display_i2c_bus_ == nullptr) {
            display_ = nullptr;
            return;
        }
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
        if (esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_) != ESP_OK) {
            display_ = nullptr;
            return;
        }
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;   // 复位引脚未连接
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = { .height = OLED_RESOLUTION_HEIGHT };
        panel_config.vendor_config = &ssd1306_config;
        if (esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_) != ESP_OK) {
            display_ = nullptr;
            return;
        }
        // 如果复位引脚有效，可以调用 reset；这里直接跳过
        vTaskDelay(pdMS_TO_TICKS(50));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            display_ = nullptr;
            return;
        }
        esp_lcd_panel_disp_on_off(panel_, true);
        display_ = new OledDisplay(panel_io_, panel_, OLED_RESOLUTION_WIDTH, OLED_RESOLUTION_HEIGHT, false, false);
        ESP_LOGI(TAG, "SSD1306 display initialized");
    }

    void InitializeUart1() {
        uart_config_t uart_config = {
            .baud_rate = UART1_BAUD_RATE,
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
        ESP_LOGI(TAG, "UART1 initialized (TX=%d, RX=%d, baud=%d)", UART1_TX_PIN, UART1_RX_PIN, UART1_BAUD_RATE);
    }

    void SendToArduino(const std::string& cmd) {
        std::string msg = cmd + "\r\n";
        int len = uart_write_bytes(UART_NUM_1, msg.c_str(), msg.length());
        ESP_LOGI(TAG, "Send to Arduino: %s -> %s", cmd.c_str(), len > 0 ? "OK" : "FAIL");
    }

    // UART 接收任务（按行处理）
    static void UartRxTask(void* arg) {
        MyESP32Board* board = (MyESP32Board*)arg;
        uint8_t buf[256];
        std::string line;
        while (true) {
            int len = uart_read_bytes(UART_NUM_1, buf, sizeof(buf)-1, pdMS_TO_TICKS(100));
            if (len > 0) {
                for (int i = 0; i < len; i++) {
                    if (buf[i] == '\n') {
                        if (!line.empty()) {
                            if (line.back() == '\r') line.pop_back();
                            ESP_LOGI(TAG, "UART received: %s", line.c_str());
                            board->HandleUartCommand(line);
                            line.clear();
                        }
                    } else {
                        line.push_back(buf[i]);
                    }
                }
            }
        }
    }

    void HandleUartCommand(const std::string& phrase) {
        std::string cmd = cmd_mapper_.FindCommand(phrase);
        if (!cmd.empty()) {
            SendToArduino(cmd);
        } else {
            ESP_LOGW(TAG, "No mapping for: %s", phrase.c_str());
        }
    }

    void InitializeButtons() {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << LED_CONTROL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(LED_CONTROL_GPIO, 0);

        boot_button_.OnClick([this]() { Application::GetInstance().ToggleChatState(); });
        boot_button_.OnLongPress([this]() { EnterWifiConfigMode(); });
    }

    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool("self.uart.send_to_arduino", "向Arduino发送文本",
            PropertyList({ Property("message", kPropertyTypeString, "消息内容") }),
            [this](const PropertyList& props) -> ReturnValue {
                std::string msg = props["message"].value<std::string>();
                SendToArduino(msg);
                return "已发送：" + msg;
            });
    }

public:
    MyESP32Board() : boot_button_(BUILTIN_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeUart1();
        InitializeButtons();
        InitializeTools();

        // 启动 UART 接收任务
        xTaskCreate(UartRxTask, "uart_rx", 4096, this, 5, &uart_rx_task_);

        if (display_ == nullptr) ESP_LOGW(TAG, "Real OLED unavailable, using dummy display");
        ESP_LOGI(TAG, "MyESP32Board fully initialized");
    }

    virtual AudioCodec* GetAudioCodec() override {
        static I2SAudioCodec codec;
        return &codec;
    }

    virtual Display* GetDisplay() override {
        return (display_ != nullptr) ? display_ : static_cast<Display*>(&dummy_display_);
    }

    virtual Backlight* GetBacklight() override {
        static NullBacklight null_bl;
        return &null_bl;
    }
};

DECLARE_BOARD(MyESP32Board);