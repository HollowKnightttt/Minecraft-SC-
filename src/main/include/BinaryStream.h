#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bedrock {

using Bytes = std::vector<uint8_t>;

// Reader + writer biner. Semua fungsi baca melempar std::out_of_range bila data
// kurang, sehingga paket cacat dari jaringan tidak pernah membuat server crash
// (pemanggil cukup menangkap std::exception).
class BinaryStream {
public:
    BinaryStream() = default;
    explicit BinaryStream(Bytes data) : buf_(std::move(data)) {}
    BinaryStream(const uint8_t* p, size_t n) : buf_(p, p + n) {}

    // ------------------------------ Write ------------------------------
    void u8(uint8_t v) { buf_.push_back(v); }
    template <typename T> void putBE(T v) {
        for (int i = int(sizeof(T)) - 1; i >= 0; --i) buf_.push_back(uint8_t(v >> (8 * i)));
    }
    template <typename T> void putLE(T v) {
        for (size_t i = 0; i < sizeof(T); ++i) buf_.push_back(uint8_t(v >> (8 * i)));
    }
    void u24LE(uint32_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); u8(uint8_t(v >> 16)); }
    void f32LE(float f) { uint32_t u; std::memcpy(&u, &f, 4); putLE(u); }
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void bytes(const Bytes& b) { buf_.insert(buf_.end(), b.begin(), b.end()); }
    void varUInt(uint32_t v) {
        while (v >= 0x80) { u8(uint8_t((v & 0x7F) | 0x80)); v >>= 7; }
        u8(uint8_t(v));
    }
    void str(const std::string& s) { varUInt(uint32_t(s.size())); bytes(s.data(), s.size()); }     // Bedrock string
    void strU16BE(const std::string& s) { putBE<uint16_t>(uint16_t(s.size())); bytes(s.data(), s.size()); } // RakNet string

    // ------------------------------ Read -------------------------------
    uint8_t getU8() { need(1); return buf_[pos_++]; }
    template <typename T> T getBE() {
        need(sizeof(T));
        using U = std::make_unsigned_t<T>;
        U v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v = static_cast<U>((v << 8) | buf_[pos_++]);
        return static_cast<T>(v);
    }
    template <typename T> T getLE() {
        need(sizeof(T));
        using U = std::make_unsigned_t<T>;
        U v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v = static_cast<U>(v | (static_cast<U>(buf_[pos_++]) << (8 * i)));
        return static_cast<T>(v);
    }
    uint32_t getU24LE() { need(3); uint32_t v = buf_[pos_] | (buf_[pos_ + 1] << 8) | (buf_[pos_ + 2] << 16); pos_ += 3; return v; }
    float getF32LE() { uint32_t u = getLE<uint32_t>(); float f; std::memcpy(&f, &u, 4); return f; }
    uint32_t getVarUInt() {
        uint32_t r = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t b = getU8();
            r |= uint32_t(b & 0x7F) << shift;
            if (!(b & 0x80)) return r;
        }
        throw std::runtime_error("VarInt terlalu panjang");
    }
    uint64_t getVarULong() {
        uint64_t r = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            uint8_t b = getU8();
            r |= uint64_t(b & 0x7F) << shift;
            if (!(b & 0x80)) return r;
        }
        throw std::runtime_error("VarLong terlalu panjang");
    }
    Bytes getBytes(size_t n) { need(n); Bytes b(buf_.begin() + pos_, buf_.begin() + pos_ + n); pos_ += n; return b; }
    std::string getStr() { return getString(getVarUInt()); }                 // varint length
    std::string getStrU16BE() { return getString(getBE<uint16_t>()); }       // u16 BE length
    std::string getStrLE32() { return getString(getLE<uint32_t>()); }        // u32 LE length (Login)
    void skip(size_t n) { need(n); pos_ += n; }

    // ------------------------------ Util -------------------------------
    size_t remaining() const { return buf_.size() - pos_; }
    bool eof() const { return pos_ >= buf_.size(); }
    size_t size() const { return buf_.size(); }
    const Bytes& bytes() const { return buf_; }
    Bytes release() { return std::move(buf_); }

private:
    void need(size_t n) const { if (remaining() < n) throw std::out_of_range("BinaryStream: data tidak cukup"); }
    std::string getString(size_t n) { need(n); std::string s(buf_.begin() + pos_, buf_.begin() + pos_ + n); pos_ += n; return s; }

    Bytes buf_;
    size_t pos_ = 0;
};

} // namespace bedrock
