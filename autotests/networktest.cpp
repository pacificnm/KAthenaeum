// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/network.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace
{
// Answers every request with a redirect to `location`, and counts the requests it gets
class Redirector : public QTcpServer
{
public:
    explicit Redirector(QByteArray location = {})
        : m_location(std::move(location))
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    if (!s->readAll().contains("\r\n\r\n"))
                        return;
                    ++requests;
                    s->write(m_location.isEmpty() ? QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")
                                                  : "HTTP/1.1 302 Found\r\nLocation: " + m_location + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/file").arg(serverPort())); }
    int requests = 0;

private:
    QByteArray m_location;
};

QNetworkReply *get(QNetworkAccessManager &nam, const QUrl &url, const std::function<bool(const QUrl &)> &allowed)
{
    QNetworkReply *reply = nam.get(Network::guarded(QNetworkRequest(url)));
    Network::allowRedirects(reply, allowed);
    return reply;
}
}

class NetworkTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aRedirectToPlainHttpIsNotFollowed()
    {
        Redirector target;  // where the redirect points: it must never be asked
        Redirector first(target.url().toEncoded());
        QNetworkAccessManager nam;
        QNetworkReply *reply = get(nam, first.url(), [](const QUrl &) {
            return true;  // even a host the caller allows: not over plain http
        });
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
        QCOMPARE(first.requests, 1);
        QCOMPARE(target.requests, 0);
        delete reply;
    }

    void aRedirectToAHostTheCallerRefusesIsNotFollowed()
    {
        Redirector first("https://elsewhere.invalid/file");
        QNetworkAccessManager nam;
        QList<QUrl> asked;
        QNetworkReply *reply = get(nam, first.url(), [&asked](const QUrl &url) {
            asked << url;
            return false;
        });
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(asked, QList<QUrl>{QUrl(QStringLiteral("https://elsewhere.invalid/file"))});
        QCOMPARE(reply->error(), QNetworkReply::OperationCanceledError);
        delete reply;
    }

    void aRedirectTheCallerAllowsIsFollowed()
    {
        // .invalid never resolves, so a followed redirect ends in a lookup failure rather than in a cancellation
        Redirector first("https://allowed.invalid/file");
        QNetworkAccessManager nam;
        QNetworkReply *reply = get(nam, first.url(), [](const QUrl &url) {
            return url.host() == QLatin1String("allowed.invalid");
        });
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(10000));
        QVERIFY2(reply->error() != QNetworkReply::OperationCanceledError, qPrintable(reply->errorString()));
        QCOMPARE(reply->url(), QUrl(QStringLiteral("https://allowed.invalid/file")));
        delete reply;
    }
};

QTEST_GUILESS_MAIN(NetworkTest)
#include "networktest.moc"
