#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_uart.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cJSON.h>

#define TAG "MyESP32Board"

// ------------------------- 短语 → 标准指令映射器 -------------------------
class CommandMapper {
private:
    std::unordered_map<std::string, std::string> phrase_to_cmd_;
    std::vector<std::pair<std::vector<std::string>, std::string>> mappings_;

public:
    CommandMapper() {
        // 预置映射（可根据需求扩展）
        AddMapping({"开灯", "打开灯", "把灯打开", "亮灯"}, "open_led");
        AddMapping({"关灯", "关闭灯", "关上灯", "熄灯"}, "close_led");
        AddMapping({"打开电视", "开电视", "看电视", "启动电视"}, "open_tv");
        AddMapping({"关上电视", "关闭电视", "关电视", "不看电视"}, "close_tv");
        AddMapping({"拉开窗帘", "打开窗帘", "天亮了", "升起窗帘"}, "open_curtain");
        AddMapping({"拉上窗帘", "关闭窗帘", "天黑了", "降下窗帘"}, "close_curtain");
    }

    void AddMapping(const std::vector<std::string>& phrases, const std::string& standard_cmd) {
        for (const auto& phrase : phrases) {
            phrase_to_cmd_[phrase] = standard_cmd;
        }
        mappings_.emplace_back(phrases, standard_cmd);
        ESP_LOGI(TAG, "Added mapping: [%s] -> %s", phrases.front().c_str(), standard_cmd.c_str());
    }

    std::string FindCommand(const std::string& spoken_phrase) const {
        auto it = phrase_to_cmd_.find(spoken_phrase);
        if (it != phrase_to_cmd_.end()) {
            return it->second;
        }
        return "";
    }

    std::vector<std::pair<std::vector<std::string>, std::string>> GetAllMappings() const {
        return mappings_;
    }
};

// ------------------------- 主板类 -------------------------
class MyESP32Board : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    bool pa_enabled_ = false;
    CommandMapper cmd_mapper_;
    bool auto_listen_started_ = false;  // 防止重复启动

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = true,
            },
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
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        panel_config.vendor_config = nullptr;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                   DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            // 用户主动触发：开始聆听（如果已经聆听则停止？根据需求不停止，所以这里仅启动）
            app.StartListening();
        });
    }

    void EnableAudioOutput(bool enable) {
        if (enable == pa_enabled_) return;
        pa_enabled_ = enable;
        gpio_set_level(AUDIO_PA_ENABLE_GPIO, enable ? 1 : 0);
        ESP_LOGI(TAG, "Audio power amplifier %s", enable ? "enabled" : "disabled");
    }

    void InitializeUart() {
        uart_config_t uart_config = {
            .baud_rate = UART1_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 256, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }

    void SendUartCommand(const std::string& standard_cmd) {
        std::string msg = standard_cmd + "\n";
        int len = uart_write_bytes(UART_NUM_1, msg.c_str(), msg.length());
        if (len > 0) {
            ESP_LOGI(TAG, "UART send: %s", standard_cmd.c_str());
        } else {
            ESP_LOGE(TAG, "UART send failed");
        }
    }

    void InitializeTools() {
        McpServer::GetInstance().AddTool(
            "command.send",
            "根据自然语言短语发送对应的标准指令到 Arduino Nano。",
            PropertyList({
                Property("phrase", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string phrase = properties["phrase"].value<std::string>();
                std::string standard_cmd = cmd_mapper_.FindCommand(phrase);
                if (standard_cmd.empty()) {
                    return "未找到对应的指令，短语: " + phrase;
                }
                SendUartCommand(standard_cmd);
                return "已发送标准指令: " + standard_cmd;
            }
        );

        McpServer::GetInstance().AddTool(
            "command.add_mapping",
            "动态添加映射。",
            PropertyList({
                Property("phrases", kPropertyTypeString),
                Property("standard_cmd", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string phrases_json = properties["phrases"].value<std::string>();
                std::string standard_cmd = properties["standard_cmd"].value<std::string>();

                cJSON* json = cJSON_Parse(phrases_json.c_str());
                if (!cJSON_IsArray(json)) {
                    cJSON_Delete(json);
                    return "phrases 参数必须是一个 JSON 数组";
                }
                std::vector<std::string> phrase_list;
                int size = cJSON_GetArraySize(json);
                for (int i = 0; i < size; i++) {
                    cJSON* item = cJSON_GetArrayItem(json, i);
                    if (cJSON_IsString(item)) {
                        phrase_list.push_back(item->valuestring);
                    }
                }
                cJSON_Delete(json);
                if (phrase_list.empty()) return "短语列表不能为空";
                cmd_mapper_.AddMapping(phrase_list, standard_cmd);
                return "映射已添加";
            }
        );

        McpServer::GetInstance().AddTool(
            "command.list_mappings",
            "列出所有映射。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                auto mappings = cmd_mapper_.GetAllMappings();
                cJSON* root = cJSON_CreateArray();
                for (const auto& pair : mappings) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON* phrases_arr = cJSON_CreateArray();
                    for (const auto& phrase : pair.first) {
                        cJSON_AddItemToArray(phrases_arr, cJSON_CreateString(phrase.c_str()));
                    }
                    cJSON_AddItemToObject(item, "phrases", phrases_arr);
                    cJSON_AddStringToObject(item, "standard_cmd", pair.second.c_str());
                    cJSON_AddItemToArray(root, item);
                }
                char* json_str = cJSON_PrintUnformatted(root);
                std::string result(json_str);
                cJSON_free(json_str);
                cJSON_Delete(root);
                return result;
            }
        );

        ESP_LOGI(TAG, "MCP tools registered");
    }

public:
    MyESP32Board() : boot_button_(BOOT_BUTTON_GPIO) {
        gpio_config_t pa_conf = {
            .pin_bit_mask = (1ULL << AUDIO_PA_ENABLE_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_conf);
        EnableAudioOutput(false);

        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeUart();
        InitializeTools();
    }

    // 网络连接后自动开始聆听（开机自动进入对话模式）
    void OnNetworkConnected() override {
        WifiBoard::OnNetworkConnected();
        if (!auto_listen_started_) {
            auto_listen_started_ = true;
            ESP_LOGI(TAG, "Network connected, auto start listening");
            Application::GetInstance().StartListening();
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN
        );
        audio_codec.OnOutputEnable([this](bool enable) {
            EnableAudioOutput(enable);
        });
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }
};

DECLARE_BOARD(MyESP32Board);