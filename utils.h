#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>

#define CONTAINERNAME "gxde"

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
	static std::string linuxKernelVersion();
	static std::string programPath();
	static std::string rootfsPath(); 
	static std::string rootfsTarPath();
};

#endif
