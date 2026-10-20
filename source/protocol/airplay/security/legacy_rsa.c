#include "legacy_rsa.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/version.h>

#include "crypto.h"
#include "legacy_rsa_key.h"

static int legacy_random(void *context, unsigned char *output, size_t size)
{
    return airplay_crypto_random(context, output, size) ? 0 : -1;
}

bool airplay_legacy_base64_decode(const char *text, uint8_t *output,
                                  size_t capacity, size_t *size_out)
{
    char padded[1028];
    size_t length, size = 0u;

    if (!text || !output || !size_out)
        return false;
    length = strlen(text);
    if (!length || length > sizeof(padded) - 4u || length % 4u == 1u)
        return false;
    for (size_t index = 0u; index < length; ++index)
    {
        unsigned char c = (unsigned char)text[index];
        if (!(isalnum(c) || c == '+' || c == '/' || c == '='))
            return false;
        if (c == '=' && (index < length - 2u ||
                        (index + 1u < length && text[index + 1u] != '=')))
            return false;
    }
    memcpy(padded, text, length);
    while (length % 4u)
        padded[length++] = '=';
    if (mbedtls_base64_decode(output, capacity, &size,
                              (const unsigned char *)padded, length) != 0)
        return false;
    *size_out = size;
    return true;
}

static bool legacy_key_init(mbedtls_pk_context *pk, AirPlayCryptoRng *rng)
{
    if (!airplay_crypto_rng_init(rng, "Switch TV Box legacy RAOP"))
        return false;
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_pk_parse_key(
               pk, (const unsigned char *)legacy_private_key,
               sizeof(legacy_private_key), NULL, 0u, legacy_random, rng) == 0;
#else
    return mbedtls_pk_parse_key(
               pk, (const unsigned char *)legacy_private_key,
               sizeof(legacy_private_key), NULL, 0u) == 0;
#endif
}

bool airplay_legacy_rsa_unwrap(const char *wrapped_key, uint8_t key_out[16])
{
    mbedtls_pk_context pk;
    AirPlayCryptoRng rng = {0};
    uint8_t wrapped[256], plain[256];
    size_t wrapped_size = 0u, plain_size = 0u;
    bool ok = false;

    if (!key_out || !airplay_legacy_base64_decode(
            wrapped_key, wrapped, sizeof(wrapped), &wrapped_size) ||
        wrapped_size != sizeof(wrapped))
        return false;
    mbedtls_pk_init(&pk);
    if (!legacy_key_init(&pk, &rng))
        goto cleanup;
    mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21,
                            MBEDTLS_MD_SHA1);
#if MBEDTLS_VERSION_MAJOR >= 3
    ok = mbedtls_rsa_rsaes_oaep_decrypt(
        mbedtls_pk_rsa(pk), legacy_random, &rng, NULL, 0u,
        &plain_size, wrapped, plain, sizeof(plain)) == 0;
#else
    ok = mbedtls_rsa_rsaes_oaep_decrypt(
        mbedtls_pk_rsa(pk), legacy_random, &rng, MBEDTLS_RSA_PRIVATE, NULL, 0u,
        &plain_size, wrapped, plain, sizeof(plain)) == 0;
#endif
    ok = ok && plain_size == 16u;
    if (ok)
        memcpy(key_out, plain, 16u);
cleanup:
    airplay_crypto_secure_zero(plain, sizeof(plain));
    airplay_crypto_rng_deinit(&rng);
    mbedtls_pk_free(&pk);
    return ok;
}

bool airplay_legacy_rsa_challenge(const char *challenge, uint32_t local_ipv4,
                                 const char *device_id, char response_out[345])
{
    mbedtls_pk_context pk;
    AirPlayCryptoRng rng = {0};
    uint8_t payload[32] = {0}, padded[256], signature[256];
    unsigned mac[6];
    size_t challenge_size = 0u, response_size = 0u;
    int consumed = 0;
    bool ok = false;

    if (!device_id || !response_out || !local_ipv4 ||
        !airplay_legacy_base64_decode(challenge, payload, 16u, &challenge_size) ||
        challenge_size != 16u ||
        sscanf(device_id, "%2x:%2x:%2x:%2x:%2x:%2x%n",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5],
               &consumed) != 6 || consumed != 17)
        return false;
    memcpy(payload + 16u, &local_ipv4, 4u);
    for (size_t index = 0u; index < 6u; ++index)
        payload[20u + index] = (uint8_t)mac[index];
    /* Raw RSA PKCS#1 v1.5 block type 1; no digest identifier in RAOP. */
    memset(padded, 0xff, sizeof(padded));
    padded[0] = 0u;
    padded[1] = 1u;
    padded[sizeof(padded) - sizeof(payload) - 1u] = 0u;
    memcpy(padded + sizeof(padded) - sizeof(payload), payload, sizeof(payload));
    mbedtls_pk_init(&pk);
    if (!legacy_key_init(&pk, &rng))
        goto cleanup;
    ok = mbedtls_rsa_private(mbedtls_pk_rsa(pk), legacy_random, &rng,
                             padded, signature) == 0 &&
         mbedtls_base64_encode((unsigned char *)response_out, 345u, &response_size,
                               signature, sizeof(signature)) == 0;
    if (ok)
    {
        while (response_size && response_out[response_size - 1u] == '=')
            --response_size;
        response_out[response_size] = '\0';
    }
cleanup:
    airplay_crypto_secure_zero(padded, sizeof(padded));
    airplay_crypto_rng_deinit(&rng);
    mbedtls_pk_free(&pk);
    return ok;
}
