#pragma once

#include <QString>
#include <QUrl>
#include <QVector>
#include <functional>

#include "importer.h"

// Connecting a teacher's Canvas (and optionally Blackbaud) once, so their
// classes, rosters and plans land in ~/Classes. Settings are school-wide
// (/etc/omamastery/connect.toml, which PacOS can ship) with a per-teacher
// override in ~/.config/omamastery/connect.toml. Tokens go in the desktop
// keyring, never in a file in ~/Classes and never on the command line.
namespace Connect {

struct Settings {
    QString canvasUrl;                // https://school.instructure.com
    QString skyApi = "https://api.sky.blackbaud.com";
    QString skyAuthorize = "https://oauth2.sky.blackbaud.com/authorization";
    QString skyToken = "https://oauth2.sky.blackbaud.com/token";
    QString blackbaudSubscriptionKey; // SKY developer subscription key
    QString blackbaudClientId;        // SKY application id
    QString blackbaudClientSecret;    // only for a confidential SKY app; PKCE needs none
    int blackbaudPort = 8765;         // http://localhost:<port>/callback, a redirect URI on the SKY app
    QString blackbaudUserId;          // fallback when /users/me isn't allowed

    bool blackbaudReady() const { return !blackbaudSubscriptionKey.isEmpty() && !blackbaudClientId.isEmpty(); }
    QUrl canvasTokenPage() const;

    static Settings load();
    // Saves the teacher's Canvas address (only that) to their own config.
    static bool saveCanvasUrl(const QString &url, QString *error);
    static QString normalizeCanvasUrl(const QString &input);
};

// Desktop keyring (secret-tool / libsecret), with a 0600 file as fallback on
// a machine without one. Keys: "canvas", "blackbaud-refresh".
namespace Secrets {
QString load(const QString &key);
bool store(const QString &key, const QString &value, QString *error);
void forget(const QString &key);
} // namespace Secrets

// Blackbaud SKY sign-in: authorization code + PKCE through the browser, with
// a one-shot listener on localhost for the redirect. Returns an access token
// (60 minutes) and keeps the refresh token in the keyring so later pulls
// don't need the browser again.
namespace Blackbaud {
QString signIn(const Settings &settings, const std::function<void(const QUrl &)> &openBrowser, QString *error);
QString refresh(const Settings &settings, QString *error);
} // namespace Blackbaud

struct PullResult {
    QVector<Importer::ClassDraft> classes;
    QVector<Importer::MeetingBlock> bells;
    QStringList warnings; // field names and counts only, never values
    QString error;
};

using Progress = std::function<void(const QString &)>;

// Two steps, so student data is only read for classes the teacher ticks:
// list() reads courses, sections and meeting times (no student names);
// fill() then reads rosters, Outcomes, modules and syllabus for the selected
// ones. canvasToken may be empty (Blackbaud only) and blackbaudToken may be
// empty (Canvas only). Each service is asked only about this teacher's own
// courses and sections.
PullResult list(const Settings &settings, const QString &canvasToken, const QString &blackbaudToken,
                const Schedule &schedule, const Progress &progress);
bool fill(const Settings &settings, const QString &canvasToken, const QString &blackbaudToken, PullResult *result,
          const Progress &progress);
// list() then fill(), keeping list()'s selection.
PullResult pull(const Settings &settings, const QString &canvasToken, const QString &blackbaudToken,
                const Schedule &schedule, const Progress &progress);

// Writes the chosen classes and (if they were placeholders) the bell times.
struct WriteResult {
    QStringList lines;
    QString error;
};
WriteResult write(const QString &root, const PullResult &pulled);

// The task for the teacher's agent after setup: draft objectives and core
// ideas from each new class's long-range plan, syllabus and Canvas modules,
// without opening roster.toml or mastery.csv.
QString draftPrompt(const QStringList &classIds);

} // namespace Connect
