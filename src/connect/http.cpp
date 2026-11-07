#include "http.h"

#include <QEventLoop>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace {

// The service's own one-line reason, from the JSON shapes Canvas and
// Blackbaud use. Never the raw body.
QString shortReason(const QJsonDocument &doc) {
    if (!doc.isObject())
        return {};
    const QJsonObject o = doc.object();
    for (const char *key : {"message", "Message", "title", "error_description", "error"}) {
        if (o.value(key).isString())
            return o.value(key).toString().left(200);
    }
    const QJsonArray errors = o.value("errors").toArray();
    if (!errors.isEmpty() && errors.first().toObject().value("message").isString())
        return errors.first().toObject().value("message").toString().left(200);
    return {};
}

} // namespace

QUrl Http::nextLink(const QByteArray &linkHeader) {
    static const QRegularExpression part(R"(<([^>]+)>\s*;\s*rel="?next"?)");
    const auto match = part.match(QString::fromUtf8(linkHeader));
    return match.hasMatch() ? QUrl(match.captured(1)) : QUrl();
}

QJsonArray Http::listOf(const QJsonDocument &doc) {
    if (doc.isArray())
        return doc.array();
    return doc.object().value("value").toArray();
}

Http::Reply Http::send(const QUrl &url, const QByteArray &method, const QByteArray &body, const Headers &extra) const {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "OmaMastery");
    for (auto it = m_headers.begin(); it != m_headers.end(); ++it)
        request.setRawHeader(it.key(), it.value());
    for (auto it = extra.begin(); it != extra.end(); ++it)
        request.setRawHeader(it.key(), it.value());

    QNetworkReply *reply = method == "POST" ? manager.post(request, body) : manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    Reply out;
    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.body = reply->readAll();
    out.json = QJsonDocument::fromJson(out.body);
    out.next = nextLink(reply->rawHeader("Link"));
    if (out.status < 200 || out.status >= 300) {
        const QString reason = shortReason(out.json);
        out.error = out.status == 0
                        ? QStringLiteral("Couldn't reach %1 (%2)").arg(url.host(), reply->errorString())
                        : QStringLiteral("%1 answered %2 for %3%4")
                              .arg(url.host())
                              .arg(out.status)
                              .arg(url.path(), reason.isEmpty() ? QString() : QStringLiteral(": ") + reason);
    }
    reply->deleteLater();
    return out;
}

Http::Reply Http::get(const QUrl &url) const {
    return send(url, "GET", {}, {});
}

Http::Reply Http::postForm(const QUrl &url, const QUrlQuery &form, const Headers &extra) const {
    Headers headers = extra;
    headers.insert("Content-Type", "application/x-www-form-urlencoded");
    return send(url, "POST", form.toString(QUrl::FullyEncoded).toUtf8(), headers);
}

QJsonArray Http::getAll(const QUrl &url, QString *error, int maxPages) const {
    QJsonArray all;
    QUrl page = url;
    for (int i = 0; i < maxPages && page.isValid() && !page.isEmpty(); ++i) {
        const Reply reply = get(page);
        if (!reply.ok()) {
            if (error)
                *error = reply.error;
            return all;
        }
        for (const auto &item : listOf(reply.json))
            all.append(item);
        page = reply.next;
    }
    return all;
}
