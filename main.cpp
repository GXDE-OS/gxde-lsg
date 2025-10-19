#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

int runCommand(string command, vector<string> args, bool isWaiting = true)
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
		return 1;
	}
	else {
		if (!isWaiting) {
			return 0;
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

bool unpackTar(string tarPath, string dirPath)
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
	int code = runCommand("/usr/bin/tar", args);
	if (code != 0) {
		// 解压错误
		return false;
	}
	return true;
}

bool isContainerRunning(string containerName)
{
	vector<string> args;
	args.push_back("-M");
	args.push_back(containerName);
	args.push_back("/bin/true");
	int code = runCommand("/usr/bin/systemd-run", args);
	return !code;
}

void loadingLinuxContainer(string containerPath, string containerName)
{
	// 允许运行 x11 程序
	system("xhost +");
	// 启用 systemd-nspawn
	vector<string> args;
	args.push_back("-b");
	args.push_back("-D");
	args.push_back(containerPath);
	args.push_back("-M");
	args.push_back(containerName);
	runCommand("/usr/bin/systemd-nspawn", args, false);
	while(!isContainerRunning(containerName)){
		sleep(1);
	}
}

void runAppInContainer(string containerName, string user, vector<string> command, string x11Display = ":0")
{
	// systemd-run -M containerName --setenv=DISPLAY=:0 firefox
	// 这里是从后往前的顺序生成 args
	vector<string> args = command;
	args.insert(args.begin(), user);
	args.insert(args.begin(), "--uid");
	args.insert(args.begin(), "--setenv=DISPLAY=" + x11Display);
	args.insert(args.begin(), containerName);
	args.insert(args.begin(), "-M");
	runCommand("/usr/bin/systemd-run", args);
}

// 尝试自行调用 sudo 提权至 root
void loadMyselfToRoot(char *argv[])
{
	execv("/usr/bin/sudo", argv);
	cerr << "错误: 无法执行命令" << std::endl;
}

void disabledNetworkManager(string containerName)
{
	vector<string> disableCommand;
	disableCommand.push_back("systemctl");
	disableCommand.push_back("disable");
	disableCommand.push_back("NetworkManager");
	vector<string> stopCommand;
	stopCommand.push_back("systemctl");
	stopCommand.push_back("stop");
	stopCommand.push_back("NetworkManager");
	runAppInContainer(containerName, "root", disableCommand);
	runAppInContainer(containerName, "root", stopCommand);
}

void runXephyr(string containerName, string user)
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

bool checkEnvironment()
{
	if (!std::filesystem::exists("/usr/bin/systemd-nspawn")) {
		cerr << "systemd-nspawn not found!" << endl;
		return false;
	}
	return true;
}

int main(int argc, char *argv[])
{
	// 判断用户是否使用 root 权限运行程序
	if (getuid() != 0) {
		cerr << "Please run with root!" << endl;
		return 1;
	}
	if (!checkEnvironment()) {
		return 1;
	}
	// 获取指定用户 uid
	string userName = "root";
	if (argc > 1) {
		userName = argv[1];
	}
	string programPath = std::filesystem::current_path();
	//string rootfsPath = programPath + "/gxde-rootfs";
	string rootfsPath = "/opt/gxde-rootfs/";
	string rootfsTarPath = programPath + "/gxde-rootfs.tar.xz";
	string containerName = "gxde";
	if (!std::filesystem::exists(rootfsPath + "/etc/os-version")) {
		cout << rootfsPath << " Not Found!" << endl;
		cout << "Try to unpack rootfs file: " << rootfsTarPath << endl;
		if (!unpackTar(rootfsTarPath, rootfsPath)) {
			cerr << "Unpack failed!" << endl;
			return 1;
		}
	}
	if (!isContainerRunning(containerName)) {
		// 如果容器已存在，则不重复 loading
		loadingLinuxContainer(rootfsPath, containerName);
	}
	if (isContainerRunning(containerName)) {
		// 禁用 network manager
		disabledNetworkManager(containerName);
		// 拷贝宿主机的 /etc/resolve.conf
		std::filesystem::copy_file("/etc/resolv.conf", 
			rootfsPath + "/etc/resolv.conf",
			std::filesystem::copy_options::overwrite_existing);
		vector<string> command;
		command.push_back("startgxde_window");
		runAppInContainer(containerName, userName, command);
	}
	while(isContainerRunning(containerName)){
		// 只有在 container 停止后才会停止运行程序
		sleep(2);
	}
	return 0;
}
