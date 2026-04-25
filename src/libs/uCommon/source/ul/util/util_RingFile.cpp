#include <ul/util/util_RingFile.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

namespace ul::util {

    void RingFile::Configure(const char *dir, const char *base_name,
                             size_t segment_max_bytes, u8 segment_count) {
        // Clamp so we never exceed the fixed arrays
        if(segment_count == 0) {
            segment_count = 1;
        }
        if(segment_count > MaxSegmentCount) {
            segment_count = MaxSegmentCount;
        }
        if(segment_max_bytes == 0) {
            segment_max_bytes = 512 * 1024;
        }

        strncpy(m_dir,  dir,       MaxPathLen - 1);
        strncpy(m_base, base_name, sizeof(m_base) - 1);
        m_dir[MaxPathLen - 1]   = '\0';
        m_base[sizeof(m_base) - 1] = '\0';

        m_segment_max_bytes = segment_max_bytes;
        m_segment_count     = segment_count;
        m_cur_seg_idx       = 0;
        m_cur_seg_bytes     = 0;
        m_dropped_writes    = 0;
    }

    void RingFile::BuildSegPath(char *out, size_t out_len, u8 idx) const {
        snprintf(out, out_len, "%s/%s.%u.log", m_dir, m_base, static_cast<unsigned>(idx));
    }

    bool RingFile::OpenSegment(u8 idx) {
        // Close current segment first
        if(m_file != nullptr) {
            fflush(m_file);
            fsync(fileno(m_file));
            fclose(m_file);
            m_file = nullptr;
        }

        char path[MaxPathLen + 256 + 16];
        BuildSegPath(path, sizeof(path), idx);

        // Open in append mode so we can resume after crash
        m_file = fopen(path, "ab+");
        if(m_file == nullptr) {
            m_dropped_writes++;
            return false;
        }

        // Record current size so Rotate fires correctly even after a resume
        fseek(m_file, 0, SEEK_END);
        m_cur_seg_bytes = static_cast<size_t>(ftell(m_file));
        m_cur_seg_idx   = idx;
        return true;
    }

    bool RingFile::Open() {
        if(m_file != nullptr) {
            return true;  // already open
        }
        if(m_base[0] == '\0') {
            return false;  // not configured
        }

        // Create the directory; ignore EEXIST
        struct stat st;
        if(stat(m_dir, &st) != 0) {
            if(mkdir(m_dir, 0777) != 0 && errno != EEXIST) {
                m_dropped_writes++;
                return false;
            }
        }

        return OpenSegment(m_cur_seg_idx);
    }

    void RingFile::Rotate() {
        const u8 next = static_cast<u8>((m_cur_seg_idx + 1) % m_segment_count);
        // Truncate the next slot so we don't accumulate forever
        char path[MaxPathLen + 256 + 16];
        BuildSegPath(path, sizeof(path), next);
        // Remove and reopen (truncates cleanly on any FS)
        remove(path);

        OpenSegment(next);
        m_cur_seg_bytes = 0;
    }

    void RingFile::Write(const char *data, size_t len) {
        if(m_file == nullptr) {
            m_dropped_writes++;
            return;
        }
        if(len == 0) {
            return;
        }

        if(m_cur_seg_bytes + len > m_segment_max_bytes) {
            Rotate();
            if(m_file == nullptr) {
                m_dropped_writes++;
                return;
            }
        }

        const size_t written = fwrite(data, 1, len, m_file);
        m_cur_seg_bytes += written;
        if(written < len) {
            m_dropped_writes++;
        }
    }

    void RingFile::Flush() {
        // Cycle E2 (SP4.13): snapshot m_file ONCE under no-lock so a parallel
        // Rotate() (which transiently nulls m_file during fclose+fopen) can't
        // hand us a stale-but-non-null pointer that we'd then dereference
        // through fflush/fileno.  Caller is supposed to hold g_ring_lock for
        // this operation; the snapshot is belt-and-suspenders for any future
        // caller path that forgets the mutex.
        //
        // Hardware crash signature this defends against:
        //   PC = uMenu + 0x7bc980 inside newlib's fileno()
        //   Fault address = 0xb0 (offset into a freed/corrupt FILE struct)
        //   Caller chain: DrainThreadFunc → RingFile::Flush → fileno → CRASH
        //   Reproduced 2× on SP4.12.1 hardware test (crash reports
        //   01777147479 and 01777148190, both same PC, both first-boot).
        //
        //   The original code was:
        //       if(m_file != nullptr) {
        //           fflush(m_file);            ← if m_file flips to null
        //           fsync(fileno(m_file));     ← here, fileno faults
        //       }
        //   Even with mutexes, an invalid FILE* (e.g. from a fopen that
        //   returned non-null but with a corrupt internal struct due to FD
        //   exhaustion) would null-deref the same way.  fflush() returning
        //   EOF means the FILE* is no longer usable; disable it before the
        //   subsequent fileno call.
        FILE *fp = m_file;
        if(fp == nullptr) {
            return;
        }
        if(fflush(fp) == EOF) {
            // FILE* is corrupt or its underlying FD was closed underneath
            // us.  Disable further use so the next Write/Flush can't crash.
            m_file = nullptr;
            m_dropped_writes++;
            return;
        }
        const int fd = fileno(fp);
        if(fd >= 0) {
            fsync(fd);
        }
    }

    void RingFile::Close() {
        FILE *fp = m_file;
        if(fp == nullptr) {
            return;
        }
        // Mirror Flush's defensive shape — fflush failure invalidates the
        // FILE*, so don't fileno/fsync after it returns EOF.
        if(fflush(fp) != EOF) {
            const int fd = fileno(fp);
            if(fd >= 0) {
                fsync(fd);
            }
        }
        fclose(fp);
        m_file = nullptr;
    }

}  // namespace ul::util
