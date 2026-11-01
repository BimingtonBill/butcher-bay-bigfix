#include "fingerprint.h"
// This executable substitutes only the file fingerprint input. The production
// installer still checks module bounds/live instructions and uses real MinHook.
bool eyeshineScaleFingerprint=true;
std::string sha256Module(HMODULE) {
    return eyeshineScaleFingerprint ? "749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7":"mismatch";
}
