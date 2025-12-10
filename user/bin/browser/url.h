/*
 * URL parsing for the browser
 */
#ifndef BROWSER_URL_H
#define BROWSER_URL_H

#include "str.h"

typedef struct {
    char host[256];
    char path[512];
    int port;
    int use_tls;  // 1 for https, 0 for http
} url_t;

static inline int parse_url(const char *url, url_t *out) {
    out->use_tls = 0;
    out->port = 80;

    // Check for https://
    if (str_eqn(url, "https://", 8)) {
        url += 8;
        out->use_tls = 1;
        out->port = 443;
    } else if (str_eqn(url, "http://", 7)) {
        url += 7;
    }

    const char *host_start = url;
    const char *host_end = url;
    while (*host_end && *host_end != '/' && *host_end != ':') host_end++;

    int host_len = host_end - host_start;
    if (host_len >= 256) return -1;
    str_ncpy(out->host, host_start, host_len);

    // Parse port if present
    if (*host_end == ':') {
        host_end++;
        out->port = parse_int(host_end);
        while (*host_end >= '0' && *host_end <= '9') host_end++;
    }

    if (*host_end == '/') str_cpy(out->path, host_end);
    else str_cpy(out->path, "/");

    return 0;
}

// Resolve a potentially relative URL against a base URL
static inline void resolve_url(const char *href, const char *base_url, char *out, int max_len) {
    if (str_eqn(href, "http://", 7) || str_eqn(href, "https://", 8)) {
        // Absolute URL
        str_ncpy(out, href, max_len - 1);
        return;
    }

    // Parse base URL to get host and scheme
    url_t base;
    if (parse_url(base_url, &base) < 0) {
        str_ncpy(out, href, max_len - 1);
        return;
    }

    char *p = out;
    char *end = out + max_len - 1;

    // Build scheme://host:port (preserve https if current page is https)
    const char *s = base.use_tls ? "https://" : "http://";
    while (*s && p < end) *p++ = *s++;
    s = base.host;
    while (*s && p < end) *p++ = *s++;

    // Add port if non-default
    int default_port = base.use_tls ? 443 : 80;
    if (base.port != default_port) {
        *p++ = ':';
        // Convert port to string
        char port_str[8];
        int port = base.port;
        int i = 0;
        do {
            port_str[i++] = '0' + (port % 10);
            port /= 10;
        } while (port > 0);
        // Reverse and copy
        while (i > 0 && p < end) {
            *p++ = port_str[--i];
        }
    }

    if (href[0] == '/') {
        // Absolute path
        s = href;
        while (*s && p < end) *p++ = *s++;
    } else {
        // Relative path - append to current directory
        // Find last / in current path
        int last_slash = 0;
        for (int i = 0; base.path[i]; i++) {
            if (base.path[i] == '/') last_slash = i;
        }
        // Copy path up to and including last /
        for (int i = 0; i <= last_slash && p < end; i++) {
            *p++ = base.path[i];
        }
        // Append relative href
        s = href;
        while (*s && p < end) *p++ = *s++;
    }
    *p = '\0';
}

#endif /* BROWSER_URL_H */
