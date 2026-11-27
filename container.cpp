#include "container.h"
#include "utils.h"
#include <unistd.h>
#include <sys/wait.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <istream>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

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
	const HostUser &user,
	std::string hostname,
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
	// gxde-dock 带有 cap_ipc_lock 文件能力，不在 nspawn 默认能力边界集内时 exec 会直接 EPERM
	args.push_back("--capability=CAP_IPC_LOCK");
	// hostname 继承宿主机
	if (hostname != "") {
		args.push_back("--hostname=" + hostname);
	}
	// 挂载 pulseaudio（容器内用户与宿主机同 uid，路径一致）
	string pulseDir = "/run/user/" + std::to_string(user.uid) + "/pulse/";
	if (std::filesystem::exists(pulseDir + "pid")) {
		args.push_back("--bind-ro=" + pulseDir);
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
	// 容器内显示的机器名：fastfetch 等工具从 DMI 读取 Host，用宿主机运行期目录中的文件覆盖
	// product_version 会被拼在名字后面（"名字 (版本)"），一并清空
	{
		const string dmiDir = "/run/gxde-lsg/dmi";
		const string sysDmi = "/sys/devices/virtual/dmi/id";
		std::error_code ec;
		std::filesystem::create_directories(dmiDir, ec);
		std::ofstream(dmiDir + "/product_name", std::ios::trunc) << productNamePrefix << Utils::hostPrettyName() << "\n";
		std::ofstream(dmiDir + "/product_version", std::ios::trunc);
		for (const char *field: {"product_name", "product_version"}) {
			if (std::filesystem::exists(sysDmi + "/" + field, ec) && std::filesystem::exists(dmiDir + "/" + field, ec)) {
				args.push_back("--bind-ro=" + dmiDir + "/" + field + ":" + sysDmi + "/" + field);
			}
		}
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
	// 挂载 /home 及宿主机用户的常用目录到容器内同名用户的家目录
	string containerHome = "/home/" + user.name;
	args.push_back("--bind=/home:" + containerHome + "/home");
	// {容器内目录名, 宿主机英文目录名, 宿主机中文目录名}
	vector<vector<string>> userDirs = {
		{"Desktop", "Desktop", "桌面"},
		{"Videos", "Videos", "视频"},
		{"Documents", "Documents", "文档"},
		{"Music", "Music", "音乐"},
		{"Downloads", "Downloads", "下载"},
		{"Pictures", "Pictures", "图片"},
	};
	for (const vector<string> &dir: userDirs) {
		for (size_t i = 1; i < dir.size(); ++i) {
			string hostDir = user.home + "/" + dir[i];
			if (std::filesystem::is_directory(hostDir)) {
				args.push_back("--bind=" + hostDir + "/:" + containerHome + "/" + dir[0]);
				break;
			}
		}
	}
	
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
	string language,
	uid_t uid)
{
	// systemd-run -M containerName --setenv=DISPLAY=:0 firefox
	// 这里是从后往前的顺序生成 args
	vector<string> args = command;
	args.insert(args.begin(), user);
	args.insert(args.begin(), "--uid");
	// 传入环境变量
	string pulseServer = uid == 0 ? "" : "/run/user/" + std::to_string(uid) + "/pulse/native";
	vector<string> envValue = {x11Display, waylandDisplay, lang, language, pulseServer};
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

// 生成单引号包裹的 shell 字面量
static string shellQuote(const string &value)
{
	string quoted = "'";
	for (char c: value) {
		if (c == '\'') {
			quoted += "'\\''";
		}
		else {
			quoted += c;
		}
	}
	return quoted + "'";
}

bool Container::provisionUser(string containerPath, const HostUser &user, string hostname, string lang)
{
	static const char *script = R"SH(
log() { echo "[gxde-lsg] $*"; }
# hostname 继承宿主机
if [ -n "$HOST" ]; then
	echo "$HOST" > /etc/hostname
	if grep -q '^127\.0\.1\.1[[:space:]]' /etc/hosts 2>/dev/null; then
		sed -i "s/^127\.0\.1\.1[[:space:]].*/127.0.1.1\t$HOST/" /etc/hosts
	else
		printf '127.0.1.1\t%s\n' "$HOST" >> /etc/hosts
	fi
fi
# 主组：与宿主机同 gid、同名
cur_group=$(getent group "$GID_" | cut -d: -f1)
if [ -z "$cur_group" ]; then
	if getent group "$GROUP" >/dev/null; then
		groupmod -g "$GID_" "$GROUP" || exit 1
	else
		groupadd -g "$GID_" "$GROUP" || exit 1
	fi
elif [ "$cur_group" != "$GROUP" ] && ! getent group "$GROUP" >/dev/null; then
	groupmod -n "$GROUP" "$cur_group" || exit 1
fi
# 账户：与宿主机同 uid、同名
HOME_DIR=/home/$NAME
cur_user=$(getent passwd "$UID_" | cut -d: -f1)
if [ -n "$cur_user" ] && [ "$cur_user" != "$NAME" ]; then
	if getent passwd "$NAME" >/dev/null; then
		log "uid $UID_ belongs to '$cur_user' but account '$NAME' also exists, fix it manually" >&2
		exit 1
	fi
	# 同 uid 的旧账户（如默认的 gxde）改名为宿主机用户，保留其配置
	old_home=$(getent passwd "$cur_user" | cut -d: -f6)
	usermod -l "$NAME" "$cur_user" || exit 1
	if [ -n "$old_home" ] && [ "$old_home" != "$HOME_DIR" ] && [ -d "$old_home" ] && [ ! -e "$HOME_DIR" ]; then
		mv "$old_home" "$HOME_DIR"
	fi
	log "renamed account '$cur_user' to '$NAME'"
elif [ -z "$cur_user" ]; then
	if getent passwd "$NAME" >/dev/null; then
		usermod -u "$UID_" "$NAME" || exit 1
	else
		useradd -M -u "$UID_" -g "$GID_" -d "$HOME_DIR" -s /bin/bash "$NAME" || exit 1
		log "created account '$NAME'"
	fi
fi
[ -x "$SHELL_" ] || SHELL_=/bin/bash
usermod -g "$GID_" -d "$HOME_DIR" -s "$SHELL_" -c "$GECOS" "$NAME" || exit 1
# 家目录：只修正目录本身的属主，不递归（其下会挂载宿主机目录）
mkdir -p "$HOME_DIR"
chown "$UID_:$GID_" "$HOME_DIR"
if [ ! -e "$HOME_DIR/.bashrc" ] && [ -d /etc/skel ]; then
	for f in /etc/skel/.[!.]* /etc/skel/*; do
		[ -e "$f" ] || continue
		b=$(basename "$f")
		if [ ! -e "$HOME_DIR/$b" ]; then
			cp -a "$f" "$HOME_DIR/$b" && chown -R "$UID_:$GID_" "$HOME_DIR/$b"
		fi
	done
fi
for g in audio video render; do
	if getent group "$g" >/dev/null; then
		usermod -aG "$g" "$NAME"
	fi
done
# 与宿主机一致：宿主机是 sudoer 则加入 sudo 组，否则移出
if [ "$SUDOER" = 1 ]; then
	usermod -aG sudo "$NAME"
else
	gpasswd -d "$NAME" sudo >/dev/null 2>&1
fi
# 密码与宿主机一致
if [ -n "$HASH" ]; then
	printf '%s:%s\n' "$NAME" "$HASH" | chpasswd -e || exit 1
fi
# 宿主机语言（LANG）对应的 locale 需在容器内生成，否则程序会报 setlocale 警告
if [ -n "$LOCALE" ] && [ "$LOCALE" != C ] && [ "$LOCALE" != POSIX ]; then
	norm=$(printf '%s' "$LOCALE" | sed 's/\.[Uu][Tt][Ff]-\{0,1\}8$/.utf8/')
	if ! locale -a 2>/dev/null | grep -qxF "$norm"; then
		entry=$(awk -v l="$LOCALE" '$1 == l { print; exit }' /usr/share/i18n/SUPPORTED 2>/dev/null)
		if [ -n "$entry" ] && [ -x /usr/sbin/locale-gen ]; then
			if grep -qE "^#[[:space:]]*$entry\$" /etc/locale.gen 2>/dev/null; then
				sed -i -E "s/^#[[:space:]]*($entry)\$/\1/" /etc/locale.gen
			else
				echo "$entry" >> /etc/locale.gen
			fi
			locale-gen >/dev/null && log "generated locale $LOCALE"
		else
			log "WARNING: locale $LOCALE is not available in the container" >&2
		fi
	fi
	if ! grep -q '^LANG=' /etc/default/locale 2>/dev/null; then
		echo "LANG=$LOCALE" >> /etc/default/locale
	fi
fi
if [ -e /usr/bin/sudo ] && [ ! -u /usr/bin/sudo ]; then
	log "WARNING: /usr/bin/sudo is not setuid, the rootfs was probably copied without preserving permissions; sudo will not work" >&2
fi
exit 0
)SH";
	// 参数写在脚本开头；脚本里含密码哈希，文件仅 root 可读，执行后立即删除
	string body = "NAME=" + shellQuote(user.name) + "\n"
		+ "UID_=" + std::to_string(user.uid) + "\n"
		+ "GID_=" + std::to_string(user.gid) + "\n"
		+ "GROUP=" + shellQuote(user.group) + "\n"
		+ "GECOS=" + shellQuote(user.gecos) + "\n"
		+ "SHELL_=" + shellQuote(user.shell) + "\n"
		+ "SUDOER=" + (user.sudoer ? "1" : "0") + "\n"
		+ "HASH=" + shellQuote(user.passwordHash) + "\n"
		+ "HOST=" + shellQuote(hostname) + "\n"
		+ "LOCALE=" + shellQuote(lang) + "\n"
		+ script;
	// 源文件是 CRLF，raw string 里会带上 \r
	body.erase(std::remove(body.begin(), body.end(), '\r'), body.end());
	string scriptInRootfs = "/root/.gxde-lsg-provision.sh";
	string scriptPath = containerPath + scriptInRootfs;
	std::error_code ec;
	std::filesystem::create_directories(containerPath + "/root", ec);
	{
		mode_t oldMask = umask(077);
		std::ofstream file(scriptPath, std::ios::trunc);
		umask(oldMask);
		if (!file) {
			cerr << "Cannot write " << scriptPath << endl;
			return false;
		}
		file << body;
	}
	chmod(scriptPath.c_str(), 0700);
	// 不开机、不注册的一次性 nspawn，此时没有任何绑定挂载，改名/移动家目录是安全的
	int code = Utils::runCommand("/usr/bin/systemd-nspawn", {"-q", "--register=no", "--console=pipe",
		"-D", containerPath, "/bin/bash", scriptInRootfs});
	std::filesystem::remove(scriptPath, ec);
	if (code != 0) {
		cerr << "Failed to set up user '" << user.name << "' in the container" << endl;
		return false;
	}
	return true;
}

bool Container::installBridgePolicy(string containerPath, const vector<string> &services)
{
	// 服务端（bluez / NetworkManager）回调客户端时用到的接口，bridge 以 root 身份代发，
	// 系统总线默认禁止 method_call，需显式放行（取自 bluez、NetworkManager 自带的策略）
	static const vector<string> callbackInterfaces = {
		"org.bluez.Agent1", "org.bluez.MediaEndpoint1", "org.bluez.MediaPlayer1",
		"org.bluez.Profile1", "org.bluez.GattCharacteristic1", "org.bluez.GattDescriptor1",
		"org.bluez.LEAdvertisement1", "org.bluez.AdvertisementMonitor1", "org.bluez.BatteryProvider1",
		"org.mpris.MediaPlayer2.Player", "org.freedesktop.DBus.ObjectManager",
		"org.freedesktop.DBus.Properties", "org.freedesktop.NetworkManager.SecretAgent",
	};
	string conf =
		"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\"\n"
		" \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
		"<!-- 由 gxde-lsg 生成：以下服务由宿主机经 dbus-bridge 提供，请勿手动修改 -->\n"
		"<busconfig>\n"
		"  <policy user=\"root\">\n";
	for (const string &name: services) {
		conf += "    <allow own=\"" + name + "\"/>\n";
	}
	for (const string &iface: callbackInterfaces) {
		conf += "    <allow send_interface=\"" + iface + "\"/>\n";
	}
	conf += "  </policy>\n  <policy context=\"default\">\n";
	for (const string &name: services) {
		conf += "    <allow send_destination=\"" + name + "\"/>\n";
	}
	conf += "  </policy>\n</busconfig>\n";

	string dir = containerPath + "/etc/dbus-1/system.d";
	string path = dir + "/gxde-lsg-bridge.conf";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	// 内容未变则不重写，避免容器运行中的 dbus-daemon 无谓地重新加载配置
	{
		std::ifstream old(path);
		std::stringstream current;
		current << old.rdbuf();
		if (old && current.str() == conf) {
			return true;
		}
	}
	std::ofstream file(path, std::ios::trunc);
	if (!file) {
		cerr << "Cannot write " << path << endl;
		return false;
	}
	file << conf;
	return true;
}
