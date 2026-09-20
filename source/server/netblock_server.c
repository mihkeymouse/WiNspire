
 //Windows WRBP server. Responsible for handling disk streaming from PC to calculator.
 //32 byte little-endian wire format matches the ide.c backend
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SECTOR_SIZE 512u
#define NETBLK_VERSION 1u
#define NETBLK_OP_HELLO 1u
#define NETBLK_OP_READ 2u
#define NETBLK_OP_WRITE 3u
#define NETBLK_OP_FLUSH 4u
#define NETBLK_OP_PING 5u
#define NETBLK_OP_TIME 8u
#define NETBLK_STATUS_OK 0u
#define NETBLK_STATUS_BAD_VERSION 2u
#define NETBLK_STATUS_RANGE 3u
#define NETBLK_STATUS_WRITE 4u
#define NETBLK_STATUS_BAD_OP 5u
#define NETBLK_DEFAULT_PORT 3860u
#define NETBLK_DEFAULT_MAX_SECTORS 1024u
#define NETBLK_PROTOCOL_MAX_SECTORS 1024u
#define NETBLK_PROTOCOL_MAX_PAYLOAD (NETBLK_PROTOCOL_MAX_SECTORS * SECTOR_SIZE)
#define NETBLK_IO_CHUNK_BYTES (64 * 1024)

typedef struct NetBlockHeader {
    unsigned char magic[4];
    unsigned short version;
    unsigned short op;
    unsigned long long lba;
    unsigned int count;
    unsigned int bytes;
    unsigned int status;
    unsigned int flags;
} NetBlockHeader;

typedef struct ServerConfig {
    const char *image_path;
    const char *listen_addr;
    unsigned short port;
    unsigned int max_sectors;
    unsigned int progress_every;
    unsigned int socket_buffer_kb;
    unsigned int recv_timeout_ms;
    unsigned int send_timeout_ms;
    unsigned int keepalive_time_ms;
    unsigned int keepalive_interval_ms;
    int read_only;
    int strict_flush;
    int no_keepalive;
} ServerConfig;

typedef struct ServerState {
    HANDLE image;
    HANDLE mapping;
    unsigned char *image_view;
    unsigned long long image_bytes;
    unsigned long long sector_count;
    ServerConfig cfg;
} ServerState;

static unsigned long long now_ms(void)
{
    return GetTickCount64();
}

static unsigned long long g_log_start_ms;

