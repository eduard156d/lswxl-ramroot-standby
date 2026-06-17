#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static time_t monotime(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return time(NULL);
	return ts.tv_sec;
}

static int parse_mac(const char *text, uint8_t mac[6])
{
	unsigned int b[6];
	if (sscanf(text, "%x:%x:%x:%x:%x:%x",
	    &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
		return -1;
	for (int i = 0; i < 6; i++) {
		if (b[i] > 0xff)
			return -1;
		mac[i] = (uint8_t)b[i];
	}
	return 0;
}

static int is_magic_packet(const uint8_t *buf, ssize_t len, const uint8_t mac[6])
{
	if (len < 102)
		return 0;
	for (ssize_t off = 0; off <= len - 102; off++) {
		int ok = 1;
		for (int i = 0; i < 6; i++) {
			if (buf[off + i] != 0xff) {
				ok = 0;
				break;
			}
		}
		if (!ok)
			continue;
		for (int rep = 0; rep < 16; rep++) {
			if (memcmp(buf + off + 6 + rep * 6, mac, 6) != 0) {
				ok = 0;
				break;
			}
		}
		if (ok)
			return 1;
	}
	return 0;
}

static int bind_udp_port(int port)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	int yes = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void print_input_event(const struct input_event *ev)
{
	printf("input event sec=%ld usec=%ld type=%u code=%u value=%d\n",
	    (long)ev->time.tv_sec, (long)ev->time.tv_usec,
	    ev->type, ev->code, ev->value);
	fflush(stdout);
}

static int open_input(const char *path)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror(path);
		return -1;
	}
	return fd;
}

static int monitor_input(int timeout, const char *input_path)
{
	int input_fd = open_input(input_path);
	if (input_fd < 0)
		return 1;

	time_t deadline = monotime() + timeout;
	for (;;) {
		time_t now = monotime();
		if (timeout >= 0 && now >= deadline)
			return 2;

		fd_set set;
		FD_ZERO(&set);
		FD_SET(input_fd, &set);

		struct timeval tv;
		struct timeval *tvp = NULL;
		if (timeout >= 0) {
			tv.tv_sec = deadline - now;
			tv.tv_usec = 0;
			tvp = &tv;
		}

		int rc = select(input_fd + 1, &set, NULL, NULL, tvp);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			return 1;
		}
		if (rc == 0)
			return 2;

		struct input_event ev;
		ssize_t len;
		while ((len = read(input_fd, &ev, sizeof(ev))) == sizeof(ev))
			print_input_event(&ev);
	}
}

static int wait_wake_or_input(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: %s wait <mac> <timeout-sec> <input-event> [port ...]\n", argv[0]);
		return 1;
	}

	uint8_t mac[6];
	if (parse_mac(argv[2], mac) != 0) {
		fprintf(stderr, "invalid mac: %s\n", argv[2]);
		return 1;
	}

	int timeout = atoi(argv[3]);
	int input_fd = open_input(argv[4]);
	if (input_fd < 0)
		return 1;

	int fds[16];
	int nfds = 0;
	if (argc > 5) {
		for (int i = 5; i < argc && nfds < 16; i++) {
			int fd = bind_udp_port(atoi(argv[i]));
			if (fd >= 0)
				fds[nfds++] = fd;
		}
	} else {
		int fd9 = bind_udp_port(9);
		if (fd9 >= 0)
			fds[nfds++] = fd9;
		int fd2304 = bind_udp_port(2304);
		if (fd2304 >= 0)
			fds[nfds++] = fd2304;
	}
	if (nfds == 0) {
		perror("bind");
		return 1;
	}

	time_t deadline = timeout > 0 ? monotime() + timeout : 0;
	for (;;) {
		time_t now = monotime();
		if (timeout > 0 && now >= deadline)
			return 2;

		fd_set set;
		FD_ZERO(&set);
		FD_SET(input_fd, &set);
		int maxfd = input_fd;
		for (int i = 0; i < nfds; i++) {
			FD_SET(fds[i], &set);
			if (fds[i] > maxfd)
				maxfd = fds[i];
		}

		struct timeval tv;
		struct timeval *tvp = NULL;
		if (timeout > 0) {
			tv.tv_sec = deadline - now;
			tv.tv_usec = 0;
			tvp = &tv;
		}

		int rc = select(maxfd + 1, &set, NULL, NULL, tvp);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			return 1;
		}
		if (rc == 0)
			return 2;

		if (FD_ISSET(input_fd, &set)) {
			struct input_event ev;
			ssize_t len;
			int saw = 0;
			while ((len = read(input_fd, &ev, sizeof(ev))) == sizeof(ev)) {
				print_input_event(&ev);
				if (ev.type == EV_SW || ev.type == EV_KEY)
					saw = 1;
			}
			if (saw)
				return 3;
		}

		for (int i = 0; i < nfds; i++) {
			if (!FD_ISSET(fds[i], &set))
				continue;
			uint8_t buf[2048];
			ssize_t len = recv(fds[i], buf, sizeof(buf), 0);
			if (len > 0 && is_magic_packet(buf, len, mac))
				return 0;
		}
	}
}

int main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "monitor") == 0) {
		if (argc != 4) {
			fprintf(stderr, "usage: %s monitor <timeout-sec> <input-event>\n", argv[0]);
			return 1;
		}
		return monitor_input(atoi(argv[2]), argv[3]);
	}
	if (argc >= 2 && strcmp(argv[1], "wait") == 0)
		return wait_wake_or_input(argc, argv);

	fprintf(stderr, "usage:\n");
	fprintf(stderr, "  %s monitor <timeout-sec> <input-event>\n", argv[0]);
	fprintf(stderr, "  %s wait <mac> <timeout-sec> <input-event> [port ...]\n", argv[0]);
	return 1;
}
