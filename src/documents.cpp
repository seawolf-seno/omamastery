#include "documents.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

namespace Documents {

namespace {

// First line of every copy we write, so we know which files are ours.
QByteArray marker(const QString &docxName) {
    return QStringLiteral("<!-- Markdown copy of %1, made by OmaMastery for your agent. Edit the .docx, not this. -->\n\n")
        .arg(docxName)
        .toUtf8();
}

bool ours(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readLine().startsWith("<!-- Markdown copy of ");
}

} // namespace

QString markdownNameFor(const QString &docxPath) {
    const QFileInfo info(docxPath);
    const QString plain = info.absolutePath() + "/" + info.completeBaseName() + ".md";
    if (!QFileInfo::exists(plain) || ours(plain))
        return plain;
    return info.absolutePath() + "/" + info.completeBaseName() + "-document.md";
}

QStringList convertForAgents(const QString &dir) {
    QStringList written;
    const QString soffice = QStandardPaths::findExecutable("soffice");
    if (soffice.isEmpty())
        return written;
    for (const QFileInfo &docx : QDir(dir).entryInfoList({"*.docx"}, QDir::Files)) {
        if (docx.fileName().startsWith("~$")) // Word's lock files
            continue;
        const QString target = markdownNameFor(docx.filePath());
        if (QFileInfo::exists(target) && (!ours(target) || QFileInfo(target).lastModified() >= docx.lastModified()))
            continue;
        // Its own profile, so an open LibreOffice window doesn't swallow the job.
        QTemporaryDir work;
        if (!work.isValid())
            continue;
        QProcess p;
        p.start(soffice, {"-env:UserInstallation=" + QUrl::fromLocalFile(work.filePath("profile")).toString(), "--headless",
                          "--convert-to", "md", "--outdir", work.path(), docx.filePath()});
        if (!p.waitForFinished(120000) || p.exitCode() != 0)
            continue;
        QFile made(work.filePath(docx.completeBaseName() + ".md"));
        if (!made.open(QIODevice::ReadOnly))
            continue;
        QSaveFile out(target);
        const QByteArray bytes = marker(docx.fileName()) + made.readAll();
        if (out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit())
            written << QFileInfo(target).fileName();
    }
    return written;
}

} // namespace Documents
