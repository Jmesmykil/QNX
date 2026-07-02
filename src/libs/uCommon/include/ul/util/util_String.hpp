
#pragma once
#include <switch.h>
#include <string>
#include <string_view>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace ul::util {

    template<typename S>
    inline const char *GetCString(const S &s) {
        return s;
    }
    
    template<>
    inline const char *GetCString<std::string>(const std::string &s) {
        return s.c_str();
    }

    inline bool StringEndsWith(const std::string &value, const std::string &ending) {
        if(ending.size() > value.size()) {
            return false;
        }
        return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
    }

    std::string FormatProgramId(const u64 program_id);

    inline u64 Get64FromString(const std::string &val) {
        return strtoull(val.c_str(), nullptr, 16);
    }

    template <size_t N>
    constexpr bool CopyToStringBuffer(char (&dst)[N], std::string_view src) noexcept {
        static_assert(N > 0, "Destination buffer must have size > 0");

        const size_t copy_len = std::min(N - 1, src.size());
        std::memcpy(dst, src.data(), copy_len);
        dst[copy_len] = '\0';

        return copy_len < src.size();  // true if truncated
    }

    std::string FormatAccount(const AccountUid value);
    std::string FormatResultDisplay(const Result rc);

    std::string FormatSha256Hash(const u8 *hash, const bool only_half);

}
