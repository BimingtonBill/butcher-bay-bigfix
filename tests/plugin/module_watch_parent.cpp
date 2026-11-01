extern "C" __declspec(dllimport) int bigfixDependencyValue();
extern "C" __declspec(dllexport) int bigfixParentValue() { return bigfixDependencyValue(); }
