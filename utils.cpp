#include "utils.h"
#include "container.h"
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <sys/utsname.h>
#include <fstream>
#include <sstream>
#include <cstdlib>

using namespace std;

void Utils::loadingKernelModule()
{
	system("modprobe kmre-ashmem");
	system("modprobe kmre-binder");
	system("modprobe kmre-virtwifi");
}

string Utils::linuxKernelVersion()
{
	struct utsname buffer;
	if (uname(&buffer) != 0) {
		return "";
	}
	return buffer.release;
}

bool Utils::callInstallDeb(int argc, char *argv[], string userName)
{
	if (!Container::isContainerRunning(CONTAINERNAME)) {
		cerr << "Container isn't running!" << endl;
		return false;
	}
	if (argc <= 2) {
		return true;
	}
	vector<string> debList;
	for (int i = 2; i < argc; ++i) {
		debList.push_back(string(argv[i]));
	}
	Container::installApp(CONTAINERNAME, debList, userName);
	return true;
}

string Utils::rootfsPath()
{
	return programPath() + "/gxde-rootfs";
}

string Utils::rootfsTarPath()
{
	return programPath() + "/gxde-rootfs.tar.xz";
}

bool Utils::unpackTar(string tarPath, string dirPath)
{
	if (!std::filesystem::exists(dirPath)) {
		// 文件夹不存在，尝试创建文件夹
		if (!std::filesystem::create_directories(dirPath)) {
			// 创建失败
			return false;
		}
		// 判断 tar 是否存在
		if (!std::filesystem::exists(tarPath)) {
			return false;
		}
	}
	vector<string> args;
	args.push_back("-xf");
	args.push_back(tarPath);
	args.push_back("-C");
	args.push_back(dirPath);
	int code = Utils::runCommand("/usr/bin/tar", args);
	if (code != 0) {
		// 解压错误
		return false;
	}
	return true;
}

// 尝试自行调用 sudo 提权至 root
void Utils::loadMyselfToRoot(char *argv[])
{
	execv("/usr/bin/sudo", argv);
	cerr << "错误: 无法执行命令" << std::endl;
}

bool Utils::checkEnvironment()
{
	if (!std::filesystem::exists("/usr/bin/systemd-nspawn")) {
		cerr << "systemd-nspawn not found!" << endl;
		return false;
	}
	return true;
}

int Utils::runCommand(string command, vector<std::string> args, bool isWaiting)
{
	// 将 vector<string> 转换为 char* 数组，供 execv 使用
	vector<char *> exec_args;
	exec_args.push_back(const_cast<char*>(command.c_str()));
	for (auto& arg : args) {
		exec_args.push_back(const_cast<char*>(arg.c_str()));
	}
	exec_args.push_back(nullptr);  // execv 要求参数列表以 nullptr 结尾
	// 创建子进程，利用 execv 执行命令
	pid_t pid = fork();
	if (pid == 0) {
		// 子进程
		execv(command.c_str(), exec_args.data());
		// 若执行到该行，则代表命令失败
		exit(EXIT_FAILURE);
	}
	else if (pid < 0) {
		// 创建子进程失败
		return isWaiting ? 1 : -1;
	}
	else {
		if (!isWaiting) {
			return pid;
		}
		// 父进程
		int status;
		pid_t ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			// waitpid failed
			return 1;
		}
		if (WIFEXITED((status))) {
			return WEXITSTATUS(status);
		}
		else if (WIFSIGNALED(status)) {
			return WTERMSIG(status);
		}
	}
	return 0;
}

void Utils::showHelp(char *argv[])
{
	cout << "GXDE LSG" << endl;
	cout << "一个利用 systemd-nspawn 在其它 Linux 发行版（包括非 Debian 发行版）运行 Debian + GXDE 桌面环境的工具" << endl;
	cout << "https://gitee.com/GXDE-OS/gxde-lsg" << endl;
	cout << "By gfdgd xi" << endl;
	
	cout << "Usage:" << endl;
	cout << argv[0] << " [COMMAND] [OPTIONS]" << endl;
	cout << endl;
	cout << "Options:" << endl;
	cout << "  --wayland    使用 FlakeWM 启动原生 Wayland 会话（需要宿主机为 Wayland 会话）" << endl;
	cout << "  --immersive  沉浸式会话（隐含 --wayland）：请求宿主机 FlakeWM 去掉窗口标题栏和边框并全屏，" << endl;
	cout << "               容器内桌面注销后关闭容器。供登录会话 \"Linux Subsystem for GXDE\" 使用" << endl;
	cout << "  --wayland-display=NAME" << endl;
	cout << "               宿主机 Wayland socket 名（默认取 $WAYLAND_DISPLAY，再默认 wayland-0）" << endl;
}

string Utils::programPath()
{	
	char l_cCurrentDir[1024];
	
	//获取当前程序绝对路径
	
	system("pwd");
	int  l_icnt = readlink("/proc/self/exe", l_cCurrentDir, 1024);
	if (l_icnt < 0 || l_icnt >= 1024)
	{
		printf("***Error***\n");
		exit(-1);
	}
	//获取当前目录绝对路径，即去掉程序名
	int  i;
	for (i = l_icnt; i >= 0; --i)
	{
		if (l_cCurrentDir[i] == '/')
		{
			l_cCurrentDir[i + 1] = '\0';
			break;
		}
	}
	return string(l_cCurrentDir);
}

