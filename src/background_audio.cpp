#include "background_audio.h"
#include "log.h"
#include "fingerprint.h"
#include <MinHook.h>
#include <dsound.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

static_assert(DSBCAPS_PRIMARYBUFFER == 0x1 && DSBCAPS_GLOBALFOCUS == 0x8000);

namespace {
using Create8 = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
using CreateBuffer = HRESULT(WINAPI*)(IDirectSound8*, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN);
using DuplicateBuffer = HRESULT(WINAPI*)(IDirectSound8*, LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER*);
using Query = HRESULT(WINAPI*)(void*, REFIID, void**);
using PropertyGet = HRESULT(WINAPI*)(void*, REFGUID, ULONG, void*, ULONG, void*, ULONG, ULONG*);
using PropertySet = HRESULT(WINAPI*)(void*, REFGUID, ULONG, void*, ULONG, void*, ULONG);
using PropertySupport = HRESULT(WINAPI*)(void*, REFGUID, ULONG, ULONG*);
Create8 originalCreate8 = nullptr;
bool addGlobalFocus = false, traceBuffers = false, traceEax = false;
std::atomic<unsigned> tracedBuffers{0}, tracedEax{0};
constexpr unsigned traceLimit = 200, eaxLimit = 512;
std::mutex hookMutex;
// Save originals by vtable SLOT, not function address: linkers may fold Get,
// Set and QuerySupport's unsupported bodies into the same address despite
// their different signatures. Patch slots atomically, keeping object/vtable
// identity and backend-owned reference counts intact. No COM wrappers/refs.
std::map<void**, void*> originals;
constexpr size_t hookLimit = 256;

template<class Fn> Fn original(void* object, size_t slot) {
    const DWORD error = GetLastError();
    auto** entry = *reinterpret_cast<void***>(object) + slot;
    Fn result = nullptr;
    {
        const std::lock_guard<std::mutex> lock(hookMutex);
        const auto found = originals.find(entry);
        if (found != originals.end()) result = reinterpret_cast<Fn>(found->second);
    }
    SetLastError(error);
    return result;
}

void hookMethod(void* object, size_t slot, void* detour) {
    auto** address = *reinterpret_cast<void***>(object) + slot;
    const auto target = *address;
    if (!target) return;
    const std::lock_guard<std::mutex> lock(hookMutex);
    if (originals.count(address) || originals.size() >= hookLimit) return;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(target), &owner)) return;
    // Allocate the saved original before publishing the detour. Another audio
    // thread can enter immediately after the atomic pointer exchange.
    const auto entry = originals.emplace(address, target).first;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(address, &memory, sizeof(memory))) { originals.erase(entry); return; }
    const bool executable = (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    DWORD protection = 0;
    if (!VirtualProtect(address, sizeof(void*), executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &protection)) {
        originals.erase(entry);
        logf("Audio: vtable slot %zu is not writable; original retained", slot);
        return;
    }
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(address), detour);
    DWORD ignored = 0;
    if (!VirtualProtect(address, sizeof(void*), protection, &ignored))
        logLineNoThrow("Audio: vtable page protection restore failed");
}

bool traceLine(std::atomic<unsigned>& count, unsigned limit, const char* label) noexcept {
    unsigned value = count.load();
    while (value <= limit) {
        if (count.compare_exchange_weak(value, value+1)) {
            if (value < limit) return true;
            try { logf("%s trace limit reached (%u calls)", label, limit); } catch (...) {}
            return false;
        }
    }
    return false;
}

