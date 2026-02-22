#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

int main() {
    struct ifaddrs *ifaddr, *ifa;
    int family;
    char addr[INET_ADDRSTRLEN];

    // 获取网络接口地址列表
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // 遍历所有接口
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;   // 忽略无地址的接口

        family = ifa->ifa_addr->sa_family;

        // 只处理 IPv4 地址
        if (family == AF_INET) {
            // 跳过回环接口
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), addr, sizeof(addr));
            printf("%s\n", addr);
            break;  // 只输出第一个 IP，若要显示所有则删除此行
        }
    }

    freeifaddrs(ifaddr);
    return 0;
}
