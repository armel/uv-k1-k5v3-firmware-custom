// k5web_server_linux.cpp
// Linux/POSIX 版本地 NTP/HTTP 时间代理 + 静态 HTTP 文件服务器。
// 与 Windows 版 k5web_server.cpp 功能一致：
//   NTP 时间代理 : http://127.0.0.1:8765/time
//   HTTP 文件服务: http://127.0.0.1:8080/
// 构建: bash compile_k5web_server_linux.sh（g++ -pthread）
// 可选参数: k5web_server_linux [http_port] [ntp_port]

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

static const int HTTP_PORT = 8080;
static const int NTP_PROXY_PORT = 8765;
static const int NTP_UDP_PORT = 123;
static const uint32_t NTP_EPOCH = 2208988800u;

static const char* NTP_SERVERS[] = {
    "ntp.aliyun.com",
    "ntp.tencent.com",
    "cn.pool.ntp.org",
    "time.asia.apple.com",
    "pool.ntp.org",
};

static const char* HTTP_TIME_SOURCES[][2] = {
    {"https://httpbin.org/get", "httpbin"},
    {"https://mirrors.tuna.tsinghua.edu.cn/", "tuna-mirror"},
    {"https://www.baidu.com/", "baidu"},
};

static const char* MONTHS[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

struct HttpRequest {
    char method[16];
    char path[512];
    char version[16];
};

struct ServerParams {
    int port;
    bool isNtpProxy;
};

// 公历日期 -> 1970-01-01 起的天数（Howard Hinnant 算法，免依赖 timegm，跨 glibc/musl 通用）
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (int)(m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static time_t parseHttpDate(const char* s) {
    int day = 0, year = 0, hour = 0, min = 0, sec = 0, mon = -1;
    char monStr[4] = {0};

    // RFC 7231 / RFC 822: Mon, 02 Jan 2006 15:04:05 GMT
    if (sscanf(s, "%*3s, %d %3s %d %d:%d:%d GMT", &day, monStr, &year, &hour, &min, &sec) != 6) {
        if (sscanf(s, "%d %3s %d %d:%d:%d GMT", &day, monStr, &year, &hour, &min, &sec) != 6) {
            return (time_t)-1;
        }
    }

    for (int i = 0; i < 12; i++) {
        if (strcasecmp(monStr, MONTHS[i]) == 0) {
            mon = i;
            break;
        }
    }
    if (mon < 0) return (time_t)-1;

    return (time_t)(daysFromCivil(year, (unsigned)(mon + 1), (unsigned)day) * 86400LL
                    + hour * 3600 + min * 60 + sec);
}

static bool queryNtpServer(const char* server, uint32_t& outUtc) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return false;

    struct timeval timeout = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct addrinfo hints = {};
    hints.ai_family = AF_INET; // 与 Windows 版一致，仅 IPv4
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = NULL;
    if (getaddrinfo(server, "123", &hints, &res) != 0 || !res) {
        if (res) freeaddrinfo(res);
        close(sock);
        return false;
    }

    struct sockaddr_in addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);

    uint8_t pkt[48] = {};
    pkt[0] = 0x1B; // NTP v3 client
    time_t now = time(NULL);
    uint32_t txSeconds = (uint32_t)(now + NTP_EPOCH);
    pkt[40] = (uint8_t)(txSeconds >> 24);
    pkt[41] = (uint8_t)(txSeconds >> 16);
    pkt[42] = (uint8_t)(txSeconds >> 8);
    pkt[43] = (uint8_t)(txSeconds);

    if (sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr*)&addr, sizeof(addr)) != (int)sizeof(pkt)) {
        close(sock);
        return false;
    }

    char buf[128];
    int len = (int)recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
    close(sock);
    if (len < 48) return false;

    uint8_t* data = (uint8_t*)buf;
    uint32_t sec = ((uint32_t)data[32] << 24) |
                   ((uint32_t)data[33] << 16) |
                   ((uint32_t)data[34] << 8) |
                   ((uint32_t)data[35]);
    outUtc = sec - NTP_EPOCH;
    return true;
}