std::array<char, 37> guidText(REFGUID guid) noexcept {
    std::array<char, 37> text{};
    std::snprintf(text.data(), text.size(), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
        guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

void propertyTrace(const char* method, void* object, REFGUID guid, ULONG id,
                   ULONG instanceSize, ULONG size, HRESULT result, ULONG output = 0,
                   const void* payload = nullptr) noexcept {
    if (!traceLine(tracedEax, eaxLimit, "EAX")) return;
    try {
        // A bounded fingerprint distinguishes changed room/source packets;
        // do not interpret unknown property layouts or inspect failed outputs.
        unsigned fingerprint = 2166136261u;
        if (SUCCEEDED(result) && payload && size <= 112) {
            const auto* bytes = static_cast<const unsigned char*>(payload);
            for (ULONG i=0; i<size; ++i) fingerprint = (fingerprint ^ bytes[i])*16777619u;
        }
        const auto name = guidText(guid);
        logf("EAX %s object=%p guid=%s property=0x%lx instance=%lu size=%lu hr=0x%08lx output=%lu payload=%08x",
             method, object, name.data(), id, instanceSize, size, static_cast<unsigned long>(result), output, fingerprint);
        // Selected effect/source slot GUIDs, without assuming a reverb preset.
        if (SUCCEEDED(result) && payload && ((id == 0x10001 && size == 28) || (id == 0x1b && size == 32))) {
            GUID slot{};
            std::memcpy(&slot, static_cast<const unsigned char*>(payload)+(id == 0x1b ? 16 : 0), sizeof(slot));
            logf("EAX packet object=%p effect/slot=%s", object, guidText(slot).data());
        }
    } catch (...) { logLineNoThrow("EAX trace unavailable; original result preserved"); }
}

void hookInterface(void* object, REFIID iid) noexcept;
HRESULT WINAPI queryDetour(void* object, REFIID iid, void** out) noexcept {
    const auto fn = original<Query>(object, 0);
    const HRESULT result = fn ? fn(object, iid, out) : E_NOINTERFACE;
    const DWORD error = GetLastError();
    if (traceEax) {
        propertyTrace("QueryInterface", object, iid, 0, 0, 0, result);
        if (SUCCEEDED(result) && out && *out) hookInterface(*out, iid);
    }
    SetLastError(error);
    return result;
}
HRESULT WINAPI getDetour(void* object, REFGUID guid, ULONG id, void* instance,
                         ULONG instanceSize, void* data, ULONG size, ULONG* returned) noexcept {
    const auto fn = original<PropertyGet>(object, 3);
    const HRESULT result = fn ? fn(object, guid, id, instance, instanceSize, data, size, returned) : E_FAIL;
    const DWORD error = GetLastError();
    propertyTrace("Get", object, guid, id, instanceSize, size, result,
                  SUCCEEDED(result) && returned ? *returned : 0);
    SetLastError(error);
    return result;
}
HRESULT WINAPI setDetour(void* object, REFGUID guid, ULONG id, void* instance,
                         ULONG instanceSize, void* data, ULONG size) noexcept {
    const auto fn = original<PropertySet>(object, 4);
    const HRESULT result = fn ? fn(object, guid, id, instance, instanceSize, data, size) : E_FAIL;
    const DWORD error = GetLastError();
    propertyTrace("Set", object, guid, id, instanceSize, size, result, 0, data);
    SetLastError(error);
    return result;
}
HRESULT WINAPI supportDetour(void* object, REFGUID guid, ULONG id, ULONG* support) noexcept {
    const auto fn = original<PropertySupport>(object, 5);
    const HRESULT result = fn ? fn(object, guid, id, support) : E_FAIL;
    const DWORD error = GetLastError();
    propertyTrace("QuerySupport", object, guid, id, 0, 0, result, SUCCEEDED(result) && support ? *support : 0);
    SetLastError(error);
    return result;
}

void traceDescriptor(const DSBUFFERDESC& descriptor, DWORD flags) noexcept {
    if (!traceLine(tracedBuffers, traceLimit, "Audio")) return;
    try {
        const bool primary = (descriptor.dwFlags & DSBCAPS_PRIMARYBUFFER) != 0;
        const WAVEFORMATEX* format = primary ? nullptr : descriptor.lpwfxFormat;
        logf("audio buffer: flags=0x%lx->0x%lx size=%lu primary=%d channels=%u rate=%lu bits=%u",
            descriptor.dwFlags, flags, descriptor.dwBufferBytes, primary ? 1 : 0,
            format ? format->nChannels : 0, format ? format->nSamplesPerSec : 0, format ? format->wBitsPerSample : 0);
    } catch (...) { logLineNoThrow("Audio descriptor trace unavailable"); }
}

HRESULT WINAPI createBufferDetour(IDirectSound8* device, LPCDSBUFFERDESC descriptor,
                                   LPDIRECTSOUNDBUFFER* buffer, LPUNKNOWN outer) noexcept {
    const DWORD incomingError = GetLastError();
    const auto fn = original<CreateBuffer>(device, 3);
    DSBUFFERDESC adjusted{};
    auto forwarded = descriptor;
    if (descriptor && descriptor->dwSize >= sizeof(DSBUFFERDESC)) {
        const DWORD flags = adjustBufferFlags(descriptor->dwFlags, addGlobalFocus);
        if (traceBuffers) traceDescriptor(*descriptor, flags);
        if (flags != descriptor->dwFlags) {
            adjusted = *descriptor;
            adjusted.dwFlags = flags;
            forwarded = &adjusted;
        }
    }
    SetLastError(incomingError);
    const HRESULT result = fn ? fn(device, forwarded, buffer, outer) : E_FAIL;
    const DWORD error = GetLastError();
    if (traceEax && SUCCEEDED(result) && buffer && *buffer) hookInterface(*buffer, IID_IDirectSoundBuffer);
    SetLastError(error);
    return result;
}
HRESULT WINAPI duplicateDetour(IDirectSound8* device, LPDIRECTSOUNDBUFFER source, LPDIRECTSOUNDBUFFER* out) noexcept {
    const auto fn = original<DuplicateBuffer>(device, 5);
    const HRESULT result = fn ? fn(device, source, out) : E_FAIL;
    const DWORD error = GetLastError();
    if (traceEax && SUCCEEDED(result) && out && *out) hookInterface(*out, IID_IDirectSoundBuffer);
    SetLastError(error);
    return result;
}

void hookInterface(void* object, REFIID iid) noexcept {
    if (!object) return;
    try {
        if (iid == IID_IDirectSound8 || iid == IID_IDirectSound) {
            hookMethod(object, 3, reinterpret_cast<void*>(&createBufferDetour));
            if (traceEax) hookMethod(object, 5, reinterpret_cast<void*>(&duplicateDetour));
        }
        if (!traceEax) return;
        if (iid == IID_IKsPropertySet) {
            hookMethod(object, 3, reinterpret_cast<void*>(&getDetour));
            hookMethod(object, 4, reinterpret_cast<void*>(&setDetour));
            hookMethod(object, 5, reinterpret_cast<void*>(&supportDetour));
        }
        if (iid == IID_IUnknown || iid == IID_IKsPropertySet || iid == IID_IDirectSoundBuffer ||
            iid == IID_IDirectSoundBuffer8 || iid == IID_IDirectSound3DBuffer || iid == IID_IDirectSound8 || iid == IID_IDirectSound)
            hookMethod(object, 0, reinterpret_cast<void*>(&queryDetour));
    } catch (...) { logLineNoThrow("Audio: interface hook unavailable; original COM object preserved"); }
}

HRESULT WINAPI create8Detour(LPCGUID guid, LPDIRECTSOUND8* device, LPUNKNOWN outer) noexcept {
    const HRESULT result = originalCreate8(guid, device, outer);
    const DWORD error = GetLastError();
    if (traceEax) propertyTrace("DirectSoundCreate8", SUCCEEDED(result) && device ? *device : nullptr, GUID_NULL, 0, 0, 0, result);
    if (SUCCEEDED(result) && device && *device) hookInterface(*device, IID_IDirectSound8);
    SetLastError(error);
    return result;
}

void traceBackend(HMODULE module) {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) { logLine("Audio backend: path unavailable"); return; }
    const auto filename = std::filesystem::path(std::wstring(path.data(), length));
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.data(), &ignored);
    std::vector<unsigned char> info(size);
    VS_FIXEDFILEINFO* version = nullptr;
    UINT versionSize = 0;
    bool dsoal = false;
    unsigned major=0, minor=0, patch=0, build=0;
    if (size && GetFileVersionInfoW(path.data(), 0, size, info.data())) {
        if (VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&version), &versionSize) && versionSize >= sizeof(*version)) {
            major=HIWORD(version->dwFileVersionMS); minor=LOWORD(version->dwFileVersionMS);
            patch=HIWORD(version->dwFileVersionLS); build=LOWORD(version->dwFileVersionLS);
        }
        // Match the product identity in version resources, not the filename.
        for (size_t i=0; i+10<=info.size(); i+=2) {
            const wchar_t* text = reinterpret_cast<const wchar_t*>(info.data()+i);
            if (_wcsnicmp(text, L"DSOAL", 5)==0) { dsoal=true; break; }
        }
    }
    const auto hash = sha256File(filename);
    const bool pinned = hash == BIGFIX_DSOAL_SHA256;
    logf("Audio backend: path=%s version=%u.%u.%u.%u DSOAL=%s sha256=%s",
         filename.u8string().c_str(), major, minor, patch, build,
         pinned ? "1 (pinned)" : dsoal ? "1 (resource)" : "unconfirmed", hash.c_str());
}
}

