#pragma once

#include <QObject>
#include <QVariant>

#include "connect/connect.h"

// The setup screen behind omamastery://setup: connect Canvas, optionally
// Blackbaud, tick classes, write them. Network work runs on a worker thread;
// the screen only ever sees class names, day/block/room and counts.
class SetupFlow : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString step READ step NOTIFY changed)          // canvas, blackbaud, classes, done
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString canvasUrl READ canvasUrl NOTIFY changed)
    Q_PROPERTY(QVariantList classes READ classes NOTIFY changed)
    Q_PROPERTY(int chosen READ chosen NOTIFY changed)
    Q_PROPERTY(QStringList results READ results NOTIFY changed)
    Q_PROPERTY(bool canAddOmaClass READ canAddOmaClass NOTIFY changed)

public:
    explicit SetupFlow(const QString &root, QObject *parent = nullptr);

    QString step() const { return m_step; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString canvasUrl() const { return m_settings.canvasUrl; }
    QVariantList classes() const;
    int chosen() const;
    QStringList results() const { return m_results; }
    bool canAddOmaClass() const;

    // From the one-time link: omamastery://setup?canvas=school.instructure.com
    Q_INVOKABLE void start(const QString &link = {});
    Q_INVOKABLE void setCanvasUrl(const QString &url);
    Q_INVOKABLE void openTokenPage();
    Q_INVOKABLE void connectCanvas(const QString &token);
    Q_INVOKABLE void connectBlackbaud();
    Q_INVOKABLE void skipBlackbaud();
    Q_INVOKABLE void toggle(int index);
    Q_INVOKABLE void finish();
    Q_INVOKABLE void draftObjectives();
    Q_INVOKABLE void addOmaClassToBar();

signals:
    void changed();
    void finished(); // classes were written: the app reloads

private:
    void runList();
    void setStep(const QString &step, const QString &message = {});
    void setBusy(bool busy, const QString &message = {});
    template <typename Work, typename Done> void inBackground(Work work, Done done);

    QString m_root;
    Connect::Settings m_settings;
    QString m_canvasToken;
    QString m_blackbaudToken;
    bool m_tokenSaved = false;
    Connect::PullResult m_result;
    QStringList m_results;
    QStringList m_written;
    QString m_step = "canvas";
    QString m_message;
    bool m_busy = false;
};
