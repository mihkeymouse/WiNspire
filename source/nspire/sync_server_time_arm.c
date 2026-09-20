#include <stddef.h>
#include <stdint.h>

#define SYS_EXIT 1
#define SYS_CLOSE 6
#define SYS_SETTIMEOFDAY 79
#define SYS_SOCKET 281
#define SYS_CONNECT 283
#define SYS_SEND 289
#define SYS_RECV 291

#define AF_INET 2
#define SOCK_STREAM 1
#define WRBP_VERSION 1u
#define WRBP_OP_TIME 8u
#define SERVER_ADDRESS_BE 0x0107a8c0u
#define SERVER_PORT_BE 0x140fu

typedef struct WrbpHeader {
	uint8_t magic[4];
	uint16_t version;
	uint16_t op;
	uint64_t lba;
	uint32_t count;
	uint32_t bytes;
	uint32_t status;
	uint32_t flags;
} WrbpHeader;

typedef char header_size_check[sizeof(WrbpHeader) == 32 ? 1 : -1];

struct sockaddr_in32 {
	uint16_t family;
	uint16_t port;
	uint32_t address;
	uint8_t zero[8];
};

struct timeval32 {
	int32_t seconds;
	int32_t microseconds;
};

static long syscall1(long number, long arg0)
{
	register long r0 __asm__("r0") = arg0;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory", "cc");
	return r0;
}

static long syscall2(long number, long arg0, long arg1)
{
	register long r0 __asm__("r0") = arg0;
	register long r1 __asm__("r1") = arg1;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7)
			 : "memory", "cc");
	return r0;
}

static long syscall3(long number, long arg0, long arg1, long arg2)
{
	register long r0 __asm__("r0") = arg0;
	register long r1 __asm__("r1") = arg1;
	register long r2 __asm__("r2") = arg2;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7)
			 : "memory", "cc");
	return r0;
}

static long syscall4(long number, long arg0, long arg1, long arg2, long arg3)
{
	register long r0 __asm__("r0") = arg0;
	register long r1 __asm__("r1") = arg1;
	register long r2 __asm__("r2") = arg2;
	register long r3 __asm__("r3") = arg3;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3),
			 "r"(r7) : "memory", "cc");
	return r0;
}

static int transfer_all(int fd, void *buffer, size_t size, int sending)
{
	uint8_t *position = buffer;

	/* TCP may split the 32-byte WRBP header across several transfers. */
	while (size) {
		long done = syscall4(sending ? SYS_SEND : SYS_RECV, fd,
			(long)position, (long)size, 0);
		if (done <= 0)
			return -1;
		position += done;
		size -= (size_t)done;
	}
	return 0;
}

__attribute__((noreturn))
static void exit_now(int fd, int status)
{
	if (fd >= 0)
		(void)syscall1(SYS_CLOSE, fd);
	(void)syscall1(SYS_EXIT, status);
	for (;;)
		;
}

void _start(void)
{
	/* These constants encode 192.168.7.1:3860 for little-endian ARM. */
	const struct sockaddr_in32 server = {
		AF_INET, SERVER_PORT_BE, SERVER_ADDRESS_BE, {0}
	};
	WrbpHeader request = {{'W', 'R', 'B', 'P'}, WRBP_VERSION,
		WRBP_OP_TIME, 0, 0, 0, 0, 0};
	WrbpHeader response;
	struct timeval32 wall_time;
	int fd;

	fd = (int)syscall3(SYS_SOCKET, AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		exit_now(-1, 1);
	if (syscall3(SYS_CONNECT, fd, (long)&server, sizeof(server)) < 0)
		exit_now(fd, 1);
	if (transfer_all(fd, &request, sizeof(request), 1) < 0 ||
	    transfer_all(fd, &response, sizeof(response), 0) < 0)
		exit_now(fd, 1);
	(void)syscall1(SYS_CLOSE, fd);

	/* The TIME reply stores Unix seconds in the header's 64-bit LBA field. */
	if (response.magic[0] != 'W' || response.magic[1] != 'R' ||
	    response.magic[2] != 'B' || response.magic[3] != 'P' ||
	    response.version != WRBP_VERSION || response.op != WRBP_OP_TIME ||
	    response.status != 0 || response.lba < 946684800ULL ||
	    response.lba > 2147483647ULL)
		exit_now(-1, 1);
	wall_time.seconds = (int32_t)response.lba;
	wall_time.microseconds = 0;
	if (syscall2(SYS_SETTIMEOFDAY, (long)&wall_time, 0) < 0)
		exit_now(-1, 1);
	exit_now(-1, 0);
}
