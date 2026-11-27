#include "dbuswire.h"
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

using namespace std;

namespace DBusWire {

static const bool NATIVE_LITTLE = (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
static const uint32_t MAX_MESSAGE_SIZE = 128 * 1024 * 1024;
static const size_t MAX_FDS_PER_READ = 253;

enum HeaderField : uint8_t {
	FIELD_PATH = 1,
	FIELD_INTERFACE = 2,
	FIELD_MEMBER = 3,
	FIELD_ERROR_NAME = 4,
	FIELD_REPLY_SERIAL = 5,
	FIELD_DESTINATION = 6,
	FIELD_SENDER = 7,
	FIELD_SIGNATURE = 8,
	FIELD_UNIX_FDS = 9,
};

/* ---------- FdList ---------- */

FdList::FdList(FdList &&other) noexcept : fds(std::move(other.fds))
{
	other.fds.clear();
}

FdList &FdList::operator=(FdList &&other) noexcept
{
	if (this != &other) {
		clear();
		fds = std::move(other.fds);
		other.fds.clear();
	}
	return *this;
}

FdList::~FdList()
{
	clear();
}

void FdList::clear()
{
	for (int fd: fds) {
		::close(fd);
	}
	fds.clear();
}

/* ---------- 编解码辅助 ---------- */

static size_t alignTo(size_t value, size_t alignment)
{
	return (value + alignment - 1) / alignment * alignment;
}

struct Writer {
	string &buf;
	bool swap;

	void pad(size_t alignment) {
		while (buf.size() % alignment) {
			buf.push_back('\0');
		}
	}
	void u8(uint8_t value) {
		buf.push_back(static_cast<char>(value));
	}
	void u32(uint32_t value) {
		pad(4);
		if (swap) {
			value = __builtin_bswap32(value);
		}
		buf.append(reinterpret_cast<const char *>(&value), 4);
	}
	void str(const string &value) {
		u32(value.size());
		buf.append(value);
		buf.push_back('\0');
	}
	void sig(const string &value) {
		u8(value.size());
		buf.append(value);
		buf.push_back('\0');
	}
};

struct Reader {
	const char *data;
	size_t len;
	bool swap;
	size_t pos;

	bool align(size_t alignment) {
		size_t next = alignTo(pos, alignment);
		if (next > len) {
			return false;
		}
		pos = next;
		return true;
	}
	bool u8(uint8_t &value) {
		if (pos + 1 > len) {
			return false;
		}
		value = static_cast<uint8_t>(data[pos++]);
		return true;
	}
	bool u32(uint32_t &value) {
		if (!align(4) || pos + 4 > len) {
			return false;
		}
		memcpy(&value, data + pos, 4);
		if (swap) {
			value = __builtin_bswap32(value);
		}
		pos += 4;
		return true;
	}
	bool str(string &value) {
		uint32_t size;
		if (!u32(size) || pos + size + 1 > len) {
			return false;
		}
		value.assign(data + pos, size);
		pos += size + 1;
		return true;
	}
	bool sig(string &value) {
		uint8_t size;
		if (!u8(size) || pos + size + 1 > len) {
			return false;
		}
		value.assign(data + pos, size);
		pos += size + 1;
		return true;
	}
};

/* ---------- Message ---------- */

string Message::describe() const
{
	static const char *names[] = {"invalid", "call", "return", "error", "signal"};
	string text = type <= TYPE_SIGNAL ? names[type] : "unknown";
	text += " " + (sender.empty() ? string("-") : sender) + " -> " +
		(destination.empty() ? string("*") : destination);
	if (!path.empty()) {
		text += " " + path;
	}
	if (!member.empty()) {
		text += " " + interface + "." + member;
	}
	if (!errorName.empty()) {
		text += " " + errorName;
	}
	text += " serial=" + to_string(serial);
	if (hasReplySerial) {
		text += " reply=" + to_string(replySerial);
	}
	if (!fds.fds.empty()) {
		text += " fds=" + to_string(fds.fds.size());
	}
	return text;
}

void Message::appendString(const string &value)
{
	Writer w{body, littleEndian != NATIVE_LITTLE};
	w.str(value);
	signature += 's';
}

void Message::appendUint32(uint32_t value)
{
	Writer w{body, littleEndian != NATIVE_LITTLE};
	w.u32(value);
	signature += 'u';
}

bool Message::readString(size_t &pos, string &out) const
{
	Reader r{body.data(), body.size(), littleEndian != NATIVE_LITTLE, pos};
	if (!r.str(out)) {
		return false;
	}
	pos = r.pos;
	return true;
}

bool Message::readUint32(size_t &pos, uint32_t &out) const
{
	Reader r{body.data(), body.size(), littleEndian != NATIVE_LITTLE, pos};
	if (!r.u32(out)) {
		return false;
	}
	pos = r.pos;
	return true;
}

Message Message::methodCall(const string &destination, const string &path,
	const string &interface, const string &member)
{
	Message m;
	m.type = TYPE_METHOD_CALL;
	m.destination = destination;
	m.path = path;
	m.interface = interface;
	m.member = member;
	return m;
}

Message Message::error(const string &destination, uint32_t replySerial,
	const string &name, const string &text)
{
	Message m;
	m.type = TYPE_ERROR;
	m.flags = FLAG_NO_REPLY_EXPECTED;
	m.destination = destination;
	m.hasReplySerial = true;
	m.replySerial = replySerial;
	m.errorName = name;
	m.appendString(text);
	return m;
}

string serialize(const Message &m)
{
	string buf;
	Writer w{buf, m.littleEndian != NATIVE_LITTLE};
	w.u8(m.littleEndian ? 'l' : 'B');
	w.u8(m.type);
	w.u8(m.flags);
	w.u8(1); // 协议版本
	w.u32(m.body.size());
	w.u32(m.serial);
	w.u32(0); // 头字段数组长度，稍后回填
	size_t fieldsStart = buf.size();

	auto stringField = [&](uint8_t code, const char *type, const string &value) {
		if (value.empty()) {
			return;
		}
		w.pad(8);
		w.u8(code);
		w.sig(type);
		if (type[0] == 'g') {
			w.sig(value);
		}
		else {
			w.str(value);
		}
	};
	auto uintField = [&](uint8_t code, uint32_t value) {
		w.pad(8);
		w.u8(code);
		w.sig("u");
		w.u32(value);
	};

	stringField(FIELD_PATH, "o", m.path);
	stringField(FIELD_INTERFACE, "s", m.interface);
	stringField(FIELD_MEMBER, "s", m.member);
	stringField(FIELD_ERROR_NAME, "s", m.errorName);
	if (m.hasReplySerial) {
		uintField(FIELD_REPLY_SERIAL, m.replySerial);
	}
	stringField(FIELD_DESTINATION, "s", m.destination);
	stringField(FIELD_SENDER, "s", m.sender);
	stringField(FIELD_SIGNATURE, "g", m.signature);
	if (!m.fds.fds.empty()) {
		uintField(FIELD_UNIX_FDS, m.fds.fds.size());
	}

	uint32_t fieldsLen = buf.size() - fieldsStart;
	if (m.littleEndian != NATIVE_LITTLE) {
		fieldsLen = __builtin_bswap32(fieldsLen);
	}
	memcpy(&buf[12], &fieldsLen, 4);
	w.pad(8);
	buf += m.body;
	return buf;
}

ssize_t parse(const char *data, size_t len, Message &m, uint32_t &unixFds)
{
	if (len < 16) {
		return 0;
	}
	if ((data[0] != 'l' && data[0] != 'B') || data[3] != 1) {
		return -1;
	}
	bool little = data[0] == 'l';
	bool swap = little != NATIVE_LITTLE;
	Reader fixed{data, len, swap, 4};
	uint32_t bodyLen, serial, fieldsLen;
	fixed.u32(bodyLen);
	fixed.u32(serial);
	fixed.u32(fieldsLen);
	if (bodyLen > MAX_MESSAGE_SIZE || fieldsLen > MAX_MESSAGE_SIZE) {
		return -1;
	}
	size_t fieldsEnd = 16 + fieldsLen;
	size_t headerEnd = alignTo(fieldsEnd, 8);
	size_t total = headerEnd + bodyLen;
	if (total > MAX_MESSAGE_SIZE) {
		return -1;
	}
	if (len < total) {
		return 0;
	}

	m = Message();
	m.littleEndian = little;
	m.type = static_cast<uint8_t>(data[1]);
	m.flags = static_cast<uint8_t>(data[2]);
	m.serial = serial;
	unixFds = 0;

	Reader r{data, fieldsEnd, swap, 16};
	while (r.pos < fieldsEnd) {
		uint8_t code;
		string type;
		if (!r.align(8) || r.pos >= fieldsEnd) {
			break;
		}
		if (!r.u8(code) || !r.sig(type) || type.size() != 1) {
			return -1;
		}
		string text;
		uint32_t number = 0;
		switch (type[0]) {
		case 's':
		case 'o':
			if (!r.str(text)) {
				return -1;
			}
			break;
		case 'g':
			if (!r.sig(text)) {
				return -1;
			}
			break;
		case 'u':
			if (!r.u32(number)) {
				return -1;
			}
			break;
		default:
			// 规范内的头字段只会使用以上类型
			return -1;
		}
		switch (code) {
		case FIELD_PATH: m.path = text; break;
		case FIELD_INTERFACE: m.interface = text; break;
		case FIELD_MEMBER: m.member = text; break;
		case FIELD_ERROR_NAME: m.errorName = text; break;
		case FIELD_REPLY_SERIAL: m.hasReplySerial = true; m.replySerial = number; break;
		case FIELD_DESTINATION: m.destination = text; break;
		case FIELD_SENDER: m.sender = text; break;
		case FIELD_SIGNATURE: m.signature = text; break;
		case FIELD_UNIX_FDS: unixFds = number; break;
		default: break; // 忽略未知字段
		}
	}
	m.body.assign(data + headerEnd, bodyLen);
	return total;
}

/* ---------- Connection ---------- */

Connection::~Connection()
{
	close();
}

void Connection::close()
{
	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}
	for (int fd: m_infds) {
		::close(fd);
	}
	m_infds.clear();
	m_inbuf.clear();
	backlog.clear();
}

uint32_t Connection::nextSerial()
{
	if (++m_serial == 0) {
		m_serial = 1;
	}
	return m_serial;
}

bool Connection::open(const string &socketPath, uid_t connectUid)
{
	close();
	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	if (socketPath.size() >= sizeof(addr.sun_path)) {
		return false;
	}
	strcpy(addr.sun_path, socketPath.c_str());
	m_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (m_fd < 0) {
		return false;
	}
	// 总线通过 SO_PEERCRED 识别身份，记录的是 connect() 时的有效 uid
	uid_t oldUid = geteuid();
	bool switched = false;
	if (connectUid != oldUid) {
		if (seteuid(connectUid) != 0) {
			close();
			return false;
		}
		switched = true;
	}
	int ret = connect(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
	if (switched && seteuid(oldUid) != 0) {
		// 无法恢复 root 身份，继续运行不安全
		_exit(1);
	}
	if (ret != 0 || !authenticate(connectUid)) {
		close();
		return false;
	}
	Message hello = Message::methodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "Hello");
	Message reply;
	size_t pos = 0;
	if (!call(hello, reply) || !reply.readString(pos, m_uniqueName)) {
		close();
		return false;
	}
	return true;
}

bool Connection::writeAll(const string &data)
{
	size_t offset = 0;
	while (offset < data.size()) {
		ssize_t n = ::send(m_fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		offset += n;
	}
	return true;
}

bool Connection::readLine(string &line)
{
	line.clear();
	while (line.size() < 4096) {
		char c;
		ssize_t n = ::recv(m_fd, &c, 1, 0);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			return false;
		}
		line.push_back(c);
		if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0) {
			line.resize(line.size() - 2);
			return true;
		}
	}
	return false;
}

