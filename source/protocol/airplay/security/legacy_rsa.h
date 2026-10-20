#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool airplay_legacy_base64_decode(const char *text, uint8_t *output,
                                  size_t capacity, size_t *size_out);
bool airplay_legacy_rsa_unwrap(const char *wrapped_key, uint8_t key_out[16]);
/* IPv4 is the actual local control socket address, in network byte order. */
bool airplay_legacy_rsa_challenge(const char *challenge, uint32_t local_ipv4,
                                 const char *device_id, char response_out[345]);
