#include "container.h"
#include "utils.h"
#include <unistd.h>

using namespace std;

bool Container::isContainerRunning(string containerName)
{
	vector<string> args;
	args.push_back("-M");
	args.push_back(containerName);
	args.push_back("/bin/true");
	int code = Utils::runCommand("/usr/bin/systemd-run", args);
	return !code;
}

void Container::loadingLinuxContainer(string containerPath, string containerName)
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
	Utils::runCommand("/usr/bin/systemd-nspawn", args, false);
	while(!isContainerRunning(containerName)){
		sleep(1);
	}
}

void Container::runAppInContainer(string containerName, string user, vector<string> command, string x11Display)
{
	// systemd-run -M containerName --setenv=DISPLAY=:0 firefox
	// 这里是从后往前的顺序生成 args
	vector<string> args = command;
	args.insert(args.begin(), user);
	args.insert(args.begin(), "--uid");
	args.insert(args.begin(), "--setenv=DISPLAY=" + x11Display);
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
