// Correct COM slot layouts, distinct implementations and owned lifetimes.
// No audio stack or game code; unused interface slots remain null.
#include <windows.h>
#include <dsound.h>
#include "eax_fixture.h"
#include <cstring>
#include <atomic>

namespace {
DWORD lastFlags=0, lastBytes=0;
WAVEFORMATEX lastFormat{};
unsigned createCalls=0, mode=0, live=0, roomMask=0;
std::atomic<unsigned> sets{0}, gets{0}, queries{0};
GUID selectedSlot{};
constexpr unsigned noQi=1, getOnly=2, setOnly=4, failSlots=8, failFirst=16,
    wrongEffect=32, lowCapacity=64, factoryFail=128, factoryNonzero=256, getFail=512;
struct Buffer;
struct Interface { void** vtable; Buffer* owner; };
struct Buffer { void** vtable; ULONG refs=1; unsigned variant; Interface prop{}, spatial{}; eax::Room room{}; GUID selected{}; };
struct Device { void** vtable; ULONG refs=1; unsigned variant; };
ULONG WINAPI addBuffer(Buffer* b) { return ++b->refs; }
ULONG WINAPI releaseBuffer(Buffer* b) { const auto refs=--b->refs; if (!refs) {--live; delete b;} return refs; }
ULONG WINAPI addInterface(Interface* p) { return addBuffer(p->owner); }
ULONG WINAPI releaseInterface(Interface* p) { return releaseBuffer(p->owner); }
HRESULT WINAPI queryBuffer(Buffer* b, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out=nullptr;
    if (iid==IID_IUnknown || iid==IID_IDirectSoundBuffer || iid==IID_IDirectSoundBuffer8) *out=b;
    else if (iid==IID_IKsPropertySet && !(mode & noQi)) *out=&b->prop;
    else if (iid==IID_IDirectSound3DBuffer) *out=&b->spatial;
    else return E_NOINTERFACE;
    addBuffer(b); return S_OK;
}
HRESULT WINAPI queryInterface(Interface* p, REFIID iid, void** out) { return queryBuffer(p->owner,iid,out); }
int slotIndex(REFGUID guid) { for (int i=0;i<4;++i) if (guid==eax::slots[i]) return i; return -1; }
bool supported(REFGUID guid, ULONG id) {
    return (guid==eax::context && id==1) || (guid==eax::source && (id==1 || id==0x1b)) ||
        (slotIndex(guid)>=0 && (id==0x10001 || id==0x10002 || id==1));
}
template<unsigned Variant> HRESULT WINAPI querySupport(Interface*, REFGUID guid, ULONG id, ULONG* support) {
    ++queries; SetLastError(0x901+Variant);
    if (!support) return E_POINTER;
    *support=0;
    if (!supported(guid,id)) return eax::unsupported;
    *support=(mode & getOnly) ? 1 : (mode & setOnly) ? 2 : 3;
    return S_OK;
}
template<unsigned Variant> HRESULT WINAPI get(Interface*, REFGUID guid, ULONG id,
        void* instance, ULONG instanceSize, void* data, ULONG size, ULONG* returned) {
    ++gets; SetLastError(0xa01+Variant);
    if (mode & getFail) return E_FAIL;
    if (instance || instanceSize || slotIndex(guid)<0 || id!=0x10002) return eax::unsupported;
    if (!data || size!=16 || !returned) return E_INVALIDARG;
    GUID effect=(mode & wrongEffect) ? GUID_NULL : eax::reverb;
    std::memcpy(data,&effect,16); *returned=16;
    if(effect==eax::reverb) selectedSlot=guid;
    return S_OK;
}
template<unsigned Variant> HRESULT WINAPI set(Interface* p, REFGUID guid, ULONG id,
        void* instance, ULONG instanceSize, void* data, ULONG size) {
    ++sets; SetLastError(0xb01+Variant);
    if (instance || instanceSize || !supported(guid,id)) return eax::unsupported;
    if (!data) return E_POINTER;
    const int slot=slotIndex(guid);
    if (slot>=0 && id==0x10001) {
        if (size!=28) return E_INVALIDARG;
        const auto& packet=*static_cast<eax::Slot*>(data);
        if (packet.effect!=eax::reverb || packet.volume!=0 || packet.lock!=1 || packet.flags!=0) return E_INVALIDARG;
        if ((mode & failSlots) || ((mode & failFirst) && slot==0)) return E_FAIL;
        p->owner->selected=guid; selectedSlot=guid; return S_OK;
    }
    if (slot>=0 && id==1) {
        if (size!=112) return E_INVALIDARG;
        if(guid!=selectedSlot) return E_INVALIDARG;
        p->owner->room=*static_cast<eax::Room*>(data);
        const auto& room=p->owner->room;
        if(room.decay==1.49f && room.room==-1000) roomMask|=1;
        if(room.decay==3.2f && room.room==-500) roomMask|=2;
        return S_OK;
    }
    if (guid==eax::source && id==0x1b) {
        if (size!=32) return E_INVALIDARG;
        const auto& sends=*static_cast<eax::Sends*>(data);
        return sends.dry==GUID_NULL && sends.wet==selectedSlot && slotIndex(sends.wet)>=0 ? S_OK : E_INVALIDARG;
    }
    return eax::unsupported;
}
void* propertyVtables[2][6] = {
    {reinterpret_cast<void*>(&queryInterface),reinterpret_cast<void*>(&addInterface),reinterpret_cast<void*>(&releaseInterface),reinterpret_cast<void*>(&get<0>),reinterpret_cast<void*>(&set<0>),reinterpret_cast<void*>(&querySupport<0>)},
    {reinterpret_cast<void*>(&queryInterface),reinterpret_cast<void*>(&addInterface),reinterpret_cast<void*>(&releaseInterface),reinterpret_cast<void*>(&get<1>),reinterpret_cast<void*>(&set<1>),reinterpret_cast<void*>(&querySupport<1>)}};
// Linkers may fold unsupported methods of different signatures into one
// address. Extra x64 ABI arguments are ignored by this identical return body.
HRESULT WINAPI notImplementedProperty(void*) { return E_NOTIMPL; }
void* foldedPropertyVtable[6] = {
    reinterpret_cast<void*>(&queryInterface),reinterpret_cast<void*>(&addInterface),reinterpret_cast<void*>(&releaseInterface),
    reinterpret_cast<void*>(&notImplementedProperty),reinterpret_cast<void*>(&notImplementedProperty),reinterpret_cast<void*>(&notImplementedProperty)};
void* spatialVtable[21] = {reinterpret_cast<void*>(&queryInterface),reinterpret_cast<void*>(&addInterface),reinterpret_cast<void*>(&releaseInterface)};
void* bufferVtable[24] = {reinterpret_cast<void*>(&queryBuffer),reinterpret_cast<void*>(&addBuffer),reinterpret_cast<void*>(&releaseBuffer)};
Buffer* newBuffer(unsigned variant) {
    auto* b=new Buffer{bufferVtable,1,variant};
    b->prop={(mode & 1024) ? foldedPropertyVtable : propertyVtables[variant],b}; b->spatial={spatialVtable,b}; ++live; return b;
}
template<unsigned Variant> HRESULT WINAPI createBuffer(Device*, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* out, LPUNKNOWN) {
    ++createCalls;
    lastFlags=desc ? desc->dwFlags : 0; lastBytes=desc ? desc->dwBufferBytes : 0;
    lastFormat=desc && desc->lpwfxFormat ? *desc->lpwfxFormat : WAVEFORMATEX{};
    if (!out || !desc || desc->dwSize!=sizeof(*desc)) return DSERR_INVALIDPARAM;
    *out=nullptr;
    if ((mode & lowCapacity) && live>=2) return DSERR_ALLOCATED;
    *out=reinterpret_cast<LPDIRECTSOUNDBUFFER>(newBuffer(Variant)); return S_OK;
}
HRESULT WINAPI duplicate(Device* d, LPDIRECTSOUNDBUFFER source, LPDIRECTSOUNDBUFFER* out) {
    if (!source || !out) return E_POINTER;
    *out=reinterpret_cast<LPDIRECTSOUNDBUFFER>(newBuffer(d->variant)); return S_OK;
}
ULONG WINAPI addDevice(Device* d) { return ++d->refs; }
ULONG WINAPI releaseDevice(Device* d) { const auto refs=--d->refs; if (!refs) delete d; return refs; }
HRESULT WINAPI queryDevice(Device* d, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out=nullptr;
    if (iid!=IID_IUnknown && iid!=IID_IDirectSound && iid!=IID_IDirectSound8) return E_NOINTERFACE;
    *out=d; addDevice(d); return S_OK;
}
void* deviceVtables[2][12] = {
    {reinterpret_cast<void*>(&queryDevice),reinterpret_cast<void*>(&addDevice),reinterpret_cast<void*>(&releaseDevice),reinterpret_cast<void*>(&createBuffer<0>),nullptr,reinterpret_cast<void*>(&duplicate)},
    {reinterpret_cast<void*>(&queryDevice),reinterpret_cast<void*>(&addDevice),reinterpret_cast<void*>(&releaseDevice),reinterpret_cast<void*>(&createBuffer<1>),nullptr,reinterpret_cast<void*>(&duplicate)}};
unsigned nextDevice=0;
}
extern "C" {
HRESULT WINAPI stubDirectSoundCreate8(LPCGUID, LPDIRECTSOUND8* out, LPUNKNOWN) {
    if (!out) return DSERR_INVALIDPARAM;
    *out=nullptr;
    if (mode & factoryFail) return E_FAIL;
    const auto variant=nextDevice++%2;
    *out=reinterpret_cast<LPDIRECTSOUND8>(new Device{deviceVtables[variant],1,variant});
    return mode & factoryNonzero ? S_FALSE : S_OK;
}
DWORD stubLastFlags() { return lastFlags; }
DWORD stubLastBytes() { return lastBytes; }
unsigned stubCreateCalls() { return createCalls; }
unsigned stubLastChannels() { return lastFormat.nChannels; }
unsigned long stubLastRate() { return lastFormat.nSamplesPerSec; }
unsigned stubLastBits() { return lastFormat.wBitsPerSample; }
void stubConfigure(unsigned value) { mode=value; selectedSlot=GUID_NULL; }
unsigned stubLiveBuffers() { return live; }
unsigned stubSets() { return sets; }
unsigned stubGets() { return gets; }
unsigned stubQueries() { return queries; }
unsigned stubRoomMask() { return roomMask; }
}
