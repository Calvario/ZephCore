/*
 * SPDX-License-Identifier: MIT
 * ZephCore TxtDataHelpers - text message type constants and string helpers
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TXT_TYPE_PLAIN          0    // a plain text message
#define TXT_TYPE_CLI_DATA       1    // a CLI command -or- reply
#define TXT_TYPE_SIGNED_PLAIN   2    // plain text, signed by sender
#define TXT_TYPE_CLI_COMMAND    3    // a CLI command (explicitly), v1.18+
#define DATA_TYPE_RESERVED      0x0000 // reserved for future use
#define DATA_TYPE_DEV           0xFFFF // developer namespace for experimenting with group/channel datagrams

class StrHelper {
public:
	static void strncpy(char *dest, const char *src, size_t buf_sz) {
		if (buf_sz == 0) return;
		size_t i = 0;
		while (i < buf_sz - 1 && src[i] != '\0') {
			dest[i] = src[i];
			i++;
		}
		dest[i] = '\0';
	}

	static void strzcpy(char *dest, const char *src, size_t buf_sz) {
		if (buf_sz == 0) return;
		size_t i = 0;
		while (i < buf_sz - 1 && src[i] != '\0') {
			dest[i] = src[i];
			i++;
		}
		while (i < buf_sz) {
			dest[i++] = '\0';
		}
	}

	/* Trim a fixed-precision decimal down to its shortest exact form:
	 * "250.000" -> "250", "62.500" -> "62.5".  Matches how Arduino MeshCore
	 * renders float CLI values, so app-side parsers see the same text.
	 * No-op on strings without a '.'. */
	static void stripTrailingZeros(char *s) {
		if (s == nullptr || strchr(s, '.') == nullptr) return;
		size_t i = strlen(s);
		while (i > 0 && s[i - 1] == '0') i--;
		if (i > 0 && s[i - 1] == '.') i--;
		s[i] = '\0';
	}

	static bool isBlank(const char *str) {
		if (str == nullptr) return true;
		while (*str) {
			if (*str != ' ' && *str != '\t' && *str != '\n' && *str != '\r') return false;
			str++;
		}
		return true;
	}
};
