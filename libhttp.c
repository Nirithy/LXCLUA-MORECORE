/*
** libhttp.c
** HTTP library for Lua
*/

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <wininet.h>

  /* Link with ws2_32.lib is handled in Makefile */
  /* #pragma comment(lib, "ws2_32") */
  /* #pragma comment(lib, "wininet") */

  #define L_SOCKET SOCKET
  #define L_INVALID_SOCKET INVALID_SOCKET
  #define L_SOCKET_ERROR SOCKET_ERROR
  #define l_closesocket closesocket

  static void l_socket_init(void) {
      WSADATA wsaData;
      WSAStartup(MAKEWORD(2, 2), &wsaData);
  }

#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/time.h>
  #include <errno.h>

  #define L_SOCKET int
  #define L_INVALID_SOCKET -1
  #define L_SOCKET_ERROR -1
  #define l_closesocket close

  static void l_socket_init(void) {}

#endif

#define L_HTTP_SOCKET "http.socket"

typedef struct {
    L_SOCKET sock;
} l_socket_ud;

/* Helper for DNS resolution using getaddrinfo (IPv4 enforced for now) */
static int l_resolve_addr(lua_State *L, const char *host, int port, struct sockaddr_in *res_addr) {
    struct addrinfo hints, *res;
    char port_str[16];

    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* Force IPv4 as we use AF_INET sockets */
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "DNS resolution failed: %s", gai_strerror(err));
        return 0; /* Error pushed */
    }

    if (res) {
        memcpy(res_addr, res->ai_addr, sizeof(struct sockaddr_in));
        freeaddrinfo(res);
        return 1; /* Success */
    }

    lua_pushnil(L);
    lua_pushstring(L, "DNS resolution failed: no address found");
    return 0;
}

/* Helper to parse URL into host, port, path */
static int parse_url(const char *url, char *host, size_t host_len, int *port, char *path, size_t path_len, int *is_https) {
    const char *p = url;
    const char *host_start;
    size_t h_len;

    *port = 80;
    *is_https = 0;

    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
        *is_https = 1;
    } else {
        return 0; /* Invalid scheme */
    }

    host_start = p;
    while (*p && *p != ':' && *p != '/') {
        p++;
    }

    h_len = p - host_start;
    if (h_len >= host_len) return 0; /* Host too long */
    memcpy(host, host_start, h_len);
    host[h_len] = '\0';

    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = (*port * 10) + (*p - '0');
            p++;
        }
    }

    if (*p == '\0') {
        if (path_len < 2) return 0;
        strcpy(path, "/");
    } else if (*p == '/') {
        if (strlen(p) >= path_len) return 0;
        strcpy(path, p);
    } else {
        return 0; /* Invalid URL format */
    }

    return 1;
}

/*
** Existing HTTP Request Implementation (Client High-Level)
*/
#if defined(_WIN32)

static int http_request(lua_State *L, const char *method) {
    const char *url = luaL_checkstring(L, 1);
    const char *body = NULL;
    size_t body_len = 0;
    const char *headers = NULL;

    if (strcmp(method, "POST") == 0) {
        body = luaL_optlstring(L, 2, NULL, &body_len);
        headers = luaL_optstring(L, 3, NULL);
    } else {
        headers = luaL_optstring(L, 2, NULL);
    }

    HINTERNET hInternet = InternetOpenA("LuaHTTPClient/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        lua_pushnil(L);
        lua_pushstring(L, "InternetOpen failed");
        return 2;
    }

    char host[256];
    char path[1024];
    int port;
    int is_https;

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https)) {
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "Invalid URL");
        return 2;
    }

    HINTERNET hConnect = InternetConnectA(hInternet, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "InternetConnect failed");
        return 2;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (is_https) {
        flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, method, path, NULL, NULL, NULL, flags, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "HttpOpenRequest failed");
        return 2;
    }

    BOOL res = HttpSendRequestA(hRequest, headers, headers ? (DWORD)strlen(headers) : 0, (LPVOID)body, (DWORD)body_len);
    if (!res) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "HttpSendRequest failed");
        return 2;
    }

    /* Get Status Code */
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &size, NULL);

    /* Read Body */
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    char buffer[4096];
    DWORD bytesRead;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        luaL_addlstring(&b, buffer, bytesRead);
    }

    lua_pushinteger(L, statusCode);
    luaL_pushresult(&b);

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return 2;
}

#elif defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__)

