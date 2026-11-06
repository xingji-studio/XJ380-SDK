// SPDX-License-Identifier: Apache-2.0
// The vendored library is used under its Apache-2.0 option. Its configured
// external RNG and threading hooks are shared with the existing HTTPS client.
#include "internal.h"
#include <mbedtls/threading.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#include <psa/crypto_platform.h>

namespace {
int               runtime_state;
uint64_t          runtime_owner;
int               runtime_error;
constexpr int64_t invalid_time = -0x7fffffffffffffffLL - 1;

void mutex_init(mbedtls_threading_mutex_t *mutex)
{
    if (mutex)
    {
        mutex->locked = 0;
        mutex->valid  = 1;
    }
}

void mutex_free(mbedtls_threading_mutex_t *mutex)
{
    if (mutex) __atomic_store_n(&mutex->valid, 0, __ATOMIC_RELEASE);
}

int mutex_lock(mbedtls_threading_mutex_t *mutex)
{
    if (!mutex || !__atomic_load_n(&mutex->valid, __ATOMIC_ACQUIRE)) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    while (__atomic_exchange_n(&mutex->locked, 1, __ATOMIC_ACQUIRE))
    {
        if (!__atomic_load_n(&mutex->valid, __ATOMIC_ACQUIRE)) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    return 0;
}

int mutex_unlock(mbedtls_threading_mutex_t *mutex)
{
    if (!mutex || !__atomic_load_n(&mutex->valid, __ATOMIC_ACQUIRE)) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    __atomic_store_n(&mutex->locked, 0, __ATOMIC_RELEASE);
    return 0;
}

int64_t certificate_time(const mbedtls_x509_time &time)
{
    static const int cumulative[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    if (time.year < 1 || time.mon < 1 || time.mon > 12 || time.day < 1 || time.day > 31 || time.hour < 0 ||
        time.hour > 23 || time.min < 0 || time.min > 59 || time.sec < 0 || time.sec > 60)
        return invalid_time;
    int64_t years  = time.year - 1;
    int64_t days   = years * 365 + years / 4 - years / 100 + years / 400 - 719162;
    days          += cumulative[time.mon - 1] + time.day - 1;
    if (time.mon > 2 && time.year % 4 == 0 && (time.year % 100 != 0 || time.year % 400 == 0)) ++days;
    return days * 86400 + time.hour * 3600 + time.min * 60 + time.sec;
}
} // namespace

extern "C" int xapi_tls_runtime_init(void)
{
    uint64_t pid            = uint64_t(xnet_sdk::raw_call(SYS_GETPID));
    uint64_t expected_owner = 0;
    __atomic_compare_exchange_n(&runtime_owner, &expected_owner, pid, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (__atomic_load_n(&runtime_owner, __ATOMIC_ACQUIRE) != pid) return -ECHILD;
    int expected = 0;
    if (__atomic_compare_exchange_n(&runtime_state, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        mbedtls_threading_set_alt(mutex_init, mutex_free, mutex_lock, mutex_unlock);
        psa_status_t status = psa_crypto_init();
        runtime_error       = status == PSA_SUCCESS ? 0 : -EIO;
        __atomic_store_n(&runtime_state, status == PSA_SUCCESS ? 2 : -1, __ATOMIC_RELEASE);
    }
    else
    {
        uint64_t start = xnet_sdk::now_ms();
        while (__atomic_load_n(&runtime_state, __ATOMIC_ACQUIRE) == 1)
        {
            if (xnet_sdk::now_ms() - start >= 1000) return -EBUSY;
            struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return runtime_error;
}

extern "C" int xapi_tls_verify_time(void *clock_error, mbedtls_x509_crt *certificate, int, uint32_t *flags)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0 || now.tv_sec < 1577836800LL)
    {
        if (clock_error) *static_cast<int *>(clock_error) = 1;
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        return 0;
    }
    int64_t from = certificate_time(certificate->valid_from);
    int64_t to   = certificate_time(certificate->valid_to);
    if (from == invalid_time || to == invalid_time || from > to)
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
    else
    {
        if (now.tv_sec < from) *flags |= MBEDTLS_X509_BADCERT_FUTURE;
        if (now.tv_sec > to) *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    }
    return 0;
}

extern "C" psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context, uint8_t *output,
                                                        size_t output_size, size_t *output_length)
{
    if (!context || !output || !output_length) return PSA_ERROR_INVALID_ARGUMENT;
    *output_length = 0;
    if (output_size > 0xffffffffULL || xapi_secure_random(output, uint32_t(output_size)))
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    *output_length = output_size;
    return PSA_SUCCESS;
}

// The existing restricted profile has no dynamic key attributes. Keep its PSA
// client shim here so native clients need no hosted-browser compatibility code.
extern "C" void psa_reset_key_attributes(psa_key_attributes_t *attributes)
{
    if (attributes) memset(attributes, 0, sizeof(*attributes));
}
