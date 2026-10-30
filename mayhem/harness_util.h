// Shared helpers for the file-input harnesses.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include <sys/mman.h>
#include <unistd.h>

// The loaders under test take a path, so the fuzz input is written to a scratch file for the
// duration of one iteration.
class ScratchFile
{
  public:
    ScratchFile(const uint8_t *data, size_t size)
    {
        // An anonymous memfd needs no cleanup (a crash leaves nothing behind); fall back to a
        // file in $TMPDIR/cwd where memfd_create is unavailable.
        int fd = memfd_create("mayhem-input", 0);
        if (fd >= 0)
        {
            path_ = "/proc/self/fd/" + std::to_string(fd);
            anonymous_ = true;
        }
        else
        {
            const char *dir = std::getenv("TMPDIR");
            path_ = std::string(dir != nullptr && *dir != '\0' ? dir : ".") + "/mayhem-input-XXXXXX";
            fd = mkstemp(&path_[0]);
            if (fd < 0)
                return;
        }
        fd_ = fd;
        size_t done = 0;
        while (done < size)
        {
            const ssize_t n = write(fd, data + done, size - done);
            if (n <= 0)
                break;
            done += static_cast<size_t>(n);
        }
        ok_ = done == size;
    }
    ~ScratchFile()
    {
        if (fd_ >= 0)
            close(fd_);
        if (!anonymous_ && !path_.empty())
            unlink(path_.c_str());
    }
    ScratchFile(const ScratchFile &) = delete;
    ScratchFile &operator=(const ScratchFile &) = delete;
    bool ok() const
    {
        return ok_;
    }
    const std::string &path() const
    {
        return path_;
    }

  private:
    std::string path_;
    int fd_ = -1;
    bool anonymous_ = false;
    bool ok_ = false;
};

// The last up-to-`n` bytes of the input, used as free-form query text so the loaded model is
// also exercised with arbitrary (possibly invalid UTF-8) strings.
inline std::string tail_bytes(const uint8_t *data, size_t size, size_t n)
{
    const size_t take = size < n ? size : n;
    return std::string(reinterpret_cast<const char *>(data) + (size - take), take);
}
