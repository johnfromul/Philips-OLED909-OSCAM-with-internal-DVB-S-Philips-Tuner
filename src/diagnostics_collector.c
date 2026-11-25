#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHUNK (64 * 1024)
#define LOG_LIMIT (5 * 1024 * 1024)

struct source {
    const char *name;
    const char *rotated;
    const char *checkpoint;
    int fd;
    off_t offset;
    bool filter;
    bool discarding;
};

static int open_regular(int directory, const char *name, int flags, mode_t mode)
{
    int fd = openat(directory, name, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, mode);
    struct stat st;
    if (fd >= 0 && (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    return fd;
}

static int open_destination(const char *path, bool create)
{
    if (create && mkdir(path, 0770) < 0 && errno != EEXIST) return -1;
    return open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

static bool usb_mounted(const char *path)
{
    struct stat disk, parent;
    char *relative = strrchr(path, '/');
    char parent_path[256];
    if (!relative || relative - path >= (long)sizeof(parent_path)) return false;
    size_t length = (size_t)(relative - path);
    memcpy(parent_path, path, length);
    parent_path[length] = '\0';
    return stat(path, &disk) == 0 && stat(parent_path, &parent) == 0 &&
           S_ISDIR(disk.st_mode) && disk.st_dev != parent.st_dev;
}

static bool write_all(int fd, const char *buffer, size_t length)
{
    while (length > 0) {
        ssize_t count = write(fd, buffer, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        buffer += count;
        length -= (size_t)count;
    }
    return true;
}

static bool rotate_logs(int directory, const char *name)
{
    char old[40], next[40];
    for (int i = 5; i > 0; --i) {
        snprintf(next, sizeof(next), "%s.%d", name, i);
        if (i == 1) snprintf(old, sizeof(old), "%s", name);
        else snprintf(old, sizeof(old), "%s.%d", name, i - 1);
        if (renameat(directory, old, directory, next) < 0 && errno != ENOENT) return false;
    }
    return true;
}

static bool append_log(int directory, const char *name, const char *bytes, size_t size)
{
    struct stat st;
    if (directory < 0) return false;
    if (fstatat(directory, name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(st.st_mode)) return false;
        if (st.st_size + (off_t)size > LOG_LIMIT && !rotate_logs(directory, name)) return false;
    } else if (errno != ENOENT) {
        return false;
    }
    int fd = open_regular(directory, name, O_WRONLY | O_CREAT | O_APPEND, 0660);
    if (fd < 0) return false;
    bool ok = fstat(fd, &st) == 0;
    if (ok && !write_all(fd, bytes, size)) {
        // Best effort if a removable disk disappears during a write.
        (void)ftruncate(fd, st.st_size);
        ok = false;
    }
    return close(fd) == 0 && ok;
}

static void save_offset(int directory, const struct source *source)
{
    struct stat st;
    if (fstat(source->fd, &st) < 0) return;
    char name[48], text[128];
    snprintf(name, sizeof(name), "%s.tmp", source->checkpoint);
    int len = snprintf(text, sizeof(text), "%ju %ju %jd %d\n",
                       (uintmax_t)st.st_dev, (uintmax_t)st.st_ino, (intmax_t)source->offset, source->discarding);
    if (len < 0 || (size_t)len >= sizeof(text)) return;
    int fd = open_regular(directory, name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    bool ok = write_all(fd, text, (size_t)len);
    if (close(fd) == 0 && ok) (void)renameat(directory, name, directory, source->checkpoint);
}

static void restore_offset(int directory, struct source *source)
{
    int fd = open_regular(directory, source->checkpoint, O_RDONLY, 0);
    if (fd < 0) return;
    char text[128];
    ssize_t count = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (count <= 0) return;
    text[count] = '\0';
    uintmax_t device, inode;
    intmax_t offset;
    int discarding = 0;
    if (sscanf(text, "%ju %ju %jd %d", &device, &inode, &offset, &discarding) < 3 || offset < 0) return;
    const char *paths[] = {source->name, source->rotated};
    for (size_t i = 0; i < sizeof(paths) / sizeof(*paths); ++i) {
        fd = open_regular(directory, paths[i], O_RDONLY, 0);
        struct stat st;
        if (fd >= 0 && fstat(fd, &st) == 0 && (uintmax_t)st.st_dev == device &&
            (uintmax_t)st.st_ino == inode && offset <= st.st_size) {
            source->fd = fd;
            source->offset = (off_t)offset;
            source->discarding = discarding != 0;
            return;
        }
        if (fd >= 0) close(fd);
    }
}

static size_t filter_lines(char *bytes, size_t size)
{
    size_t written = 0;
    for (size_t start = 0; start < size;) {
        size_t length = (size_t)((char *)memchr(bytes + start, '\n', size - start) - (bytes + start)) + 1;
        if (memmem(bytes + start, length, "(ecm)", 5) || memmem(bytes + start, length, "(dvbapi)", 8)) {
            memmove(bytes + written, bytes + start, length);
            written += length;
        }
        start += length;
    }
    return written;
}

static void collect(int directory, struct source *source, int usb, int fallback)
{
    int next = open_regular(directory, source->name, O_RDONLY, 0);
    if (source->fd < 0) {
        source->fd = next;
        source->offset = 0;
        next = -1;
    }
    if (source->fd < 0) return;
    struct stat current, candidate;
    if (fstat(source->fd, &current) < 0) {
        if (next >= 0) close(next);
        return;
    }
    bool changed = next >= 0 && fstat(next, &candidate) == 0 &&
        (current.st_dev != candidate.st_dev || current.st_ino != candidate.st_ino);
    if (current.st_size < source->offset) {
        source->offset = 0;
        source->discarding = false;
    }
    for (unsigned batch = 0; batch < 16; ++batch) {
        char bytes[CHUNK];
        ssize_t n = pread(source->fd, bytes, sizeof(bytes), source->offset);
        if (n < 0) break;
        if (source->discarding && n > 0) {
            char *newline = memchr(bytes, '\n', (size_t)n);
            source->offset += newline ? (off_t)(newline - bytes + 1) : n;
            source->discarding = newline == NULL;
            save_offset(directory, source);
            continue;
        }
        size_t consumed = (size_t)n;
        while (consumed > 0 && bytes[consumed - 1] != '\n') --consumed;
        if (consumed > 0) {
            size_t length = source->filter ? filter_lines(bytes, consumed) : consumed;
            if (length && !append_log(usb, source->name, bytes, length) &&
                !append_log(fallback, source->name, bytes, length)) break;
            source->offset += (off_t)consumed;
            save_offset(directory, source);
            continue;
        }
        if (n == CHUNK) {
            // Bound memory even for a corrupt line longer than a whole chunk.
            // Keep discarding its bytes until the first newline on the next read.
            source->offset += n;
            source->discarding = true;
            save_offset(directory, source);
            continue;
        }
        if (!changed) break;
        close(source->fd);
        source->fd = next;
        source->offset = 0;
        source->discarding = false;
        next = -1;
        changed = false;
        save_offset(directory, source);
    }
    if (next >= 0) close(next);
}

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    umask(0007);
    int directory = open_destination(argv[1], false);
    if (directory < 0) return 1;
    int lock = open_regular(directory, "diagnostics-collector.lock", O_WRONLY | O_CREAT, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) return 1;
    struct source sources[] = {
        {.name="bridge.log", .rotated="bridge.log.1", .checkpoint="diagnostics-bridge.offset", .fd=-1},
        {.name="oscam.log", .rotated="oscam.log-prev", .checkpoint="diagnostics-oscam.offset", .fd=-1, .filter=true},
    };
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); ++i) restore_offset(directory, &sources[i]);
    char usb_path[300];
    if (snprintf(usb_path, sizeof(usb_path), "%s/oscam-diagnostics", argv[2]) >= (int)sizeof(usb_path)) return 2;
    for (;;) {
        int usb = usb_mounted(argv[2]) ? open_destination(usb_path, true) : -1;
        int fallback = open_destination(argv[3], true);
        for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); ++i) {
            collect(directory, &sources[i], usb, fallback);
        }
        if (usb >= 0) close(usb);
        if (fallback >= 0) close(fallback);
        sleep(3);
    }
}
