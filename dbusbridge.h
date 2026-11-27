#ifndef DBUSBRIDGE_H
#define DBUSBRIDGE_H

// 把宿主机 system bus 上的服务（NetworkManager / BlueZ / UPower）
// 转发到容器的 system bus 上，使容器内的 GXDE 可以控制宿主机的 WiFi、蓝牙并读取电量
//
// 运行在宿主机上（需要 root）：
//   - 在容器总线上以 root 身份占用对应的服务名
//   - 容器内每个客户端在宿主机总线上对应一条独立连接（以宿主机桌面用户身份），
//     这样 SecretAgent / BlueZ Agent 这类服务端回调能准确送回对应客户端，
//     宿主机 polkit 也按桌面用户而不是 root 进行授权

#include <string>
#include <vector>
#include <sys/types.h>

namespace DBusBridge {

struct Options {
	std::string machine;          // 容器名
	std::string containerBus;     // 直接指定容器总线 socket 路径（调试用），为空则通过 machine 查找
	std::string hostBus = "/run/dbus/system_bus_socket";
	uid_t hostUid = 0;            // 连接宿主机总线时使用的身份
	bool verbose = false;
	std::vector<std::string> services;
};

Options defaultOptions(const std::string &machine);
// 解析 `gxde-lsg dbus-bridge` 之后的参数，失败返回 false
bool parseArgs(int argc, char *argv[], Options &options);
// 运行转发循环直到容器总线断开；readyFd >= 0 时在初始化完成后写入一个字节并关闭
int run(const Options &options, int readyFd = -1);
// 在子进程中启动转发并等待其初始化完成，返回子进程 pid（失败返回 -1）
pid_t spawn(const Options &options, int timeoutSeconds = 10);

}

#endif