static void log_prefix(FILE *stream)
{
    SYSTEMTIME st;
    unsigned long long now = now_ms();

    if (!g_log_start_ms)
        g_log_start_ms = now;
    GetLocalTime(&st);
    fprintf(stream, "[%02u:%02u:%02u.%03u +%8.3fs] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            (double)(now - g_log_start_ms) / 1000.0);
}

static void log_printf(const char *fmt, ...)
{
    va_list ap;

    log_prefix(stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void log_errorf(const char *fmt, ...)
{
    va_list ap;

    log_prefix(stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void log_socket_close(const char *where, int rc);

static void usage(const char *argv0)
{
    fprintf(stderr,
            "WiNspire disk server\n"
            "Usage: %s --image PATH [--readonly] [--strict-flush]\n",
            argv0);
}

static int parse_u32(const char *s, unsigned int min, unsigned int max,
                     unsigned int *out)
{
    char *end = NULL;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno || end == s || *end || v < min || v > max) {
        fprintf(stderr, "Invalid number '%s': expected %u..%u.\n", s, min, max);
        return -1;
    }
    *out = (unsigned int)v;
    return 0;
}

static int parse_args(int argc, char **argv, ServerConfig *cfg)
{
    int i;

    memset(cfg, 0, sizeof(*cfg));
    cfg->listen_addr = "192.168.7.1";
    cfg->port = NETBLK_DEFAULT_PORT;
    cfg->max_sectors = NETBLK_DEFAULT_MAX_SECTORS;
    cfg->progress_every = 256;
    cfg->socket_buffer_kb = 1024;
    cfg->keepalive_time_ms = 10000;
    cfg->keepalive_interval_ms = 2000;
    cfg->no_keepalive = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            cfg->image_path = argv[++i];
        } else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            cfg->listen_addr = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            unsigned int v;
            if (parse_u32(argv[++i], 1, 65535, &v) < 0)
                return -1;
            cfg->port = (unsigned short)v;
        } else if (strcmp(argv[i], "--max-sectors") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 1, 1024, &cfg->max_sectors) < 0)
                return -1;
        } else if (strcmp(argv[i], "--progress-every") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 1, 1000000, &cfg->progress_every) < 0)
                return -1;
        } else if (strcmp(argv[i], "--socket-buffer-kb") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 16, 8192, &cfg->socket_buffer_kb) < 0)
                return -1;
        } else if (strcmp(argv[i], "--recv-timeout-ms") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 0, 3600000, &cfg->recv_timeout_ms) < 0)
                return -1;
        } else if (strcmp(argv[i], "--send-timeout-ms") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 0, 3600000, &cfg->send_timeout_ms) < 0)
                return -1;
        } else if (strcmp(argv[i], "--keepalive-time-ms") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 1000, 3600000, &cfg->keepalive_time_ms) < 0)
                return -1;
        } else if (strcmp(argv[i], "--keepalive-interval-ms") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], 500, 3600000, &cfg->keepalive_interval_ms) < 0)
                return -1;
        } else if (strcmp(argv[i], "--readonly") == 0 ||
                   strcmp(argv[i], "--read-only") == 0) {
            cfg->read_only = 1;
        } else if (strcmp(argv[i], "--strict-flush") == 0) {
            cfg->strict_flush = 1;
        } else if (strcmp(argv[i], "--keepalive") == 0) {
            cfg->no_keepalive = 0;
        } else if (strcmp(argv[i], "--no-keepalive") == 0) {
            cfg->no_keepalive = 1;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            return 1;
        } else if (!cfg->image_path && argv[i][0] != '-') {
            cfg->image_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option, extra argument, or missing value: '%s'. Use --help.\n", argv[i]);
            return -1;
        }
    }

    if (!cfg->image_path || !*cfg->image_path) {
        fprintf(stderr, "A raw disk image is required. Use --image PATH.\n");
        return -1;
    }
    return 0;
}

static int send_all(SOCKET s, const void *buf, size_t len)
{
    const char *p = (const char *)buf;

    while (len) {
        int chunk = len > NETBLK_IO_CHUNK_BYTES ? NETBLK_IO_CHUNK_BYTES : (int)len;
        int n = send(s, p, chunk, 0);
        if (n <= 0) {
            log_socket_close("send", n);
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void log_socket_close(const char *where, int rc)
{
    int err = WSAGetLastError();

    if (rc == 0)
        log_printf("%s: peer closed connection\n", where);
    else
        log_printf("%s: socket error %d\n", where, err);
}

static void setup_client_socket(SOCKET client, const ServerConfig *cfg)
{
    int flag = 1;
    int bufsize = (int)cfg->socket_buffer_kb * 1024;
    DWORD bytes = 0;

    setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
               (const char *)&flag, sizeof(flag));
    setsockopt(client, SOL_SOCKET, SO_RCVBUF,
               (const char *)&bufsize, sizeof(bufsize));
    setsockopt(client, SOL_SOCKET, SO_SNDBUF,
               (const char *)&bufsize, sizeof(bufsize));
    if (cfg->recv_timeout_ms)
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&cfg->recv_timeout_ms,
                   sizeof(cfg->recv_timeout_ms));
    if (cfg->send_timeout_ms)
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                   (const char *)&cfg->send_timeout_ms,
                   sizeof(cfg->send_timeout_ms));
    if (!cfg->no_keepalive) {
        struct tcp_keepalive ka;

        memset(&ka, 0, sizeof(ka));
        ka.onoff = 1;
        ka.keepalivetime = cfg->keepalive_time_ms;
        ka.keepaliveinterval = cfg->keepalive_interval_ms;
        setsockopt(client, SOL_SOCKET, SO_KEEPALIVE,
                   (const char *)&flag, sizeof(flag));
        WSAIoctl(client, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                 NULL, 0, &bytes, NULL, NULL);
    }
}

