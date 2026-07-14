/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Win64 half of resources_os.h. Paths arrive as UTF-8 with '/' separators and are
// converted to UTF-16 at this edge (fixed stack buffers -- MAXPATH bounds them; the
// anostr_to_utf16 route is for anostr_t values, not NUL-terminated edge strings).
// Durability per plan: temp opened share-mode 0; ReplaceFileW when the target exists,
// else MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH); both retried 5x with 100 ms
// backoff on sharing violations. Directory fsync does not exist here: no-op success.

#ifdef _WIN32

#include "resources_os.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

#define WPATH_CAP 512

static bool to_wide(const char *utf8, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
    return n > 0;
}

bool rmos_exists(const char *abs)
{
    wchar_t w[WPATH_CAP];
    if (!to_wide(abs, w, WPATH_CAP))
        return false;
    // No FILE_FLAG_BACKUP_SEMANTICS: a directory refuses to open, which is what we want.
    HANDLE h = CreateFileW(w, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

int rmos_read_open(const char *abs, rmos_file *out)
{
    wchar_t w[WPATH_CAP];
    if (!to_wide(abs, w, WPATH_CAP))
        return -1;
    HANDLE h = CreateFileW(w, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    out->h = (intptr_t)h;
    return 0;
}

int64_t rmos_size_hint(rmos_file f)
{
    LARGE_INTEGER sz;
    if (!GetFileSizeEx((HANDLE)f.h, &sz) || sz.QuadPart < 0)
        return -1;
    return (int64_t)sz.QuadPart;
}

int rmos_read_chunk(rmos_file f, void *buf, size_t cap, size_t *got)
{
    *got = 0;
    DWORD want = cap > 0x7fffffffu ? 0x7fffffffu : (DWORD)cap;
    DWORD n = 0;
    if (!ReadFile((HANDLE)f.h, buf, want, &n, NULL))
        return -1;
    *got = n;       // n == 0 is EOF
    return 0;
}

// Positional. ReadFile with an OVERLAPPED offset on a synchronous handle is positional and
// blocking, but it ALSO advances the file pointer (MSDN) -- so one handle is SINGLE-OWNER.
// A short read is NOT EOF: only *got == 0 is. The caller loops.
int rmos_read_at(rmos_file f, uint64_t off, void *buf, size_t cap, size_t *got)
{
    *got = 0;
    DWORD want = cap > 0x7fffffffu ? 0x7fffffffu : (DWORD)cap;
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset     = (DWORD)(off & 0xffffffffu);
    ov.OffsetHigh = (DWORD)(off >> 32);
    DWORD n = 0;
    if (!ReadFile((HANDLE)f.h, buf, want, &n, &ov))
        return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;   // past the end: *got stays 0
    *got = n;
    return 0;
}

// Advisory only. Win32 hints are open-time flags (FILE_FLAG_SEQUENTIAL_SCAN), so there is
// nothing to say after the fact; a refused hint is not a failure.
int rmos_advise(rmos_file f, uint64_t off, uint64_t len, rmos_advice advice)
{
    (void)f; (void)off; (void)len; (void)advice;
    return 0;
}

// Advisory only: mtime LIES on SMB. Hot reload filters with it and confirms by content hash.
int rmos_stat_hint(const char *abs, uint64_t *mtime, uint64_t *size)
{
    wchar_t w[WPATH_CAP];
    if (!to_wide(abs, w, WPATH_CAP))
        return -1;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &d)
        || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    if (mtime)
        *mtime = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    if (size)
        *size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return 0;
}

void rmos_read_close(rmos_file f)
{
    CloseHandle((HANDLE)f.h);
}

int rmos_mkdir_p(char *abs)
{
    if (abs == NULL || abs[0] == '\0')
        return -1;
    wchar_t w[WPATH_CAP];
    if (!to_wide(abs, w, WPATH_CAP))
        return -1;
    // Create components left to right; skip the drive/UNC head ("C:/", "//server/").
    for (int i = 3; w[i] != L'\0'; i++) {
        if (w[i] != L'/' && w[i] != L'\\')
            continue;
        wchar_t save = w[i];
        w[i] = L'\0';
        if (!CreateDirectoryW(w, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD attr = GetFileAttributesW(w);
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                w[i] = save;
                return -1;
            }
        }
        w[i] = save;
    }
    if (!CreateDirectoryW(w, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return -1;
    DWORD attr = GetFileAttributesW(w);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 0 : -1;
}

int rmos_open_excl(const char *abs, rmos_file *out)
{
    wchar_t w[WPATH_CAP];
    if (!to_wide(abs, w, WPATH_CAP))
        return -1;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0 /* share nothing: the protocol temp */,
                           NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_EXISTS ? 1 : -1;
    out->h = (intptr_t)h;
    return 0;
}

int rmos_write_all(rmos_file f, const void *data, size_t len)
{
    const char *p = data;
    while (len > 0) {
        DWORD want = len > 0x7fffffffu ? 0x7fffffffu : (DWORD)len;
        DWORD n = 0;
        if (!WriteFile((HANDLE)f.h, p, want, &n, NULL) || n == 0)
            return -1;
        p   += n;
        len -= n;
    }
    return 0;
}

int rmos_sync(rmos_file f)
{
    return FlushFileBuffers((HANDLE)f.h) ? 0 : -1;
}

int rmos_close(rmos_file f)
{
    return CloseHandle((HANDLE)f.h) ? 0 : -1;
}

int rmos_rename_replace(const char *from, const char *to)
{
    wchar_t wf[WPATH_CAP], wt[WPATH_CAP];
    if (!to_wide(from, wf, WPATH_CAP) || !to_wide(to, wt, WPATH_CAP))
        return -1;
    for (int attempt = 0; ; attempt++) {
        DWORD attr = GetFileAttributesW(wt);
        BOOL ok;
        if (attr != INVALID_FILE_ATTRIBUTES)
            ok = ReplaceFileW(wt, wf, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL);
        else
            ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (ok)
            return 0;
        if (GetLastError() != ERROR_SHARING_VIOLATION || attempt >= 4)
            return -1;
        Sleep(100);
    }
}

int rmos_rename_new(const char *from, const char *to)
{
    wchar_t wf[WPATH_CAP], wt[WPATH_CAP];
    if (!to_wide(from, wf, WPATH_CAP) || !to_wide(to, wt, WPATH_CAP))
        return -1;
    for (int attempt = 0; ; attempt++) {
        if (MoveFileExW(wf, wt, MOVEFILE_WRITE_THROUGH))
            return 0;
        DWORD e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS)
            return 1;
        if (e != ERROR_SHARING_VIOLATION || attempt >= 4)
            return -1;
        Sleep(100);
    }
}

int rmos_sync_dir(const char *dir)
{
    (void)dir;      // no directory-durability primitive on NTFS; rename metadata is
    return 0;       // journaled, and the plan's Windows protocol ends at the move
}

int rmos_unlink(const char *abs)
{
    wchar_t w[WPATH_CAP];
    if (!to_wide(abs, w, WPATH_CAP))
        return -1;
    return DeleteFileW(w) ? 0 : -1;
}

int rmos_scan_dir(const char *dir, void (*cb)(const char *name, void *ctx), void *ctx)
{
    wchar_t w[WPATH_CAP];
    char pattern[WPATH_CAP];
    size_t len = strlen(dir);
    if (len + 3 > sizeof pattern)
        return -1;
    memcpy(pattern, dir, len);
    pattern[len] = '/';
    pattern[len + 1] = '*';
    pattern[len + 2] = '\0';
    if (!to_wide(pattern, w, WPATH_CAP))
        return -1;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        char name[WPATH_CAP];
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name,
                                NULL, NULL) > 0)
            cb(name, ctx);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

#endif // _WIN32