DWORD adjustBufferFlags(DWORD flags, bool enable) {
    return !enable || (flags & DSBCAPS_PRIMARYBUFFER) ? flags : flags | DSBCAPS_GLOBALFOCUS;
}

void installBackgroundAudio(HMODULE dsound, const Settings& settings) {
    if (!dsound) return;
    addGlobalFocus=settings.backgroundAudio; traceBuffers=settings.traceAudio; traceEax=settings.traceEax;
    if (traceEax) {
        try { traceBackend(dsound); }
        catch (...) { logLineNoThrow("Audio backend: identity unavailable; audio hooks continue"); }
    }
    if (!addGlobalFocus && !traceBuffers && !traceEax) {
        logLine("Audio: BackgroundAudio=0 TraceAudio=0 TraceEax=0; no DirectSound hooks");
        return;
    }
    const auto target = reinterpret_cast<void*>(GetProcAddress(dsound, "DirectSoundCreate8"));
    if (!target) { logLine("Audio: no DirectSoundCreate8 export"); return; }
    HMODULE held = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(target), &held)) {
        logLine("Audio: cannot retain factory module; hooks skipped");
        return;
    }
    const auto status = MH_CreateHook(target, reinterpret_cast<void*>(&create8Detour), reinterpret_cast<void**>(&originalCreate8));
    if (status != MH_OK) { logf("Audio: factory hook unavailable: %s", MH_StatusToString(status)); return; }
    // Existing module watch enables the factory; later COM hooks enable themselves.
    logf("Audio: BackgroundAudio=%d TraceAudio=%d TraceEax=%d; DirectSoundCreate8 hooked",
         addGlobalFocus, traceBuffers, traceEax);
}
