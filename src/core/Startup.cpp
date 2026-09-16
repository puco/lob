#include "Startup.h"

namespace Lob
{

QElapsedTimer &startupTimer()
{
    static QElapsedTimer timer;
    return timer;
}

} // namespace Lob
