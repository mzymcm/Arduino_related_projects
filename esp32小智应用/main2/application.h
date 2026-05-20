#ifndef APPLICATION_H
#define APPLICATION_H

#include "audio_service.h"
#include "ota.h"
#include "protocol.h"
#include "state_machine.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Event bits for main loop
#define MAIN_EVENT_SCHEDULE          (1 << 0)
#define MAIN_EVENT_SEND_AUDIO        (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2)
#define MAIN_EVENT_VAD_CHANGE        (1 << 3)
#define MAIN_EVENT_CLOCK_TICK        (1 << 4)
#define MAIN_EVENT_ERROR             (1 << 5)
#define MAIN_EVENT_NETWORK_CONNECTED (1 << 6)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 7)
#define MAIN_EVENT_TOGGLE_CHAT       (1 << 8)
#define MAIN_EVENT_START_LISTENING   (1 << 9)
#define MAIN_EVENT_STOP_LISTENING    (1 << 10)
#define MAIN_EVENT_ACTIVATION_DONE   (1 << 11)
#define MAIN_EVENT_STATE_CHANGED     (1 << 12)

enum class ListeningMode {
    kListeningModeManualStop,
    kListeningModeAutoStop,
    kListeningModeRealtime
};

enum class AbortReason {
    kAbortReasonNone,
    kAbortReasonWakeWordDetected
};

enum class AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide
};

class Application {
public:
    Application();
    ~Application();

    void Initialize();
    void Run();

    void ToggleChatState();
    void StartListening();
    void StopListening();
    void WakeWordInvoke(const std::string& wake_word);
    bool CanEnterSleepMode();

    void RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback);
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    void PlaySound(const std::string_view& sound);
    void ResetProtocol();

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }

private:
    bool SetDeviceState(DeviceState state);

    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void HandleActivationDoneEvent();
    void HandleStateChangedEvent();
    void HandleToggleChatEvent();
    void HandleStartListeningEvent();
    void HandleStopListeningEvent();
    void HandleWakeWordDetectedEvent();

    void ActivationTask();
    void CheckAssetsVersion();
    void CheckNewVersion();
    void InitializeProtocol();
    void ShowActivationCode(const std::string& code, const std::string& message);
    void Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound);
    void DismissAlert();
    void Schedule(std::function<void()>&& callback);
    void AbortSpeaking(AbortReason reason);
    void SetListeningMode(ListeningMode mode);
    ListeningMode GetDefaultListeningMode() const;
    void Reboot();
    bool UpgradeFirmware(const std::string& url, const std::string& version);
    void ContinueOpenAudioChannel(ListeningMode mode);
    void ContinueWakeWordInvoke(const std::string& wake_word);

    EventGroupHandle_t event_group_;
    esp_timer_handle_t clock_timer_;

    AudioService audio_service_;
    StateMachine state_machine_;
    std::unique_ptr<Protocol> protocol_;
    std::unique_ptr<Ota> ota_;

    std::mutex mutex_;
    std::vector<std::function<void()>> main_tasks_;

    TaskHandle_t activation_task_handle_ = nullptr;

    std::string last_error_message_;
    int clock_ticks_ = 0;
    bool has_server_time_ = false;
    bool assets_version_checked_ = false;
    bool aborted_ = false;
    bool play_popup_on_listening_ = false;
    ListeningMode listening_mode_ = ListeningMode::kListeningModeManualStop;
    AecMode aec_mode_ = AecMode::kAecOff;
    std::function<void(const std::string&)> mcp_broadcast_callback_;

    // ========== 自动持续对话开关 ==========
    bool auto_listen_enabled_ = true;
};

#endif // APPLICATION_H