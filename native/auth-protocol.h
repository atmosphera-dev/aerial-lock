#pragma once

#include <stddef.h>
#include <string.h>
#include <security/pam_appl.h>

/* PAM bounds include their trailing NUL. Hex frames never contain secrets in
 * argv, diagnostics, locale-dependent escaping or delimiter ambiguity. */
#define AUTH_LINE_SIZE (2 * PAM_MAX_RESP_SIZE + 16)
#define AUTH_MESSAGE_SIZE (2 * PAM_MAX_MSG_SIZE + 16)

static inline void auth_wipe(void *p, size_t n) {
    explicit_bzero(p, n);
}

static inline int auth_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static inline void auth_hex(const char *src, size_t n, char *dst) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)src[i];
        dst[i * 2] = digits[c >> 4];
        dst[i * 2 + 1] = digits[c & 15];
    }
}

static inline int auth_unhex(const char *src, size_t n, char *dst, size_t cap) {
    if (n % 2 || n / 2 >= cap) return -1;
    for (size_t i = 0; i < n / 2; ++i) {
        int hi = auth_nibble(src[i * 2]), lo = auth_nibble(src[i * 2 + 1]);
        if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) return -1;
        dst[i] = (char)((hi << 4) | lo);
    }
    dst[n / 2] = 0;
    return (int)(n / 2);
}
