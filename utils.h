#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>
#include <sys/types.h>

#define CONTAINERNAME "gxde"

// 宿主机上运行 gxde-lsg 的用户，容器内会创建与之一致的账户
struct HostUser {
	std::string name;
	uid_t uid = 0;
	gid_t gid = 0;
	std::string group;        // 主组名
	std::string gecos;
	std::string home;         // 宿主机上的家目录
	std::string shell;
	std::string passwordHash; // 宿主机 /etc/shadow 中的密码哈希，读不到则为空
	bool sudoer = false;      // 是否属于宿主机的 sudo/wheel/admin 组
};

class Utils {
public:
	static int runCommand(std::string command, std::vector<std::string> args, bool isWaiting = true);
	static void loadMyselfToRoot(char *argv[]);
	static bool checkEnvironment();
	static bool unpackTar(std::string tarPath, std::string dirPath);
	static void showHelp(char *argv[]);
	static bool callInstallDeb(int argc, char *argv[], std::string userName);
	static void loadingKernelModule();
	static std::string getEnv(std::string envName);
	static std::string hostWaylandSocket(uid_t uid);
	static bool hostUser(std::string name, HostUser &user);
	static std::string hostName();
	// 宿主机发行版名称（os-release 的 PRETTY_NAME）
	static std::string hostPrettyName();
	static std::string linuxKernelVersion();
	static std::string programPath();
	static std::string rootfsPath(); 
	static std::string rootfsTarPath();
};

#endif
