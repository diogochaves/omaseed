#pragma once

// FakeHttp — just enough HTTP/1.1 to answer QNetworkAccessManager, for the tests that check
// what an engine sends on the wire and how it reads the answers: each request is recorded
// (method, target, headers, body) and a handler picks the response.

#include "core/ApiError.hpp"
#include "core/Reply.hpp"

#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

namespace fakehttp {

using omaseed::core::ApiError;
using omaseed::core::Reply;
using omaseed::core::Result;

inline QByteArray httpResponse(int code,
                               const QByteArray &reason,
                               const QByteArray &body,
                               const QList<QByteArray> &extraHeaders = {})
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    response += "Content-Type: text/plain; charset=UTF-8\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const QByteArray &header : extraHeaders)
        response += header + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

/// One HTTP request as the fixture saw it.
struct Recorded
{
    QByteArray method;
    QByteArray target;
    QList<QByteArray> headers;
    QByteArray body;

    QByteArray header(const QByteArray &name) const
    {
        for (const QByteArray &line : headers) {
            const qsizetype colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).toLower() == name.toLower())
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }
};

/// Speaks just enough HTTP to answer QNetworkAccessManager.
class FakeServer : public QTcpServer
{
public:
    using Handler = std::function<QByteArray(const Recorded &, int index)>;

    explicit FakeServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    void setHandler(Handler handler) { m_handler = std::move(handler); }

    QList<Recorded> requests;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;

            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            Recorded recorded;
            const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
            recorded.method = requestLine.value(0);
            recorded.target = requestLine.value(1);
            for (qsizetype i = 1; i < lines.size(); ++i)
                recorded.headers.append(lines.at(i).trimmed());

            const QByteArray lengthHeader = recorded.header("Content-Length");
            const qsizetype bodyLength = lengthHeader.isEmpty() ? 0 : lengthHeader.toLongLong();
            if (buffer->size() - (headerEnd + 4) < bodyLength)
                return; // wait for the rest of the body
            recorded.body = buffer->mid(headerEnd + 4, bodyLength);
            buffer->clear();

            const int index = int(requests.size());
            requests.append(recorded);

            const QByteArray response =
                m_handler ? m_handler(recorded, index) : httpResponse(200, "OK", "ok");
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    Handler m_handler;
};

template<class T>
Result<T> await(QObject *context, Reply<T> *reply, int timeoutMs = 10000)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const Result<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace fakehttp