// HTTP 兜底时间源：调用 curl 取响应 Date 头（POSIX 下无 wininet，用系统自带 curl 最省依赖）
static bool queryHttpTimeSource(const char* url, const char* name, uint32_t& outUtc, const char*& outSource) {
    if (system("command -v curl >/dev/null 2>&1") != 0) return false;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -s -m 6 -I \"%s\" 2>/dev/null", url);
    FILE* fp = popen(cmd, "r");
    if (!fp) return false;

    char line[256];
    time_t t = (time_t)-1;
    while (fgets(line, sizeof(line), fp)) {
        // curl -I 输出原始响应头，HTTP/2 下头名为小写 date:
        if (strncasecmp(line, "date:", 5) != 0) continue;
        char* v = line + 5;
        while (*v == ' ') v++;
        char* nl = strpbrk(v, "\r\n");
        if (nl) *nl = '\0';
        t = parseHttpDate(v);
        if (t != (time_t)-1) break;
    }
    pclose(fp);
    if (t == (time_t)-1) return false;

    outUtc = (uint32_t)t;
    outSource = name;
    return true;
}

static bool getNetworkTime(uint32_t& outUtc, const char*& outSource) {
    for (size_t i = 0; i < sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]); i++) {
        if (queryNtpServer(NTP_SERVERS[i], outUtc)) {
            outSource = NTP_SERVERS[i];
            return true;
        }
        fprintf(stderr, "NTP source failed: %s\n", NTP_SERVERS[i]);
    }

    fprintf(stderr, "All NTP servers failed, trying HTTP time sources...\n");
    for (size_t i = 0; i < sizeof(HTTP_TIME_SOURCES) / sizeof(HTTP_TIME_SOURCES[0]); i++) {
        const char* sourceName = NULL;
        if (queryHttpTimeSource(HTTP_TIME_SOURCES[i][0], HTTP_TIME_SOURCES[i][1], outUtc, sourceName)) {
            outSource = sourceName;
            return true;
        }
        fprintf(stderr, "HTTP time source failed: %s\n", HTTP_TIME_SOURCES[i][1]);
    }

    outUtc = (uint32_t)time(NULL);
    outSource = "system time (NTP/HTTP unavailable)";
    fprintf(stderr, "HTTP time sources also failed, falling back to system time.\n");
    return true;
}

static void sendHttpResponse(int client, int status, const char* statusText,
                              const char* contentType, const char* body, size_t bodyLen,
                              const char* extraHeaders) {
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status, statusText, contentType, bodyLen, extraHeaders ? extraHeaders : "");
    if (n > 0) send(client, header, n, 0);
    if (body && bodyLen > 0) send(client, body, (int)bodyLen, 0);
}

static bool parseHttpRequest(int client, HttpRequest& req) {
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n = (int)recv(client, buf + total, (int)sizeof(buf) - 1 - total, 0);
        if (n <= 0) return false;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return sscanf(buf, "%15s %511s %15s", req.method, req.path, req.version) == 3;
}

static void handleNtpProxy(int client, const HttpRequest& req) {
    const char* corsHeaders =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n";

    if (strcmp(req.method, "OPTIONS") == 0) {
        sendHttpResponse(client, 204, "No Content", "text/plain", "", 0, corsHeaders);
        return;
    }

    if (strcmp(req.method, "GET") != 0 ||
        (strcmp(req.path, "/time") != 0 && strcmp(req.path, "/time/") != 0)) {
        sendHttpResponse(client, 404, "Not Found", "application/json; charset=utf-8",
                         "{\"error\":\"not found\"}", 21, corsHeaders);
        return;
    }

    uint32_t utc;
    const char* source;
    getNetworkTime(utc, source);

    uint32_t beijing = utc + 8u * 3600u;
    time_t bt = (time_t)beijing;
    struct tm tmb = {};
    gmtime_r(&bt, &tmb);

    char datetime[32];
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", &tmb);

    char body[512];
    int n = snprintf(body, sizeof(body),
        "{\"unixtime\":%u,\"unixtime_utc\":%u,\"datetime_beijing\":\"%s\",\"source\":\"%s\"}",
        beijing, utc, datetime, source);

    sendHttpResponse(client, 200, "OK", "application/json; charset=utf-8", body, (size_t)n, corsHeaders);
}