static int recv_all(SOCKET s, void *buf, size_t len)
{
    char *p = (char *)buf;

    while (len) {
        int chunk = len > NETBLK_IO_CHUNK_BYTES ? NETBLK_IO_CHUNK_BYTES : (int)len;
        int n = recv(s, p, chunk, 0);
        if (n <= 0) {
            log_socket_close("recv", n);
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_discard(SOCKET s, unsigned char *scratch,
                        unsigned int scratch_bytes,
                        unsigned int bytes)
{
    while (bytes) {
        unsigned int chunk = bytes < scratch_bytes ? bytes : scratch_bytes;

        if (!chunk || recv_all(s, scratch, chunk) < 0)
            return -1;
        bytes -= chunk;
    }
    return 0;
}

static void init_header(NetBlockHeader *h, unsigned int op,
                        unsigned long long lba, unsigned int count,
                        unsigned int bytes, unsigned int status,
                        unsigned int flags)
{
    memset(h, 0, sizeof(*h));
    h->magic[0] = 'W';
    h->magic[1] = 'R';
    h->magic[2] = 'B';
    h->magic[3] = 'P';
    h->version = NETBLK_VERSION;
    h->op = (unsigned short)op;
    h->lba = lba;
    h->count = count;
    h->bytes = bytes;
    h->status = status;
    h->flags = flags;
}

static int send_header(SOCKET s, unsigned int op, unsigned long long lba,
                       unsigned int count, unsigned int bytes,
                       unsigned int status, unsigned int flags)
{
    NetBlockHeader h;

    init_header(&h, op, lba, count, bytes, status, flags);
    return send_all(s, &h, sizeof(h));
}

static const char *hello_profile(unsigned int flags)
{
    switch (flags & 0xffu) {
    case 1: return "Windows 95 / 98 / Me";
    case 2: return "Windows 2000";
    case 3: return "Windows XP / XP Embedded";
    default: return "unknown";
    }
}

static int read_file_exact(HANDLE image, unsigned long long offset,
                            void *buf, unsigned int bytes)
{
    LARGE_INTEGER li;
    unsigned char *p = (unsigned char *)buf;
    unsigned int done = 0;

    li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(image, li, NULL, FILE_BEGIN))
        return -1;
    while (done < bytes) {
        DWORD got = 0;
        DWORD want = bytes - done;
        if (!ReadFile(image, p + done, want, &got, NULL) || got == 0)
            return -1;
        done += got;
    }
    return 0;
}

static int write_file_exact(HANDLE image, unsigned long long offset,
                             const void *buf, unsigned int bytes)
{
    LARGE_INTEGER li;
    const unsigned char *p = (const unsigned char *)buf;
    unsigned int done = 0;

    li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(image, li, NULL, FILE_BEGIN))
        return -1;
    while (done < bytes) {
        DWORD wrote = 0;
        DWORD want = bytes - done;
        if (!WriteFile(image, p + done, want, &wrote, NULL) || wrote == 0)
            return -1;
        done += wrote;
    }
    return 0;
}

static int send_image_exact(ServerState *st, SOCKET client,
                            unsigned long long offset, unsigned int bytes,
                            unsigned char *scratch)
{
    if (st->image_view)
        return send_all(client, st->image_view + offset, bytes);
    if (read_file_exact(st->image, offset, scratch, bytes) < 0)
        return -1;
    return send_all(client, scratch, bytes);
}

static int write_image_exact(ServerState *st, unsigned long long offset,
                             const void *buf, unsigned int bytes)
{
    if (st->image_view) {
        memcpy(st->image_view + offset, buf, bytes);
        return 0;
    }
    return write_file_exact(st->image, offset, buf, bytes);
}

static int recv_image_write(ServerState *st, SOCKET client,
                                  unsigned long long offset,
                                  unsigned int bytes,
                                  unsigned char *scratch,
                                  unsigned int scratch_bytes)
{
    while (bytes) {
        unsigned int chunk = bytes < scratch_bytes ? bytes : scratch_bytes;

        if (!chunk || recv_all(client, scratch, chunk) < 0)
            return -1;
        if (write_image_exact(st, offset, scratch, chunk) < 0)
            return -1;
        offset += chunk;
        bytes -= chunk;
    }
    return 0;
}

static int flush_image(ServerState *st)
{
    if (st->image_view &&
        !FlushViewOfFile(st->image_view, (SIZE_T)st->image_bytes))
        return -1;
    return FlushFileBuffers(st->image) ? 0 : -1;
}

static void log_rate(const char *kind, unsigned long long op_count,
                     unsigned long long sectors, unsigned long long start_ms,
                     unsigned long long lba, unsigned int count)
{
    double elapsed = (double)(now_ms() - start_ms) / 1000.0;
    double kib = (double)(sectors * SECTOR_SIZE) / 1024.0;
    if (elapsed < 0.001)
        elapsed = 0.001;
    log_printf("%s #%" PRIu64 ": lba=%" PRIu64 " count=%u total=%" PRIu64
               " sectors (%.1f KiB, %.1f KiB/s)\n",
               kind, op_count, lba, count, sectors, kib, kib / elapsed);
}

static void log_read_rate(unsigned long long op_count,
                          unsigned long long sectors,
                          unsigned long long start_ms,
                          unsigned long long lba,
                          unsigned int count,
                          unsigned long long gap_ms)
{
    double elapsed = (double)(now_ms() - start_ms) / 1000.0;
    double kib = (double)(sectors * SECTOR_SIZE) / 1024.0;

    if (elapsed < 0.001)
        elapsed = 0.001;
    log_printf("READ #%" PRIu64 ": lba=%" PRIu64 " count=%u dt=%" PRIu64
               "ms total=%" PRIu64
               " sectors (%.1f KiB, %.1f KiB/s)\n",
               op_count, lba, count, gap_ms, sectors,
               kib, kib / elapsed);
}

static int handle_client(ServerState *st, SOCKET client)
{
    unsigned long long hello_count = 0, read_count = 0, write_count = 0;
    unsigned long long flush_count = 0, ping_count = 0;
    unsigned long long read_sectors = 0, write_sectors = 0;
    unsigned long long started = now_ms();
    unsigned long long last_read_start_ms = 0;
    unsigned long long last_ping_ms = 0;
    unsigned long long last_ping_loop = 0;
    int have_last_ping = 0;
    unsigned int max_bytes = st->cfg.max_sectors * SECTOR_SIZE;
    unsigned char *buf = (unsigned char *)malloc(max_bytes);
    const char *phase = "waiting for request header";
    unsigned int last_op = 0;
    unsigned long long last_lba = 0;
    unsigned int last_count = 0;
    int rc = -1;

    if (!buf) {
        log_errorf("out of memory for %u-byte transfer buffer\n", max_bytes);
        return -1;
    }

    for (;;) {
        NetBlockHeader h;
        unsigned long long end_lba;
        unsigned int need;

        phase = "receiving request header";
        if (recv_all(client, &h, sizeof(h)) < 0) {
            log_printf("client disconnected after hello=%" PRIu64
                       " read=%" PRIu64 " write=%" PRIu64
                       " flush=%" PRIu64 " ping=%" PRIu64
                       " last_phase=\"%s\" last_op=%u last_lba=%" PRIu64
                       " last_count=%u\n",
                       hello_count, read_count, write_count,
                       flush_count, ping_count,
                       phase, last_op, last_lba, last_count);
            rc = 0;
            break;
        }
        last_op = h.op;
        last_lba = h.lba;
        last_count = h.count;
        if (h.magic[0] != 'W' || h.magic[1] != 'R' ||
            h.magic[2] != 'B' || h.magic[3] != 'P') {
            const unsigned char *raw = (const unsigned char *)&h;
            unsigned int i;

            log_errorf("bad magic from client; closing stream for clean reconnect: ");
            for (i = 0; i < sizeof(h); i++)
                fprintf(stderr, "%02x%s", raw[i],
                        i + 1 == sizeof(h) ? "\n" : " ");
            break;
        }
        if (h.version != NETBLK_VERSION) {
            send_header(client, h.op, 0, 0, 0, NETBLK_STATUS_BAD_VERSION, 0);
            continue;
        }

        switch (h.op) {
        case NETBLK_OP_TIME:
        {
            time_t wall_time = time(NULL);

            if (wall_time < 0) {
                send_header(client, NETBLK_OP_TIME, 0, 0, 0,
                            NETBLK_STATUS_BAD_OP, 0);
                continue;
            }
            log_printf("TIME-SYNC: epoch=%" PRIu64 "\n",
                       (uint64_t)wall_time);
            phase = "sending TIME response";
            if (send_header(client, NETBLK_OP_TIME,
                            (unsigned long long)wall_time,
                            0, 0, NETBLK_STATUS_OK, 0) < 0)
                goto out;
            rc = 0;
            goto out;
        }
        case NETBLK_OP_HELLO:
            hello_count++;
            if (hello_count == 1)
                g_log_start_ms = now_ms();
            log_printf("HELLO #%" PRIu64 ": profile=%s sectors=%" PRIu64
                       " sector_size=%u max=%u writable=%d\n",
                       hello_count, hello_profile(h.flags),
                       st->sector_count, SECTOR_SIZE,
                       st->cfg.max_sectors, !st->cfg.read_only);
            phase = "sending HELLO response";
            if (send_header(client, NETBLK_OP_HELLO, st->sector_count,
                            SECTOR_SIZE, st->cfg.max_sectors, 0,
                            st->cfg.read_only ? 0u : 1u) < 0)
                goto out;
            break;
        case NETBLK_OP_READ:
        {
            unsigned long long read_start_ms;
            unsigned long long read_gap_ms;

            end_lba = h.lba + h.count;
            if (!h.count || h.count > st->cfg.max_sectors ||
                end_lba < h.lba || end_lba > st->sector_count) {
                send_header(client, NETBLK_OP_READ, 0, 0, 0,
                            NETBLK_STATUS_RANGE, 0);
                continue;
            }
            need = h.count * SECTOR_SIZE;
            phase = "sending READ header/payload";
            read_start_ms = now_ms();
            if (send_header(client, NETBLK_OP_READ, h.lba, h.count,
                            need, 0, 0) < 0 ||
                send_image_exact(st, client, h.lba * SECTOR_SIZE,
                                 need, buf) < 0)
                goto out;
            read_count++;
            read_sectors += h.count;
            read_gap_ms = last_read_start_ms ?
                read_start_ms - last_read_start_ms : 0;
            last_read_start_ms = read_start_ms;
            log_read_rate(read_count, read_sectors, started,
                          h.lba, h.count, read_gap_ms);
            break;
        }
        case NETBLK_OP_WRITE:
            need = h.count * SECTOR_SIZE;
            end_lba = h.lba + h.count;
            if (st->cfg.read_only || !h.count ||
                h.count > NETBLK_PROTOCOL_MAX_SECTORS || h.bytes != need ||
                end_lba < h.lba || end_lba > st->sector_count) {
                if (h.bytes && h.bytes <= NETBLK_PROTOCOL_MAX_PAYLOAD) {
                    phase = "discarding rejected WRITE payload";
                    if (recv_discard(client, buf, max_bytes, h.bytes) < 0)
                        goto out;
                } else if (h.bytes) {
                    log_errorf("WRITE payload too large to resync: lba=%" PRIu64
                               " count=%u bytes=%u\n",
                               h.lba, h.count, h.bytes);
                    goto out;
                }
                send_header(client, NETBLK_OP_WRITE, 0, 0, 0,
                            NETBLK_STATUS_WRITE, 0);
                continue;
            }
            if (h.count > st->cfg.max_sectors)
                log_printf("WRITE compatibility split: lba=%" PRIu64
                           " count=%u advertised_max=%u\n",
                           h.lba, h.count, st->cfg.max_sectors);
            phase = "receiving WRITE payload";
            if (recv_image_write(st, client, h.lba * SECTOR_SIZE,
                                       need, buf, max_bytes) < 0) {
                send_header(client, NETBLK_OP_WRITE, 0, 0, 0,
                            NETBLK_STATUS_WRITE, 0);
                goto out;
            }
            phase = "sending WRITE response";
            if (send_header(client, NETBLK_OP_WRITE, h.lba, h.count,
                            0, 0, 0) < 0)
                goto out;
            write_count++;
            write_sectors += h.count;
            if (write_count <= 16 ||
                (write_count % st->cfg.progress_every) == 0)
                log_rate("WRITE", write_count, write_sectors, started,
                         h.lba, h.count);
            break;
        case NETBLK_OP_FLUSH:
        {
            unsigned int status = NETBLK_STATUS_OK;

            /* Strict flush must report a disk failure, not acknowledge success. */
            if (!st->cfg.read_only && st->cfg.strict_flush && flush_image(st) < 0) {
                log_errorf("failed to flush image (error %lu)\n", GetLastError());
                status = NETBLK_STATUS_WRITE;
            }
            phase = "sending FLUSH response";
            if (send_header(client, NETBLK_OP_FLUSH, 0, 0, 0, status, 0) < 0)
                goto out;
            flush_count++;
            if (flush_count <= 8 ||
                (flush_count % st->cfg.progress_every) == 0)
                log_printf("FLUSH #%" PRIu64 "\n", flush_count);
            break;
        }
        case NETBLK_OP_PING:
        {
            /* An oversized payload cannot be read into the transfer buffer. */
            if (h.bytes > max_bytes)
                goto out;
            if (h.bytes) {
                phase = "receiving PING telemetry";
                if (recv_all(client, buf, h.bytes) < 0)
                    goto out;
            }
            phase = "sending PING response";
            if (send_header(client, NETBLK_OP_PING, 0, 0, 0, 0, 0) < 0)
                goto out;
            ping_count++;
            if (ping_count <= 8 ||
                (ping_count % st->cfg.progress_every) == 0) {
                unsigned int cs = h.flags >> 16;
                unsigned int ip_hi = h.status;
                unsigned int ip_lo = h.flags & 0xffffu;
                unsigned int ip = (ip_hi << 16) | ip_lo;
                unsigned long long this_ms = now_ms();
                double loops_per_sec = 0.0;

                if (have_last_ping && this_ms > last_ping_ms) {
                    unsigned long long delta_loop = h.lba - last_ping_loop;
                    double sec = (double)(this_ms - last_ping_ms) / 1000.0;
                    loops_per_sec = (double)delta_loop / sec;
                }
                last_ping_ms = this_ms;
                last_ping_loop = h.lba;
                have_last_ping = 1;

                if (h.lba || h.count || h.status || h.flags) {
                    log_printf("STATE #%" PRIu64 ": loop=%" PRIu64
                               " cyc=%u rate=%.1floops/s "
                               "cs:ip=%04x:%08x\n",
                               ping_count, h.lba, h.count, loops_per_sec,
                               cs, ip);
                } else {
                    log_printf("STATE #%" PRIu64 "\n", ping_count);
                }
            }
            break;
        }
        default:
            if (h.bytes > max_bytes || (h.bytes && recv_all(client, buf, h.bytes) < 0))
                goto out;
            if (send_header(client, h.op, 0, 0, 0, NETBLK_STATUS_BAD_OP, 0) < 0)
                goto out;
            break;
        }
        phase = "waiting for request header";
        fflush(stdout);
    }

out:
    if (!st->cfg.read_only && write_count)
        flush_image(st);
    if (rc < 0) {
        log_printf("client session ended during %s after hello=%" PRIu64
                   " read=%" PRIu64 " write=%" PRIu64
                   " flush=%" PRIu64 " ping=%" PRIu64
                   " last_op=%u last_lba=%" PRIu64
                   " last_count=%u\n",
                   phase, hello_count, read_count, write_count,
                   flush_count, ping_count,
                   last_op, last_lba, last_count);
    }
    free(buf);
    return rc;
}

int main(int argc, char **argv)
{
    ServerState st;
    WSADATA wsa;
    SOCKET listener = INVALID_SOCKET;
    int opt = 1;
    struct sockaddr_in addr;
    LARGE_INTEGER image_size;
    DWORD access;
    int arg_rc;
    int waiting_for_bind = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    memset(&st, 0, sizeof(st));
    arg_rc = parse_args(argc, argv, &st.cfg);
    if (arg_rc != 0) {
        usage(argv[0]);
        return arg_rc > 0 ? 0 : 2;
    }

    access = GENERIC_READ | (st.cfg.read_only ? 0 : GENERIC_WRITE);
    st.image = CreateFileA(st.cfg.image_path, access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (st.image == INVALID_HANDLE_VALUE) {
        log_errorf("failed to open image '%s' (error %lu)\n",
                   st.cfg.image_path, GetLastError());
        log_errorf("Check the path and file permissions. Close other programs using the image.\n");
        return 1;
    }
    if (!GetFileSizeEx(st.image, &image_size) || image_size.QuadPart <= 0) {
        log_errorf("failed to stat image '%s'\n", st.cfg.image_path);
        CloseHandle(st.image);
        return 1;
    }
    st.image_bytes = (unsigned long long)image_size.QuadPart;
    st.sector_count = (unsigned long long)image_size.QuadPart / SECTOR_SIZE;
    st.mapping = CreateFileMappingA(st.image, NULL,
                                    st.cfg.read_only ? PAGE_READONLY :
                                    PAGE_READWRITE, 0, 0, NULL);
    if (st.mapping) {
        DWORD map_access = FILE_MAP_READ |
            (st.cfg.read_only ? 0 : FILE_MAP_WRITE);

        st.image_view = (unsigned char *)MapViewOfFile(st.mapping,
                                                       map_access, 0, 0, 0);
        if (!st.image_view) {
            log_errorf("warning: MapViewOfFile failed (%lu); using file I/O fallback\n",
                       GetLastError());
            CloseHandle(st.mapping);
            st.mapping = NULL;
        }
    } else {
        log_errorf("warning: CreateFileMapping failed (%lu); using file I/O fallback\n",
                   GetLastError());
    }

    log_printf("WiNspire disk server\n");
    log_printf("  image:  %s (%" PRIu64 " sectors, %s)\n",
               st.cfg.image_path, st.sector_count,
               st.cfg.read_only ? "read-only" : "read/write");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_errorf("WSAStartup failed\n");
        if (st.image_view)
            UnmapViewOfFile(st.image_view);
        if (st.mapping)
            CloseHandle(st.mapping);
        CloseHandle(st.image);
        return 1;
    }

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        log_errorf("socket failed: %d\n", WSAGetLastError());
        goto fail;
    }
    if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (const char *)&opt, sizeof(opt)) != 0) {
        log_errorf("SO_EXCLUSIVEADDRUSE failed: %d\n", WSAGetLastError());
        goto fail;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(st.cfg.port);
    if (inet_pton(AF_INET, st.cfg.listen_addr, &addr.sin_addr) != 1) {
        log_errorf("bad listen address '%s'\n", st.cfg.listen_addr);
        goto fail;
    }
    for (;;) {
        if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            break;
        if (WSAGetLastError() != WSAEADDRNOTAVAIL) {
            log_errorf("bind %s:%u failed: %d\n",
                       st.cfg.listen_addr, st.cfg.port, WSAGetLastError());
            goto fail;
        }
        if (!waiting_for_bind) {
            log_printf("waiting for RNDIS address %s...\n", st.cfg.listen_addr);
            waiting_for_bind = 1;
        }
        Sleep(1000);
    }
    if (listen(listener, 1) != 0) {
        log_errorf("listen failed: %d\n", WSAGetLastError());
        goto fail;
    }

    for (;;) {
        SOCKET client;
        struct sockaddr_in remote;
        int remote_len = sizeof(remote);
        char remote_ip[64] = {0};

        log_printf("waiting for WiNspire...\n");
        fflush(stdout);
        client = accept(listener, (struct sockaddr *)&remote, &remote_len);
        if (client == INVALID_SOCKET) {
            log_errorf("accept failed: %d\n", WSAGetLastError());
            break;
        }
        setup_client_socket(client, &st.cfg);
        inet_ntop(AF_INET, &remote.sin_addr, remote_ip, sizeof(remote_ip));
        log_printf("client connected: %s:%u\n", remote_ip, ntohs(remote.sin_port));
        fflush(stdout);
        handle_client(&st, client);
        closesocket(client);
    }

fail:
    if (listener != INVALID_SOCKET)
        closesocket(listener);
    WSACleanup();
    if (st.image_view) {
        if (!st.cfg.read_only && st.cfg.strict_flush)
            FlushViewOfFile(st.image_view, (SIZE_T)st.image_bytes);
        UnmapViewOfFile(st.image_view);
    }
    if (st.mapping)
        CloseHandle(st.mapping);
    CloseHandle(st.image);
    return 1;
}
