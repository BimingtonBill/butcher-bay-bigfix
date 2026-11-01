// Texture-size patch data derived from TCoRFix (Nezarn, MIT).
#include "patch_table.h"
#include <psapi.h>
#include <algorithm>
#include <mutex>
#include <system_error>

const PatchSite& skipLogoVideosSite() {
    // EU 1.1 GameWorld frontend init, sole reference at +0x470AA. This default
    // command runs only when QUICKMAP/QUICKMENU are empty. The GUI's "intro"
    // root leads through static splash screens and five logos; "main" is the
    // Press ENTER title. Story videos use separate windows/decoder paths.
    static const PatchSite site = [] {
        const char before[] = "cg_rootmenu \"intro\"";
        const char after[] = "cg_rootmenu \"main\"";
        PatchSite value{"skip_startup_logos", L"GameWorld_Win64_AMD64.dll", 0x1e80e0,
                        {before, before + sizeof(before)}, {after, after + sizeof(after)}};
        value.replacement.resize(value.expected.size(), 0);
        return value;
    }();
    return site;
}

const PatchSite& borderlessModeSelectionSite() {
    // EU 1.1: DC_InitVidModeFromEnvironment compares VID_MODE with "desktop".
    // Always take the numeric route so the renderer's imported ModeList_Find
    // sees the request, including after saved VIDEO_DISPLAY_* options rewrite it.
    // Context: TEST EAX,EAX; JNE numeric; MOV EBX,R15D; JMP apply_mode_zero.
    static const PatchSite site{"borderless_numeric_selection", L"MSystem.dll", 0x77885,
        {0x85, 0xc0, 0x75, 0x08, 0x41, 0x8b, 0xdf, 0xe9, 0xb2, 0x01, 0x00, 0x00},
        {0x85, 0xc0, 0xeb, 0x08, 0x41, 0x8b, 0xdf, 0xe9, 0xb2, 0x01, 0x00, 0x00}};
    return site;
}

const PatchSite& windowedModeSelectionSite() {
    static const PatchSite site = [] {
        auto value = borderlessModeSelectionSite();
        value.name = "original_windowed_selection";
        value.replacement = value.expected;
        // Always fall through into the original synthetic/windowed mode-0 path.
        value.replacement[2] = 0x90;
        value.replacement[3] = 0x90;
        return value;
    }();
    return site;
}

const std::vector<PatchSite>& sitesFor(Build) {
    // Four bytes of context precede the conditional branch.
    static const std::vector<PatchSite> sites{
        {"texture_size_clamp", L"RndrGL.dll", 0x84b3,
         {0x00, 0x08, 0x00, 0x00, 0x7e, 0x0a},
         {0x00, 0x08, 0x00, 0x00, 0xeb, 0x0a}}
    };
    return sites;
}

namespace {
bool validRange(size_t size, size_t offset, size_t count) {
    return offset <= size && count <= size - offset;
}
bool validSite(size_t size, const PatchSite& site) {
    return !site.expected.empty() && site.expected.size() == site.replacement.size() &&
           validRange(size, site.rva, site.expected.size());
}
PatchResult verifyPatch(const uint8_t* base, const PatchSite& site) {
    const auto* address = base + site.rva;
    if (std::equal(site.replacement.begin(), site.replacement.end(), address)) return PatchResult::AlreadyApplied;
    if (std::equal(site.expected.begin(), site.expected.end(), address)) return PatchResult::Applied;
    return PatchResult::Mismatch;
}
uint16_t read16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}

PatchResult applyPatch(uint8_t* base, size_t size, const PatchSite& site) {
    if (!base || !validSite(size, site)) return PatchResult::BadAddress;
    const auto result = verifyPatch(base, site);
    if (result == PatchResult::Applied)
        std::copy(site.replacement.begin(), site.replacement.end(), base + site.rva);
    return result;
}

PatchResult applyPatchLive(HMODULE module, const PatchSite& site) {
    // Serialize our own patch calls. The caller must keep the module loaded and
    // arrange startup patching before another thread executes these instructions.
    static std::mutex patchMutex;
    const std::lock_guard<std::mutex> lock(patchMutex);
    MODULEINFO info{};
    if (!module || !K32GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) ||
        !validSite(info.SizeOfImage, site)) return PatchResult::BadAddress;
    auto* base = static_cast<uint8_t*>(info.lpBaseOfDll);
    auto* address = base + site.rva;
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return PatchResult::BadAddress;
    const auto offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(region.BaseAddress);
    // Refuse a patch crossing protection regions: one saved protection would not
    // be enough to restore both safely.
    if (!validRange(region.RegionSize, offset, site.expected.size())) return PatchResult::BadAddress;
    const auto verified = verifyPatch(base, site);
    if (verified != PatchResult::Applied) return verified;
    DWORD oldProtection = 0;
    if (!VirtualProtect(address, site.expected.size(), PAGE_EXECUTE_READWRITE, &oldProtection))
        return PatchResult::BadAddress;
    const auto result = applyPatch(base, info.SizeOfImage, site);
    DWORD ignored = 0;
    if (!VirtualProtect(address, site.expected.size(), oldProtection, &ignored))
        throw std::system_error(GetLastError(), std::system_category(), "restore patch protection");
    if (result == PatchResult::Applied && !FlushInstructionCache(GetCurrentProcess(), address, site.expected.size()))
        throw std::system_error(GetLastError(), std::system_category(), "FlushInstructionCache");
    return result;
}

std::optional<size_t> rvaToFileOffset(const uint8_t* pe, size_t size, uint32_t rva) {
    if (!pe || size < 64 || read16(pe) != 0x5a4d) return {};
    const size_t nt = read32(pe + 0x3c);
    if (nt < 64 || !validRange(size, nt, 24) || read32(pe + nt) != 0x00004550) return {};
    const size_t sectionCount = read16(pe + nt + 6);
    const size_t optionalSize = read16(pe + nt + 20);
    const size_t optional = nt + 24;
    if (optionalSize < 64 || !validRange(size, optional, optionalSize)) return {};
    const auto magic = read16(pe + optional);
    if (magic != 0x10b && magic != 0x20b) return {};
    const size_t sections = optional + optionalSize;
    if (!validRange(size, sections, sectionCount * 40)) return {};
    const size_t headers = read32(pe + optional + 60);
    if (headers > size || headers < sections + sectionCount * 40) return {};
    if (rva < headers) return size_t(rva);
    for (size_t i = 0; i < sectionCount; ++i) {
        const auto* section = pe + sections + i * 40;
        const uint32_t virtualAddress = read32(section + 12);
        const size_t rawSize = read32(section + 16);
        const size_t rawOffset = read32(section + 20);
        if (rva < virtualAddress || size_t(rva - virtualAddress) >= rawSize) continue;
        if (rawOffset < headers || !validRange(size, rawOffset, rawSize)) return {};
        return rawOffset + (rva - virtualAddress);
    }
    return {};
}

const PatchSite& forceEyeshineSite() {
    // EU 1.1 GameClasses togglenightvision handler (+0x1CD061): `bt [r12+258h],14h`
    // then `jb` past the toggle when the player lacks Eyeshine. Test-only: NOP the
    // jb so Eyeshine works in early scenes. Cutscene state (cmp eax,3) still gates.
    static const PatchSite site{"force_eyeshine", L"GameClasses_Win64_AMD64.dll", 0x1cd06b,
                                {0x0f, 0x82, 0x62, 0x02, 0x00, 0x00},
                                {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00}};
    return site;
}
