#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <printf.h>
//====== Pico 和 nRF24L01 Pico_nRF24L01_GET 制作的接收端
//==	nRF24L01		Pico 引脚
//==	VCC		3V3(OUT)
//==	GND		GND
//==	CE		GP7
//==	CSN		GP9
//==	SCK		GP10
//==	MOSI		GP11
//==	MISO		GP8
// --- 1. 手动创建SPI1对象 ---
// 使用 arduino::MbedSPI 类来创建实例
arduino::MbedSPI SPI1(8, 11, 10);  // MISO=GP8, MOSI=GP11, SCK=GP10

// --- 2. 定义RF24使用的引脚 ---
#define CE_PIN   7   // CE 连接 GP7
#define CSN_PIN  9   // CSN 连接 GP9

RF24 radio(CE_PIN, CSN_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial); // 等待串口连接

  Serial.println("=== nRF24L01 接收端启动 ===");

  SPI1.begin();                     // 初始化 SPI1
  if (!radio.begin(&SPI1)) {        // 传入 SPI1 对象
    Serial.println("[错误] 模块初始化失败！");
    while (1);
  }
  Serial.println("[信息] 模块初始化成功。");

  // 配置接收参数
  radio.setPALevel(RF24_PA_LOW);    // 低功率，稳定优先
  radio.setDataRate(RF24_250KBPS);  // 最低速率，抗干扰最好

  const uint64_t pipeAddress = 0xABCDABCDABLL;
  radio.openReadingPipe(1, pipeAddress);
  radio.startListening();           // 进入接收模式

  // 打印模块详细配置（可选，便于调试）
  printf_begin();
  radio.printDetails();

  Serial.println("[信息] 接收端已就绪，等待数据...");
}

void loop() {
  // ----- 核心接收逻辑：持续检查并读取数据 -----
  if (radio.available()) {
    char receivedData[32] = {0};          // 缓冲区，最大32字节
    radio.read(&receivedData, sizeof(receivedData));
    Serial.print("收到数据: ");
    Serial.println(receivedData);
  }
  // 不需要 delay，让循环高速检查，确保不丢包
}