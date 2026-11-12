// The English catalogue (docs/DECISIONS.md C41, #197). A `%n` string reads "1 commands" unless a
// translation gives it a singular, so every one of them must have its two numerus forms in
// translations/omaseed_en.ts, the embedded .qm must hand them to tr() and qsTr() under the
// context lupdate gave them, and the committed file must hold exactly the `%n` strings the
// sources have (a new one without forms, or one left behind, fails here).

#include "ui/i18n/Translations.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QList>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QXmlStreamReader>

#include <memory>

namespace {

struct NumerusMessage
{
    QString context;
    QString source;
    QString comment;
    bool unfinished = false;
    QStringList forms;

    QString key() const { return context + u'\x1f' + source + u'\x1f' + comment; }
    QString where() const { return QStringLiteral("%1: \"%2\"").arg(context, source); }
};

/// The numerus messages of a .ts file, in file order. False when it can't be read.
bool readNumerus(const QString &path, QList<NumerusMessage> *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    QXmlStreamReader xml(&file);
    QString context;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == u"name") {
            context = xml.readElementText();
        } else if (xml.name() == u"message") {
            const bool numerus = xml.attributes().value(u"numerus") == u"yes";
            NumerusMessage message;
            message.context = context;
            while (!(xml.isEndElement() && xml.name() == u"message") && !xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == u"source")
                    message.source = xml.readElementText();
                else if (xml.name() == u"comment")
                    message.comment = xml.readElementText();
                else if (xml.name() == u"translation")
                    message.unfinished = xml.attributes().value(u"type") == u"unfinished";
                else if (xml.name() == u"numerusform")
                    message.forms.append(xml.readElementText());
            }
            if (numerus)
                out->append(message);
        }
    }
    if (xml.hasError()) {
        *error = QStringLiteral("%1: %2").arg(path, xml.errorString());
        return false;
    }
    return true;
}

QString withCount(QString form, int n)
{
    return form.replace(QStringLiteral("%n"), QString::number(n));
}

} // namespace

class TestTranslations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QString error;
        QVERIFY2(readNumerus(QStringLiteral(OMASEED_TS_FILE), &m_committed, &error),
                 qPrintable(error));
        QVERIFY2(!m_committed.isEmpty(), "the catalogue has no %n strings");
        QVERIFY(omaseed::i18n::installTranslations(QCoreApplication::instance()));
    }

    /// English has two forms; both are written, and none is left unfinished (lrelease would
    /// drop it, and the string would read "1 commands" again).
    void everyStringHasBothForms()
    {
        QStringList missing;
        for (const NumerusMessage &message : std::as_const(m_committed)) {
            const bool filled = message.forms.size() == 2 && !message.forms.at(0).isEmpty() &&
                                !message.forms.at(1).isEmpty();
            if (message.unfinished || !filled)
                missing.append(message.where());
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("fill the numerus forms in translations/omaseed_en.ts "
                                           "for:\n  %1")
                                .arg(missing.join(QStringLiteral("\n  ")))));
    }

    /// Through the .qm in the resources, as the app reads it: n = 1 gives the singular, n = 2
    /// the plural, under the context lupdate extracted (a class name, or a QML file's name).
    void rendersSingularAndPlural()
    {
        for (const NumerusMessage &message : std::as_const(m_committed)) {
            QCOMPARE(message.forms.size(), 2);
            const QByteArray context = message.context.toUtf8();
            const QByteArray source = message.source.toUtf8();
            const QByteArray comment = message.comment.toUtf8();
            const char *disambiguation = comment.isEmpty() ? nullptr : comment.constData();
            for (int n : {1, 2}) {
                const QString got = QCoreApplication::translate(
                    context.constData(), source.constData(), disambiguation, n);
                const QString want = withCount(message.forms.at(n == 1 ? 0 : 1), n);
                QVERIFY2(got == want,
                         qPrintable(QStringLiteral("%1 with n = %2: \"%3\", not \"%4\"")
                                        .arg(message.where())
                                        .arg(n)
                                        .arg(got, want)));
            }
        }
    }

    /// QML's qsTr() takes its context from the file's name, as lupdate does: a component named
    /// like one of the views reads its singular too.
    void qmlReadsTheSameForms()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "QtObject {\n"
                          "    property string one: qsTr(\"%n commands\", \"\", 1)\n"
                          "    property string two: qsTr(\"%n commands\", \"\", 2)\n"
                          "}\n",
                          QUrl(QStringLiteral("qrc:/tst_translations/CommandPalette.qml")));
        const std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("one").toString(), QStringLiteral("1 command"));
        QCOMPARE(object->property("two").toString(), QStringLiteral("2 commands"));
    }

    /// The committed file holds exactly the `%n` strings of the sources: lupdate, run by the
    /// fixture with update_translations' sources and options into the build tree, finds no
    /// string the file lacks and none it has left behind. When this fails, run
    /// `cmake --build build --target update_translations` and fill the new forms.
    void inSyncWithTheSources()
    {
        QList<NumerusMessage> extracted;
        QString error;
        QVERIFY2(readNumerus(QStringLiteral(OMASEED_TS_CHECK_FILE), &extracted, &error),
                 qPrintable(error + QStringLiteral(" (the tst_translations_lupdate fixture "
                                                   "writes it; run the test through ctest)")));
        QSet<QString> committedKeys;
        for (const NumerusMessage &message : std::as_const(m_committed))
            committedKeys.insert(message.key());
        QSet<QString> extractedKeys;
        QStringList added;
        for (const NumerusMessage &message : std::as_const(extracted)) {
            extractedKeys.insert(message.key());
            if (!committedKeys.contains(message.key()))
                added.append(message.where());
        }
        QStringList gone;
        for (const NumerusMessage &message : std::as_const(m_committed)) {
            if (!extractedKeys.contains(message.key()))
                gone.append(message.where());
        }
        QVERIFY2(added.isEmpty() && gone.isEmpty(),
                 qPrintable(QStringLiteral("translations/omaseed_en.ts is out of date; run "
                                           "`cmake --build build --target update_translations` "
                                           "and fill the new forms.\n new in the sources:\n  %1\n "
                                           "no longer in the sources:\n  %2")
                                .arg(added.join(QStringLiteral("\n  ")),
                                     gone.join(QStringLiteral("\n  ")))));
    }

private:
    QList<NumerusMessage> m_committed;
};

QTEST_GUILESS_MAIN(TestTranslations)
#include "tst_translations.moc"
