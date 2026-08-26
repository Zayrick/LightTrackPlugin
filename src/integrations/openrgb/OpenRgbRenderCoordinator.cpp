#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

namespace lighttrack::openrgb
{
std::mutex& ControllerFrameMutex()
{
    static std::mutex mutex;
    return mutex;
}
}
