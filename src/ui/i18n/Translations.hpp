#pragma once

// The app's translations (docs/DECISIONS.md C41, #197): Qt Linguist's .ts files under
// translations/, compiled to .qm by lrelease at build time and embedded under :/i18n, so nothing
// is installed beside the binary. Only English exists: it carries the numerus forms of every
// `%n` string ("1 server", "2 servers"); every other string falls back to its source text.

class QCoreApplication;

namespace omaseed::i18n {

/// Where the English catalogue lives in the resources.
inline constexpr const char *englishCatalogue = ":/i18n/omaseed_en.qm";

/// Installs the English catalogue on `app`, for every locale: there is no other language yet.
/// When there is, the system locale's file goes in first and English stays installed after it,
/// so numerus forms still resolve for strings a translation leaves out. False (and a warning)
/// when the catalogue is missing from the resources.
bool installTranslations(QCoreApplication *app);

} // namespace omaseed::i18n
