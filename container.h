#ifndef CONTAINER_H
#define CONTAINER_H

#include <string>
#include <vector>

class Container {
public:
	static bool isContainerRunning(std::string containerName);
	static void loadingLinuxContainer(std::string containerPath, std::string containerName);
	static void runAppInContainer(std::string containerName, std::string user, std::vector<std::string> command, std::string x11Display = ":0");
	static void disabledNetworkManager(std::string containerName);
	static void runXephyr(std::string containerName, std::string user);
	
private:

};

#endif
