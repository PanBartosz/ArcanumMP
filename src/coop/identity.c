#include "coop/identity.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>
#include <io.h>
#define file_write _write
#define file_close _close
#else
#include <unistd.h>
#define file_write write
#define file_close close
#endif

bool coop_identity_valid(const uint8_t id[COOP_IDENTITY_SIZE])
{
    uint8_t any = 0;
    for (unsigned i = 0; i < COOP_IDENTITY_SIZE; i++)
        any |= id[i];
    return any != 0;
}

bool coop_identity_generate(uint8_t out[COOP_IDENTITY_SIZE])
{
    uint8_t id[COOP_IDENTITY_SIZE];
#ifdef _WIN32
    if (BCryptGenRandom(NULL, id, sizeof(id), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
#else
    FILE* source = fopen("/dev/urandom", "rb");
    if (!source) return false;
    bool good = fread(id, 1, sizeof(id), source) == sizeof(id);
    fclose(source);
    if (!good) return false;
#endif
    if (!coop_identity_valid(id)) return false;
    memcpy(out, id, sizeof(id));
    return true;
}

static bool read_identity(const char* path, uint8_t out[COOP_IDENTITY_SIZE])
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    uint8_t id[COOP_IDENTITY_SIZE];
    bool good = fread(id, 1, sizeof(id), file) == sizeof(id) && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!good || !coop_identity_valid(id)) {
        errno = EINVAL;
        return false;
    }
    memcpy(out, id, sizeof(id));
    return true;
}

bool coop_identity_load_or_create(const char* path, uint8_t out[COOP_IDENTITY_SIZE])
{
    if (read_identity(path, out)) return true;
    if (errno != ENOENT) return false;
    uint8_t id[COOP_IDENTITY_SIZE];
    if (!coop_identity_generate(id)) return false;
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_BINARY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd < 0) return errno == EEXIST && read_identity(path, out);
    bool good = file_write(fd, id, sizeof(id)) == sizeof(id);
    if (file_close(fd) != 0) good = false;
    if (!good) {
        remove(path);
        return false;
    }
    memcpy(out, id, sizeof(id));
    return true;
}
