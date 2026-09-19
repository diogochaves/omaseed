#pragma once

#include <QString>

namespace omaseed::core {

/// The version CMake was configured with, e.g. "0.1.0".
QString version();

/// Version plus the Qt runtime it is linked against, for --version and bug reports.
QString versionLine();

} // namespace omaseed::core
