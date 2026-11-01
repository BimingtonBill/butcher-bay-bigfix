// Data-only harness image: game DLLs are never loaded by the tests. The test
// writes artificial function bodies at the documented RVAs inside this array.
#include <windows.h>
extern "C" __declspec(dllexport) unsigned char bigfixVideoImage[0x210000] = {1};
