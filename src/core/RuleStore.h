#pragma once

#include "Rule.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QTimer>

class QFileSystemWatcher;

namespace Lob
{

/**
 * Rules and preferences in ~/.config/lob/rules.json.
 *
 * JSON rather than KConfig because the interesting content is an ordered list
 * the user is expected to read, diff and hand-edit. The file is watched, so an
 * edit outside the app takes effect without a restart.
 */
class RuleStore : public QObject
{
    Q_OBJECT

public:
    explicit RuleStore(QObject *parent = nullptr);

    static QString filePath();

    bool load();
    bool save();
    QString lastError() const;

    const QList<Rule> &rules() const;
    bool setRules(const QList<Rule> &rules);

    /// Records "always send this host here", replacing any previous memory for
    /// the same host so repeated choices do not pile up.
    bool remember(const QString &host, const QString &targetId, bool privateWindow);
    bool forget(const QString &host);
    bool hasMemory(const QString &host) const;

    QString fallbackTargetId() const;
    bool setFallbackTargetId(const QString &targetId);

    /// Milliseconds the hold bar stays up before an automatic choice proceeds.
    int holdMs() const;
    bool setHoldMs(int ms);

    bool stripTracking() const;
    bool setStripTracking(bool strip);

    /// Parameter patterns to strip; "*" suffix matches by prefix.
    QStringList trackingParameters() const;

    /// Whether a link that arrives wrapped in a redirector is read through to
    /// where it actually goes before anything is decided about it.
    bool unwrapRedirects() const;

    /// Redirectors beyond the built-in ones: "host[/path]" -> query parameter.
    QMap<QString, QString> redirectWrappers() const;

    /// Whether a shortener may be asked over the network where its link goes.
    /// Off by default: it costs a request, and a wait, per shortened link.
    bool resolveShorteners() const;

    /// Ids of "other handler" targets the user has explicitly enabled.
    QStringList enabledOtherHandlers() const;
    bool setOtherHandlerEnabled(const QString &targetId, bool enabled);

Q_SIGNALS:
    void changed();
    void errorOccurred(const QString &message);

private:
    void watchFile();
    bool writeRoot(const QJsonObject &root);
    bool fail(const QString &message);
    void apply(const QJsonObject &root, const QList<Rule> &rules);

    QList<Rule> m_rules;
    QString m_fallbackTargetId;
    int m_holdMs = 600;
    bool m_stripTracking = true;
    QStringList m_trackingParameters;
    bool m_unwrapRedirects = true;
    bool m_resolveShorteners = false;
    QMap<QString, QString> m_redirectWrappers;
    QStringList m_enabledOtherHandlers;
    QFileSystemWatcher *m_watcher;
    QTimer m_reloadTimer;
    QJsonObject m_root;
    QByteArray m_snapshot;
    bool m_hasFile = false;
    bool m_writable = true;
    QString m_error;
};

} // namespace Lob
