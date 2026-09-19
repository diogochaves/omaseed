#pragma once

#include <QString>

namespace omaseed::core::paths {

/// `$XDG_CONFIG_HOME`, or `~/.config` when unset, matching the XDG base directory spec.
/// Tests point `XDG_CONFIG_HOME` at a `QTemporaryDir` before constructing a store.
QString configHome();

/// `<configHome>/omaseed`, created on demand by callers that write into it.
QString configDir();

/// `<configDir>/secrets`, created on demand by `SecretStore`'s file backend.
QString secretsDir();

} // namespace omaseed::core::paths
