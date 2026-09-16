#pragma once

#include "Rule.h"

#include <QList>
#include <QObject>
#include <QString>

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

    void load();
    bool save();

    const QList<Rule> &rules() const;
    void setRules(const QList<Rule> &rules);

    /// Records "always send this host here", replacing any previous memory for
    /// the same host so repeated choices do not pile up.
    void remember(const QString &host, const QString &targetId, bool privateWindow);
    void forget(const QString &host);
    bool hasMemory(const QString &host) const;

    QString fallbackTargetId() const;
    void setFallbackTargetId(const QString &targetId);

    /// Milliseconds the hold bar stays up before an automatic choice proceeds.
    int holdMs() const;
    void setHoldMs(int ms);

    bool stripTracking() const;
    void setStripTracking(bool strip);

    /// Parameter patterns to strip; "*" suffix matches by prefix.
    QStringList trackingParameters() const;

    /// Ids of "other handler" targets the user has explicitly enabled.
    QStringList enabledOtherHandlers() const;
    void setOtherHandlerEnabled(const QString &targetId, bool enabled);

Q_SIGNALS:
    void changed();

private:
    void watchFile();

    QList<Rule> m_rules;
    QString m_fallbackTargetId;
    int m_holdMs = 600;
    bool m_stripTracking = true;
    QStringList m_trackingParameters;
    QStringList m_enabledOtherHandlers;
    QFileSystemWatcher *m_watcher;
    bool m_writing = false;
};

} // namespace Lob
