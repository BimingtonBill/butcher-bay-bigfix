#define BIGFIX_BUILD_MODE_STUB
#include "mode_stub.h"
namespace { int last[4]{}; }
int CDisplayContext::ModeList_Find(int w, int h, int f, int hz) {
    last[0] = w; last[1] = h; last[2] = f; last[3] = hz;
    return 7;
}
extern "C" MODE_API void bigfixReadModeRequest(int* values) {
    for (int i = 0; i < 4; ++i) values[i] = last[i];
}
