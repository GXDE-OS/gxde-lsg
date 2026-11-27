#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>
#include <string.h>
#include <strings.h>
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
	// 容器内使用与宿主机一致的账户：同名、同 uid/gid、同密码，sudoer 身份也一致
	HostUser hostUser;
	if (argc > 1 && strcmp(argv[1], "install-deb") == 0) {
		if (!Utils::hostUser("", hostUser)) {
			cerr << "Cannot determine the host user, please run via sudo or pkexec" << endl;
			return 1;
		}
		return !Utils::callInstallDeb(argc, argv, hostUser.name);
	}
	string localUserName = ""; // 显式指定的宿主机用户名（可选）
	bool waylandSession = false; // --wayland：使用 FlakeWM 启动原生 Wayland 会话
	for (int i = 1; i < argc; ++i) {
		if (strcasecmp(argv[i], "--wayland") == 0) {
			waylandSession = true;
		}
		else if (argv[i][0] == '-') {
			// 未知选项不能当成用户名，否则会悄悄退回默认会话
			cerr << "Unknown option: " << argv[i] << endl;
			Utils::showHelp(argv);
			return 1;
		}
		else if (localUserName == "") {
			localUserName = argv[i];
		}
	}
	string programPath = Utils::programPath();
	string rootfsPath = Utils::rootfsPath();
	string rootfsTarPath = Utils::rootfsTarPath();
	string containerName = CONTAINERNAME;
	
	if (!Utils::hostUser(localUserName, hostUser)) {
		if (localUserName != "") {
			cerr << "Host user '" << localUserName << "' not found (or is root)" << endl;
		}
		else {
			cerr << "Cannot determine the host user: run via sudo/pkexec, or pass it: " << argv[0] << " <user>" << endl;
		}
		return 1;
	}
	string userName = hostUser.name; // 容器内的用户名
	string hostname = Utils::hostName();
	cout << "Container user: " << userName << " (uid " << hostUser.uid << ", "
		<< (hostUser.sudoer ? "sudoer" : "not sudoer") << "), hostname: " << hostname << endl;
	
	string x11Display = Utils::getEnv("DISPLAY");
	string hostWaylandSocket = Utils::hostWaylandSocket(hostUser.uid);
	// 只有 Wayland 会话才把宿主机 Wayland 交给容器，X11 会话里的程序应留在 Xephyr 内
	string waylandDisplay = waylandSession ? Container::containerWaylandSocket : "";
	string lang = Utils::getEnv("LANG");
	string language = Utils::getEnv("LANGUAGE");
	
	if (waylandSession && hostWaylandSocket == "") {
		cerr << "--wayland requires a host Wayland session, but no Wayland socket was found!" << endl;
		return 1;
	}
	if (!waylandSession && x11Display == "") {
		cerr << "$DISPLAY Not Found!" << endl;
		return 1;
	}
	if (hostWaylandSocket != "") {
		cout << "Host Wayland socket: " << hostWaylandSocket << endl;
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
		if (!Container::provisionUser(rootfsPath, hostUser, hostname, lang)) {
			return 1;
		}
		if (!Container::loadingLinuxContainer(rootfsPath, containerName, hostUser, hostname, Container::isLinuxKernelSupportContainer(), hostWaylandSocket)) {
			return 1;
		}
	}
	if (Container::isContainerRunning(containerName)) {
		// 网络、蓝牙、电源服务由宿主机提供：屏蔽容器内的同名服务并启动转发
		Container::maskServices(containerName, {"NetworkManager.service", "bluetooth.service", "upower.service"});
		DBusBridge::Options bridgeOptions = DBusBridge::defaultOptions(containerName);
		bridgeOptions.hostUid = hostUser.uid;
		if (DBusBridge::spawn(bridgeOptions) < 0) {
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
		vector<string> command = Container::desktopSessionCommand(waylandSession);
		Container::runAppInContainer(containerName, 
			userName, 
			command, 
			x11Display, 
			waylandDisplay,
			lang,
			language,
			hostUser.uid);
	}
	while(Container::isContainerRunning(containerName)){
		// 只有在 container 停止后才会停止运行程序
		sleep(2);
	}
	return 0;
}
