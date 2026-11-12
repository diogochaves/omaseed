#include "ui/i18n/Translations.hpp"

#include <QCoreApplication>
#include <QString>
#include <QTranslator>
#include <QtGlobal>

namespace omaseed::i18n {

bool installTranslations(QCoreApplication *app)
{
    // Parented to the application, so it lives as long as the strings it translates.
    auto *english = new QTranslator(app);
    if (!english->load(QString::fromLatin1(englishCatalogue))) {
        qWarning("omaseed: no %s in the resources; plurals read as their source text",
                 englishCatalogue);
        delete english;
        return false;
    }
    return QCoreApplication::installTranslator(english);
}

} // namespace omaseed::i18n
