#include "DataTbls/BinLayout.h"
#include <array>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        std::array<unsigned char, 20> b{};
        std::int32_t count;
        std::size_t stride;
        auto valid = [&](std::size_t bytes, std::size_t native = 8) {
            return d2bin::recordLayout(b.data(), bytes, native, count, stride);
        };
        auto require = [](bool ok) { if (!ok) throw std::runtime_error("BIN layout regression"); };
        for (std::size_t n = 0; n < 4; ++n) require(!valid(n));
        require(!d2bin::recordLayout(nullptr, 4, 8, count, stride));
        require(valid(4) && count == 0 && stride == 8);
        require(!valid(5)); // zero count must not hide payload
        b[0] = 2;
        require(valid(20) && count == 2 && stride == 8);
        require(valid(12, 16) && count == 2 && stride == 4); // widening input
        require(!valid(19)); // formerly integer division accepted trailing bytes
        require(!valid(4));
        require(!valid(20, 0));
        require(!valid(20, std::numeric_limits<std::size_t>::max()));
        b.fill(255); require(!valid(20)); // negative count
        b.fill(0); b[3] = 128; require(!valid(20)); // INT32_MIN
        b[0] = 255; b[1] = 255; b[2] = 255; b[3] = 127; require(!valid(20));
        std::cout << "PASS BIN count, payload, stride and widening allocation bounds\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
