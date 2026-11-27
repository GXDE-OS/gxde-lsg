#include "dbusbridge.h"
#include "dbuswire.h"
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;
using namespace DBusWire;

namespace DBusBridge {

namespace {

const char *DBUS_NAME = "org.freedesktop.DBus";
const char *DBUS_PATH = "/org/freedesktop/DBus";
const char *ERROR_FAILED = "org.freedesktop.DBus.Error.Failed";
const uint32_t NAME_FLAG_REPLACE_EXISTING = 0x2;
const uint32_t NAME_REPLY_IN_QUEUE = 2;

#define LOG(text) (cerr << "[dbus-bridge] " << text << endl)

// 容器内某个客户端在宿主机总线上的代理连接
struct HostClient {
	Connection conn;
	map<uint32_t, uint32_t> pending; // 宿主机侧 serial -> 容器侧原始 serial
};

// 宿主机服务回调容器客户端时，等待容器客户端回复
struct ReversePending {
	string containerClient;
	string hostSender;
	uint32_t hostSerial;
};

struct Service {
	string name;
	Connection container;   // 在容器总线上占用 name
	Connection hostSignal;  // 在宿主机总线上监听该服务的广播信号与在线状态
	map<string, unique_ptr<HostClient>> clients;  // 容器客户端唯一名 -> 宿主机连接
	map<uint32_t, ReversePending> reversePending; // 容器侧 serial -> 宿主机调用
	map<uint32_t, string> containerInternal;      // 自己发出的总线调用
	bool owned = false;
};

enum ConnKind { KIND_CONTAINER, KIND_HOST_SIGNAL, KIND_HOST_CLIENT };

struct PollTarget {
	Service *service;
	ConnKind kind;
	string client;
};

class Bridge {
public:
	explicit Bridge(const Options &options) : m_options(options) {}
	int run(int readyFd);

private:
	bool resolveContainerBus();
	bool setupService(Service &s);
	void requestName(Service &s);
	void releaseName(Service &s);
	HostClient *hostClientFor(Service &s, const string &containerClient);
	void dropClient(Service &s, const string &containerClient);
	void replyError(Connection &conn, const string &destination, uint32_t serial, const string &text);
	void forward(Connection &conn, Message &m);

	void handleContainer(Service &s, Message &m);
	void handleHostSignal(Service &s, Message &m);
	void handleHostClient(Service &s, const string &containerClient, Message &m);
	bool dispatch(const PollTarget &target, Connection *conn);
	void drainBacklogs();

