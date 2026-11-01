#include "fingerprint.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace {
void checkStatus(NTSTATUS status, const char* operation) {
    if (status < 0)
        throw std::runtime_error(std::string(operation) + " failed, NTSTATUS=" + std::to_string(status));
}

struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~Hash() { if (handle) BCryptDestroyHash(handle); }
};

class Sha256 {
public:
    Sha256() {
        checkStatus(BCryptOpenAlgorithmProvider(&algorithm_.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
                    "BCryptOpenAlgorithmProvider");
        // Windows 7+ allocates the hash object when pbHashObject is null.
        checkStatus(BCryptCreateHash(algorithm_.handle, &hash_.handle, nullptr, 0, nullptr, 0, 0),
                    "BCryptCreateHash");
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(const uint8_t* data, size_t size) {
        while (size) {
            const auto chunk = static_cast<ULONG>((std::min)(size, size_t((std::numeric_limits<ULONG>::max)())));
            checkStatus(BCryptHashData(hash_.handle, const_cast<PUCHAR>(data), chunk, 0), "BCryptHashData");
            data += chunk;
            size -= chunk;
        }
    }
    std::string finish() {
        std::array<uint8_t, 32> digest{};
        checkStatus(BCryptFinishHash(hash_.handle, digest.data(), static_cast<ULONG>(digest.size()), 0),
                    "BCryptFinishHash");
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (auto byte : digest) {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        }
        return result;
    }
private:
    Algorithm algorithm_;
    Hash hash_; // destroyed before the algorithm provider
};
}

std::string sha256Buffer(const uint8_t* data, size_t size) {
    if (!data && size) throw std::invalid_argument("null SHA-256 buffer");
    Sha256 hash;
    hash.update(data, size);
    return hash.finish();
}

std::string sha256File(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open file for SHA-256");
    Sha256 hash;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount())
        hash.update(buffer.data(), static_cast<size_t>(file.gcount()));
    if (file.bad() || !file.eof()) throw std::runtime_error("cannot read file for SHA-256");
    return hash.finish();
}

std::string sha256Module(HMODULE module) {
    if (!module) throw std::invalid_argument("null module for SHA-256");
    std::vector<wchar_t> path(256);
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length) throw std::system_error(GetLastError(), std::system_category(), "GetModuleFileNameW");
        if (length < path.size()) return sha256File(std::filesystem::path(std::wstring(path.data(), length)));
        if (path.size() >= 32768) throw std::runtime_error("module path exceeds Windows path limit");
        path.resize((std::min)(path.size() * 2, size_t(32768)));
    }
}

Build identify(std::string_view hash) {
    return hash == "400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348"
        ? Build::EU11_x64 : Build::Unknown;
}
