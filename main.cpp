#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>
#include <string.h>
#include "utils.h"
#include "container.h"
#include "dbusbridge.h"

using namespace std;

int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "--help") == 0) {
		Utils::showHelp(argv);
		return 1;
	}
	if (argc > 1 && strcmp(argv[1], "dbus-bridge") == 0) {
		// 手动运行转发（调试用）：gxde-lsg dbus-bridge [-v] [--machine NAME] [--host-uid UID] [SERVICE...]
		DBusBridge::Options options = DBusBridge::defaultOptions(CONTAINERNAME);
		if (!DBusBridge::parseArgs(argc - 2, argv + 2, options)) {
			cerr << "Usage: " << argv[0] << " dbus-bridge [-v] [--machine NAME] [--container-bus PATH] "
				"[--host-bus PATH] [--host-uid UID] [SERVICE...]" << endl;
			return 1;
		}
		return DBusBridge::run(options);
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
		if (!Container::loadingLinuxContainer(rootfsPath, containerName, localUserName, Container::isLinuxKernelSupportContainer())) {
			return 1;
		}
	}
	if (Container::isContainerRunning(containerName)) {
		// 网络、蓝牙、电源服务由宿主机提供：屏蔽容器内的同名服务并启动转发
		Container::maskServices(containerName, {"NetworkManager.service", "bluetooth.service", "upower.service"});
		if (DBusBridge::spawn(DBusBridge::defaultOptions(containerName)) < 0) {
			cerr << "D-Bus bridge failed, WiFi/Bluetooth/Power of host will be unavailable" << endl;
		}
		// 拷贝宿主机的 /etc/resolve.conf
		// 目标若是符号链接（如指向 /run/systemd/resolve/...），直接写入会顺着链接改写宿主机文件，
		// 因此先删除链接本身；目标被挂载占用时删除/拷贝失败则跳过，不抛异常
		std::error_code ec;
		string resolvPath = rootfsPath + "/etc/resolv.conf";
		if (std::filesystem::is_symlink(resolvPath, ec)) {
			std::filesystem::remove(resolvPath, ec);
		}
		std::filesystem::copy_file("/etc/resolv.conf", resolvPath,
			std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) {
			cerr << "Copy resolv.conf failed: " << ec.message() << endl;
		}
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
