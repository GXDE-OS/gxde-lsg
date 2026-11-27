#ifndef CONTAINER_H
#define CONTAINER_H

#include <string>
#include <vector>
#include "utils.h"

class Container {
public:
	static bool isContainerRunning(std::string containerName);
	// 宿主机 Wayland socket 在容器内的挂载位置
	static constexpr const char *containerWaylandSocket = "/run/host-wayland/wayland-0";
	// 容器内 fastfetch 等工具看到的机器名（DMI product_name），后接宿主机发行版名称
	static constexpr const char *productNamePrefix = "Linux Subsystem for GXDE Compatibility Layer on ";
	static bool loadingLinuxContainer(std::string containerPath, std::string containerName, const HostUser &user, std::string hostname, bool loadSystemd=true, std::string hostWaylandSocket="");
	// 容器启动前在 rootfs 中创建/同步与宿主机一致的账户，写入 hostname，生成宿主机语言对应的 locale
	static bool provisionUser(std::string containerPath, const HostUser &user, std::string hostname, std::string lang = "");
	static std::vector<std::string> desktopSessionCommand(bool waylandSession);
	static void runAppInContainer(std::string containerName, 
		std::string user, 
		std::vector<std::string> command, 
		std::string x11Display = "", 
		std::string waylandDisplay = "",
		std::string lang = "",
		std::string language = "",
		uid_t uid = 0);
	static void maskServices(std::string containerName, std::vector<std::string> units);
	// 为 dbus-bridge 转发的服务写入容器系统总线策略，不依赖容器内是否装了对应软件包
	static bool installBridgePolicy(std::string containerPath, const std::vector<std::string> &services);
	static void runXephyr(std::string containerName, std::string user);
	static void installApp(std::string containerName, std::vector<std::string> debList, std::string user="root");
	static bool isLinuxKernelSupportContainer();
	
private:

};

#endif
