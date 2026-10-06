/* LD_PRELOAD shim for OLR fault tests: makes pread() on one file fail the way a network
   filesystem does after the file is removed on the server, without needing an NFS server.

   Control file (path in OLRT_FAULT_CTL, default /opt/OpenLogReplicator/work/fault.ctl), one line:
       <mode> <path-suffix>
   mode: estale  pread returns -1, errno ESTALE (NFS: file removed by another client)
         eio     pread returns -1, errno EIO    (NFS soft mount: server not responding)
         zero    pread returns 0                (end of file: the file shrank)
   Until the control file exists every pread is passed through unchanged.

   OLRT_SLOW_US=<n> with OLRT_SLOW_SUFFIX=<path-suffix>: every pread on that file first sleeps n us, so a
   test can change the file while the reader is still near its start. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef ssize_t (*pread_fn)(int, void*, size_t, off_t);

static int path_matches(int fd, const char* suffix) {
    char link[64], path[4096];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(link, path, sizeof(path) - 1);
    if (len <= 0)
        return 0;
    path[len] = 0;
    // an unlinked file still open shows as "<path> (deleted)"
    static const char deleted[] = " (deleted)";
    size_t dl = sizeof(deleted) - 1;
    if ((size_t)len > dl && strcmp(path + len - dl, deleted) == 0) {
        len -= dl;
        path[len] = 0;
    }
    size_t sl = strlen(suffix);
    return (size_t)len >= sl && strcmp(path + len - sl, suffix) == 0;
}

static int fault_for(int fd) {
    const char* ctl = getenv("OLRT_FAULT_CTL");
    if (ctl == NULL)
        ctl = "/opt/OpenLogReplicator/work/fault.ctl";
    FILE* f = fopen(ctl, "r");
    if (f == NULL)
        return 0;
    char mode[16] = {0}, suffix[1024] = {0};
    int n = fscanf(f, "%15s %1023s", mode, suffix);
    fclose(f);
    if (n != 2)
        return 0;

    if (!path_matches(fd, suffix))
        return 0;

    if (strcmp(mode, "estale") == 0)
        return ESTALE;
    if (strcmp(mode, "eio") == 0)
        return EIO;
    if (strcmp(mode, "zero") == 0)
        return -1;
    return 0;
}

static ssize_t faulty(pread_fn real, int fd, void* buf, size_t count, off_t offset) {
    const char* slow = getenv("OLRT_SLOW_US");
    const char* slowSuffix = getenv("OLRT_SLOW_SUFFIX");
    if (slow != NULL && slowSuffix != NULL && path_matches(fd, slowSuffix))
        usleep((useconds_t)strtoul(slow, NULL, 10));
    int f = fault_for(fd);
    if (f == -1)
        return 0;
    if (f > 0) {
        errno = f;
        return -1;
    }
    return real(fd, buf, count, offset);
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    static pread_fn real = NULL;
    if (real == NULL)
        real = (pread_fn)dlsym(RTLD_NEXT, "pread");
    return faulty(real, fd, buf, count, offset);
}

ssize_t pread64(int fd, void* buf, size_t count, off_t offset) {
    static pread_fn real = NULL;
    if (real == NULL)
        real = (pread_fn)dlsym(RTLD_NEXT, "pread64");
    return faulty(real, fd, buf, count, offset);
}
