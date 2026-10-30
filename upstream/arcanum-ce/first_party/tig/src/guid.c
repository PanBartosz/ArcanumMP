#include "tig/guid.h"

#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

int tig_guid_init(TigInitInfo* init_info)
{
    (void)init_info;

    srand((unsigned int)time(NULL));

    return TIG_OK;
}

void tig_guid_exit(void)
{
}

/**
 * Generates UUID v4.
 *
 * Permanent object identities must remain independent across processes, even
 * when their gameplay random generators have the same seed.
 */
void tig_guid_create(TigGuid* guid)
{
    uint8_t bytes[16];
#ifdef _WIN32
    if (BCryptGenRandom(NULL, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) abort();
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) abort();
    size_t offset = 0;
    while (offset < sizeof(bytes)) {
        ssize_t count = read(fd, bytes + offset, sizeof(bytes) - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            close(fd);
            abort();
        }
        offset += (size_t)count;
    }
    close(fd);
#endif

    // http://tools.ietf.org/html/rfc4122.html#section-4.4
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 1

    memcpy(guid, bytes, sizeof(bytes));
}

bool tig_guid_is_equal(const TigGuid* a, const TigGuid* b)
{
    return memcmp(a, b, sizeof(TigGuid)) == 0;
}
