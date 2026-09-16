#pragma once

#include <QElapsedTimer>

namespace Lob
{

/// Started at the top of main(). Lets latency be reported from process start
/// rather than from the moment the picker happens to be asked to show, which
/// is the difference between measuring the daemon's benefit and hiding it.
QElapsedTimer &startupTimer();

} // namespace Lob
