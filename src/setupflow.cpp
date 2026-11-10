#include "setupflow.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrlQuery>

#include "classbook.h"
#include "samples.h"

namespace {

Schedule loadSchedule(const QString &root) {
    Schedule schedule;
    Classbook book(root);
    if (book.exists()) {
        schedule.load(book.schedulePath());
    } else {
        QTemporaryDir scratch;
        QString report, error;
        Samples::init(scratch.path(), false, &report, &error);
        schedule.load(scratch.path() + "/schedule.toml");
    }
    return schedule;
}

QString omaClassDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omarchy/plugins/omaclass";
}

} // namespace

SetupFlow::SetupFlow(const QString &root, QObject *parent)
    : QObject(parent), m_root(root.isEmpty() ? Classbook::defaultRoot() : root) {}

template <typename Work, typename Done> void SetupFlow::inBackground(Work work, Done done) {
    QPointer<SetupFlow> self(this);
    QThread *thread = QThread::create([self, work, done]() {
        auto result = work();
        QMetaObject::invokeMethod(qApp, [self, done, result]() {
            if (self)
                done(result);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QVariantList SetupFlow::classes() const {
    QVariantList list;
    for (const auto &d : m_result.classes) {
        QStringList parts;
        if (!d.day.isEmpty() || d.block > 0)
            parts << tr("%1 day · Block %2").arg(d.day.isEmpty() ? "?" : d.day, d.block > 0 ? QString::number(d.block) : "?");
        if (!d.room.isEmpty())
            parts << tr("Room %1").arg(d.room);
        if (d.studentCount >= 0)
            parts << tr("%n students", nullptr, d.studentCount);
        if (!d.note.isEmpty())
            parts << d.note;
        list.append(QVariantMap{{"name", d.name}, {"detail", parts.join(" · ")}, {"selected", d.selected}});
    }
    return list;
}

int SetupFlow::chosen() const {
    int n = 0;
    for (const auto &d : m_result.classes)
        n += d.selected ? 1 : 0;
    return n;
}

bool SetupFlow::canAddOmaClass() const {
    if (!QFileInfo::exists(omaClassDir() + "/manifest.json") || QStandardPaths::findExecutable("omarchy").isEmpty())
        return false;
    QFile shell(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omarchy/shell.json");
    return !(shell.open(QIODevice::ReadOnly) && shell.readAll().contains("\"omaclass\""));
}

void SetupFlow::setStep(const QString &step, const QString &message) {
    m_step = step;
    m_message = message;
    m_busy = false;
    emit changed();
}

void SetupFlow::setBusy(bool busy, const QString &message) {
    m_busy = busy;
    m_message = message;
    emit changed();
}

void SetupFlow::start(const QString &link) {
    m_settings = Connect::Settings::load();
    const QUrl url(link);
    if (url.scheme() == "omamastery") {
        const QString canvas = Connect::Settings::normalizeCanvasUrl(QUrlQuery(url).queryItemValue("canvas"));
        if (!canvas.isEmpty())
            m_settings.canvasUrl = canvas;
    }
    m_results.clear();
    m_result = {};
    // Already connected once: go straight to the class list (a new term's rosters).
    m_canvasToken = Connect::Secrets::load("canvas");
    m_tokenSaved = !m_canvasToken.isEmpty();
    if (m_tokenSaved && !m_settings.canvasUrl.isEmpty()) {
        setStep("canvas");
        if (m_settings.blackbaudReady())
            setStep("blackbaud");
        else
            runList();
        return;
    }
    m_canvasToken.clear();
    setStep("canvas");
}

void SetupFlow::setCanvasUrl(const QString &url) {
    m_settings.canvasUrl = Connect::Settings::normalizeCanvasUrl(url);
    emit changed();
}

void SetupFlow::openTokenPage() {
    if (!m_settings.canvasUrl.isEmpty())
        QDesktopServices::openUrl(m_settings.canvasTokenPage());
}

void SetupFlow::connectCanvas(const QString &token) {
    if (m_settings.canvasUrl.isEmpty()) {
        setStep("canvas", tr("Type your school's Canvas address first."));
        return;
    }
    m_canvasToken = token.trimmed();
    m_tokenSaved = false;
    if (m_canvasToken.isEmpty()) {
        setStep("canvas", tr("Paste the token from Canvas."));
        return;
    }
    if (m_settings.blackbaudReady())
        setStep("blackbaud");
    else
        runList();
}

void SetupFlow::connectBlackbaud() {
    setBusy(true, tr("Sign in to Blackbaud in your browser…"));
    const Connect::Settings settings = m_settings;
    inBackground(
        [settings]() {
            QString error;
            QString token = Connect::Blackbaud::refresh(settings, &error);
            if (token.isEmpty()) {
                error.clear();
                token = Connect::Blackbaud::signIn(settings, [](const QUrl &url) {
                    QProcess::startDetached("xdg-open", {url.toString()});
                }, &error);
            }
            return QPair<QString, QString>(token, error);
        },
        [this](const QPair<QString, QString> &r) {
            if (r.first.isEmpty()) {
                setStep("blackbaud", r.second);
                return;
            }
            m_blackbaudToken = r.first;
            runList();
        });
}

void SetupFlow::skipBlackbaud() {
    m_blackbaudToken.clear();
    runList();
}

void SetupFlow::runList() {
    setBusy(true, tr("Reading your classes…"));
    const Connect::Settings settings = m_settings;
    const QString canvas = m_canvasToken, blackbaud = m_blackbaudToken, root = m_root;
    inBackground(
        [=]() { return Connect::list(settings, canvas, blackbaud, loadSchedule(root), [](const QString &) {}); },
        [this](const Connect::PullResult &result) {
            if (!result.error.isEmpty()) {
                if (m_tokenSaved && result.error.contains("token"))
                    Connect::Secrets::forget("canvas");
                m_canvasToken.clear();
                setStep("canvas", result.error);
                return;
            }
            // The connection worked: keep it for next term's rosters.
            QString error;
            Connect::Settings::saveCanvasUrl(m_settings.canvasUrl, &error);
            if (!m_tokenSaved && !m_canvasToken.isEmpty())
                m_tokenSaved = Connect::Secrets::store("canvas", m_canvasToken, &error);
            m_result = result;
            setStep("classes", result.warnings.join('\n'));
        });
}

void SetupFlow::toggle(int index) {
    if (index < 0 || index >= m_result.classes.size() || m_busy)
        return;
    m_result.classes[index].selected = !m_result.classes[index].selected;
    emit changed();
}

void SetupFlow::finish() {
    if (chosen() == 0)
        return;
    setBusy(true, tr("Setting up your classes…"));
    const Connect::Settings settings = m_settings;
    const QString canvas = m_canvasToken, blackbaud = m_blackbaudToken, root = m_root;
    const Connect::PullResult listed = m_result;
    inBackground(
        [=]() {
            Connect::PullResult r = listed;
            Connect::WriteResult w;
            if (!Connect::fill(settings, canvas, blackbaud, &r, [](const QString &) {}))
                w.error = r.error;
            else
                w = Connect::write(root, r);
            w.lines += r.warnings;
            QStringList ids;
            Importer::assignIds(root, &r.classes);
            for (const auto &d : r.classes)
                if (d.selected)
                    ids << d.id;
            return QPair<Connect::WriteResult, QStringList>(w, ids);
        },
        [this](const QPair<Connect::WriteResult, QStringList> &r) {
            if (!r.first.error.isEmpty()) {
                setStep("classes", r.first.error);
                return;
            }
            m_results = r.first.lines;
            m_written = r.second;
            setStep("done");
            emit finished();
        });
}

void SetupFlow::draftObjectives() {
    const QString omarchy = QStandardPaths::findExecutable("omarchy");
    if (omarchy.isEmpty() || m_written.isEmpty()) {
        setStep("done", tr("Omarchy's agent launcher isn't installed."));
        return;
    }
    QProcess::startDetached(omarchy, {"agent", "prompt", Connect::draftPrompt(m_written)}, m_root);
    setStep("done", tr("Opened your agent in %1").arg(QDir::home().relativeFilePath(m_root).prepend("~/")));
}

void SetupFlow::addOmaClassToBar() {
    QProcess::execute("omarchy", {"bar", "put", "omaclass", "--section", "right", "--index", "0"});
    setStep("done", tr("OmaClass is in your bar."));
}
