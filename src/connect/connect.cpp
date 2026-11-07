#include "connect.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <toml++/toml.hpp>

#include "../samples.h"
#include "http.h"

namespace Connect {

namespace {

QString configDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omamastery";
}

void readSettings(const QString &path, Settings *s) {
    if (!QFileInfo::exists(path))
        return;
    try {
        const toml::table t = toml::parse_file(path.toStdString());
        auto str = [&](const char *key, QString *out) {
            if (auto v = t[key].value<std::string>())
                *out = QString::fromStdString(*v).trimmed();
        };
        str("canvas_url", &s->canvasUrl);
        str("sky_api", &s->skyApi);
        str("sky_authorize", &s->skyAuthorize);
        str("sky_token", &s->skyToken);
        str("blackbaud_subscription_key", &s->blackbaudSubscriptionKey);
        str("blackbaud_client_id", &s->blackbaudClientId);
        str("blackbaud_client_secret", &s->blackbaudClientSecret);
        str("blackbaud_user_id", &s->blackbaudUserId);
        if (auto port = t["blackbaud_port"].value<int64_t>())
            s->blackbaudPort = int(*port);
    } catch (const toml::parse_error &) {
    }
}

QString randomUrlSafe(int bytes) {
    QByteArray raw(bytes, 0);
    for (int i = 0; i < bytes; ++i)
        raw[i] = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString secretsFile() {
    return configDir() + "/secrets";
}

bool useKeyring() {
    return qEnvironmentVariable("OMAMASTERY_NO_KEYRING").isEmpty() && !QStandardPaths::findExecutable("secret-tool").isEmpty();
}

QHash<QString, QString> readSecretsFile() {
    QHash<QString, QString> out;
    QFile file(secretsFile());
    if (!file.open(QIODevice::ReadOnly))
        return out;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const int eq = line.indexOf('=');
        if (eq > 0)
            out.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)));
    }
    return out;
}

