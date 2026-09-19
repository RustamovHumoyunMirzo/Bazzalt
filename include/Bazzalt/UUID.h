#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

namespace Bazzalt {

class UUID final {
public:
    constexpr UUID() = default;
    constexpr UUID(std::uint64_t high, std::uint64_t low) : m_high(high), m_low(low) {}

    [[nodiscard]] static constexpr UUID Root() { return {}; }

    [[nodiscard]] static UUID Generate() {
        static thread_local std::mt19937_64 generator{std::random_device{}()};
        std::uint64_t high = generator();
        std::uint64_t low = generator();
        // RFC 4122 version 4 and variant bits.
        high = (high & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
        low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
        return {high, low};
    }

    [[nodiscard]] static bool TryParse(std::string_view text, UUID& result) {
        std::array<char, 32> digits{};
        std::size_t count = 0;
        for (const char character : text) {
            if (character == '-') continue;
            if (count == digits.size() || !IsHexDigit(character)) return false;
            digits[count++] = character;
        }
        if (count != digits.size()) return false;

        std::uint64_t high = 0;
        std::uint64_t low = 0;
        for (std::size_t index = 0; index < 16; ++index) high = (high << 4) | HexValue(digits[index]);
        for (std::size_t index = 16; index < 32; ++index) low = (low << 4) | HexValue(digits[index]);
        result = UUID{high, low};
        return true;
    }

    [[nodiscard]] std::string ToString() const {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0')
               << std::setw(8) << static_cast<std::uint32_t>(m_high >> 32) << '-'
               << std::setw(4) << static_cast<std::uint16_t>(m_high >> 16) << '-'
               << std::setw(4) << static_cast<std::uint16_t>(m_high) << '-'
               << std::setw(4) << static_cast<std::uint16_t>(m_low >> 48) << '-'
               << std::setw(12) << (m_low & 0x0000ffffffffffffULL);
        return stream.str();
    }

    [[nodiscard]] constexpr std::uint64_t GetHigh() const { return m_high; }
    [[nodiscard]] constexpr std::uint64_t GetLow() const { return m_low; }
    [[nodiscard]] constexpr bool IsValid() const { return m_high != 0 || m_low != 0; }
    [[nodiscard]] constexpr bool IsRoot() const { return m_high == 0 && m_low == 0; }
    [[nodiscard]] explicit constexpr operator bool() const { return IsValid(); }

    friend constexpr bool operator==(UUID left, UUID right) {
        return left.m_high == right.m_high && left.m_low == right.m_low;
    }
    friend constexpr bool operator!=(UUID left, UUID right) { return !(left == right); }
    friend constexpr bool operator<(UUID left, UUID right) {
        return left.m_high < right.m_high || (left.m_high == right.m_high && left.m_low < right.m_low);
    }

private:
    static constexpr bool IsHexDigit(char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    }
    static constexpr std::uint64_t HexValue(char value) {
        return value >= '0' && value <= '9' ? static_cast<std::uint64_t>(value - '0')
             : value >= 'a' && value <= 'f' ? static_cast<std::uint64_t>(value - 'a' + 10)
                                            : static_cast<std::uint64_t>(value - 'A' + 10);
    }

    std::uint64_t m_high = 0;
    std::uint64_t m_low = 0;
};

} // namespace Bazzalt

template<>
struct std::hash<Bazzalt::UUID> {
    std::size_t operator()(Bazzalt::UUID value) const noexcept {
        const std::size_t high = std::hash<std::uint64_t>{}(value.GetHigh());
        const std::size_t low = std::hash<std::uint64_t>{}(value.GetLow());
        return high ^ (low + 0x9e3779b9U + (high << 6U) + (high >> 2U));
    }
};
