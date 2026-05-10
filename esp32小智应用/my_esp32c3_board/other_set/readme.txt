编译与烧录步骤
在 main/CMakeLists.txt 和 main/Kconfig.projbuild 中正确添加您的开发板选项（见前文）。

执行以下命令：

idf.py set-target esp32c3
idf.py menuconfig

# 进入 Xiaozhi Assistant → Board Type，选择 "My ESP32-C3 Board"
# 保存退出
idf.py build
idf.py -p COMx flash monitor   # 替换 COMx 为实际串口号
烧录成功后，设备会进入配网模式。通过手机小程序配网后，就可以用语音控制 LED 并发送串口消息了。
