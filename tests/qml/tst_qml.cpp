#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest>

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

/// Hook for the QML test engine: hands each test file's engine the TestFiles above.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("TestFiles"), &m_files);
    }

private:
    TestFiles m_files;
};

QUICK_TEST_MAIN_WITH_SETUP(omaseed_qml, Setup)

#include "tst_qml.moc"
