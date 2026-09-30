#include "MtaCipher.hpp"
#include "MtaProtocol.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>

bool MtaCipher::InitializeFromOpenReply(const std::uint8_t* data, std::size_t size, std::string& error)
{
    ready_ = false;
    if (!data || size < 33 || data[0] != mta::kOpenReplyId) { error = "bad reply"; return false; }
    if (!std::equal(mta::kOfflineMagic.begin(), mta::kOfflineMagic.end(), data + 1)) { error = "bad magic"; return false; }

    serverToken_.assign(data + 17, data + 33);
    std::vector<std::uint8_t> key;
    tag_.clear();
    mode_ = 0;
    idleIntervalUs_ = 0;
    pingIntervalUs_ = 0;
    lastEncryptedSendUs_ = 0;

    std::size_t offset = 33;
    if (offset < size && data[offset] == mta::kHelloMark)
    {
        ++offset;
        bool terminated = false;
        while (offset < size)
        {
            const auto type = data[offset++];
            if (type == mta::kHelloEnd) { terminated = true; break; }
            if (offset >= size) { error = "truncated"; return false; }
            const auto length = data[offset++];
            if (offset + length > size) { error = "overflow"; return false; }
            if (type == mta::kTlvMode && length) mode_ = data[offset];
            else if (type == mta::kTlvKey) key.assign(data + offset, data + offset + length);
            else if (type == mta::kTlvTag) tag_.assign(data + offset, data + offset + length);
            else if (type == mta::kTlvIdle && length == 4) std::memcpy(&idleIntervalUs_, data + offset, 4);
            else if (type == mta::kTlvPing && length == 4) std::memcpy(&pingIntervalUs_, data + offset, 4);
            offset += length;
        }
        if (!terminated) { error = "unterminated"; return false; }
    }

    for (std::size_t i = 0; i < sbox_.size(); ++i) sbox_[i] = static_cast<std::uint8_t>(i);
    if (!key.empty())
    {
        std::uint8_t j = 0;
        for (std::size_t i = 0; i < sbox_.size(); ++i)
        {
            j = static_cast<std::uint8_t>(j + sbox_[i] + key[i % key.size()]);
            std::swap(sbox_[i], sbox_[j]);
        }
        std::uint8_t i = 0;
        j = 0;
        for (std::uint32_t drop = 0; drop < mta::kSboxDrops; ++drop)
        {
            j = static_cast<std::uint8_t>(j + sbox_[i]);
            std::swap(sbox_[i], sbox_[j]);
            ++i;
        }
    }
    ready_ = true;
    return true;
}

bool MtaCipher::Decode(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& plain, std::string& error) const
{
    if (!ready_ || !data || !size) { error = "empty"; return false; }
    if (mode_ != mta::kModeCipher) { plain.assign(data, data + size); return true; }
    if (tag_.size() > mta::kTagLimit) { error = "tag"; return false; }

    const std::size_t header = tag_.size() + mta::kHeaderExtra;
    if (size <= header || !std::equal(tag_.begin(), tag_.end(), data)) { error = "header"; return false; }

    const auto storedCrc = static_cast<std::uint16_t>(data[tag_.size()] | (data[tag_.size() + 1] << 8));
    const auto seed = data[tag_.size() + 2];
    const auto* cipher = data + header;
    const auto cipherSize = size - header;
    if (Crc16(cipher, cipherSize) != storedCrc) { error = "crc"; return false; }

    plain.resize(cipherSize);
    Crypt(cipher, cipherSize, seed, plain.data());
    return true;
}

int MtaCipher::Encode(const std::uint8_t* data, std::size_t size, std::uint8_t* output, std::size_t capacity)
{
    std::lock_guard<std::mutex> lock(encodeMutex_);
    if (!ready_ || !data || !output) return 0;
    if (mode_ != mta::kModeCipher)
    {
        if (size > capacity) return 0;
        std::memcpy(output, data, size);
        return static_cast<int>(size);
    }

    const auto tagSize = tag_.size() <= mta::kTagLimit ? tag_.size() : 0;
    const std::size_t total = tagSize + mta::kHeaderExtra + size;
    if (total > capacity) return 0;

    std::copy_n(tag_.begin(), tagSize, output);
    lcg_ = mta::kLcgMul * lcg_ + mta::kLcgAdd;
    const auto seed = static_cast<std::uint8_t>(lcg_ >> 16);
    auto* cipher = output + tagSize + mta::kHeaderExtra;
    Crypt(data, size, seed, cipher);
    const auto crc = Crc16(cipher, size);
    output[tagSize] = static_cast<std::uint8_t>(crc);
    output[tagSize + 1] = static_cast<std::uint8_t>(crc >> 8);
    output[tagSize + 2] = seed;
    lastEncryptedSendUs_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    return static_cast<int>(total);
}

int MtaCipher::Transform(void* context, const unsigned char* input, unsigned int inputLength,
    unsigned char* output, unsigned int outputCapacity)
{
    return context ? static_cast<MtaCipher*>(context)->Encode(input, inputLength, output, outputCapacity) : 0;
}

std::uint16_t MtaCipher::Crc16(const std::uint8_t* data, std::size_t size)
{
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>((crc << 1) ^ ((crc & 0x8000) ? mta::kCrcPoly : 0));
    }
    return crc;
}

void MtaCipher::Crypt(const std::uint8_t* input, std::size_t size, std::uint8_t seed, std::uint8_t* output) const
{
    for (std::size_t index = 0; index < size; ++index)
    {
        const auto a = sbox_[static_cast<std::uint8_t>(index)];
        seed = static_cast<std::uint8_t>(seed + a);
        output[index] = static_cast<std::uint8_t>(input[index] ^ sbox_[static_cast<std::uint8_t>(a + sbox_[seed])]);
    }
}