static int http_request(lua_State *L, const char *method) {
    const char *url = luaL_checkstring(L, 1);
    const char *body = NULL;
    size_t body_len = 0;
    const char *headers = NULL;

    if (strcmp(method, "POST") == 0) {
        body = luaL_optlstring(L, 2, NULL, &body_len);
        headers = luaL_optstring(L, 3, NULL);
    } else {
        headers = luaL_optstring(L, 2, NULL);
    }

    char host[256];
    char path[1024];
    int port;
    int is_https;

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https)) {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid URL");
        return 2;
    }

    if (is_https) {
        lua_pushnil(L);
        lua_pushstring(L, "HTTPS not supported on this platform without external libraries");
        return 2;
    }

    struct sockaddr_in serv_addr;
    if (!l_resolve_addr(L, host, port, &serv_addr)) {
        return 2; /* Error already pushed */
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Connection failed");
        return 2;
    }

    /* Construct HTTP Request */
    luaL_Buffer req;
    luaL_buffinit(L, &req);
    luaL_addstring(&req, method);
    luaL_addstring(&req, " ");
    luaL_addstring(&req, path);
    luaL_addstring(&req, " HTTP/1.0\r\n");
    luaL_addstring(&req, "Host: ");
    luaL_addstring(&req, host);
    luaL_addstring(&req, "\r\n");
    luaL_addstring(&req, "User-Agent: LuaHTTPClient/1.0\r\n");
    luaL_addstring(&req, "Connection: close\r\n");

    if (body) {
        char len_str[32];
        snprintf(len_str, sizeof(len_str), "%lu", (unsigned long)body_len);
        luaL_addstring(&req, "Content-Length: ");
        luaL_addstring(&req, len_str);
        luaL_addstring(&req, "\r\n");
    }

    if (headers) {
        luaL_addstring(&req, headers);
        if (headers[strlen(headers)-1] != '\n') {
            luaL_addstring(&req, "\r\n");
        }
    }

    luaL_addstring(&req, "\r\n");
    if (body) {
        luaL_addlstring(&req, body, body_len);
    }

    luaL_pushresult(&req);
    const char *req_str = lua_tostring(L, -1);
    size_t req_len = lua_rawlen(L, -1);

    ssize_t sent = 0;
    while (sent < req_len) {
        ssize_t n = send(sockfd, req_str + sent, req_len - sent, 0);
        if (n < 0) {
            close(sockfd);
            lua_pushnil(L);
            lua_pushstring(L, "Send failed");
            return 2;
        }
        sent += n;
    }
    lua_pop(L, 1); /* pop request string */

    /* Read Response */
    luaL_Buffer resp;
    luaL_buffinit(L, &resp);
    char buffer[4096];
    ssize_t n;
    while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        luaL_addlstring(&resp, buffer, n);
    }
    close(sockfd);

    luaL_pushresult(&resp);
    const char *full_resp = lua_tostring(L, -1);
    size_t full_len = lua_rawlen(L, -1);

    /* Parse Status Code */
    int status_code = 0;
    const char *status_line_end = strstr(full_resp, "\r\n");
    if (status_line_end) {
        const char *p = full_resp;
        while (p < status_line_end && *p != ' ') p++;
        if (p < status_line_end) {
            status_code = atoi(p + 1);
        }
    }

    /* Find Body */
    const char *body_start = strstr(full_resp, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        lua_pushinteger(L, status_code);
        lua_pushlstring(L, body_start, full_len - (body_start - full_resp));
    } else {
        lua_pushinteger(L, status_code);
        lua_pushlstring(L, "", 0);
    }

    return 2;
}

#else

/* Placeholder for other platforms */
static int http_request(lua_State *L, const char *method) {
    lua_pushnil(L);
    lua_pushstring(L, "Platform not supported");
    return 2;
}

#endif

static int l_http_get(lua_State *L) {
    return http_request(L, "GET");
}

static int l_http_post(lua_State *L) {
    return http_request(L, "POST");
}

/*
** Socket API Implementation
*/

static l_socket_ud *l_check_socket(lua_State *L, int index) {
    return (l_socket_ud *)luaL_checkudata(L, index, L_HTTP_SOCKET);
}

static int l_socket_close(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    if (ud->sock != L_INVALID_SOCKET) {
        l_closesocket(ud->sock);
        ud->sock = L_INVALID_SOCKET;
    }
    return 0;
}

