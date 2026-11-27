#ifndef DBUSWIRE_H
#define DBUSWIRE_H

// 极简 D-Bus 线协议实现，仅供 dbusbridge 转发消息使用
// 只解析/重写消息头，消息体按原始字节透传，因此不需要完整的类型系统
// 不依赖 libdbus / sd-bus，以保持静态编译

#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <sys/types.h>

namespace DBusWire {

enum MessageType : uint8_t {
	TYPE_INVALID = 0,
	TYPE_METHOD_CALL = 1,
	TYPE_METHOD_RETURN = 2,
	TYPE_ERROR = 3,
	TYPE_SIGNAL = 4,
};

enum MessageFlag : uint8_t {
	FLAG_NO_REPLY_EXPECTED = 0x1,
	FLAG_NO_AUTO_START = 0x2,
	FLAG_ALLOW_INTERACTIVE_AUTHORIZATION = 0x4,
};

// 持有一组 unix fd，析构时关闭
class FdList {
public:
	FdList() = default;
	FdList(const FdList &) = delete;
	FdList &operator=(const FdList &) = delete;
	FdList(FdList &&other) noexcept;
	FdList &operator=(FdList &&other) noexcept;
	~FdList();
	void clear();
	std::vector<int> fds;
};

class Message {
public:
	bool littleEndian = (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
	uint8_t type = TYPE_INVALID;
	uint8_t flags = 0;
	uint32_t serial = 0;
	std::string path;
	std::string interface;
	std::string member;
	std::string errorName;
	std::string destination;
	std::string sender;
	std::string signature;
	bool hasReplySerial = false;
	uint32_t replySerial = 0;
	std::string body; // 原始消息体字节
	FdList fds;

	bool isReply() const { return type == TYPE_METHOD_RETURN || type == TYPE_ERROR; }
	bool expectsReply() const { return type == TYPE_METHOD_CALL && !(flags & FLAG_NO_REPLY_EXPECTED); }
	std::string describe() const;

	// 仅用于自己构造的简单消息（本机字节序）
	void appendString(const std::string &value);
	void appendUint32(uint32_t value);
	// 读取消息体参数，pos 为消息体内偏移
	bool readString(size_t &pos, std::string &out) const;
	bool readUint32(size_t &pos, uint32_t &out) const;

	static Message methodCall(const std::string &destination, const std::string &path,
		const std::string &interface, const std::string &member);
	static Message error(const std::string &destination, uint32_t replySerial,
		const std::string &name, const std::string &text);
};

// 序列化消息（不含 fd）
std::string serialize(const Message &message);
// 解析一条消息：返回消耗的字节数，数据不完整返回 0，格式错误返回 -1
ssize_t parse(const char *data, size_t len, Message &out, uint32_t &unixFds);

class Connection {
public:
	Connection() = default;
	Connection(const Connection &) = delete;
	Connection &operator=(const Connection &) = delete;
	~Connection();

	// 连接到 unix socket，完成 EXTERNAL 认证与 Hello
	// connectUid 为连接时使用的有效 uid（总线据此识别身份），需要 root 才能切换
	bool open(const std::string &socketPath, uid_t connectUid);
	void close();
	int fd() const { return m_fd; }
	const std::string &uniqueName() const { return m_uniqueName; }
	uint32_t nextSerial();
	// 发送消息，serial 必须已设置
	bool send(const Message &message);
	// 读取当前可读的数据并解析出完整消息，连接断开或出错时返回 false
	bool receive(std::vector<Message> &out);
	// 同步调用，期间收到的无关消息放入 backlog
	bool call(Message &message, Message &reply, int timeoutMs = 25000);

	std::deque<Message> backlog;

private:
	bool authenticate(uid_t uid);
	bool writeAll(const std::string &data);
	bool readLine(std::string &line);

	int m_fd = -1;
	uint32_t m_serial = 0;
	bool m_unixFds = false;
	std::string m_uniqueName;
	std::string m_inbuf;
	std::deque<int> m_infds;
};

}

#endif
