sudo apt-get install build-essential git linux-headers-$(uname -r)
sudo apt-get install make gcc


cd ~/sd1306
nano ssd1306_fb.c
make clean
make
# 卸载模块
echo 1-003c > /sys/bus/i2c/drivers/ssd1306_fb/unbind
sudo rmmod ssd1306_fb
# 重新绑定设备
sudo insmod ssd1306_fb.ko
##  或   insmod ssd1306_fb.ko swap_bit_order=1

###创建目标目录并复制模块
mkdir -p /lib/modules/$(uname -r)/extra/
cp ~/sd1306/ssd1306_fb.ko /lib/modules/$(uname -r)/extra/
depmod

##### 将模块名添加到 /etc/modules 文件：
sudo echo "ssd1306_fb" | tee -a /etc/modules
##### 如果需要传递参数（如 swap_bit_order=1），创建 /etc/modprobe.d/ssd1306_fb.conf：
echo "options ssd1306_fb swap_bit_order=1" | tee /etc/modprobe.d/ssd1306_fb.conf

sudo sh -c 'echo ssd1306_fb 0x3c > /sys/bus/i2c/devices/i2c-1/new_device'
i2cdetect -y 1	# 应显示 UU
ls /dev/fb*	# 应出现 /dev/fb1

现在可以使用 modprobe ssd1306_fb 加载模块，并支持自动加载依赖。

# 全屏白色
sh -c "tr '\000' '\377' < /dev/zero | dd of=/dev/fb1 bs=1024 count=1 2>/dev/null"

# 全屏黑色
dd if=/dev/zero of=/dev/fb1 bs=1024 count=0 2>/dev/null

# 左上角是否有一个白点
printf '\x01' | dd of=/dev/fb1 bs=1 seek=0 count=1 conv=notrunc 2>/dev/null



创建 systemd 服务 /etc/systemd/system/ssd1306.service：
nano /etc/systemd/system/ssd1306.service
[Unit]
Description=SSD1306 framebuffer setup
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'sudo insmod /root/sd1306/ssd1306_fb.ko & sudo  echo ssd1306_fb 0x3c > /sys/bus/i2c/devices/i2c-1/new_device'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target

sudo systemctl daemon-reload
sudo systemctl enable ssd1306.service


nano /boot/cmdline.txt
### 在原有参数行末尾添加（注意空格分隔）：
fbcon=map:1
或 fbcon=map:1,rotate:2












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
