#ifndef CONTAINER_H
#define CONTAINER_H

#include <string>
#include <vector>

class Container {
public:
	static bool isContainerRunning(std::string containerName);
	// 宿主机 Wayland socket 在容器内的挂载位置
	static constexpr const char *containerWaylandSocket = "/run/host-wayland/wayland-0";
	static bool loadingLinuxContainer(std::string containerPath, std::string containerName, std::string localUserName, bool loadSystemd=true, std::string hostWaylandSocket="");
	static std::vector<std::string> desktopSessionCommand(bool waylandSession);
	static void runAppInContainer(std::string containerName, 
		std::string user, 
		std::vector<std::string> command, 
		std::string x11Display = "", 
		std::string waylandDisplay = "",
		std::string lang = "",
		std::string language = "");
	static void maskServices(std::string containerName, std::vector<std::string> units);
	static void runXephyr(std::string containerName, std::string user);
	static void installApp(std::string containerName, std::vector<std::string> debList, std::string user="root");
	static bool isLinuxKernelSupportContainer();
	
private:

};

#endif
