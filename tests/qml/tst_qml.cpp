#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest>

#include <memory>

/// Files on disk for the QML tests, as `TestFiles` (a context property): a private temporary
/// folder, gone when the run ends, and a way to put a file in it. #64's rename guard reads a
/// real folder.
class TestFiles : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QString folder() const { return m_dir.path(); }

    /// Writes `text` to `relativePath` under folder(), making its folders; whether it worked.
    Q_INVOKABLE bool write(const QString &relativePath, const QString &text)
    {
        const QString path = m_dir.filePath(relativePath);
        if (!m_dir.isValid() || !QDir().mkpath(QFileInfo(path).path()))
            return false;
        QFile file(path);
        const QByteArray bytes = text.toUtf8();
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }

private:
    QTemporaryDir m_dir;
};

/// The base size every Theme in the run reads, as `TestTheme` (a context property): the
/// Appearance pane's override, without the App that applies it in the real launch (#156);
/// the skin choice likewise.
/// A test that moves it puts it back to 0, the shell's own (12, the harness's empty XDG).
class TestTheme : public QObject
{
    Q_OBJECT

public:
    explicit TestTheme(omaseed::integration::ThemeReader *reader)
        : m_reader(reader)
    {}

    Q_INVOKABLE void setBaseSize(int pixels) { m_reader->setBaseSizeOverride(pixels); }
    /// Appearance's skin choice as App hands it to the reader (#171). A test that moves it
    /// puts it back to "omarchy".
    Q_INVOKABLE void setSkinChoice(const QString &choice) { m_reader->setSkinChoice(choice); }

private:
    omaseed::integration::ThemeReader *m_reader;
};

/// Hook for the QML test engine: hands each test file's engine the TestFiles and TestTheme
/// above. The reader is the one every engine's Theme binds to, as main() wires its own.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        m_reader = std::make_unique<omaseed::integration::ThemeReader>();
        m_theme = std::make_unique<TestTheme>(m_reader.get());
        omaseed::integration::Theme::setDefaultReader(m_reader.get());
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("TestFiles"), &m_files);
        engine->rootContext()->setContextProperty(QStringLiteral("TestTheme"), m_theme.get());
    }

    void cleanupTestCase() { omaseed::integration::Theme::setDefaultReader(nullptr); }

private:
    TestFiles m_files;
    std::unique_ptr<omaseed::integration::ThemeReader> m_reader;
    std::unique_ptr<TestTheme> m_theme;
};

QUICK_TEST_MAIN_WITH_SETUP(omaseed_qml, Setup)

#include "tst_qml.moc"