bool Connection::authenticate(uid_t uid)
{
	string uidText = to_string(uid);
	string hex;
	static const char *digits = "0123456789abcdef";
	for (unsigned char c: uidText) {
		hex.push_back(digits[c >> 4]);
		hex.push_back(digits[c & 0xf]);
	}
	string line;
	if (!writeAll(string(1, '\0') + "AUTH EXTERNAL " + hex + "\r\n") ||
		!readLine(line) || line.compare(0, 3, "OK ") != 0) {
		return false;
	}
	if (!writeAll("NEGOTIATE_UNIX_FD\r\n") || !readLine(line)) {
		return false;
	}
	m_unixFds = line == "AGREE_UNIX_FD";
	return writeAll("BEGIN\r\n");
}

bool Connection::send(const Message &message)
{
	if (m_fd < 0 || (!message.fds.fds.empty() && !m_unixFds)) {
		return false;
	}
	string data = serialize(message);
	size_t offset = 0;
	bool fdsSent = message.fds.fds.empty();
	vector<char> control;
	while (offset < data.size()) {
		iovec iov{&data[offset], data.size() - offset};
		msghdr header{};
		header.msg_iov = &iov;
		header.msg_iovlen = 1;
		if (!fdsSent) {
			// fd 必须随消息的第一个字节一起发送
			size_t size = sizeof(int) * message.fds.fds.size();
			control.assign(CMSG_SPACE(size), 0);
			header.msg_control = control.data();
			header.msg_controllen = control.size();
			cmsghdr *cmsg = CMSG_FIRSTHDR(&header);
			cmsg->cmsg_level = SOL_SOCKET;
			cmsg->cmsg_type = SCM_RIGHTS;
			cmsg->cmsg_len = CMSG_LEN(size);
			memcpy(CMSG_DATA(cmsg), message.fds.fds.data(), size);
		}
		ssize_t n = sendmsg(m_fd, &header, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		fdsSent = true;
		offset += n;
	}
	return true;
}

bool Connection::receive(vector<Message> &out)
{
	if (m_fd < 0) {
		return false;
	}
	char buffer[65536];
	alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * MAX_FDS_PER_READ)];
	iovec iov{buffer, sizeof(buffer)};
	msghdr header{};
	header.msg_iov = &iov;
	header.msg_iovlen = 1;
	header.msg_control = control;
	header.msg_controllen = sizeof(control);
	ssize_t n = recvmsg(m_fd, &header, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
	if (n < 0) {
		return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
	}
	for (cmsghdr *cmsg = CMSG_FIRSTHDR(&header); cmsg; cmsg = CMSG_NXTHDR(&header, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
			size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
			int *fds = reinterpret_cast<int *>(CMSG_DATA(cmsg));
			for (size_t i = 0; i < count; ++i) {
				m_infds.push_back(fds[i]);
			}
		}
	}
	if (n == 0 || (header.msg_flags & MSG_CTRUNC)) {
		// 对端关闭，或者 fd 被截断导致后续消息无法对应
		return false;
	}
	m_inbuf.append(buffer, n);

	size_t offset = 0;
	while (true) {
		Message message;
		uint32_t unixFds = 0;
		ssize_t used = parse(m_inbuf.data() + offset, m_inbuf.size() - offset, message, unixFds);
		if (used < 0 || unixFds > m_infds.size()) {
			return false;
		}
		if (used == 0) {
			break;
		}
		for (uint32_t i = 0; i < unixFds; ++i) {
			message.fds.fds.push_back(m_infds.front());
			m_infds.pop_front();
		}
		offset += used;
		out.push_back(std::move(message));
	}
	m_inbuf.erase(0, offset);
	return true;
}

static long long nowMs()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

bool Connection::call(Message &message, Message &reply, int timeoutMs)
{
	message.serial = nextSerial();
	uint32_t serial = message.serial;
	if (!send(message)) {
		return false;
	}
	long long deadline = nowMs() + timeoutMs;
	while (true) {
		long long remaining = deadline - nowMs();
		if (remaining <= 0) {
			return false;
		}
		pollfd pfd{m_fd, POLLIN, 0};
		int ret = poll(&pfd, 1, static_cast<int>(remaining));
		if (ret < 0 && errno == EINTR) {
			continue;
		}
		if (ret <= 0) {
			return false;
		}
		vector<Message> messages;
		bool ok = receive(messages);
		bool found = false;
		for (Message &m: messages) {
			if (!found && m.isReply() && m.hasReplySerial && m.replySerial == serial) {
				reply = std::move(m);
				found = true;
			}
			else {
				backlog.push_back(std::move(m));
			}
		}
		if (found) {
			return reply.type == TYPE_METHOD_RETURN;
		}
		if (!ok) {
			return false;
		}
	}
}

}
