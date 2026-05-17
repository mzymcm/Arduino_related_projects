编译与烧录步骤
设置目标芯片：
bash
idf.py set-target esp32

配置项目：
bash
idf.py menuconfig
进入 Xiaozhi Assistant → Board Type，选择 My ESP32 Board (双核 ESP32 + OLED 优化引脚)。

清理并构建：
bash
idf.py clean
idf.py build

idf.py fullclean
idf.py build

烧录并监视：
bash
idf.py erase_flash
idf.py -p COM6 flash monitor

验证 OLED 显示：上电后屏幕应显示 "XiaoZhi AI Ready"，配网或配对时验证码会显示在屏幕上。





主要修改了以下文件：

application.h

添加了 uart_query_callback_、uart_query_text_、uart_query_pending_ 成员变量

声明了 SendTextMessage 方法

application.cc

实现了 SendTextMessage 方法

修改了 InitializeProtocol 中的 OnIncomingJson 回调：

修正 sentence_start 的 Schedule lambda 捕获 this

在 stop 分支添加 UART 查询模式的完整应答收集与回调逻辑

protocol.h

在 Protocol 类中添加了公开的 SendJson 方法，用于发送任意 JSON 文本

my_esp32_board.cc

修改了 InitializeUart1 中的 UART 接收任务，实现按行解析、识别 Q: 前缀提问，并调用 Application::SendTextMessage 将答案通过串口返回

这四个文件的改动协同实现了“Arduino Nano 串口提问 → ESP32 AI 回答 → 串口回传”的功能。