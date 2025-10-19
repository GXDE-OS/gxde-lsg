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
		Utils::showHelp();
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
	// 获取指定用户 uid
	string userName = "root";
	if (argc > 1) {
		userName = argv[1];
	}
	//string programPath = std::filesystem::current_path();
	string programPath = Utils::programPath();
	string rootfsPath = programPath + "/gxde-rootfs";
	//string rootfsPath = "/opt/gxde-rootfs/";
	string rootfsTarPath = programPath + "/gxde-rootfs.tar.xz";
	string containerName = "gxde";
	if (!std::filesystem::exists(rootfsPath + "/etc/os-version")) {
		cout << rootfsPath << " Not Found!" << endl;
		cout << "Try to unpack rootfs file: " << rootfsTarPath << endl;
		if (!Utils::unpackTar(rootfsTarPath, rootfsPath)) {
			cerr << "Unpack failed!" << endl;
			return 1;
		}
	}
	if (!Container::isContainerRunning(containerName)) {
		// 如果容器已存在，则不重复 loading
		Container::loadingLinuxContainer(rootfsPath, containerName);
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
		Container::runAppInContainer(containerName, userName, command);
	}
	while(Container::isContainerRunning(containerName)){
		// 只有在 container 停止后才会停止运行程序
		sleep(2);
	}
	return 0;
}