bool writeSecretsFile(const QHash<QString, QString> &secrets, QString *error) {
    QDir().mkpath(configDir());
    QByteArray bytes;
    for (auto it = secrets.begin(); it != secrets.end(); ++it)
        bytes += it.key().toUtf8() + '=' + it.value().toUtf8() + '\n';
    QSaveFile file(secretsFile());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Couldn't save %1").arg(secretsFile());
        return false;
    }
    QFile::setPermissions(secretsFile(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

// ---- Blackbaud token exchange ----

struct Tokens {
    QString access;
    QString refresh;
    QString error;
};

Tokens exchange(const Settings &s, QUrlQuery form) {
    Http::Headers headers;
    if (s.blackbaudClientSecret.isEmpty())
        form.addQueryItem("client_id", s.blackbaudClientId);
    else
        headers.insert("Authorization", "Basic " + (s.blackbaudClientId + ":" + s.blackbaudClientSecret).toUtf8().toBase64());
    const Http::Reply reply = Http().postForm(QUrl(s.skyToken), form, headers);
    Tokens t;
    const QJsonObject o = reply.json.object();
    t.access = o.value("access_token").toString();
    t.refresh = o.value("refresh_token").toString();
    if (!reply.ok() || t.access.isEmpty())
        t.error = QStringLiteral("Blackbaud sign-in didn't work. %1").arg(reply.ok() ? QStringLiteral("No token came back.") : reply.error);
    return t;
}

// ---- Pull helpers ----

Http canvasClient(const Settings &, const QString &token) {
    return Http({{"Authorization", "Bearer " + token.toUtf8()}});
}

QUrl canvasUrl(const Settings &s, const QString &path, const QList<QPair<QString, QString>> &query = {}) {
    QUrl url(s.canvasUrl + "/api/v1" + path);
    QUrlQuery q;
    for (const auto &pair : query)
        q.addQueryItem(pair.first, pair.second);
    url.setQuery(q);
    return url;
}

Http skyClient(const Settings &s, const QString &token) {
    return Http({{"Authorization", "Bearer " + token.toUtf8()}, {"Bb-Api-Subscription-Key", s.blackbaudSubscriptionKey.toUtf8()}});
}

QUrl skyUrl(const Settings &s, const QString &path, const QList<QPair<QString, QString>> &query = {}) {
    QUrl url(s.skyApi + "/school/v1" + path);
    QUrlQuery q;
    for (const auto &pair : query)
        q.addQueryItem(pair.first, pair.second);
    url.setQuery(q);
    return url;
}

QString currentSchoolYear(const Http &sky, const Settings &s, QString *error) {
    const Http::Reply reply = sky.get(skyUrl(s, "/years"));
    if (!reply.ok()) {
        *error = reply.error;
        return {};
    }
    QString fallback;
    for (const auto &value : Http::listOf(reply.json)) {
        const QJsonObject y = value.toObject();
        const QString label = y.value("school_year_label").toString();
        if (y.value("current_year").toBool())
            return label;
        const QDate begin = QDate::fromString(y.value("begin_date").toString().left(10), Qt::ISODate);
        const QDate end = QDate::fromString(y.value("end_date").toString().left(10), Qt::ISODate);
        if (begin.isValid() && end.isValid() && begin <= QDate::currentDate() && QDate::currentDate() <= end)
            fallback = label;
    }
    return fallback;
}

} // namespace

// ---------------------------------------------------------------- Settings

QString Settings::normalizeCanvasUrl(const QString &input) {
    QString s = input.trimmed();
    while (s.endsWith('/'))
        s.chop(1);
    if (s.isEmpty())
        return {};
    if (!s.contains("://")) {
        if (!s.contains('.'))
            s += ".instructure.com";
        s.prepend("https://");
    }
    const QUrl url(s);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    // Plain http only for a test server on this computer.
    const bool local = url.host() == "localhost" || url.host() == "127.0.0.1";
    return (local ? url.scheme() : QStringLiteral("https")) + "://" + url.authority();
}

QUrl Settings::canvasTokenPage() const {
    return QUrl(canvasUrl + "/profile/settings");
}

Settings Settings::load() {
    Settings s;
    readSettings("/etc/omamastery/connect.toml", &s);
    readSettings(configDir() + "/connect.toml", &s);
    if (!qEnvironmentVariable("OMAMASTERY_CANVAS_URL").isEmpty())
        s.canvasUrl = qEnvironmentVariable("OMAMASTERY_CANVAS_URL");
    if (!qEnvironmentVariable("OMAMASTERY_SKY_API").isEmpty())
        s.skyApi = qEnvironmentVariable("OMAMASTERY_SKY_API");
    s.canvasUrl = normalizeCanvasUrl(s.canvasUrl);
    return s;
}

bool Settings::saveCanvasUrl(const QString &url, QString *error) {
    QDir().mkpath(configDir());
    const QString path = configDir() + "/connect.toml";
    QString text;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly))
        text = QString::fromUtf8(in.readAll());
    in.close();
    const QString line = QStringLiteral("canvas_url = \"%1\"").arg(url);
    static const QRegularExpression existing(R"(^canvas_url\s*=.*$)", QRegularExpression::MultilineOption);
    if (existing.match(text).hasMatch())
        text.replace(existing, line);
    else
        text = (text.isEmpty() ? QStringLiteral("# OmaMastery's connection to your school. Tokens are in your keyring, not here.\n") : text) + line + "\n";
    QSaveFile out(path);
    const QByteArray bytes = text.toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error)
            *error = QStringLiteral("Couldn't save %1").arg(path);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- Secrets

QString Secrets::load(const QString &key) {
    if (useKeyring()) {
        QProcess p;
        p.start("secret-tool", {"lookup", "service", "omamastery", "key", key});
        if (p.waitForFinished(30000) && p.exitCode() == 0) {
            const QString value = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if (!value.isEmpty())
                return value;
        }
    }
    return readSecretsFile().value(key);
}

bool Secrets::store(const QString &key, const QString &value, QString *error) {
    if (useKeyring()) {
        QProcess p;
        p.start("secret-tool", {"store", "--label", "OmaMastery " + key, "service", "omamastery", "key", key});
        p.write(value.toUtf8());
        p.closeWriteChannel();
        if (p.waitForFinished(30000) && p.exitCode() == 0)
            return true;
    }
    auto secrets = readSecretsFile();
    secrets.insert(key, value);
    return writeSecretsFile(secrets, error);
}

