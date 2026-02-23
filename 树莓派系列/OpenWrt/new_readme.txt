######====  第一步设置IP为自动获取。
uci set network.lan.proto='dhcp'
uci commit network
/etc/init.d/network restart

######====  设置为中文
apk add luci-i18n-base-zh-cn

######====  安装nano
apk add nano

######====  防火墙设置
nano /etc/config/firewall
都设为：ACCEPT

######====  通过SSH直接以root用户身份登录。
nano /etc/ssh/sshd_config
修改/etc/ssh/sshd_config文件，找到PermitRootLogin选项，并将其修改为：
PermitRootLogin yes

######====  设置wifi
nano /etc/config/wireless

config wifi-device 'radio0'
        option type 'mac80211'
        option path 'platform/soc/20980000.usb/usb1/1-1/1-1:1.0'
        option band '2g'
        option channel '1'
        option htmode 'HT20'
        option disabled '0'
config wifi-iface 'default_radio0'
        option device 'radio0'
        option network 'wwan lan'
        option mode 'sta'
        option ssid 'CMCC-gyHQ'
        option encryption 'psk2'
        option disabled '0'
        option key '2fw5f8g7'


nano /etc/config/network

config interface 'loopback'
        option device 'lo'
        option proto 'static'
        option ipaddr '127.0.0.1'
        option netmask '255.0.0.0'
config globals 'globals'
        option ula_prefix 'fd3f:1ae6:b52f::/48'
config device
        option name 'br-lan'
        option type 'bridge'
        list ports 'eth0'
config interface 'lan'
        option device 'br-lan'
        option proto 'static'
        option ipaddr '192.168.1.21'
        option netmask '255.255.255.0'
        option ip6assign '60'
config interface 'wwan'
        option proto 'dhcp'






######====  扩容TF卡
apk update
apk add fdisk

#查看磁盘
fdisk -l
apk add parted

#修复
parted -l
apk add cfdisk resize2fs block-mount

cfdisk /dev/mmcblk0

#查看当前的所有分区
ls /dev/mmcblk*

#把/dev/sda1建立为swap交换分区mkfs.ext4
mkswap /dev/mmcblk0p3

#卸载分区
umount /dev/mmcblk0p3

#格式化分区
mkfs.ext4 /dev/mmcblk0p3	

#对分区进行检查
e2fsck -f /dev/mmcblk0p3 

#调整分区大小
resize2fs /dev/mmcblk0p3 

#创建文件夹
mkdir -p /mnt/mmcblk0p3

#将文件夹挂载到/dev/mmcblk0p3分区
mount /dev/mmcblk0p3 /mnt/mmcblk0p3

在web中进行挂载，并选择启用，挂载点选择“作为根文件系统使用（/）”

如果没有挂载点，请安装如下工具：
apk add block-mount


#逐条执行，确保成功

mkdir -p /tmp/introot

mkdir -p /tmp/extroot

mount --bind / /tmp/introot

mount /dev/mmcblk0p3 /tmp/extroot

tar -C /tmp/introot -cvf - . | tar -C /tmp/extroot -xf -

umount /tmp/introot

umount /tmp/extroot

# 执行完以上后点“web 里的保存并应用”，然后重新启动。

# 重启动系统

reboot

# 查看磁盘情况。

root@OpenWrt:~# df  -hT


查询内核版本
cat /proc/version
