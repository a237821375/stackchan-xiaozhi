#include "../main/boards/m5stack/stackchan-k151/cores3_pcm_output.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const int16_t input[] = {-32768, -123, -1, 0, 1, 123, 32767};
    std::vector<int32_t> output;
    auto capture = [&output](const int32_t* data, size_t bytes) {
        assert(bytes % sizeof(int32_t) == 0);
        output.insert(output.end(), data, data + bytes / sizeof(int32_t));
        return true;
    };
    assert(WriteCoreS3Pcm32(input, 7, capture) == 7);
    assert((output ==
            std::vector<int32_t>{INT32_MIN, -8060928, -65536, 0, 65536, 8060928, 2147418112}));
    std::vector<int16_t> long_input(721, 123);
    output.clear();
    std::vector<size_t> chunks;
    assert(WriteCoreS3Pcm32(long_input.data(), 721, [&](const int32_t* data, size_t bytes) {
               chunks.push_back(bytes);
               return capture(data, bytes);
           }) == 721);
    assert((chunks == std::vector<size_t>{960, 960, 960, 4}));
    assert(output.size() == 721);
    for (auto sample : output)
        assert(sample == 8060928);
    int calls = 0;
    assert(WriteCoreS3Pcm32(long_input.data(), 721,
                            [&calls](const int32_t*, size_t) { return ++calls < 2; }) == 240);
    assert(calls == 2);
    calls = 0;
    auto no_write = [&calls](const int32_t*, size_t) {
        ++calls;
        return true;
    };
    assert(WriteCoreS3Pcm32(nullptr, 0, no_write) == 0);
    assert(WriteCoreS3Pcm32(nullptr, -1, no_write) == 0);
    assert(WriteCoreS3Pcm32(nullptr, 1, no_write) == 0);
    assert(calls == 0);
    std::cout << "CoreS3 signed PCM32 amplitude, chunks and write failures: PASS\n";
}
