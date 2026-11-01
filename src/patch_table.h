#pragma once

#include "fingerprint.h"
#include <optional>
#include <vector>

struct PatchSite {
    const char* name;
    const wchar_t* module;
    uint32_t rva;
    std::vector<uint8_t> expected;
    std::vector<uint8_t> replacement;
};

enum class PatchResult { Applied, AlreadyApplied, Mismatch, BadAddress };

const std::vector<PatchSite>& sitesFor(Build build);
const PatchSite& borderlessModeSelectionSite();
const PatchSite& windowedModeSelectionSite();
const PatchSite& skipLogoVideosSite();
const PatchSite& forceEyeshineSite();
PatchResult applyPatch(uint8_t* imageBase, size_t imageSize, const PatchSite& site);
// Caller holds the module loaded. Protection/cache failures throw.
PatchResult applyPatchLive(HMODULE module, const PatchSite& site);
// Raw file bytes, not a loaded image. Returns no offset for unbacked/malformed RVAs.
std::optional<size_t> rvaToFileOffset(const uint8_t* pe, size_t size, uint32_t rva);
