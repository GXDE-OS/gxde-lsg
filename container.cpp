#include "container.h"
#include "utils.h"
#include <unistd.h>
#include <filesystem>
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

void Container::loadingLinuxContainer(string containerPath, string containerName, bool loadSystemd)
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
	// 挂载 / 和 /home
	args.push_back("--bind=/:/sd");
	// --bind-ro=
	Utils::runCommand("/usr/bin/systemd-nspawn", args, false);
	while(!isContainerRunning(containerName)){
		sleep(1);
	}
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
	vector<string> envValue = {x11Display, waylandDisplay, lang, language};
	vector<string> envName = {"DISPLAY", "WAYLAND_DISPLAY", "LANG", "LANGUAGE"};
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

void Container::disabledNetworkManager(string containerName)
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
