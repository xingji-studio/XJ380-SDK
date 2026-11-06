// SPDX-License-Identifier: Apache-2.0
// Mbed TLS is linked under its Apache-2.0 license option.
#include <xtuiapi.h>
#include <xauth.h>
#include <xnet_runtime.h>
#include <errno.h>
#include <string.h>
#include <mbedtls/rsa.h>

extern "C" int xapi_tls_runtime_init(void);

namespace
{
void erase(void *data, size_t bytes)
{
    volatile uint8_t *cursor = (volatile uint8_t *)data;
    while (bytes--) *cursor++ = 0;
}

struct Secret
{
    XAuthPlaintext plain;
    XAuthResult result;
    ~Secret() { erase(this, sizeof(*this)); }
};

int random_bytes(void *, unsigned char *output, size_t bytes)
{
    return bytes > 0xffffffffU || xapi_secure_random(output, (uint32_t)bytes) ? -1 : 0;
}

int64_t authenticate(uint32_t operation, const char *username, const char *password,
                     const uint8_t *authorization, uint8_t *grant)
{
    if (!password) return -EINVAL;
    size_t length = 0;
    while (length < XAUTH_PASSWORD_BYTES && password[length]) ++length;
    if (length == XAUTH_PASSWORD_BYTES) return -ENAMETOOLONG;
    if (username && strlen(username) >= 64) return -ENAMETOOLONG;
    int64_t error = xapi_tls_runtime_init();
    if (error) return error;
    XAuthBegin begin = {};
    begin.size = sizeof(begin);
    begin.version = XAUTH_VERSION;
    begin.operation = operation;
    if (username) strcpy(begin.username, username);
    if (authorization) memcpy(begin.grant, authorization, sizeof(begin.grant));
    XAuthChallenge challenge = {};
    error = (int64_t)enter_syscall(XAPI_AUTH_BEGIN, (uint64_t)&begin, (uint64_t)&challenge, 0, 0, 0, 0);
    erase(begin.grant, sizeof(begin.grant));
    if (error) return error;
    if (challenge.size != sizeof(challenge) || challenge.version != XAUTH_VERSION ||
        challenge.operation != operation || challenge.exponent != 65537 ||
        !memchr(challenge.username, 0, sizeof(challenge.username))) return -EPROTO;
    Secret secret = {};
    secret.plain.version = XAUTH_VERSION;
    secret.plain.operation = operation;
    secret.plain.id = challenge.id;
    memcpy(secret.plain.nonce, challenge.nonce, sizeof(secret.plain.nonce));
    memcpy(secret.plain.username, challenge.username, sizeof(secret.plain.username));
    memcpy(secret.plain.password, password, length);
    if (authorization) memcpy(secret.plain.grant, authorization, sizeof(secret.plain.grant));

    mbedtls_rsa_context key;
    mbedtls_rsa_init(&key);
    static const unsigned char exponent[] = {1, 0, 1};
    static const unsigned char label[] = "XJ380 auth v1";
    XAuthRequest request = {};
    request.size = sizeof(request);
    request.version = XAUTH_VERSION;
    request.id = challenge.id;
    int encrypted = mbedtls_rsa_import_raw(&key, challenge.modulus, sizeof(challenge.modulus),
                                           NULL, 0, NULL, 0, NULL, 0, exponent, sizeof(exponent));
    if (!encrypted) encrypted = mbedtls_rsa_complete(&key);
    if (!encrypted) encrypted = mbedtls_rsa_check_pubkey(&key);
    if (!encrypted) encrypted = mbedtls_rsa_set_padding(&key, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    if (!encrypted)
        encrypted = mbedtls_rsa_rsaes_oaep_encrypt(&key, random_bytes, NULL, label, sizeof(label) - 1,
                                                  sizeof(secret.plain), (const unsigned char *)&secret.plain,
                                                  request.ciphertext);
    mbedtls_rsa_free(&key);
    erase(&secret.plain, sizeof(secret.plain));
    if (encrypted) return -EIO;
    error = (int64_t)enter_syscall(XAPI_AUTH_FINISH, (uint64_t)&request, (uint64_t)&secret.result, 0, 0, 0, 0);
    if (!error && (secret.result.size != sizeof(secret.result) || secret.result.version != XAUTH_VERSION))
        error = -EPROTO;
    if (!error && grant) memcpy(grant, secret.result.grant, XAUTH_TOKEN_BYTES);
    return error;
}
}

extern "C" UINT64 xapi_UserLogin(WSTR username, WSTR password)
{
    return (UINT64)authenticate(XAUTH_LOGIN, username, password, NULL, NULL);
}

extern "C" UINT64 xapi_UserCreateFirst(WSTR username, WSTR password)
{
    return (UINT64)authenticate(XAUTH_CREATE_FIRST, username, password, NULL, NULL);
}

extern "C" int64_t xapi_GetAccountInfo(XAccountInfo *info)
{
    if (!info) return -EINVAL;
    memset(info, 0, sizeof(*info));
    return (int64_t)enter_syscall(XAPI_ACCOUNT_INFO, (uint64_t)info, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_UserVerifyPassword(const char *password, uint8_t grant[XAUTH_TOKEN_BYTES])
{
    if (!grant) return -EINVAL;
    erase(grant, XAUTH_TOKEN_BYTES);
    return authenticate(XAUTH_VERIFY_PASSWORD, NULL, password, NULL, grant);
}

extern "C" int64_t xapi_UserChangePassword(const char *password, const uint8_t grant[XAUTH_TOKEN_BYTES])
{
    if (!grant) return -EINVAL;
    return authenticate(XAUTH_CHANGE_PASSWORD, NULL, password, grant, NULL);
}