void Secrets::forget(const QString &key) {
    if (useKeyring())
        QProcess::execute("secret-tool", {"clear", "service", "omamastery", "key", key});
    auto secrets = readSecretsFile();
    if (secrets.remove(key))
        writeSecretsFile(secrets, nullptr);
}

// ---------------------------------------------------------------- Blackbaud

QString Blackbaud::signIn(const Settings &s, const std::function<void(const QUrl &)> &openBrowser, QString *error) {
    if (!s.blackbaudReady()) {
        *error = QStringLiteral("Blackbaud isn't set up for this school yet (no SKY app in connect.toml).");
        return {};
    }
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, quint16(s.blackbaudPort))) {
        *error = QStringLiteral("Port %1 is busy, so Blackbaud can't send you back here.").arg(s.blackbaudPort);
        return {};
    }
    const QString redirect = QStringLiteral("http://localhost:%1/callback").arg(s.blackbaudPort);
    const QString state = randomUrlSafe(24);
    const QString verifier = randomUrlSafe(48);
    const QString challenge = QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                                      .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QUrl url(s.skyAuthorize);
    QUrlQuery q;
    q.addQueryItem("client_id", s.blackbaudClientId);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("redirect_uri", redirect);
    q.addQueryItem("state", state);
    q.addQueryItem("code_challenge", challenge);
    q.addQueryItem("code_challenge_method", "S256");
    url.setQuery(q);
    openBrowser(url);

    // Wait (up to five minutes) for the browser to come back with a code.
    QString code;
    QString returnedState;
    QString denied;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket)
            return;
        if (!socket->waitForReadyRead(5000)) {
            socket->deleteLater();
            return;
        }
        const QByteArray line = socket->readLine();
        const QByteArray target = line.split(' ').value(1);
        const QUrl back(QStringLiteral("http://localhost") + QString::fromLatin1(target));
        if (back.path() != "/callback") {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QUrlQuery params(back);
        code = params.queryItemValue("code");
        returnedState = params.queryItemValue("state");
        denied = params.queryItemValue("error");
        const QByteArray page = "<!doctype html><meta charset=utf-8><title>OmaMastery</title>"
                                "<body style='font:18px system-ui;background:#0e0e10;color:#e8e8ea;display:grid;place-items:center;height:90vh'>"
                                "<p>Blackbaud is connected. You can close this tab and go back to OmaMastery.</p>";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(page.size()) + "\r\n\r\n" + page);
        socket->flush();
        socket->disconnectFromHost();
        loop.quit();
    });
    timeout.start(5 * 60 * 1000);
    loop.exec();
    server.close();

    if (!denied.isEmpty()) {
        *error = QStringLiteral("Blackbaud sign-in was cancelled (%1).").arg(denied.left(60));
        return {};
    }
    if (code.isEmpty()) {
        *error = QStringLiteral("Blackbaud sign-in timed out.");
        return {};
    }
    if (returnedState != state) {
        *error = QStringLiteral("Blackbaud sign-in didn't match this request, so it was ignored. Try again.");
        return {};
    }
    QUrlQuery form;
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code", code);
    form.addQueryItem("redirect_uri", redirect);
    form.addQueryItem("code_verifier", verifier);
    const Tokens t = exchange(s, form);
    if (!t.error.isEmpty()) {
        *error = t.error;
        return {};
    }
    if (!t.refresh.isEmpty())
        Secrets::store("blackbaud-refresh", t.refresh, nullptr);
    return t.access;
}

QString Blackbaud::refresh(const Settings &s, QString *error) {
    const QString refreshToken = Secrets::load("blackbaud-refresh");
    if (refreshToken.isEmpty() || !s.blackbaudReady())
        return {};
    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", refreshToken);
    const Tokens t = exchange(s, form);
    if (!t.error.isEmpty()) {
        *error = t.error;
        return {};
    }
    if (!t.refresh.isEmpty())
        Secrets::store("blackbaud-refresh", t.refresh, nullptr);
    return t.access;
}

// ---------------------------------------------------------------- Pull

