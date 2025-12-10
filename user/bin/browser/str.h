/*
 * String helper functions for the browser
 */
#ifndef BROWSER_STR_H
#define BROWSER_STR_H

static inline int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static inline int str_eqn(const char *a, const char *b, int n) {
    while (n > 0 && *a && *b && *a == *b) { a++; b++; n--; }
    return n == 0;
}

static inline int str_ieqn(const char *a, const char *b, int n) {
    while (n > 0 && *a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++; n--;
    }
    return n == 0;
}

static inline void str_cpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static inline void str_ncpy(char *dst, const char *src, int n) {
    while (n > 0 && *src) { *dst++ = *src++; n--; }
    *dst = '\0';
}

static inline int parse_int(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

#endif /* BROWSER_STR_H */
