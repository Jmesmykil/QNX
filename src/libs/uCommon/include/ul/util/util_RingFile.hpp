
#pragma once
#include <cstdio>
#include <cstddef>
#include <switch.h>

namespace ul::util {

    // 4-segment rotating log file.  Each segment is named <base>.<idx>.log
    // where idx runs [0, segment_count).  When the active segment exceeds
    // segment_max_bytes the writer advances to idx+1 (wrapping) and
    // overwrites the next slot.  The class is NOT thread-safe by itself;
    // callers that need concurrent access must hold their own lock.
    class RingFile {
    public:
        static constexpr u8 MaxSegmentCount = 16;
        static constexpr size_t MaxPathLen   = 512;

        RingFile() :
            m_file(nullptr),
            m_cur_seg_idx(0),
            m_cur_seg_bytes(0),
            m_segment_max_bytes(0),
            m_segment_count(0),
            m_dropped_writes(0)
        {
            m_dir[0]  = '\0';
            m_base[0] = '\0';
        }

        // Call before first use.  Idempotent — calling Open() a second time
        // after a successful open is a no-op (returns true).
        void Configure(const char *dir, const char *base_name,
                       size_t segment_max_bytes, u8 segment_count);

        bool Open();
        void Write(const char *data, size_t len);
        void Flush();
        void Close();

        int  CurrentSegmentIndex() const { return static_cast<int>(m_cur_seg_idx); }
        u64  DroppedWrites()        const { return m_dropped_writes; }

    private:
        void BuildSegPath(char *out, size_t out_len, u8 idx) const;
        bool OpenSegment(u8 idx);
        void Rotate();

        FILE  *m_file;
        u8     m_cur_seg_idx;
        size_t m_cur_seg_bytes;
        size_t m_segment_max_bytes;
        u8     m_segment_count;
        u64    m_dropped_writes;
        char   m_dir[MaxPathLen];
        char   m_base[256];
    };

}  // namespace ul::util
