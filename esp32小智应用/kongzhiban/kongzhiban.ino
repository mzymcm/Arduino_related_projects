#include <Arduino.h>
#include "WS2812_Lib_for_Air001.h"

// ==================== 引脚定义 ====================
#define LED_PIN    PA7          // 板载 WS2812 数据
#define B_LED_PIN  PB3        // 外部灯光控制引脚
#define CUT_PIN    PA1          // 360度舵机信号线
#define TV_TX_PIN  PA4          // 软件串口 TX

#define LED_COUNT  1

// ==================== 舵机旋转时间（毫秒）====================
// 根据实测分别调整
const unsigned long FORWARD_DURATION = 1200;   // 正转一圈
const unsigned long BACKWARD_DURATION = 1300;  // 反转一圈

// ==================== WS2812 ====================
AIR001_WS2812 rgbLed(LED_COUNT, TYPE_GRB);

// ==================== 软件串口发送状态机（重写）====================
struct TvSender {
  const char* str;        // 待发送字符串
  uint8_t byteIdx;        // 当前字符索引
  uint8_t bitIdx;         // 当前位 (0=起始,1..8=数据,9=停止)
  unsigned long nextBitTime;  // 下一次切换电平时刻 (微秒)
  bool active;

  void start(const char* s) {
    if (active) return;
    str = s;
    byteIdx = 0;
    bitIdx = 0;
    active = true;
    // 立即发起始位（低电平）
    digitalWrite(TV_TX_PIN, LOW);
    nextBitTime = micros() + 104;  // 104us 后切换
  }

  void update() {
    if (!active) return;
    if (micros() < nextBitTime) return;  // 未到切换时刻

    // 当前位结束，进入下一位
    if (bitIdx == 0) {
      // 起始位结束，开始发数据位0 (LSB)
      bitIdx = 1;
      uint8_t data = str[byteIdx];
      digitalWrite(TV_TX_PIN, (data >> 0) & 0x01);
      nextBitTime += 104;
    } else if (bitIdx >= 1 && bitIdx <= 8) {
      if (bitIdx == 8) {
        // 最后一个数据位结束，准备发停止位
        bitIdx = 9;
        digitalWrite(TV_TX_PIN, HIGH);
        nextBitTime += 104;
      } else {
        bitIdx++;
        uint8_t data = str[byteIdx];
        digitalWrite(TV_TX_PIN, (data >> (bitIdx - 1)) & 0x01);
        nextBitTime += 104;
      }
    } else if (bitIdx == 9) {
      // 停止位结束，该字节发送完成
      byteIdx++;
      if (str[byteIdx] == '\0') {
        // 字符串发送完毕
        active = false;
        // 保持 TX 为高电平（空闲状态）
        digitalWrite(TV_TX_PIN, HIGH);
      } else {
        // 开始下一个字节的起始位
        bitIdx = 0;
        digitalWrite(TV_TX_PIN, LOW);
        nextBitTime += 104;
      }
    }
  }
};

TvSender tvSender;

// ==================== 360度舵机 PWM 生成 ====================
enum ServoState { SERVO_IDLE, SERVO_FORWARD, SERVO_BACKWARD };
ServoState servoState = SERVO_IDLE;
unsigned long servoStartTime = 0;
unsigned long lastPulseTime = 0;
const int pulsePeriod = 20000;    // 50Hz
int currentHighTime = 1500;       // 停止
bool pulseHigh = false;

void startServoForward() {
  servoState = SERVO_FORWARD;
  servoStartTime = millis();
  currentHighTime = 1300;
}

void startServoBackward() {
  servoState = SERVO_BACKWARD;
  servoStartTime = millis();
  currentHighTime = 1700;
}

void stopServo() {
  servoState = SERVO_IDLE;
  currentHighTime = 1500;
}

void updateServo() {
  unsigned long now = millis();
  if (servoState == SERVO_FORWARD && (now - servoStartTime >= FORWARD_DURATION)) {
    stopServo();
  } else if (servoState == SERVO_BACKWARD && (now - servoStartTime >= BACKWARD_DURATION)) {
    stopServo();
  }

  unsigned long nowUs = micros();
  if (!pulseHigh) {
    if (nowUs - lastPulseTime >= pulsePeriod) {
      digitalWrite(CUT_PIN, HIGH);
      lastPulseTime = nowUs;
      pulseHigh = true;
    }
  } else {
    if (nowUs - lastPulseTime >= currentHighTime) {
      digitalWrite(CUT_PIN, LOW);
      pulseHigh = false;
      lastPulseTime = nowUs;
    }
  }
}

// ==================== 命令处理 ====================
char cmdBuffer[16];
uint8_t cmdIndex = 0;

void processCommand(const char* cmd) {
  if (strcmp(cmd, "open_led") == 0) {
    rgbLed.setLedColorData(0, 0xFFFFFF);
    rgbLed.show();
    digitalWrite(B_LED_PIN, HIGH);
    Serial.println("LED ON (white).");
  } else if (strcmp(cmd, "close_led") == 0) {
    rgbLed.setLedColorData(0, 0);
    rgbLed.show();
    digitalWrite(B_LED_PIN, LOW);
    Serial.println("LED OFF.");
  } else if (strcmp(cmd, "open_tv") == 0) {
    tvSender.start("open_tv\r\n");
    Serial.println("Sending TV open command...");
  } else if (strcmp(cmd, "close_tv") == 0) {
    tvSender.start("close_tv\r\n");
    Serial.println("Sending TV close command...");
  } else if (strcmp(cmd, "open_curtain") == 0) {
    startServoForward();
    Serial.println("curtain motor forward.");
  } else if (strcmp(cmd, "close_curtain") == 0) {
    startServoBackward();
    Serial.println("curtain motor backward.");
  } else {
    Serial.print("Unknown: ");
    Serial.println(cmd);
  }
}

// ==================== 初始化 ====================
void setup() {
  pinMode(CUT_PIN, OUTPUT);
  digitalWrite(CUT_PIN, LOW);

  pinMode(B_LED_PIN, OUTPUT);
  digitalWrite(B_LED_PIN, LOW);

  pinMode(TV_TX_PIN, OUTPUT);
  digitalWrite(TV_TX_PIN, HIGH);

  // WS2812 上电熄灭
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(50);
  rgbLed.begin();
  rgbLed.setBrightness(20);
  rgbLed.setLedColorData(0, 0);
  rgbLed.show();
  rgbLed.show();

  Serial.begin(9600);
  while (!Serial);
  Serial.println("System ready.");
  Serial.println("Commands: open_led, close_led, open_tv, close_tv, open_curtain, close_curtain");
}

void loop() {
  // 接收串口命令
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        cmdBuffer[cmdIndex] = '\0';
        processCommand(cmdBuffer);
        cmdIndex = 0;
      }
    } else if (cmdIndex < sizeof(cmdBuffer) - 1) {
      cmdBuffer[cmdIndex++] = c;
    }
  }

  // 更新所有非阻塞任务
  tvSender.update();
  updateServo();
}