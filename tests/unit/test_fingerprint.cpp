#include <doctest/doctest.h>
#include "fingerprint.h"

TEST_CASE("sha256_buffer_known_vectors") {
    CHECK(sha256Buffer(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const uint8_t abc[] = {'a', 'b', 'c'};
    CHECK(sha256Buffer(abc, sizeof(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_THROWS(sha256Buffer(nullptr, 1));
}
TEST_CASE("fingerprints_accept_only_known_build") {
    CHECK(identify("400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348") == Build::EU11_x64);
    CHECK(identify("") == Build::Unknown);
    CHECK(identify(std::string(64, '0')) == Build::Unknown);
}
TEST_CASE("module_hash_uses_on_disk_image_and_errors_are_explicit") {
    wchar_t path[32768]{};
    REQUIRE(GetModuleFileNameW(nullptr, path, 32768) > 0);
    const auto hash = sha256File(path);
    CHECK(hash.size() == 64);
    CHECK(sha256Module(GetModuleHandleW(nullptr)) == hash);
    CHECK_THROWS(sha256Module(nullptr));
    CHECK_THROWS(sha256Module(reinterpret_cast<HMODULE>(1)));
    CHECK_THROWS(sha256File(std::filesystem::path(path) / L"nonexistent"));
}
