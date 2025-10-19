#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>

class Utils {
public:
	static int runCommand(std::string command, std::vector<std::string> args, bool isWaiting = true);
	static void loadMyselfToRoot(char *argv[]);
	static bool checkEnvironment();
	static bool unpackTar(std::string tarPath, std::string dirPath);
	static void showHelp();
	static std::string programPath();
};

#endif
