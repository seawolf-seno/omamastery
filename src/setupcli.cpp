// `omamastery setup`: the one-time setup in a terminal. Same steps as the
// setup screen: connect Canvas (and Blackbaud if the school has it), tick the
// classes, write them to ~/Classes. Prints counts, never student names.

#include "setupcli.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <termios.h>
#include <unistd.h>

#include "classbook.h"
#include "connect/connect.h"
#include "samples.h"

namespace {

QTextStream &out() {
    static QTextStream s(stdout);
    return s;
}

QString ask(const QString &question, bool hidden = false) {
    out() << question << Qt::flush;
    termios old{};
    const bool tty = isatty(STDIN_FILENO);
    if (hidden && tty) {
        tcgetattr(STDIN_FILENO, &old);
        termios quiet = old;
        quiet.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    QTextStream in(stdin);
    const QString line = in.readLine();
    if (hidden && tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        out() << "\n";
    }
    return line.trimmed();
}

QString describe(const Importer::ClassDraft &d) {
    QStringList parts;
    if (!d.day.isEmpty() || d.block > 0)
        parts << QStringLiteral("%1%2").arg(d.day.isEmpty() ? "?" : d.day, d.block > 0 ? QString::number(d.block) : "?");
    if (!d.room.isEmpty())
        parts << "room " + d.room;
    if (d.studentCount >= 0)
        parts << QStringLiteral("%1 students").arg(d.studentCount);
    QStringList from;
    if (!d.canvasCourse.isEmpty())
        from << "Canvas";
    if (!d.blackbaudSection.isEmpty())
        from << "Blackbaud";
    parts << from.join('+');
    return d.name + "  ·  " + parts.join("  ·  ");
}

} // namespace

int SetupCli::run(const QStringList &arguments) {
    Classbook book(qEnvironmentVariable("OMAMASTERY_HOME"));
    const bool acceptAll = arguments.contains("--yes");
    const bool dryRun = arguments.contains("--dry-run");

    if (arguments.contains("--help") || arguments.contains("-h")) {
        out() << "Usage: omamastery setup [--canvas URL] [--blackbaud] [--yes] [--dry-run] [--forget]\n\n"
                 "Brings your classes from Canvas (and Blackbaud) into " << book.root() << ", once.\n"
                 "  --canvas URL   your school's Canvas address\n"
                 "  --blackbaud    sign in to Blackbaud for days, blocks, rooms and bell times\n"
                 "  --yes          set up the ticked classes without asking\n"
                 "  --dry-run      read everything, write nothing, print counts\n"
                 "  --forget       remove the saved Canvas and Blackbaud connections\n";
        return 0;
    }
    if (arguments.contains("--forget")) {
        Connect::Secrets::forget("canvas");
        Connect::Secrets::forget("blackbaud-refresh");
        out() << "Forgot your Canvas and Blackbaud connections. Your classes are untouched.\n";
        return 0;
    }

    Connect::Settings settings = Connect::Settings::load();
    const int canvasFlag = arguments.indexOf("--canvas");
    if (canvasFlag > 0)
        settings.canvasUrl = Connect::Settings::normalizeCanvasUrl(arguments.value(canvasFlag + 1));
    while (settings.canvasUrl.isEmpty()) {
        const QString typed = ask("Your school's Canvas address (like yourschool.instructure.com): ");
        if (typed.isEmpty())
            return 1;
        settings.canvasUrl = Connect::Settings::normalizeCanvasUrl(typed);
    }

    QString canvasToken = qEnvironmentVariable("OMAMASTERY_CANVAS_TOKEN");
    const bool savedToken = canvasToken.isEmpty() && !(canvasToken = Connect::Secrets::load("canvas")).isEmpty();
    if (canvasToken.isEmpty()) {
        out() << "\nOmaMastery reads your courses with a Canvas access token. In Canvas:\n"
              << "  " << settings.canvasTokenPage().toString() << "\n"
              << "  → \"+ New access token\", purpose \"OmaMastery\", then copy the token.\n\n";
        canvasToken = ask("Paste the token (it won't show): ", true);
        if (canvasToken.isEmpty())
            return 1;
    }

    QString blackbaudToken = qEnvironmentVariable("OMAMASTERY_BLACKBAUD_TOKEN");
    if (blackbaudToken.isEmpty() && settings.blackbaudReady()) {
        QString error;
        blackbaudToken = Connect::Blackbaud::refresh(settings, &error);
        if (blackbaudToken.isEmpty() && arguments.contains("--blackbaud")) {
            out() << "Opening Blackbaud in your browser to sign in…\n" << Qt::flush;
            blackbaudToken = Connect::Blackbaud::signIn(settings, [](const QUrl &url) {
                if (!QProcess::startDetached("xdg-open", {url.toString()}))
                    out() << "Open this address to sign in:\n" << url.toString() << "\n" << Qt::flush;
            }, &error);
        }
        if (!error.isEmpty())
            out() << error << "\n";
    } else if (arguments.contains("--blackbaud") && !settings.blackbaudReady()) {
        out() << "Blackbaud isn't set up for this school yet: add its SKY app to /etc/omamastery/connect.toml.\n";
    }

    // The school calendar decides A and B days; before ~/Classes exists, use the default one.
    QTemporaryDir scratch;
    QString schedulePath = book.schedulePath();
    if (!book.exists()) {
        QString report, error;
        Samples::init(scratch.path(), false, &report, &error);
        schedulePath = scratch.path() + "/schedule.toml";
    }
    Schedule schedule;
    schedule.load(schedulePath);

    auto progress = [](const QString &text) { out() << text << "\n" << Qt::flush; };
    Connect::PullResult result = Connect::list(settings, canvasToken, blackbaudToken, schedule, progress);
    if (!result.error.isEmpty()) {
        out() << result.error << "\n";
        if (savedToken && result.error.contains("token"))
            out() << "Run `omamastery setup --forget`, then `omamastery setup` with a new token.\n";
        return 1;
    }

    // Keep the connection for later (new rosters each term) once it has worked.
    if (!dryRun) {
        QString error;
        Connect::Settings::saveCanvasUrl(settings.canvasUrl, &error);
        if (!savedToken && qEnvironmentVariable("OMAMASTERY_CANVAS_TOKEN").isEmpty() &&
            !Connect::Secrets::store("canvas", canvasToken, &error))
            out() << error << "\n";
    }

    for (;;) {
        out() << "\nYour classes (ticked ones get set up):\n";
        for (int i = 0; i < result.classes.size(); ++i)
            out() << (result.classes[i].selected ? "  [x] " : "  [ ] ") << QString::number(i + 1).rightJustified(2) << "  "
                  << describe(result.classes[i]) << "\n";
        if (acceptAll)
            break;
        const QString reply = ask("\nPress Enter to set up the ticked classes, or type numbers to tick/untick (like 2 5): ");
        if (reply.isEmpty())
            break;
        for (const QString &n : reply.split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts)) {
            const int i = n.toInt() - 1;
            if (i >= 0 && i < result.classes.size())
                result.classes[i].selected = !result.classes[i].selected;
        }
    }

