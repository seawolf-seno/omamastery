#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

// Small blocking HTTP client for the one-time setup. Each call runs a local
// event loop, so it works from the CLI and from a worker thread alike. Error
// text never includes a response body that could carry student data: only
// the status and the service's own short error message.
class Http {
public:
    using Headers = QHash<QByteArray, QByteArray>;

    struct Reply {
        int status = 0;
        QByteArray body;
        QJsonDocument json;
        QUrl next;      // Link: rel="next", for paged lists
        QString error;  // empty when the call worked
        bool ok() const { return error.isEmpty(); }
    };

    explicit Http(Headers headers = {}) : m_headers(std::move(headers)) {}
    void setHeader(const QByteArray &name, const QByteArray &value) { m_headers.insert(name, value); }

    Reply get(const QUrl &url) const;
    Reply postForm(const QUrl &url, const QUrlQuery &form, const Headers &extra = {}) const;

    // Follows Link: rel="next" and joins every page. Lists that come wrapped
    // as {"value": [...]} (Blackbaud) are unwrapped.
    QJsonArray getAll(const QUrl &url, QString *error, int maxPages = 50) const;

    static QUrl nextLink(const QByteArray &linkHeader);
    static QJsonArray listOf(const QJsonDocument &doc);

private:
    Reply send(const QUrl &url, const QByteArray &method, const QByteArray &body, const Headers &extra) const;
    Headers m_headers;
};