string Utils::getEnv(string envName)
{
	const char* value = getenv(envName.c_str());
	if (value == nullptr) {
		return "";
	}
	return value;
}

// 返回宿主机 Wayland socket 的绝对路径，宿主机不是 Wayland 会话时返回空字符串
// 经 sudo 提权后 WAYLAND_DISPLAY / XDG_RUNTIME_DIR 通常会被清掉，此时按宿主机用户的 uid 推测
string Utils::hostWaylandSocket(uid_t uid)
{
	string display = getEnv("WAYLAND_DISPLAY");
	if (display == "") {
		display = "wayland-0";
	}
	string socketPath = display;
	if (display[0] != '/') {
		string runtimeDir = getEnv("XDG_RUNTIME_DIR");
		if (runtimeDir == "" || runtimeDir == "/run/user/0") {
			runtimeDir = "/run/user/" + std::to_string(uid);
		}
		socketPath = runtimeDir + "/" + display;
	}
	std::error_code ec;
	if (!std::filesystem::is_socket(socketPath, ec)) {
		return "";
	}
	return socketPath;
}

// 按 ':' 切分 /etc/passwd、/etc/group、/etc/shadow 的一行（保留空字段）
static vector<string> splitFields(const string &line, char sep = ':')
{
	vector<string> fields;
	std::stringstream stream(line);
	string field;
	while (std::getline(stream, field, sep)) {
		fields.push_back(field);
	}
	if (!line.empty() && line.back() == sep) {
		fields.push_back("");
	}
	return fields;
}

// 确定宿主机用户：name 非空时按用户名查找，否则取发起 sudo/pkexec 的用户
// 静态链接下 getpwnam 等依赖 NSS 动态库，这里直接解析 /etc/passwd 等文件
bool Utils::hostUser(string name, HostUser &user)
{
	string uidText;
	if (name == "") {
		for (const char *env: {"SUDO_UID", "PKEXEC_UID"}) {
			uidText = getEnv(env);
			if (uidText != "") {
				break;
			}
		}
		if (uidText == "") {
			return false;
		}
	}
	bool found = false;
	string line;
	std::ifstream passwd("/etc/passwd");
	while (std::getline(passwd, line)) {
		vector<string> f = splitFields(line);
		if (f.size() < 7) {
			continue;
		}
		if ((name != "" && f[0] == name) || (name == "" && f[2] == uidText)) {
			user.name = f[0];
			user.uid = strtoul(f[2].c_str(), nullptr, 10);
			user.gid = strtoul(f[3].c_str(), nullptr, 10);
			user.gecos = f[4];
			user.home = f[5];
			user.shell = f[6];
			found = true;
			break;
		}
	}
	// 不把宿主机 root 映射进容器
	if (!found || user.uid == 0) {
		return false;
	}
	user.group = user.name;
	user.sudoer = false;
	std::ifstream groupFile("/etc/group");
	while (std::getline(groupFile, line)) {
		vector<string> f = splitFields(line);
		if (f.size() < 3) {
			continue;
		}
		bool isPrimary = strtoul(f[2].c_str(), nullptr, 10) == user.gid;
		if (isPrimary) {
			user.group = f[0];
		}
		bool isMember = isPrimary;
		if (!isMember && f.size() >= 4) {
			for (const string &member: splitFields(f[3], ',')) {
				if (member == user.name) {
					isMember = true;
					break;
				}
			}
		}
		if (isMember && (f[0] == "sudo" || f[0] == "wheel" || f[0] == "admin")) {
			user.sudoer = true;
		}
	}
	user.passwordHash = "";
	std::ifstream shadow("/etc/shadow");
	while (std::getline(shadow, line)) {
		vector<string> f = splitFields(line);
		// 只同步真正的密码哈希，锁定（! / *）或空密码不同步
		if (f.size() >= 2 && f[0] == user.name && f[1].size() > 1 && f[1][0] == '$') {
			user.passwordHash = f[1];
			break;
		}
	}
	return true;
}

string Utils::hostName()
{
	char name[256] = {0};
	if (gethostname(name, sizeof(name) - 1) != 0) {
		return "";
	}
	return name;
}

string Utils::hostPrettyName()
{
	// 按 os-release(5) 的约定：先读 /etc/os-release，不存在再读 /usr/lib/os-release
	for (const char *path: {"/etc/os-release", "/usr/lib/os-release"}) {
		std::ifstream file(path);
		if (!file) {
			continue;
		}
		string line, name;
		while (std::getline(file, line)) {
			string *target = nullptr;
			string value;
			if (line.rfind("PRETTY_NAME=", 0) == 0) {
				value = line.substr(12);
			}
			else if (line.rfind("NAME=", 0) == 0 && name == "") {
				value = line.substr(5);
				target = &name;
			}
			else {
				continue;
			}
			if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
				value = value.substr(1, value.size() - 2);
			}
			if (!target) {
				if (value != "") {
					return value;
				}
				continue;
			}
			*target = value;
		}
		if (name != "") {
			return name;
		}
	}
	return "Linux";
}
