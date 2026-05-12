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