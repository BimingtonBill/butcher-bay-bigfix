// Exercise the production IAT installer with a small, real live patch in this
// executable instead of writing game code. Only fingerprint input and the live
// patch address are substituted; PE walking, byte checks and writes stay real.
#include "fingerprint.h"
#include "patch_table.h"
#include <algorithm>
#include <stdexcept>

namespace {
uint8_t selectionBytes[12]{};
std::wstring fixtureScenario;
std::vector<uint8_t> originalBytes;
bool threwAfterWrite = false;
PatchSite fixtureSite(const PatchSite& site) {
    auto mapped = site;
    mapped.rva = fixtureScenario == L"address" ? UINT32_MAX : static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(selectionBytes) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
    return mapped;
}
PatchResult fixtureApply(HMODULE, const PatchSite& site) {
    const auto result = applyPatchLive(GetModuleHandleW(nullptr), fixtureSite(site));
    if (fixtureScenario == L"write_failure" && !threwAfterWrite && result == PatchResult::Applied) {
        threwAfterWrite = true;
        throw std::runtime_error("simulated cache/protection failure after write");
    }
    return result;
}
}
void configureWindowPolicyFixture(std::wstring_view scenario) {
    fixtureScenario = scenario;
    const auto& site = borderlessModeSelectionSite();
    std::copy(site.expected.begin(), site.expected.end(), selectionBytes);
    if (scenario == L"bytes") selectionBytes[0] ^= 1;
    originalBytes.assign(std::begin(selectionBytes), std::end(selectionBytes));
}
bool windowPolicyFixtureMatches(bool applied) {
    const auto& expected = applied ? borderlessModeSelectionSite().replacement : originalBytes;
    return std::equal(expected.begin(), expected.end(), selectionBytes);
}
#define applyPatchLive fixtureApply
#include "../../src/window_mode.cpp"
#undef applyPatchLive
