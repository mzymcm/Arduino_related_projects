#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <printf.h>
//====== Pico_nRF24L01_POST 发送端
//==	nRF24L01		Pico 引脚
//==	VCC		3V3(OUT)
//==	GND		GND
//==	CE		GP7
//==	CSN		GP9
//==	SCK		GP10
//==	MOSI		GP11
//==	MISO		GP8
// 顺序: MISO, MOSI, SCK
arduino::MbedSPI SPI1(8, 11, 10);
#define CE_PIN   7
#define CSN_PIN  9

RF24 radio(CE_PIN, CSN_PIN);
const uint64_t pipeAddress = 0xABCDABCDABLL;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  SPI1.begin();
  delay(10);

  if (!radio.begin(&SPI1)) {
    Serial.println("初始化失败");
    while (1);
  }
  Serial.println("初始化成功");

  radio.openWritingPipe(pipeAddress);
  radio.setPALevel(RF24_PA_MIN);     // 最低功率，避免饱和
  radio.setDataRate(RF24_250KBPS);
  radio.setRetries(15, 15);          // 增加重试
  radio.stopListening();

  printf_begin();
  radio.printDetails();
}

void loop() {
  const char payload[] = "Hello from Pico";
  bool success = radio.write(&payload, sizeof(payload));
  
  if (success) {
    Serial.println("发送成功");
  } else {
    Serial.println("发送失败，重试中...");
  }
  delay(1000);
}