static int l_socket_accept(lua_State *L) {
    l_socket_ud *server = l_check_socket(L, 1);
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    if (server->sock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket is closed");
        return 2;
    }

    L_SOCKET newsock = accept(server->sock, (struct sockaddr *)&cli_addr, &clilen);
    if (newsock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Accept failed");
        return 2;
    }

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = newsock;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static int l_socket_recv(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    size_t len = (size_t)luaL_optinteger(L, 2, 4096);

    if (ud->sock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket is closed");
        return 2;
    }

    char *buffer = (char *)malloc(len);
    if (!buffer) {
        return luaL_error(L, "Out of memory");
    }

    int n = recv(ud->sock, buffer, (int)len, 0);
    if (n > 0) {
        lua_pushlstring(L, buffer, n);
        free(buffer);
        return 1;
    } else if (n == 0) {
        free(buffer);
        return 0; /* Connection closed */
    } else {
        free(buffer);
        lua_pushnil(L);
        lua_pushstring(L, "Receive error");
        return 2;
    }
}

static int l_socket_send(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    size_t sent = 0;

    if (ud->sock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket is closed");
        return 2;
    }

    while (sent < len) {
        size_t remaining = len - sent;
        int chunk;
        int n;

        /* Clamp chunk size to ensure it fits in int (required by Windows send) and avoiding huge buffers */
        chunk = (remaining > 65536) ? 65536 : (int)remaining;

        n = send(ud->sock, data + sent, chunk, 0);
        if (n < 0) {
            lua_pushnil(L);
            lua_pushstring(L, "Send error");
            lua_pushinteger(L, (lua_Integer)sent); /* Return partial count as 3rd result */
            return 3;
        }
        sent += n;
    }

    lua_pushinteger(L, (lua_Integer)sent);
    return 1;
}

static int l_socket_settimeout(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    int ms = (int)(luaL_checknumber(L, 2) * 1000); /* seconds to ms */

#ifdef _WIN32
    DWORD timeout = ms;
    setsockopt(ud->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(ud->sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(ud->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(ud->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return 0;
}

static int l_socket_bind(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    const char *host = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);

    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (strcmp(host, "*") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (!l_resolve_addr(L, host, port, &addr)) {
            return 2; /* Error pushed by l_resolve_addr */
        }
    }

    if (bind(ud->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Bind failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_socket_listen(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    int backlog = (int)luaL_optinteger(L, 2, 5);

    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    if (listen(ud->sock, backlog) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Listen failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_socket_connect(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    const char *host = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);

    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    struct sockaddr_in addr;
    if (!l_resolve_addr(L, host, port, &addr)) {
        return 2;
    }

    if (connect(ud->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Connection failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_socket_shutdown(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    const char *how_str = luaL_optstring(L, 2, "both");
    int how = 2; /* SD_BOTH or SHUT_RDWR */

#ifdef _WIN32
    if (strcmp(how_str, "read") == 0) how = SD_RECEIVE;
    else if (strcmp(how_str, "write") == 0) how = SD_SEND;
    else how = SD_BOTH;
#else
    if (strcmp(how_str, "read") == 0) how = SHUT_RD;
    else if (strcmp(how_str, "write") == 0) how = SHUT_WR;
    else how = SHUT_RDWR;
#endif

    if (ud->sock != L_INVALID_SOCKET) {
        shutdown(ud->sock, how);
    }
    return 0;
}

static int l_socket_getsockname(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getsockname(ud->sock, (struct sockaddr *)&addr, &len) < 0) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, inet_ntoa(addr.sin_addr));
    lua_pushinteger(L, ntohs(addr.sin_port));
    return 2;
}

/* Constructor: http.server(port) */
static int l_http_server(lua_State *L) {
    int port = (int)luaL_checkinteger(L, 1);

    L_SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        l_closesocket(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Bind failed");
        return 2;
    }

    if (listen(sockfd, 5) < 0) {
        l_closesocket(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Listen failed");
        return 2;
    }

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = sockfd;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

/* Constructor: http.client(host, port) */
static int l_http_socket_new(lua_State *L) {
    L_SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    /* Set SO_REUSEADDR by default to avoid "Address already in use" annoyances during dev */
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = sockfd;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static int l_http_client(lua_State *L) {
    const char *host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);

    struct sockaddr_in serv_addr;
    if (!l_resolve_addr(L, host, port, &serv_addr)) {
        return 2;
    }

    L_SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        l_closesocket(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Connection failed");
        return 2;
    }

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = sockfd;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static const luaL_Reg httplib[] = {
    {"get", l_http_get},
    {"post", l_http_post},
    {"server", l_http_server},
    {"client", l_http_client},
    {"socket", l_http_socket_new},
    {NULL, NULL}
};

static const luaL_Reg socket_methods[] = {
    {"bind", l_socket_bind},
    {"listen", l_socket_listen},
    {"connect", l_socket_connect},
    {"accept", l_socket_accept},
    {"recv", l_socket_recv},
    {"send", l_socket_send},
    {"close", l_socket_close},
    {"shutdown", l_socket_shutdown},
    {"getsockname", l_socket_getsockname},
    {"settimeout", l_socket_settimeout},
    {"__gc", l_socket_close},
    {NULL, NULL}
};

LUAMOD_API int luaopen_http(lua_State *L) {
    l_socket_init();

    luaL_newmetatable(L, L_HTTP_SOCKET);
    lua_pushvalue(L, -1); /* push metatable */
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable */
    luaL_setfuncs(L, socket_methods, 0);
    lua_pop(L, 1);

    luaL_newlib(L, httplib);
    return 1;
}
