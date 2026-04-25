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
        if(m_file != nullptr) {
            fflush(m_file);
            fsync(fileno(m_file));
        }
    }

    void RingFile::Close() {
        if(m_file != nullptr) {
            fflush(m_file);
            fsync(fileno(m_file));
            fclose(m_file);
            m_file = nullptr;
        }
    }

}  // namespace ul::util
