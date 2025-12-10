/*
 * HTTP client for the browser
 */
#ifndef BROWSER_HTTP_H
#define BROWSER_HTTP_H

#include "str.h"
#include "url.h"
#include "../../lib/vibe.h"

typedef struct {
    int status_code;
    int content_length;
    char location[512];
    int header_len;
} http_response_t;

static inline int find_header_end(const char *buf, int len) {
    for (int i = 0; i < len - 3; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return i + 4;
        }
    }
    return -1;
}

static inline int parse_headers(const char *buf, int len, http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    resp->content_length = -1;
    resp->header_len = find_header_end(buf, len);
    if (resp->header_len < 0) return -1;

    const char *p = buf;
    if (!str_eqn(p, "HTTP/1.", 7)) return -1;
    p += 7;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    resp->status_code = parse_int(p);

    while (*p && *p != '\r') p++;
    if (*p == '\r') p += 2;

    while (p < buf + resp->header_len - 2) {
        const char *line_end = p;
        while (line_end < buf + resp->header_len && *line_end != '\r') line_end++;

        if (str_ieqn(p, "Content-Length:", 15)) {
            const char *val = p + 15;
            while (*val == ' ') val++;
            resp->content_length = parse_int(val);
        } else if (str_ieqn(p, "Location:", 9)) {
            const char *val = p + 9;
            while (*val == ' ') val++;
            int loc_len = line_end - val;
            if (loc_len >= 512) loc_len = 511;
            str_ncpy(resp->location, val, loc_len);
        }
        p = line_end + 2;
    }
    return 0;
}

static inline int http_get(kapi_t *k, url_t *url, char *response, int max_response, http_response_t *resp) {
    uint32_t ip = k->dns_resolve(url->host);
    if (ip == 0) return -1;

    // Connect (TLS or plain TCP)
    int sock;
    if (url->use_tls) {
        sock = k->tls_connect(ip, url->port, url->host);
    } else {
        sock = k->tcp_connect(ip, url->port);
    }
    if (sock < 0) return -1;

    char request[1024];
    char *p = request;
    const char *s;

    s = "GET "; while (*s) *p++ = *s++;
    s = url->path; while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\nHost: "; while (*s) *p++ = *s++;
    s = url->host; while (*s) *p++ = *s++;
    s = "\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n";
    while (*s) *p++ = *s++;
    *p = '\0';

    // Send request
    int sent;
    if (url->use_tls) {
        sent = k->tls_send(sock, request, p - request);
    } else {
        sent = k->tcp_send(sock, request, p - request);
    }
    if (sent < 0) {
        if (url->use_tls) k->tls_close(sock);
        else k->tcp_close(sock);
        return -1;
    }

    int total = 0;
    int timeout = 0;
    resp->header_len = 0;

    while (total < max_response - 1 && timeout < 500) {
        int n;
        if (url->use_tls) {
            n = k->tls_recv(sock, response + total, max_response - 1 - total);
        } else {
            n = k->tcp_recv(sock, response + total, max_response - 1 - total);
        }
        if (n < 0) break;  // Connection closed
        if (n == 0) {
            k->net_poll();
            k->sleep_ms(10);
            timeout++;
            continue;
        }
        total += n;
        timeout = 0;

        // Check if we got headers yet
        if (resp->header_len == 0) {
            response[total] = '\0';
            parse_headers(response, total, resp);

            // If we have Content-Length and got all content, we're done
            if (resp->header_len > 0 && resp->content_length >= 0) {
                int body_received = total - resp->header_len;
                if (body_received >= resp->content_length) break;
            }
        }
    }

    response[total] = '\0';
    if (url->use_tls) k->tls_close(sock);
    else k->tcp_close(sock);
    if (resp->header_len == 0) parse_headers(response, total, resp);
    return total;
}

static inline int is_redirect(int status) {
    return status == 301 || status == 302 || status == 307 || status == 308;
}

#endif /* BROWSER_HTTP_H */
