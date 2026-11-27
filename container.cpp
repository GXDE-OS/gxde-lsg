#include "container.h"
#include "utils.h"
#include <unistd.h>
#include <sys/wait.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <istream>
#include <iostream>

using namespace std;

bool Container::isLinuxKernelSupportContainer()
{
	string version = Utils::linuxKernelVersion();
	istringstream iss(version);
	string token;
	int majorVersion;
	int minVersion;
	// 获取 Linux 内核版本号第一位
	getline(iss, token, '.');
	majorVersion = atoi(token.c_str());
	// 获取 Linux 内核版本号第二位
	getline(iss, token, '.');
	minVersion = atoi(token.c_str());
	// 要求版本号要大于 5.4
	return (majorVersion > 5) || ((majorVersion == 5) && (minVersion > 4));
}

bool Container::isContainerRunning(string containerName)
{
	vector<string> args;
	args.push_back("-M");
	args.push_back(containerName);
	args.push_back("/bin/true");
	int code = Utils::runCommand("/usr/bin/systemd-run", args);
	return !code;
}

void Container::installApp(string containerName, vector<string> debList, string user)
{
	string rootfsPath = Utils::rootfsPath();
	vector<string> command;
	string debInstallerPath = rootfsPath + "/usr/bin/gxde-deb-installer";
	string useUser = user;
	if (std::filesystem::exists(debInstallerPath)) {
		command.push_back("gxde-deb-installer");	
	}
	else {
		// 如果不存在 GXDE deb installer,则使用 apt/aptss 进行安装
		useUser = "root";
		if (std::filesystem::exists(rootfsPath + "/usr/bin/aptss")) {
			// 如果存在 aptss 则使用 aptss
			command.push_back("aptss");
		}
		else {
			// 使用 apt
			command.push_back("apt");	
		}
		command.push_back("install");
		command.push_back("-y");
		command.push_back("--allow-downgrades");
	}
	for (string i: debList) {
		command.push_back(i);
	}
	runAppInContainer(containerName, useUser, command);
}

bool Container::loadingLinuxContainer(
	string containerPath, 
	string containerName, 
	std::string localUserName, 
	bool loadSystemd,
	std::string hostWaylandSocket)
{
	// 允许运行 x11 程序
	system("xhost +");
	// 启用 systemd-nspawn
	vector<string> args;
	if (loadSystemd) {
		args.push_back("-b");	
	}
	args.push_back("-D");
	args.push_back(containerPath);
	args.push_back("-M");
	args.push_back(containerName);
	// 挂载 pulseaudio
	if (std::filesystem::exists("/run/user/1000/pulse/pid")) {
		args.push_back("--bind-ro=/run/user/1000/pulse/");
	}
	// 挂载宿主机 Wayland socket，供 --wayland（FlakeWM）会话使用
	if (hostWaylandSocket != "") {
		args.push_back("--bind-ro=" + hostWaylandSocket + ":" + containerWaylandSocket);
	}
	// 挂载 GPU 设备，供容器内 FlakeWM / Xwayland / 应用使用硬件渲染
	// 仅绑定设备节点不够，还需让 cgroup 允许访问 DRM 字符设备
	if (std::filesystem::exists("/dev/dri")) {
		args.push_back("--bind=/dev/dri");
		args.push_back("--property=DeviceAllow=char-drm rw");
	}
	// 挂载路径
	vector<string> path = {"/dev/kvm", "/dev/binder", "/dev/hwbinder", "/dev/vndbinder", 
		"/dev/ashmem"};
	for(long unsigned int i = 0; i < path.size(); ++i){
		string setPath = path[i];
		if (std::filesystem::exists(setPath)) {
			args.push_back("--bind=" + setPath);	
		}
	}
	// 挂载 / 和 /home
	//args.push_back("--bind=/:/sd");
	//args.push_back("--bind=/:/home/gxde/sd");
	args.push_back("--bind=/home:/home/gxde/home");
	if (localUserName != "") {
		string homePath = "/home/" + localUserName;
		if (std::filesystem::exists(homePath + "/Desktop")) {
			args.push_back("--bind=" + homePath + "/Desktop/:/home/gxde/Desktop");
		}
		else if (std::filesystem::exists(homePath + "/桌面")) {
			args.push_back("--bind=" + homePath + "/Desktop/:/home/gxde/Desktop");
		}	
		if (std::filesystem::exists(homePath + "/Videos")) {
			args.push_back("--bind=" + homePath + "/Videos/:/home/gxde/Videos");
		}
		else if (std::filesystem::exists(homePath + "/视频")) {
			args.push_back("--bind=" + homePath + "/Videos/:/home/gxde/Videos");
		}
		if (std::filesystem::exists(homePath + "/Documents")) {
			args.push_back("--bind=" + homePath + "/Documents/:/home/gxde/Documents");
		}
		else if (std::filesystem::exists(homePath + "/文档")) {
			args.push_back("--bind=" + homePath + "/文档/:/home/gxde/Documents");
		}
		if (std::filesystem::exists(homePath + "/Music")) {
			args.push_back("--bind=" + homePath + "/Music/:/home/gxde/Music");
		}
		else if (std::filesystem::exists(homePath + "/音乐")) {
			args.push_back("--bind=" + homePath + "/音乐/:/home/gxde/Music");
		}
		if (std::filesystem::exists(homePath + "/Downloads")) {
			args.push_back("--bind=" + homePath + "/Downloads/:/home/gxde/Downloads");
		}
		else if (std::filesystem::exists(homePath + "/下载")) {
			args.push_back("--bind=" + homePath + "/下载/:/home/gxde/Downloads");
		}
		if (std::filesystem::exists(homePath + "/Pictures")) {
			args.push_back("--bind=" + homePath + "/Pictures/:/home/gxde/Pictures");
		}
		else if (std::filesystem::exists(homePath + "/图片")) {
			args.push_back("--bind=" + homePath + "/图片/:/home/gxde/Pictures");
		}
	}
	
	//args.push_back("--bind=/:/home/gxde/host");
	// 部分发行版（如 Arch）默认没有 /media，绑定不存在的路径会导致 nspawn 启动失败
	if (std::filesystem::exists("/media")) {
		args.push_back("--bind=/media:/media");
	}
	// --bind-ro=
	pid_t nspawnPid = Utils::runCommand("/usr/bin/systemd-nspawn", args, false);
	if (nspawnPid < 0) {
		return false;
	}
	while(!isContainerRunning(containerName)){
		// nspawn 已退出说明容器启动失败，不再继续等待
		int status;
		if (waitpid(nspawnPid, &status, WNOHANG) == nspawnPid) {
			cerr << "systemd-nspawn exited before the container was ready" << endl;
			return false;
		}
		sleep(1);
	}
	return true;
}

