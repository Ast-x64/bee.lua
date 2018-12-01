#pragma once

#if defined _WIN32
#	include <winsock2.h>
#	include <mswsock.h>
#else
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <unistd.h>
#endif

#define net_assert(x) do { assert(x); (x); } while (0)
#if defined _WIN32
#	define net_assert_success(x) net_assert((x) != SOCKET_ERROR)
#else
#	define net_assert_success(x) net_assert((x) == 0)
#endif

#include <net/endpoint.h>

namespace bee::net::socket {
#if defined _WIN32
	typedef SOCKET fd_t;
	enum { retired_fd = (fd_t)INVALID_SOCKET };
#else
	typedef int fd_t;
	enum { retired_fd = -1 };
#endif

	void initialize();
	fd_t open(int family, int protocol);
	bool close(fd_t s);
	void shutdown(fd_t s);
	void shutdown_read(fd_t s);
	void shutdown_write(fd_t s);
	void nonblocking(fd_t s);
	void keepalive(fd_t s, int keepalive, int keepalive_cnt, int keepalive_idle, int keepalive_intvl);
	void udp_connect_reset(fd_t s);
	void send_buffer(fd_t s, int bufsize);
	void recv_buffer(fd_t s, int bufsize);
	void reuse(fd_t s);
	int  connect(fd_t s, const endpoint& ep);
	int  bind(fd_t s, const endpoint& ep);
	int  listen(fd_t s, int backlog);
	int  accept(fd_t s, fd_t& sock);
	int  recv(fd_t s, char* buf, int len);
	int  send(fd_t s, const char* buf, int len);
	int  recvfrom(fd_t s, char* buf, int len, endpoint& ep);
	int  sendto(fd_t s, const char* buf, int len, const endpoint& ep);
	bool getpeername(fd_t s, endpoint& ep);
	bool getsockname(fd_t s, endpoint& ep);
	int  errcode();
	int  errcode(fd_t fd);
	std::string errmessage(int errcode);
	std::string errmessage();
}
