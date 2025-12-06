#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>
#include <string.h>
#include "utils.h"
#include "container.h"

using namespace std;

int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "--help") == 0) {
		Utils::showHelp(argv);
		return 1;
	}
	// 判断用户是否使用 root 权限运行程序
	if (getuid() != 0) {
		cerr << "Please run with root!" << endl;
		return 1;
	}
	if (!Utils::checkEnvironment()) {
		return 1;
	}
	cout << "Linux Kernel Version: " << Utils::linuxKernelVersion() << endl;
	cout << (Container::isLinuxKernelSupportContainer() ? "Support" : "Unsupport") << " systemd in this kernel version." << endl;
	// 获取指定用户 uid
	string userName = "gxde";  // 容器内的用户名
	string localUserName = ""; // 宿主机用户名
	if (argc > 1) {
		localUserName = argv[1];
	}
	if (argc > 1 && strcmp(argv[1], "install-deb") == 0) {
		return !Utils::callInstallDeb(argc, argv, userName);
	}
	string programPath = Utils::programPath();
	string rootfsPath = Utils::rootfsPath();
	string rootfsTarPath = Utils::rootfsTarPath();
	string containerName = CONTAINERNAME;
	
	string x11Display = Utils::getEnv("DISPLAY");
	string waylandDisplay = Utils::getEnv("WAYLAND_DISPLAY");
	string lang = Utils::getEnv("LANG");
	string language = Utils::getEnv("LANGUAGE");
	
	if (x11Display == "") {
		cerr << "$DISPLAY Not Found!" << endl;
		return 1;
	}
	
	if (!std::filesystem::exists(rootfsPath + "/etc/hostname")) {
		cout << rootfsPath << " Not Found!" << endl;
		cout << "Try to unpack rootfs file: " << rootfsTarPath << endl;
		if (!Utils::unpackTar(rootfsTarPath, rootfsPath)) {
			cerr << "Unpack failed!" << endl;
			return 1;
		}
	}
	if (!Container::isContainerRunning(containerName)) {
		// 如果容器已存在，则不重复 loading
		Container::loadingLinuxContainer(rootfsPath, containerName, localUserName, Container::isLinuxKernelSupportContainer());
	}
	if (Container::isContainerRunning(containerName)) {
		// 禁用 network manager
		Container::disabledNetworkManager(containerName);
		// 拷贝宿主机的 /etc/resolve.conf
		std::filesystem::copy_file("/etc/resolv.conf", 
			rootfsPath + "/etc/resolv.conf",
			std::filesystem::copy_options::overwrite_existing);
		vector<string> command;
		command.push_back("startgxde_window");
		Container::runAppInContainer(containerName, 
			userName, 
			command, 
			x11Display, 
			waylandDisplay,
			lang,
			language);
	}
	while(Container::isContainerRunning(containerName)){
		// 只有在 container 停止后才会停止运行程序
		sleep(2);
	}
	return 0;
}
