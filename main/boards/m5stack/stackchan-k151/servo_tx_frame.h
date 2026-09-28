#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace stackchan {
// Stage the original FTServo header/payload/checksum until wFlushSCS.
class ServoTxFrame {
public:
    int Append(const uint8_t* p, int n) {
        if (!p || n < 0 || failed_ || size_t(n) > bytes_.size() - used_) {
            failed_ = true;
            return 0;
        }
        std::copy(p, p + n, bytes_.begin() + used_);
        used_ += n;
        return n;
    }
    template <typename Send>
    bool Flush(Send send) {
        bool ok = !failed_ && used_ && send(bytes_.data(), used_) == int(used_);
        Clear();
        return ok;
    }
    void Clear() {
        used_ = 0;
        failed_ = false;
    }

private:
    std::array<uint8_t, 64> bytes_{};
    size_t used_ = 0;
    bool failed_ = false;
};
}  // namespace stackchan