	Options m_options;
	string m_containerBus;
	vector<unique_ptr<Service>> m_services;
};

bool isNameOwnerChanged(const Message &m, string &name, string &oldOwner, string &newOwner)
{
	if (m.type != TYPE_SIGNAL || m.sender != DBUS_NAME || m.member != "NameOwnerChanged") {
		return false;
	}
	size_t pos = 0;
	return m.readString(pos, name) && m.readString(pos, oldOwner) && m.readString(pos, newOwner);
}

bool readLeader(const string &machine, string &leader)
{
	ifstream file("/run/systemd/machines/" + machine);
	string line;
	while (getline(file, line)) {
		if (line.compare(0, 7, "LEADER=") == 0) {
			leader = line.substr(7);
			return !leader.empty();
		}
	}
	return false;
}

bool Bridge::resolveContainerBus()
{
	if (!m_options.containerBus.empty()) {
		m_containerBus = m_options.containerBus;
		return true;
	}
	string leader;
	if (!readLeader(m_options.machine, leader)) {
		return false;
	}
	// 通过容器 init 进程的根目录访问容器内的 system bus（需要 root）
	m_containerBus = "/proc/" + leader + "/root/run/dbus/system_bus_socket";
	return access(m_containerBus.c_str(), F_OK) == 0;
}

void Bridge::forward(Connection &conn, Message &m)
{
	if (m_options.verbose) {
		LOG("  => " << m.describe());
	}
	if (!conn.send(m)) {
		LOG("send failed: " << m.describe());
	}
}

void Bridge::replyError(Connection &conn, const string &destination, uint32_t serial, const string &text)
{
	Message error = Message::error(destination, serial, ERROR_FAILED, "gxde-lsg dbus-bridge: " + text);
	error.serial = conn.nextSerial();
	conn.send(error);
}

void Bridge::requestName(Service &s)
{
	Message m = Message::methodCall(DBUS_NAME, DBUS_PATH, DBUS_NAME, "RequestName");
	m.appendString(s.name);
	m.appendUint32(NAME_FLAG_REPLACE_EXISTING);
	m.serial = s.container.nextSerial();
	s.containerInternal[m.serial] = "RequestName";
	s.container.send(m);
}

void Bridge::releaseName(Service &s)
{
	Message m = Message::methodCall(DBUS_NAME, DBUS_PATH, DBUS_NAME, "ReleaseName");
	m.appendString(s.name);
	m.serial = s.container.nextSerial();
	s.containerInternal[m.serial] = "ReleaseName";
	s.container.send(m);
	s.owned = false;
}

bool Bridge::setupService(Service &s)
{
	if (!s.container.open(m_containerBus, geteuid())) {
		LOG(s.name << ": cannot connect to container bus " << m_containerBus);
		return false;
	}
	if (!s.hostSignal.open(m_options.hostBus, m_options.hostUid)) {
		LOG(s.name << ": cannot connect to host bus " << m_options.hostBus);
		return false;
	}
	auto addMatch = [](Connection &conn, const string &rule) {
		Message m = Message::methodCall(DBUS_NAME, DBUS_PATH, DBUS_NAME, "AddMatch");
		m.appendString(rule);
		Message reply;
		return conn.call(m, reply);
	};
	// 容器客户端断开时释放对应的宿主机连接
	if (!addMatch(s.container, "type='signal',sender='org.freedesktop.DBus',"
			"interface='org.freedesktop.DBus',member='NameOwnerChanged',arg2=''") ||
		// 宿主机服务的所有广播信号
		!addMatch(s.hostSignal, "type='signal',sender='" + s.name + "'") ||
		// 宿主机服务上线 / 下线
		!addMatch(s.hostSignal, "type='signal',sender='org.freedesktop.DBus',"
			"interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" + s.name + "'")) {
		LOG(s.name << ": AddMatch failed");
		return false;
	}
	Message query = Message::methodCall(DBUS_NAME, DBUS_PATH, DBUS_NAME, "NameHasOwner");
	query.appendString(s.name);
	Message reply;
	uint32_t hasOwner = 0;
	size_t pos = 0;
	if (!s.hostSignal.call(query, reply) || !reply.readUint32(pos, hasOwner)) {
		LOG(s.name << ": NameHasOwner failed");
		return false;
	}
	if (hasOwner) {
		requestName(s);
	}
	else {
		LOG(s.name << ": not running on host, waiting for it");
	}
	return true;
}

HostClient *Bridge::hostClientFor(Service &s, const string &containerClient)
{
	auto it = s.clients.find(containerClient);
	if (it != s.clients.end()) {
		return it->second.get();
	}
	unique_ptr<HostClient> client(new HostClient);
	if (!client->conn.open(m_options.hostBus, m_options.hostUid)) {
		LOG(s.name << ": cannot open host connection for " << containerClient);
		return nullptr;
	}
	if (m_options.verbose) {
		LOG(s.name << ": container " << containerClient << " <-> host " << client->conn.uniqueName());
	}
	HostClient *result = client.get();
	s.clients[containerClient] = std::move(client);
	return result;
}

void Bridge::dropClient(Service &s, const string &containerClient)
{
	if (s.clients.erase(containerClient) && m_options.verbose) {
		LOG(s.name << ": container client " << containerClient << " gone");
	}
	for (auto it = s.reversePending.begin(); it != s.reversePending.end();) {
		if (it->second.containerClient == containerClient) {
			it = s.reversePending.erase(it);
		}
		else {
			++it;
		}
	}
}

// 来自容器总线：容器客户端的调用、对回调的回复、总线自身的信号
void Bridge::handleContainer(Service &s, Message &m)
{
	if (m_options.verbose) {
		LOG(s.name << " [container] " << m.describe());
	}
	if (m.type == TYPE_METHOD_CALL) {
		string caller = m.sender;
		uint32_t callerSerial = m.serial;
		bool expectsReply = m.expectsReply();
		HostClient *client = hostClientFor(s, caller);
		if (!client) {
			if (expectsReply) {
				replyError(s.container, caller, callerSerial, "cannot connect to host system bus");
			}
			return;
		}
		m.serial = client->conn.nextSerial();
		m.destination = s.name;
		m.sender.clear();
		if (expectsReply) {
			client->pending[m.serial] = callerSerial;
		}
		if (m_options.verbose) {
			LOG("  => " << m.describe());
		}
		if (!client->conn.send(m)) {
			client->pending.erase(m.serial);
			if (expectsReply) {
				replyError(s.container, caller, callerSerial, "forwarding to host failed");
			}
		}
		return;
	}

	if (m.isReply()) {
		if (!m.hasReplySerial) {
			return;
		}
		auto internal = s.containerInternal.find(m.replySerial);
		if (internal != s.containerInternal.end()) {
			if (internal->second == "RequestName" && m.type == TYPE_METHOD_RETURN) {
				size_t pos = 0;
				uint32_t code = 0;
				if (m.readUint32(pos, code) && code == NAME_REPLY_IN_QUEUE) {
					LOG(s.name << ": name busy in container, queued (is the container's own service masked?)");
				}
			}
			else if (m.type == TYPE_ERROR) {
				LOG(s.name << ": " << internal->second << " failed: " << m.errorName);
			}
			s.containerInternal.erase(internal);
			return;
		}
		auto it = s.reversePending.find(m.replySerial);
		if (it == s.reversePending.end()) {
			return;
		}
		ReversePending pending = it->second;
		s.reversePending.erase(it);
		auto client = s.clients.find(pending.containerClient);
		if (m.sender != pending.containerClient || client == s.clients.end()) {
			return;
		}
		m.serial = client->second->conn.nextSerial();
		m.replySerial = pending.hostSerial;
		m.destination = pending.hostSender;
		m.sender.clear();
		forward(client->second->conn, m);
		return;
	}

	if (m.type == TYPE_SIGNAL && m.sender == DBUS_NAME) {
		string name, oldOwner, newOwner;
		size_t pos = 0;
		if (isNameOwnerChanged(m, name, oldOwner, newOwner)) {
			if (newOwner.empty() && s.clients.count(name)) {
				dropClient(s, name);
			}
		}
		else if (m.member == "NameAcquired" && m.readString(pos, name) && name == s.name) {
			s.owned = true;
			LOG(s.name << ": now forwarded into container");
		}
		else if (m.member == "NameLost" && m.readString(pos, name) && name == s.name) {
			s.owned = false;
			LOG(s.name << ": released in container");
		}
	}
}

// 来自宿主机监听连接：服务的广播信号、服务上下线
void Bridge::handleHostSignal(Service &s, Message &m)
{
	if (m.type == TYPE_METHOD_CALL) {
		if (m.expectsReply()) {
			replyError(s.hostSignal, m.sender, m.serial, "no object here");
		}
		return;
	}
	if (m.type != TYPE_SIGNAL) {
		return;
	}
	if (m.sender == DBUS_NAME) {
		string name, oldOwner, newOwner;
		if (isNameOwnerChanged(m, name, oldOwner, newOwner) && name == s.name) {
			if (newOwner.empty()) {
				LOG(s.name << ": stopped on host");
				releaseName(s);
			}
			else if (oldOwner.empty()) {
				LOG(s.name << ": started on host");
				requestName(s);
			}
		}
		return;
	}
	if (!s.owned) {
		return;
	}
	if (m_options.verbose) {
		LOG(s.name << " [host] " << m.describe());
	}
	// 以容器内服务名的身份重新广播
	m.serial = s.container.nextSerial();
	m.destination.clear();
	m.sender.clear();
	forward(s.container, m);
}

// 来自某个容器客户端对应的宿主机连接：调用的回复、服务端回调（Agent 等）
void Bridge::handleHostClient(Service &s, const string &containerClient, Message &m)
{
	auto it = s.clients.find(containerClient);
	if (it == s.clients.end()) {
		return;
	}
	HostClient &client = *it->second;
	if (m_options.verbose) {
		LOG(s.name << " [host:" << containerClient << "] " << m.describe());
	}

	if (m.isReply()) {
		if (!m.hasReplySerial) {
			return;
		}
		auto pending = client.pending.find(m.replySerial);
		if (pending == client.pending.end()) {
			return;
		}
		m.replySerial = pending->second;
		client.pending.erase(pending);
		m.serial = s.container.nextSerial();
		m.destination = containerClient;
		m.sender.clear();
		forward(s.container, m);
		return;
	}

	if (m.type == TYPE_METHOD_CALL) {
		string hostSender = m.sender;
		uint32_t hostSerial = m.serial;
		bool expectsReply = m.expectsReply();
		m.serial = s.container.nextSerial();
		m.destination = containerClient;
		m.sender.clear();
		if (expectsReply) {
			s.reversePending[m.serial] = {containerClient, hostSender, hostSerial};
		}
		if (m_options.verbose) {
			LOG("  => " << m.describe());
		}
		if (!s.container.send(m)) {
			s.reversePending.erase(m.serial);
			if (expectsReply) {
				replyError(client.conn, hostSender, hostSerial, "forwarding to container failed");
			}
		}
		return;
	}

	// 发给该连接的单播信号
	if (m.type == TYPE_SIGNAL && m.sender != DBUS_NAME && m.destination == client.conn.uniqueName()) {
		m.serial = s.container.nextSerial();
		m.destination = containerClient;
		m.sender.clear();
		forward(s.container, m);
	}
}

void Bridge::drainBacklogs()
{
	for (auto &service: m_services) {
		Service &s = *service;
		while (!s.container.backlog.empty()) {
			Message m = std::move(s.container.backlog.front());
			s.container.backlog.pop_front();
			handleContainer(s, m);
		}
		while (!s.hostSignal.backlog.empty()) {
			Message m = std::move(s.hostSignal.backlog.front());
			s.hostSignal.backlog.pop_front();
			handleHostSignal(s, m);
		}
		vector<string> names;
		for (auto &client: s.clients) {
			names.push_back(client.first);
		}
		for (const string &name: names) {
			while (true) {
				auto it = s.clients.find(name);
				if (it == s.clients.end() || it->second->conn.backlog.empty()) {
					break;
				}
				Message m = std::move(it->second->conn.backlog.front());
				it->second->conn.backlog.pop_front();
				handleHostClient(s, name, m);
			}
		}
	}
}

// 返回 false 表示需要退出
bool Bridge::dispatch(const PollTarget &target, Connection *conn)
{
	Service &s = *target.service;
	vector<Message> messages;
	bool ok = conn->receive(messages);
	for (Message &m: messages) {
		switch (target.kind) {
		case KIND_CONTAINER: handleContainer(s, m); break;
		case KIND_HOST_SIGNAL: handleHostSignal(s, m); break;
		case KIND_HOST_CLIENT: handleHostClient(s, target.client, m); break;
		}
	}
	if (ok) {
		return true;
	}
	switch (target.kind) {
	case KIND_CONTAINER:
		LOG("container bus closed, exiting");
		return false;
	case KIND_HOST_SIGNAL:
		LOG("host bus closed, exiting");
		return false;
	case KIND_HOST_CLIENT:
		LOG(s.name << ": host connection for " << target.client << " closed");
		dropClient(s, target.client);
		return true;
	}
	return true;
}

int Bridge::run(int readyFd)
{
	// 等待容器内 dbus 启动
	bool found = false;
	for (int i = 0; i < 60 && !(found = resolveContainerBus()); ++i) {
		sleep(1);
	}
	if (!found) {
		LOG("container bus not found for machine '" << m_options.machine << "'");
		return 1;
	}
	for (const string &name: m_options.services) {
		unique_ptr<Service> service(new Service);
		service->name = name;
		if (!setupService(*service)) {
			return 1;
		}
		m_services.push_back(std::move(service));
	}
	LOG("forwarding host uid " << m_options.hostUid << " <-> " << m_containerBus);
	if (readyFd >= 0) {
		char c = 1;
		if (write(readyFd, &c, 1) < 0) {
			// 父进程已不在等待，忽略
		}
		::close(readyFd);
	}

	while (true) {
		drainBacklogs();
		vector<pollfd> pfds;
		vector<PollTarget> targets;
		for (auto &service: m_services) {
			Service *s = service.get();
			pfds.push_back({s->container.fd(), POLLIN, 0});
			targets.push_back({s, KIND_CONTAINER, ""});
			pfds.push_back({s->hostSignal.fd(), POLLIN, 0});
			targets.push_back({s, KIND_HOST_SIGNAL, ""});
			for (auto &client: s->clients) {
				pfds.push_back({client.second->conn.fd(), POLLIN, 0});
				targets.push_back({s, KIND_HOST_CLIENT, client.first});
			}
		}
		if (poll(pfds.data(), pfds.size(), -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			LOG("poll failed: " << strerror(errno));
			return 1;
		}
		for (size_t i = 0; i < pfds.size(); ++i) {
			if (!pfds[i].revents) {
				continue;
			}
			const PollTarget &target = targets[i];
			Connection *conn = nullptr;
			if (target.kind == KIND_CONTAINER) {
				conn = &target.service->container;
			}
			else if (target.kind == KIND_HOST_SIGNAL) {
				conn = &target.service->hostSignal;
			}
			else {
				// 处理前面的消息时该客户端可能已被移除
				auto it = target.service->clients.find(target.client);
				if (it == target.service->clients.end()) {
					continue;
				}
				conn = &it->second->conn;
			}
			if (!dispatch(target, conn)) {
				return target.kind == KIND_CONTAINER ? 0 : 1;
			}
		}
	}
}

uid_t uidFromEnv()
{
	for (const char *name: {"SUDO_UID", "PKEXEC_UID"}) {
		const char *value = getenv(name);
		if (value && *value) {
			return static_cast<uid_t>(strtoul(value, nullptr, 10));
		}
	}
	return getuid();
}

}

Options defaultOptions(const string &machine)
{
	Options options;
	options.machine = machine;
	options.hostUid = uidFromEnv();
	options.services = {"org.freedesktop.NetworkManager", "org.bluez", "org.freedesktop.UPower"};
	return options;
}

bool parseArgs(int argc, char *argv[], Options &options)
{
	vector<string> services;
	for (int i = 0; i < argc; ++i) {
		string arg = argv[i];
		auto value = [&](string &out) {
			if (i + 1 >= argc) {
				return false;
			}
			out = argv[++i];
			return true;
		};
		string text;
		if (arg == "-v" || arg == "--verbose") {
			options.verbose = true;
		}
		else if (arg == "--machine") {
			if (!value(options.machine)) return false;
		}
		else if (arg == "--container-bus") {
			if (!value(options.containerBus)) return false;
		}
		else if (arg == "--host-bus") {
			if (!value(options.hostBus)) return false;
		}
		else if (arg == "--host-uid") {
			if (!value(text)) return false;
			options.hostUid = static_cast<uid_t>(strtoul(text.c_str(), nullptr, 10));
		}
		else if (!arg.empty() && arg[0] == '-') {
			return false;
		}
		else {
			services.push_back(arg);
		}
	}
	if (!services.empty()) {
		options.services = services;
	}
	return true;
}

int run(const Options &options, int readyFd)
{
	Bridge bridge(options);
	return bridge.run(readyFd);
}

pid_t spawn(const Options &options, int timeoutSeconds)
{
	int pipeFds[2];
	if (pipe2(pipeFds, O_CLOEXEC) != 0) {
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		return -1;
	}
	if (pid == 0) {
		::close(pipeFds[0]);
		_exit(run(options, pipeFds[1]));
	}
	::close(pipeFds[1]);
	pollfd pfd{pipeFds[0], POLLIN, 0};
	char c = 0;
	bool ready = poll(&pfd, 1, timeoutSeconds * 1000) > 0 && read(pipeFds[0], &c, 1) == 1;
	::close(pipeFds[0]);
	if (!ready) {
		LOG("bridge did not become ready");
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		return -1;
	}
	return pid;
}

}
