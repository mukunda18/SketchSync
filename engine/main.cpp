#include "engine/engine.h"

#ifdef _WIN32
#include <windows.h>
#include <iostream>
#endif

int main()
{
#ifdef _WIN32
    if (GetConsoleWindow() == nullptr)
        AllocConsole();
    std::cout << "SketchSync engine started.\n";
#endif
    sketch_app app;
    return app.run();
}
