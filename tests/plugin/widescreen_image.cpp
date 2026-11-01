// Artificial PE address space only. No game code is loaded or executed.
#include <windows.h>
extern "C" __declspec(dllexport) unsigned char widescreenImage[0x300000]={1};
