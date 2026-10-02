#pragma once

// StatusService — the app's side of `io.github.diogochaves.omaseed.Status` (#13, docs/20 §5):
// what the companion bar widget's buttons and keys do. integration::StatusBus exports the
// interface and calls this; this calls App's public API and QbtClient, the same requests the
// add sheet and the tray send.
//
// The widget's adds never raise the window: a source added from the bar stays in the bar. The
// widget learns the outcome from the reply and, a moment later, from the status file.

#include "integration/StatusBus.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::app {

class App;

class StatusService : public QObject, public omaseed::integration::StatusHandler
{
    Q_OBJECT

public:
    explicit StatusService(App *app, QObject *parent = nullptr);

    bool toggleAltSpeed() override;
    bool setAltSpeed(bool on) override;
    void describeSources(const QStringList &sources, Reply reply) override;
    void addSources(const QStringList &sources,
                    const QString &category,
                    bool stopped,
                    Reply reply) override;
    void openAddSheet(const QStringList &sources) override;
    void showTorrent(const QString &hash) override;
    void openFolder(const QString &hash) override;

    /// One source as read for the widget. Thread-safe: parseSource runs on the pool.
    struct Source
    {
        QString source;                        ///< as the widget sent it
        QString kind = QStringLiteral("none"); ///< "magnet" | "url" | "file" | "none"
        QString value; ///< what goes to the server: the magnet, the URL, the file's path
        bool valid = false;
        QString error;
        QString name;
        qint64 size = 0; ///< bytes, 0 when the source does not say
        QString infoHashV1;
        QString infoHashV2;
        QString hash; ///< the id the server will key it by (TorrentFile::torrentIdFor)
        QString fileName;
        QByteArray bytes; ///< a .torrent's content, read once for the add
    };
    /// Reads one source: a magnet, an http(s) URL, or a .torrent path / file:// URI.
    static Source parseSource(const QString &text);

    /// The category the widget's last add used ("" for none): state.json's "widgetAdd".
    static QString rememberedCategory();

Q_SIGNALS:
    /// The window should come up (OpenAddSheet): main.cpp raises it.
    void raiseRequested();

private:
    /// Parses `sources` on the thread pool and calls `done` back on this object's thread.
    void parseInBackground(const QStringList &sources,
                           std::function<void(const QList<Source> &)> done);
    /// Whether the library already holds a parsed source (App::findDuplicate).
    bool isDuplicate(const Source &source) const;

    App *m_app;
};

} // namespace omaseed::app