void Container::runAppInContainer(string containerName, 
	string user, 
	vector<string> command, 
	string x11Display, 
	string waylandDisplay,
	string lang,
	string language)
{
	// systemd-run -M containerName --setenv=DISPLAY=:0 firefox
	// 这里是从后往前的顺序生成 args
	vector<string> args = command;
	args.insert(args.begin(), user);
	args.insert(args.begin(), "--uid");
	// 传入环境变量
	vector<string> envValue = {x11Display, waylandDisplay, lang, language, "/run/user/1000/pulse/native"};
	vector<string> envName = {"DISPLAY", "WAYLAND_DISPLAY", "LANG", "LANGUAGE", "PULSE_SERVER"};
	for(long unsigned int i = 0; i < envValue.size(); ++i){
		string name = envName[i];
		string value = envValue[i];
		if (value != "") {
			args.insert(args.begin(), "--setenv=" + name + "=" + value);	
		}
	}
	args.insert(args.begin(), containerName);
	args.insert(args.begin(), "-M");
	Utils::runCommand("/usr/bin/systemd-run", args);
}

void Container::maskServices(string containerName, vector<string> units)
{
	// 这些服务改由宿主机通过 dbus-bridge 提供，容器内的同名服务会抢占总线名，需要屏蔽
	vector<string> command = {"systemctl", "mask", "--now"};
	command.insert(command.end(), units.begin(), units.end());
	runAppInContainer(containerName, "root", command);
}

void Container::runXephyr(string containerName, string user)
{
	vector<string> command;
	command.push_back("Xephyr");
	command.push_back("-br");
	command.push_back("-ac");
	command.push_back("-noreset");
	command.push_back("-resizeable");
	command.push_back("-title");
	command.push_back("GXDE OS");
	command.push_back(":1");
	runAppInContainer(containerName, user, command);
}

// 启动 GXDE 桌面的命令
// waylandSession 为 false：Xephyr 中的 X11 会话（startgxde_window）
// waylandSession 为 true：原生 Wayland 会话，FlakeWM 以 wlroots 的 wayland 后端作为窗口运行在宿主机 Wayland 上
std::vector<std::string> Container::desktopSessionCommand(bool waylandSession)
{
	if (!waylandSession) {
		return {"startgxde_window"};
	}
	static const char *script = R"SH(
if [[ ! -S ${WAYLAND_DISPLAY:-} ]]; then
	echo "Host Wayland socket not found in container: ${WAYLAND_DISPLAY:-<unset>}" >&2
	echo "If the container was started without it, restart it: machinectl poweroff gxde" >&2
	exit 1
fi
if ! command -v startflakewm >/dev/null; then
	echo "startflakewm not found, install gxde-flake-session in the container" >&2
	exit 1
fi
# systemd-run --uid 不会建 XDG_RUNTIME_DIR，FlakeWM 需要在这里创建自己的 Wayland socket
if [[ -z ${XDG_RUNTIME_DIR:-} ]]; then
	export XDG_RUNTIME_DIR=/tmp/gxde-lsg-runtime-$UID
	mkdir -p -m 700 "$XDG_RUNTIME_DIR"
fi
# 只用 wayland 后端（嵌套在宿主机窗口里），不碰 DRM/libinput
export WLR_BACKENDS=wayland
# 容器里没有 GPU 设备时用软件渲染
if [[ ! -d /dev/dri ]]; then
	export WLR_RENDERER=pixman
fi
unset DISPLAY
echo "GXDE FlakeWM session on host Wayland: $WAYLAND_DISPLAY"
# 不能用 dbus-launch --exit-with-session：它靠 stdin/X 连接判断会话结束，
# systemd-run 下 stdin 是 /dev/null 且没有 DISPLAY，总线会被立刻杀掉
exec dbus-run-session -- startflakewm
)SH";
	// 源文件是 CRLF，raw string 里会带上 \r，交给 bash 前去掉
	std::string s = script;
	s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
	return {"/bin/bash", "-c", s};
}