PullResult list(const Settings &s, const QString &canvasToken, const QString &blackbaudToken, const Schedule &schedule,
                const Progress &progress) {
    PullResult r;
    if (!canvasToken.isEmpty()) {
        progress(QStringLiteral("Reading your Canvas courses…"));
        QString error;
        const QJsonArray courses = canvasClient(s, canvasToken).getAll(
            canvasUrl(s, "/courses", {{"enrollment_type", "teacher"}, {"enrollment_state", "active"},
                                      {"include[]", "total_students"}, {"include[]", "sections"}, {"per_page", "100"}}),
            &error);
        if (!error.isEmpty()) {
            r.error = error.contains(" 401") ? QStringLiteral("Canvas didn't accept that token. Make a new one and try again.") : error;
            return r;
        }
        r.classes = Importer::draftsFromCanvasCourses(courses);
    }

    if (!blackbaudToken.isEmpty()) {
        progress(QStringLiteral("Reading your Blackbaud schedule…"));
        const Http sky = skyClient(s, blackbaudToken);
        QString teacher = s.blackbaudUserId;
        const Http::Reply me = sky.get(skyUrl(s, "/users/me"));
        if (me.ok() && me.json.isObject())
            teacher = me.json.object().value("id").toVariant().toString();
        QString error;
        const QString year = teacher.isEmpty() ? QString() : currentSchoolYear(sky, s, &error);
        if (teacher.isEmpty()) {
            r.warnings << QStringLiteral("Blackbaud skipped: couldn't tell which teacher you are (%1). Set blackbaud_user_id in connect.toml.")
                              .arg(me.error.isEmpty() ? QStringLiteral("no id") : me.error);
        } else if (year.isEmpty()) {
            r.warnings << QStringLiteral("Blackbaud skipped: couldn't read the school year. %1").arg(error);
        } else {
            const QJsonArray sections = sky.getAll(skyUrl(s, "/academics/teachers/" + teacher + "/sections", {{"school_year", year}}), &error);
            if (!error.isEmpty()) {
                r.warnings << QStringLiteral("Blackbaud sections skipped: %1").arg(error);
            } else {
                QVector<Importer::ClassDraft> extra = Importer::matchSections(&r.classes, Importer::draftsFromBlackbaudSections(sections));
                // Sections with no Canvas course (advisory, study hall) are listed, not ticked,
                // unless Blackbaud is the only source.
                for (auto &d : extra)
                    d.selected = canvasToken.isEmpty();
                r.classes += extra;

                QStringList ids;
                for (const auto &d : r.classes)
                    if (!d.blackbaudSection.isEmpty())
                        ids << d.blackbaudSection;
                const QDate from = QDate::currentDate().addDays(1 - QDate::currentDate().dayOfWeek());
                const QJsonArray meetings = sky.getAll(
                    skyUrl(s, "/schedules/meetings",
                           {{"start_date", from.toString(Qt::ISODate)}, {"end_date", from.addDays(27).toString(Qt::ISODate)},
                            {"section_ids", ids.join(',')}}),
                    &error);
                if (!error.isEmpty())
                    r.warnings << QStringLiteral("Blackbaud meeting times skipped: %1").arg(error);
                else if (meetings.isEmpty())
                    r.warnings << QStringLiteral("Blackbaud has no class meetings in the next four weeks, so day, block and room weren't set.");
                else
                    r.bells = Importer::applyMeetings(meetings, schedule, &r.classes);
            }
        }
    }
    if (r.classes.isEmpty() && r.error.isEmpty())
        r.error = QStringLiteral("No classes found where you're the teacher.");
    return r;
}

