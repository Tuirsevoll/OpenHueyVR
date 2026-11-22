/* <dirent.h> for MSVC: as much of it as ClankForth asks (system.c's `dir-list`: the names in a
 * folder), read with Win32's FindFirstFile. Found through the include path
 * openhuey-src/CMakeLists.txt gives clankforth for MSVC; it shadows no other Windows header. */
#ifndef CLANKFORTH_MSVC_DIRENT_H
#define CLANKFORTH_MSVC_DIRENT_H

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct dirent {
    char d_name[260];   /* (WIN32_FIND_DATA's cFileName: MAX_PATH) */
};

typedef struct {
    HANDLE find;
    WIN32_FIND_DATAA found;
    int more;
    struct dirent e;
} DIR;

static DIR *opendir(const char *path) {
    char pattern[520];
    size_t n, i;
    DIR *d;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    n = strlen(path);
    if (n == 0 || n + 3 >= sizeof(pattern)) {
        errno = ENOENT;
        return NULL;
    }
    for (i = 0; i < n; i++) {
        pattern[i] = path[i] == '/' ? '\\' : path[i];   /* (Win32 wants its own separator) */
    }
    if (pattern[n - 1] != '\\') {
        pattern[n++] = '\\';
    }
    pattern[n++] = '*';
    pattern[n] = 0;
    d = (DIR *)calloc(1, sizeof(*d));
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    d->find = FindFirstFileA(pattern, &d->found);
    if (d->find == INVALID_HANDLE_VALUE) {
        free(d);
        errno = ENOENT;
        return NULL;
    }
    d->more = 1;
    return d;
}

/* (the folder's own `.` and `..` come back as Win32 gives them: system.c skips them, as ever) */
static struct dirent *readdir(DIR *d) {
    if (d == NULL || !d->more) {
        return NULL;
    }
    memcpy(d->e.d_name, d->found.cFileName, sizeof(d->e.d_name));
    d->e.d_name[sizeof(d->e.d_name) - 1] = 0;
    d->more = FindNextFileA(d->find, &d->found) ? 1 : 0;
    return &d->e;
}

static int closedir(DIR *d) {
    if (d == NULL) {
        return -1;
    }
    if (d->find != INVALID_HANDLE_VALUE) {
        FindClose(d->find);
    }
    free(d);
    return 0;
}

#endif   /* _MSC_VER */
#endif   /* CLANKFORTH_MSVC_DIRENT_H */
