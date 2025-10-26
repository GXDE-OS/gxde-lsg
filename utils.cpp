#include "utils.h"
#include "container.h"
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <sys/utsname.h>

using namespace std;

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

void Utils::showHelp(char *argv[])
{
	cout << "GXDE LSG" << endl;
	cout << "一个利用 systemd-nspawn 在其它 Linux 发行版（包括非 Debian 发行版）运行 Debian + GXDE 桌面环境的工具" << endl;
	cout << "https://gitee.com/GXDE-OS/gxde-lsg" << endl;
	cout << "By gfdgd xi" << endl;
	
	cout << "Usage:" << endl;
	cout << argv[0] << " [COMMAND] [OPTIONS]" << endl;
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
