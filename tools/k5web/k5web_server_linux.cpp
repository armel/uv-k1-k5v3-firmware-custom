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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ifaddrs.h>
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

// ---- 手机扫码上报 GPS 暂存：/setgps 写入，/getgps 读取即清（一次性） ----
struct GpsEntry {
    bool valid;
    char token[32];
    double lat, lon, alt;
    time_t ts;
};
static const int GPS_SLOT_COUNT = 16;
static GpsEntry g_gps[GPS_SLOT_COUNT];
static pthread_mutex_t g_gpsLock = PTHREAD_MUTEX_INITIALIZER;

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

// 从查询串中提取参数值（query 形如 "t=abc&lat=31.2"），未找到返回 NULL
static const char* queryParam(const char* query, const char* name, char* out, size_t outSize) {
    char buf[512];
    strncpy(buf, query, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char* pair = strtok(buf, "&"); pair; pair = strtok(NULL, "&")) {
        char* eq = strchr(pair, '=');
        if (!eq) continue;
        char key[64];
        *eq = '\0';
        urlDecode(pair, key, sizeof(key));
        if (strcmp(key, name) == 0) {
            urlDecode(eq + 1, out, outSize);
            return out;
        }
    }
    return NULL;
}

// 手机 APP 扫码后上报 GPS：GET /setgps?t=令牌&lat=..&lon=..&alt=..
static void handleSetGps(int client, const HttpRequest& req) {
    const char* corsHeaders =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n";

    if (strcmp(req.method, "OPTIONS") == 0) {
        sendHttpResponse(client, 204, "No Content", "text/plain", "", 0, corsHeaders);
        return;
    }
    if (strcmp(req.method, "GET") != 0) {
        sendHttpResponse(client, 405, "Method Not Allowed", "text/plain", "Method Not Allowed", 18, corsHeaders);
        return;
    }

    const char* q = strchr(req.path, '?');
    if (!q) {
        sendHttpResponse(client, 400, "Bad Request", "text/plain", "missing parameters", 19, corsHeaders);
        return;
    }

    char t[32] = {0}, latS[32] = {0}, lonS[32] = {0}, altS[32] = {0};
    if (!queryParam(q + 1, "t", t, sizeof(t)) ||
        !queryParam(q + 1, "lat", latS, sizeof(latS)) ||
        !queryParam(q + 1, "lon", lonS, sizeof(lonS))) {
        sendHttpResponse(client, 400, "Bad Request", "text/plain", "missing token/lat/lon", 23, corsHeaders);
        return;
    }
    if (!queryParam(q + 1, "alt", altS, sizeof(altS))) strncpy(altS, "0", sizeof(altS) - 1);

    double lat = atof(latS), lon = atof(lonS), alt = atof(altS);
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 || t[0] == '\0') {
        sendHttpResponse(client, 400, "Bad Request", "text/plain", "invalid parameters", 19, corsHeaders);
        return;
    }

    pthread_mutex_lock(&g_gpsLock);
    GpsEntry* slot = NULL;
    time_t oldest = (time_t)-1;
    GpsEntry* oldestSlot = &g_gps[0];
    for (int i = 0; i < GPS_SLOT_COUNT; i++) {
        if (g_gps[i].valid && strcmp(g_gps[i].token, t) == 0) { slot = &g_gps[i]; break; }
        if (!g_gps[i].valid && !slot) slot = &g_gps[i];
        if (g_gps[i].ts < oldest) { oldest = g_gps[i].ts; oldestSlot = &g_gps[i]; }
    }
    if (!slot) slot = oldestSlot; // 槽位已满时覆盖最旧一条
    snprintf(slot->token, sizeof(slot->token), "%s", t);
    slot->lat = lat;
    slot->lon = lon;
    slot->alt = alt;
    slot->ts = time(NULL);
    slot->valid = true;
    pthread_mutex_unlock(&g_gpsLock);

    sendHttpResponse(client, 200, "OK", "text/plain; charset=utf-8", "OK", 2, corsHeaders);
}

// 网页轮询读取手机上报的位置：GET /getgps?t=令牌（读取即清，一次性）
static void handleGetGps(int client, const HttpRequest& req) {
    const char* corsHeaders =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n";

    if (strcmp(req.method, "GET") != 0) {
        sendHttpResponse(client, 405, "Method Not Allowed", "text/plain", "Method Not Allowed", 18, corsHeaders);
        return;
    }

    char t[32] = {0};
    const char* q = strchr(req.path, '?');
    if (q) queryParam(q + 1, "t", t, sizeof(t));

    char body[256];
    int n = 0;
    pthread_mutex_lock(&g_gpsLock);
    GpsEntry* hit = NULL;
    if (t[0]) {
        for (int i = 0; i < GPS_SLOT_COUNT; i++) {
            if (g_gps[i].valid && strcmp(g_gps[i].token, t) == 0) { hit = &g_gps[i]; break; }
        }
    }
    if (hit) {
        n = snprintf(body, sizeof(body), "{\"ok\":true,\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f}",
                     hit->lat, hit->lon, hit->alt);
        hit->valid = false; // 一次性读取
    } else {
        n = snprintf(body, sizeof(body), "{\"ok\":false}");
    }
    pthread_mutex_unlock(&g_gpsLock);

    sendHttpResponse(client, 200, "OK", "application/json; charset=utf-8", body, (size_t)n, corsHeaders);
}

// 打印本机局域网 IPv4 地址，供手机扫码访问
static void printLanAddresses(int httpPort) {
    struct ifaddrs* ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) return;
    for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
        struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
        if (strncmp(ip, "127.", 4) == 0 || strncmp(ip, "169.254.", 8) == 0 ||
            strcmp(ip, "0.0.0.0") == 0) continue;
        printf("手机扫码访问: http://%s:%d/setgps\n", ip, httpPort);
    }
    freeifaddrs(ifaddr);
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
            } else if (strncmp(req.path, "/setgps", 7) == 0 &&
                       (req.path[7] == '\0' || req.path[7] == '?')) {
                handleSetGps(client, req);
            } else if (strncmp(req.path, "/getgps", 7) == 0 &&
                       (req.path[7] == '\0' || req.path[7] == '?')) {
                handleGetGps(client, req);
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
    printLanAddresses(httpPort);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    pthread_join(hNtp, NULL);
    pthread_join(hHttp, NULL);
    return 0;
}
