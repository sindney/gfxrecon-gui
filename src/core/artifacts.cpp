#include "core/artifacts.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace gfx {
std::string Sha256Text(std::string_view value) {
#ifdef _WIN32
    if (value.size() > ULONG_MAX) throw std::runtime_error("Hash input is too large");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open SHA-256 provider");
    unsigned char digest[32];
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("SHA-256 operation failed");
    std::ostringstream text; text << std::hex << std::setfill('0');
    for (auto byte : digest) text << std::setw(2) << unsigned(byte);
    return text.str();
#else
    throw std::runtime_error("Text fingerprinting is currently supported on Windows");
#endif
}
std::string Sha256File(const std::filesystem::path& file, const std::atomic_bool* cancel) {
#ifdef _WIN32
    struct Algorithm { BCRYPT_ALG_HANDLE h = nullptr; ~Algorithm() { if (h) BCryptCloseAlgorithmProvider(h, 0); } } algorithm;
    struct Hash { BCRYPT_HASH_HANDLE h = nullptr; ~Hash() { if (h) BCryptDestroyHash(h); } };
    auto check = [](NTSTATUS code) { if (code < 0) throw std::runtime_error("SHA-256 operation failed"); };
    check(BCryptOpenAlgorithmProvider(&algorithm.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD size = 0, written = 0;
    check(BCryptGetProperty(algorithm.h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &written, 0));
    std::vector<unsigned char> object(size);
    Hash hash;
    check(BCryptCreateHash(algorithm.h, &hash.h, object.data(), size, nullptr, 0, 0));
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot fingerprint " + file.u8string());
    unsigned char buffer[65536];
    while (input.read(reinterpret_cast<char*>(buffer), sizeof(buffer)) || input.gcount()) {
        if (cancel && cancel->load()) throw std::runtime_error("Fingerprint cancelled");
        check(BCryptHashData(hash.h, buffer, static_cast<ULONG>(input.gcount()), 0));
    }
    if (input.bad()) throw std::runtime_error("I/O error fingerprinting " + file.u8string());
    unsigned char digest[32]; check(BCryptFinishHash(hash.h, digest, sizeof(digest), 0));
    std::ostringstream text; text << std::hex << std::setfill('0');
    for (auto byte : digest) text << std::setw(2) << unsigned(byte);
    return text.str();
#else
    throw std::runtime_error("File fingerprinting is currently supported on Windows");
#endif
}
void AtomicJson(const std::filesystem::path& file, const nlohmann::json& value) {
    auto temporary = file;
    temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".partial";
    { std::ofstream output(temporary, std::ios::binary); if (!(output << value.dump(2))) throw std::runtime_error("Cannot write artifact manifest"); }
    std::filesystem::rename(temporary, file);
}
} // namespace gfx
