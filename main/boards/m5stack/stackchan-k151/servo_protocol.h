#pragma once
#include <cstdint>
#include <vector>
namespace stackchan {
using Bytes = std::vector<uint8_t>;
inline Bytes Packet(uint8_t id, uint8_t instruction, const Bytes& parameters) {
    Bytes out{0xff, 0xff, id, static_cast<uint8_t>(parameters.size() + 2), instruction};
    out.insert(out.end(), parameters.begin(), parameters.end());
    uint8_t sum = 0;
    for (size_t i = 2; i < out.size(); ++i)
        sum += out[i];
    out.push_back(static_cast<uint8_t>(~sum));
    return out;
}
inline bool Reply(const Bytes& bytes, uint8_t id, size_t count, Bytes& data) {
    if (bytes.size() != count + 6 || bytes[0] != 255 || bytes[1] != 255 || bytes[2] != id ||
        bytes[3] != count + 2 || bytes[4] != 0)
        return false;
    uint8_t sum = 0;
    for (size_t i = 2; i < bytes.size(); ++i)
        sum += bytes[i];
    if (sum != 255)
        return false;
    data.assign(bytes.begin() + 5, bytes.end() - 1);
    return true;
}
inline int Word(const uint8_t* p) { return (int(p[0]) << 8) | p[1]; }
inline void AddWord(Bytes& p, int value) {
    p.push_back((value >> 8) & 255);
    p.push_back(value & 255);
}
}  // namespace stackchan