    int chosen = 0;
    for (const auto &d : result.classes)
        chosen += d.selected ? 1 : 0;
    if (chosen == 0) {
        out() << "Nothing ticked, nothing set up.\n";
        return 0;
    }

    if (!Connect::fill(settings, canvasToken, blackbaudToken, &result, progress)) {
        out() << result.error << "\n";
        return 1;
    }

    if (dryRun) {
        out() << "\nDry run: nothing written. Would set up in " << book.root() << ":\n";
        for (const auto &d : result.classes)
            if (d.selected)
                out() << "  " << d.name << ": " << d.students.size() << " students, " << d.objectives.size() << " objectives"
                      << (d.planSource.isEmpty() ? QString() : " from " + d.planSource) << "\n";
    } else {
        const Connect::WriteResult written = Connect::write(book.root(), result);
        if (!written.error.isEmpty()) {
            out() << written.error << "\n";
            return 1;
        }
        out() << "\nSet up in " << book.root() << ":\n";
        for (const auto &line : written.lines)
            out() << "  " << line << "\n";
    }
    for (const auto &warning : result.warnings)
        out() << "Note: " << warning << "\n";
    if (!dryRun)
        out() << "\nNext: put each class's long-range plan in its folder, then open OmaMastery and press\n"
                 "\"Draft objectives\" (or run `omamastery draft`). Student names stay in roster.toml on\n"
                 "this computer; your agent works from the plans only.\n";
    return 0;
}

int SetupCli::draft() {
    Classbook book(qEnvironmentVariable("OMAMASTERY_HOME"));
    QStringList ids;
    for (const auto &info : book.loadClasses())
        if (!info.sample)
            ids << info.id;
    if (ids.isEmpty()) {
        out() << "No real classes yet: run `omamastery setup` first.\n";
        return 1;
    }
    const QString omarchy = QStandardPaths::findExecutable("omarchy");
    if (omarchy.isEmpty()) {
        out() << Connect::draftPrompt(ids) << "\n";
        return 0;
    }
    return QProcess::startDetached(omarchy, {"agent", "prompt", Connect::draftPrompt(ids)}, book.root()) ? 0 : 1;
}
