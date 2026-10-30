// POSIX stand-in for the handful of Win32 file-mapping calls that engine/ngram/octagram/octagram_gram.cpp
// includes <Windows.h> for. Only used by mayhem/build.sh (via -I), never part of the product build.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <map>
#include <mutex>

using DWORD = unsigned long;
using BOOL = int;
using HANDLE = void *;
union LARGE_INTEGER
{
    long long QuadPart;
};

#define GENERIC_READ 0
#define FILE_SHARE_READ 0
#define OPEN_EXISTING 0
#define FILE_ATTRIBUTE_NORMAL 0
#define PAGE_READONLY 0
#define FILE_MAP_READ 0
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))

inline HANDLE CreateFileW(const char *path, DWORD, DWORD, void *, DWORD, DWORD, void *)
{
    const int fd = ::open(path, O_RDONLY);
    return fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd) + 1);
}

inline int shim_fd(HANDLE handle)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(handle) - 1);
}

inline BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER *size)
{
    struct stat st;
    if (::fstat(shim_fd(handle), &st) != 0)
        return 0;
    size->QuadPart = st.st_size;
    return 1;
}

// The "mapping handle" is the file handle itself; the mapping is created in MapViewOfFile.
inline HANDLE CreateFileMappingW(HANDLE file, void *, DWORD, DWORD, DWORD, const char *)
{
    return file;
}

inline std::map<const void *, size_t> &shim_views()
{
    static std::map<const void *, size_t> views;
    return views;
}

inline std::mutex &shim_views_mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline void *MapViewOfFile(HANDLE mapping, DWORD, DWORD, DWORD, size_t)
{
    struct stat st;
    if (::fstat(shim_fd(mapping), &st) != 0 || st.st_size <= 0)
        return nullptr;
    const size_t length = static_cast<size_t>(st.st_size);
    void *view = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, shim_fd(mapping), 0);
    if (view == MAP_FAILED)
        return nullptr;
    std::lock_guard<std::mutex> lock(shim_views_mutex());
    shim_views()[view] = length;
    return view;
}

inline BOOL UnmapViewOfFile(const void *view)
{
    std::lock_guard<std::mutex> lock(shim_views_mutex());
    const auto found = shim_views().find(view);
    if (found == shim_views().end())
        return 0;
    ::munmap(const_cast<void *>(view), found->second);
    shim_views().erase(found);
    return 1;
}

inline BOOL CloseHandle(HANDLE handle)
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        ::close(shim_fd(handle));
    return 1;
}
