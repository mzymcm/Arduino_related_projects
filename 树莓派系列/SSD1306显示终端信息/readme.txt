1. 查看当前已加载模块
lsmod | grep ssd1306

2. 卸载现有模块
sudo rmmod ssd1306_fb
如果提示 rmmod: ERROR: Module ssd1306_fb is not currently loaded，则可能驱动已注册但模块未加载（比如编译进内核），但树莓派标准内核不会包含此驱动，更可能是之前加载过但 probe 失败导致驱动残留。可以尝试强制卸载或重启系统。

3. 再次加载新模块
sudo insmod ssd1306_fb.ko

4. 确认 fb 设备节点
ls /dev/fb*
系统已有 ILI9340 占用 /dev/fb1，SSD1306 可能注册为 /dev/fb2（或 /dev/fb0，但通常 /dev/fb0 是主显示）。请记录实际节点，后续测试使用该节点。

5. 触发刷新线程
驱动在 fb_open 中才设置 running = true 并唤醒刷新线程。因此必须打开 fb 设备一次（例如用 dd 或 cat 写入数据），线程才会开始工作。
# 假设节点为 /dev/fb2
sudo dd if=/dev/zero of=/dev/fb2 bs=1024 count=1   # 清屏（全黑）

6. 显示随机点
sudo cat /dev/urandom > /dev/fb2








清除重新再操作步骤
1. 删除现有 I2C 设备（即使显示 3c）
sudo sh -c 'echo 0x3c > /sys/bus/i2c/devices/i2c-1/delete_device'
可能会提示 Can't find device in list，忽略即可。

2. 卸载驱动模块
sudo rmmod ssd1306_fb
确认模块已卸载：

lsmod | grep ssd1306   # 应无输出
3. 检查 /dev/fb2 是否消失
ls /dev/fb*
如果 /dev/fb2 仍在，说明模块未完全卸载或系统有残留，可尝试重启系统确保干净：

sudo reboot
重启后重新登录，确认 /dev/fb2 已不存在（ls /dev/fb* 应只有 /dev/fb0 和 /dev/fb1）。

4. 重新加载驱动（选择合适的参数）
进入驱动源码目录，重新加载模块。根据您之前的成功经验，可能需要 swap_bit_order=1：

cd ~/sd1306
sudo insmod ssd1306_fb.ko swap_bit_order=1
如果不确定，也可先不加参数，后续测试若显示异常再调整。

5. 绑定 I2C 设备
sudo sh -c 'echo ssd1306_fb 0x3c > /sys/bus/i2c/devices/i2c-1/new_device'

6. 检查绑定结果
dmesg | tail -20          # 查看 probe 信息
sudo i2cdetect -y 1       # 应显示 UU
ls /dev/fb*               # 应出现 /dev/fb2
如果 i2cdetect 显示 UU 且 dmesg 有 probe 成功的信息，说明绑定成功。

7. 测试显示
使用之前的 fbwrite.c 测试（注意根据 swap_bit_order 调整位序）：
gcc -o fbdraw fbdraw.c
sudo ./fbdraw


a. 创建目标目录并复制模块
sudo mkdir -p /lib/modules/$(uname -r)/extra/
sudo cp ssd1306_fb.ko /lib/modules/$(uname -r)/extra/




8. 开机自动加载驱动并绑定设备
创建 systemd 服务 /etc/systemd/system/ssd1306.service：
[Unit]
Description=SSD1306 framebuffer setup
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'insmod /root/sd1306/ssd1306_fb.ko && echo ssd1306_fb 0x3c > /sys/bus/i2c/devices/i2c-1/new_device'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target


9. 启用：
sudo systemctl enable ssd1306.service

10. 确保开机自动加载（可选）
设置开机自启：
将模块加入 /etc/modules-load.d/ssd1306.conf：
ssd1306_fb
设置模块参数（如需）在 /etc/modprobe.d/ssd1306.conf：
options ssd1306_fb swap_bit_order=1



如果有问题：
a. 重新加载 systemd 并启用服务
sudo systemctl daemon-reload
sudo systemctl enable ssd1306.service
b. 移除旧的 modules-load.d 配置（可选）
sudo rm /etc/modules-load.d/ssd1306.conf 2>/dev/null
c. 重启测试
sudo reboot




2. 重新编译驱动
cd ~/sd1306
make clean
make
sudo cp ssd1306_fb.ko /lib/modules/$(uname -r)/extra/
3. 卸载旧模块并加载新模块
# 删除设备
sudo sh -c 'echo 0x3c > /sys/bus/i2c/devices/i2c-1/delete_device' 2>/dev/null

# 卸载模块
sudo rmmod ssd1306_fb
# 加载新模块（可带上之前确认的参数，如 swap_bit_order=1）
sudo insmod ssd1306_fb.ko swap_bit_order=1
# 重新绑定设备
sudo sh -c 'echo ssd1306_fb 0x3c > /sys/bus/i2c/devices/i2c-1/new_device'





# 解除绑定
echo 1-003c > /sys/bus/i2c/drivers/ssd1306_fb/unbind

# 全屏白色（正确的方式）
sudo sh -c "tr '\000' '\377' < /dev/zero | dd of=/dev/fb1 bs=1024 count=1 2>/dev/null"
# 全屏黑色（正确的方式）
sudo dd if=/dev/zero of=/dev/fb1 bs=1024 count=1 2>/dev/null

# 将 fb1 绑定到控制台 2
sudo con2fbmap 1 1   # 第一个 1 表示 fb1，第二个 1 表示 tty2
# 然后切换到 tty2（Ctrl+Alt+F2），看是否有光标



sudo cp /root/sd1306/ssd1306_fb.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a

[Unit]
Description=SSD1306 framebuffer setup
After=local-fs.target systemd-modules-load.service
Before=multi-user.target

[Service]
Type=oneshot
ExecStartPre=/sbin/modprobe i2c-dev
ExecStart=/bin/sh -c 'insmod /root/sd1306/ssd1306_fb.ko && if [ ! -e /sys/bus/i2c/devices/1-003c ]; then echo ssd1306_fb 0x3c > /sys/bus/i2c/devices/i2c-1/new_device; fi'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target