#pragma once
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace stackchan {
inline bool EncoderPositionSafe(int raw) { return raw >= 0 && raw <= 1023; }
// Stored as one NVS blob: a configuration change always clears its approval.
struct HeadCalibration {
    uint32_t schema = 1;
    int32_t yaw_zero = -1;
    int32_t pitch_zero = -1;
    uint32_t verified = 0;
    bool Valid() const {
        // All emitted targets (yaw +/-30, pitch 5..60) must fit the 10-bit encoder.
        return schema == 1 && verified <= 1 && yaw_zero >= 96 && yaw_zero <= 927 &&
               pitch_zero >= 0 && pitch_zero <= 831;
    }
    bool Approved() const { return Valid() && verified == 1; }
};
static_assert(sizeof(HeadCalibration) == 16);
inline std::optional<HeadCalibration> ParseCalibrationCommand(const char* line) {
    constexpr char prefix[] = "head calibrate ";
    if (!line || std::strncmp(line, prefix, sizeof(prefix) - 1) != 0)
        return std::nullopt;
    const char* cursor = line + sizeof(prefix) - 1;
    long values[2]{};
    for (auto& value : values) {
        while (std::isspace(static_cast<unsigned char>(*cursor)))
            ++cursor;
        if (!*cursor)
            return std::nullopt;
        errno = 0;
        char* end = nullptr;
        value = std::strtol(cursor, &end, 10);
        if (errno == ERANGE || end == cursor || value < 0 || value > 1023 ||
            (*end && !std::isspace(static_cast<unsigned char>(*end))))
            return std::nullopt;
        cursor = end;
    }
    while (std::isspace(static_cast<unsigned char>(*cursor)))
        ++cursor;
    if (*cursor)
        return std::nullopt;
    HeadCalibration result{1, static_cast<int32_t>(values[0]), static_cast<int32_t>(values[1]), 0};
    return result.Valid() ? std::optional<HeadCalibration>(result) : std::nullopt;
}
}  // namespace stackchan