static int urlDecode(const char* src, char* dst, size_t dstSize) {
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dstSize) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return (int)j;
}

static const char* getMimeType(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".mjs") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".gif") == 0) return "image/gif";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, ".woff") == 0) return "font/woff";
    if (strcasecmp(ext, ".woff2") == 0) return "font/woff2";
    if (strcasecmp(ext, ".ttf") == 0) return "font/ttf";
    if (strcasecmp(ext, ".bin") == 0) return "application/octet-stream";
    return "application/octet-stream";
}

static void handleStaticFile(int client, const HttpRequest& req) {
    if (strcmp(req.method, "GET") != 0) {
        sendHttpResponse(client, 405, "Method Not Allowed", "text/plain",
                         "Method Not Allowed", 18, NULL);
        return;
    }

    char decoded[512];
    urlDecode(req.path, decoded, sizeof(decoded));

    char* q = strchr(decoded, '?');
    if (q) *q = '\0';
    char* f = strchr(decoded, '#');
    if (f) *f = '\0';

    if (decoded[0] != '/' || strstr(decoded, "..")) {
        sendHttpResponse(client, 404, "Not Found", "text/plain", "Not Found", 9, NULL);
        return;
    }

    char filePath[1024];
    if (decoded[1] == '\0') {
        strncpy(filePath, "index.html", sizeof(filePath) - 1);
    } else {
        snprintf(filePath, sizeof(filePath), "%s", decoded + 1);
    }
    filePath[sizeof(filePath) - 1] = '\0';

    FILE* fp = fopen(filePath, "rb");
    if (!fp) {
        sendHttpResponse(client, 404, "Not Found", "text/plain", "Not Found", 9, NULL);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* buf = (char*)malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        sendHttpResponse(client, 500, "Internal Server Error", "text/plain",
                         "Out of memory", 13, NULL);
        return;
    }

    fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    sendHttpResponse(client, 200, "OK", getMimeType(filePath), buf, (size_t)size, NULL);
    free(buf);
}

static void* serverThread(void* arg) {
    ServerParams* p = (ServerParams*)arg;

    int listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock < 0) {
        fprintf(stderr, "socket() failed for port %d: %s\n", p->port, strerror(errno));
        return NULL;
    }

    int reuse = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)p->port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Bind port %d failed: %s\n", p->port, strerror(errno));
        close(listenSock);
        return NULL;
    }

    if (listen(listenSock, SOMAXCONN) < 0) {
        fprintf(stderr, "listen() failed for port %d: %s\n", p->port, strerror(errno));
        close(listenSock);
        return NULL;
    }

    printf("Listening on port %d (%s)\n", p->port, p->isNtpProxy ? "NTP proxy" : "HTTP server");
    fflush(stdout);

    while (true) {
        int client = accept(listenSock, NULL, NULL);
        if (client < 0) continue;

        HttpRequest req = {};
        if (parseHttpRequest(client, req)) {
            if (p->isNtpProxy) {
                handleNtpProxy(client, req);
            } else {
                handleStaticFile(client, req);
            }
        }
        shutdown(client, SHUT_RDWR);
        close(client);
    }

    return NULL;
}

int main(int argc, char** argv) {
    // 端口可用命令行参数覆盖，便于 8080/8765 被占用时调整
    int httpPort = HTTP_PORT;
    int ntpPort = NTP_PROXY_PORT;
    if (argc > 1) httpPort = atoi(argv[1]);
    if (argc > 2) ntpPort = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN); // 客户端提前断开时不终止进程

    ServerParams ntpParams = {ntpPort, true};
    ServerParams httpParams = {httpPort, false};

    pthread_t hNtp, hHttp;
    if (pthread_create(&hNtp, NULL, serverThread, &ntpParams) != 0 ||
        pthread_create(&hHttp, NULL, serverThread, &httpParams) != 0) {
        fprintf(stderr, "Failed to create server threads.\n");
        return 1;
    }

    printf("K5Web server started.\n");
    printf("NTP proxy: http://127.0.0.1:%d/time\n", ntpPort);
    printf("Web tool:  http://127.0.0.1:%d/\n", httpPort);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    pthread_join(hNtp, NULL);
    pthread_join(hHttp, NULL);
    return 0;
}