bool fill(const Settings &s, const QString &canvasToken, const QString &blackbaudToken, PullResult *r, const Progress &progress) {
    for (auto &d : r->classes) {
        if (!d.selected)
            continue;
        if (!d.canvasCourse.isEmpty() && !canvasToken.isEmpty()) {
            progress(QStringLiteral("Reading %1 from Canvas…").arg(d.name));
            const Http canvas = canvasClient(s, canvasToken);
            const QString base = "/courses/" + d.canvasCourse;
            QString error;
            const QJsonArray users = canvas.getAll(
                canvasUrl(s, base + "/users", {{"enrollment_type[]", "student"}, {"enrollment_state[]", "active"}, {"per_page", "100"}}), &error);
            if (!error.isEmpty()) {
                r->error = error;
                return false;
            }
            d.students = Importer::studentsFromCanvas(users);

            QString soft;
            const QJsonArray outcomes = canvas.getAll(canvasUrl(s, base + "/outcome_group_links", {{"outcome_style", "full"}, {"per_page", "100"}}), &soft);
            Importer::objectivesFromOutcomes(outcomes, &d);
            soft.clear();
            const QJsonArray modules = canvas.getAll(canvasUrl(s, base + "/modules", {{"include[]", "items"}, {"per_page", "100"}}), &soft);
            const QJsonArray assignments = soft.isEmpty() ? canvas.getAll(canvasUrl(s, base + "/assignments", {{"per_page", "100"}}), &soft) : QJsonArray();
            if (!soft.isEmpty())
                r->warnings << QStringLiteral("%1: modules not read (%2)").arg(d.name, soft);
            Importer::objectivesFromModules(modules, assignments, &d);
            const Http::Reply course = canvas.get(canvasUrl(s, base, {{"include[]", "syllabus_body"}}));
            if (course.ok())
                d.syllabus = Importer::syllabusText(course.json.object().value("syllabus_body").toString());
        } else if (!d.blackbaudSection.isEmpty() && !blackbaudToken.isEmpty()) {
            progress(QStringLiteral("Reading %1 from Blackbaud…").arg(d.name));
            QString error;
            const QJsonArray students =
                skyClient(s, blackbaudToken).getAll(skyUrl(s, "/academics/sections/" + d.blackbaudSection + "/students"), &error);
            if (!error.isEmpty())
                r->warnings << QStringLiteral("%1: roster not read (%2)").arg(d.name, error);
            d.students = Importer::studentsFromBlackbaud(students);
        }
    }
    return true;
}

PullResult pull(const Settings &s, const QString &canvasToken, const QString &blackbaudToken, const Schedule &schedule,
                const Progress &progress) {
    PullResult r = list(s, canvasToken, blackbaudToken, schedule, progress);
    if (r.error.isEmpty())
        fill(s, canvasToken, blackbaudToken, &r, progress);
    return r;
}

WriteResult write(const QString &root, const PullResult &pulled) {
    WriteResult w;
    const QString repo = Importer::gitRepoAbove(root);
    if (!repo.isEmpty()) {
        w.error = QStringLiteral("%1 is inside a git repository (%2). Real student names can't go there: "
                                 "pick a folder outside it with OMAMASTERY_HOME.")
                      .arg(root, repo);
        return w;
    }
    QString report;
    QString error;
    if (!Samples::init(root, false, &report, &error)) {
        w.error = error;
        return w;
    }
    Samples::addRealClassRules(root + "/AGENTS.md");

    QVector<Importer::ClassDraft> chosen;
    for (const auto &d : pulled.classes)
        if (d.selected)
            chosen.append(d);
    Importer::assignIds(root, &chosen);
    for (const auto &d : chosen) {
        if (!Importer::writeClass(root, d, &report, &error)) {
            w.error = error;
            return w;
        }
        w.lines << report;
    }
    if (Importer::writeBellTimes(root + "/schedule.toml", pulled.bells, &report))
        w.lines << report;
    return w;
}

QString draftPrompt(const QStringList &classIds) {
    return QStringLiteral(
               "I'm a teacher. You're in ~/Classes. Read AGENTS.md first and follow its rules. I just set up these "
               "classes from Canvas: %1. For each one, read the files in its folder that describe the course: "
               "long-range-plan.* if there is one, syllabus.md, canvas-plan.md, plan.toml and ideas.toml. Never open "
               "roster.toml or mastery.csv: they hold real students. Then draft the course's core ideas in ideas.toml "
               "and clear learning objectives in plan.toml: each one a thing a student can do, tied to a core idea, "
               "in the week it's taught, skipping days off in schedule.toml. Keep existing objective ids. Do one class "
               "at a time and show me each before you save it.")
        .arg(classIds.join(", "));
}

} // namespace Connect
