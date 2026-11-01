#pragma once
#ifdef BIGFIX_BUILD_MODE_STUB
#define MODE_API __declspec(dllexport)
#else
#define MODE_API __declspec(dllimport)
#endif
class CDisplayContext {
public:
    MODE_API virtual int ModeList_Find(int width, int height, int format, int hz);
};
extern "C" MODE_API void bigfixReadModeRequest(int* values);
