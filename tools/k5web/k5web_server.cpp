// k5web_server.cpp
// Local NTP/HTTP time proxy + static HTTP file server.
// Replaces time_proxy.py + python -m http.server; no Python runtime required.
// Build: see compile_k5web_server.bat (MinGW-w64) or compile_k5web_server_msvc.bat (MSVC)

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdint.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <ctype.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

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
        if (_stricmp(monStr, MONTHS[i]) == 0) {
            mon = i;
            break;
        }
    }
    if (mon < 0) return (time_t)-1;

    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    return _mkgmtime(&tm);
}

static bool queryNtpServer(const char* server, uint32_t& outUtc) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return false;

    DWORD timeoutMs = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    HOSTENT* he = gethostbyname(server);
    if (!he || he->h_length == 0) {
        closesocket(sock);
        return false;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NTP_UDP_PORT);
    memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);

    uint8_t pkt[48] = {};
    pkt[0] = 0x1B; // NTP v3 client
    time_t now = time(NULL);
    uint32_t txSeconds = (uint32_t)(now + NTP_EPOCH);
    pkt[40] = (uint8_t)(txSeconds >> 24);
    pkt[41] = (uint8_t)(txSeconds >> 16);
    pkt[42] = (uint8_t)(txSeconds >> 8);
    pkt[43] = (uint8_t)(txSeconds);

    if (sendto(sock, (char*)pkt, sizeof(pkt), 0, (sockaddr*)&addr, sizeof(addr)) != (int)sizeof(pkt)) {
        closesocket(sock);
        return false;
    }

    char buf[128];
    int len = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
    closesocket(sock);
    if (len < 48) return false;

    uint8_t* data = (uint8_t*)buf;
    uint32_t sec = ((uint32_t)data[32] << 24) |
                   ((uint32_t)data[33] << 16) |
                   ((uint32_t)data[34] << 8) |
                   ((uint32_t)data[35]);
    outUtc = sec - NTP_EPOCH;
    return true;
}

static bool queryHttpTimeSource(const char* url, const char* name, uint32_t& outUtc, const char*& outSource) {
    HINTERNET hInternet = InternetOpenA("k5web-time-proxy/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTO_REDIRECT, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    char dateBuf[128] = {};
    DWORD dateLen = sizeof(dateBuf);
    BOOL ok = HttpQueryInfoA(hUrl, HTTP_QUERY_DATE, dateBuf, &dateLen, NULL);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    if (!ok || dateLen == 0) return false;

    time_t t = parseHttpDate(dateBuf);
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

static void sendHttpResponse(SOCKET client, int status, const char* statusText,
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

static bool parseHttpRequest(SOCKET client, HttpRequest& req) {
    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n = recv(client, buf + total, (int)sizeof(buf) - 1 - total, 0);
        if (n <= 0) return false;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return sscanf(buf, "%15s %511s %15s", req.method, req.path, req.version) == 3;
}

static void handleNtpProxy(SOCKET client, const HttpRequest& req) {
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
    gmtime_s(&tmb, &bt);

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
    if (_stricmp(ext, ".html") == 0 || _stricmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (_stricmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (_stricmp(ext, ".mjs") == 0) return "application/javascript; charset=utf-8";
    if (_stricmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (_stricmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (_stricmp(ext, ".png") == 0) return "image/png";
    if (_stricmp(ext, ".jpg") == 0 || _stricmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(ext, ".gif") == 0) return "image/gif";
    if (_stricmp(ext, ".svg") == 0) return "image/svg+xml";
    if (_stricmp(ext, ".ico") == 0) return "image/x-icon";
    if (_stricmp(ext, ".woff") == 0) return "font/woff";
    if (_stricmp(ext, ".woff2") == 0) return "font/woff2";
    if (_stricmp(ext, ".ttf") == 0) return "font/ttf";
    if (_stricmp(ext, ".bin") == 0) return "application/octet-stream";
    return "application/octet-stream";
}

static void handleStaticFile(SOCKET client, const HttpRequest& req) {
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

static unsigned __stdcall serverThread(void* arg) {
    ServerParams* p = (ServerParams*)arg;

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed for port %d: %d\n", p->port, WSAGetLastError());
        return 1;
    }

    int reuse = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)p->port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind port %d failed: %d\n", p->port, WSAGetLastError());
        closesocket(listenSock);
        return 1;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed for port %d: %d\n", p->port, WSAGetLastError());
        closesocket(listenSock);
        return 1;
    }

    printf("Listening on port %d (%s)\n", p->port, p->isNtpProxy ? "NTP proxy" : "HTTP server");

    while (true) {
        SOCKET client = accept(listenSock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        HttpRequest req = {};
        if (parseHttpRequest(client, req)) {
            if (p->isNtpProxy) {
                handleNtpProxy(client, req);
            } else {
                handleStaticFile(client, req);
            }
        }
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    return 0;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", rc);
        return 1;
    }

    ServerParams ntpParams = {NTP_PROXY_PORT, true};
    ServerParams httpParams = {HTTP_PORT, false};

    HANDLE hNtp = (HANDLE)_beginthreadex(NULL, 0, serverThread, &ntpParams, 0, NULL);
    HANDLE hHttp = (HANDLE)_beginthreadex(NULL, 0, serverThread, &httpParams, 0, NULL);

    if (!hNtp || !hHttp) {
        fprintf(stderr, "Failed to create server threads.\n");
        WSACleanup();
        return 1;
    }

    printf("K5Web server started.\n");
    printf("NTP proxy: http://127.0.0.1:%d/time\n", NTP_PROXY_PORT);
    printf("Web tool:  http://127.0.0.1:%d/\n", HTTP_PORT);
    printf("Press Ctrl+C to stop.\n");

    HANDLE handles[2] = {hNtp, hHttp};
    WaitForMultipleObjects(2, handles, TRUE, INFINITE);

    CloseHandle(hNtp);
    CloseHandle(hHttp);
    WSACleanup();
    return 0;
}
