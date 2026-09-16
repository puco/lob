#include "RuleStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

Q_LOGGING_CATEGORY(LOG_RULES, "lob.rules")

namespace Lob
{

namespace
{

constexpr int kSchemaVersion = 1;

QString matchKindToString(MatchKind kind)
{
    switch (kind) {
    case MatchKind::Host:
        return QStringLiteral("host");
    case MatchKind::HostSuffix:
        return QStringLiteral("hostSuffix");
    case MatchKind::PathPrefix:
        return QStringLiteral("pathPrefix");
    case MatchKind::Regex:
        return QStringLiteral("regex");
    }
    return QStringLiteral("host");
}

MatchKind matchKindFromString(const QString &value)
{
    if (value == QLatin1String("hostSuffix")) {
        return MatchKind::HostSuffix;
    }
    if (value == QLatin1String("pathPrefix")) {
        return MatchKind::PathPrefix;
    }
    if (value == QLatin1String("regex")) {
        return MatchKind::Regex;
    }
    return MatchKind::Host;
}

QString actionToString(RuleAction action)
{
    switch (action) {
    case RuleAction::Open:
        return QStringLiteral("open");
    case RuleAction::Ask:
        return QStringLiteral("ask");
    case RuleAction::Copy:
        return QStringLiteral("copy");
    }
    return QStringLiteral("open");
}

RuleAction actionFromString(const QString &value)
{
    if (value == QLatin1String("ask")) {
        return RuleAction::Ask;
    }
    if (value == QLatin1String("copy")) {
        return RuleAction::Copy;
    }
    return RuleAction::Open;
}

} // namespace

RuleStore::RuleStore(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        if (m_writing) {
            return;
        }
        // Editors replace rather than rewrite, which drops the watch, so it is
        // re-established after every change.
        QTimer::singleShot(100, this, [this] {
            load();
            watchFile();
            Q_EMIT changed();
        });
    });

    load();
    watchFile();
}

QString RuleStore::filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1String("/lob/rules.json");
}

void RuleStore::watchFile()
{
    const QString path = filePath();
    if (!m_watcher->files().contains(path) && QFileInfo::exists(path)) {
        m_watcher->addPath(path);
    }
}

void RuleStore::load()
{
    m_rules.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return; // no config yet is a perfectly normal state
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        // Keep whatever is on disk: overwriting a file the user was editing
        // because it is momentarily invalid would lose their work.
        qCWarning(LOG_RULES) << "ignoring malformed" << filePath() << ":" << error.errorString();
        return;
    }

    const QJsonObject root = doc.object();
    m_fallbackTargetId = root.value(QStringLiteral("fallbackTarget")).toString();
    m_holdMs = root.value(QStringLiteral("holdMs")).toInt(600);

    m_enabledOtherHandlers.clear();
    const QJsonArray enabled = root.value(QStringLiteral("enabledOtherHandlers")).toArray();
    for (const QJsonValue &value : enabled) {
        m_enabledOtherHandlers << value.toString();
    }

    const QJsonArray rules = root.value(QStringLiteral("rules")).toArray();
    for (const QJsonValue &value : rules) {
        const QJsonObject object = value.toObject();

        Rule rule;
        rule.matchKind = matchKindFromString(object.value(QStringLiteral("match")).toString());
        rule.pattern = object.value(QStringLiteral("pattern")).toString();
        rule.action = actionFromString(object.value(QStringLiteral("action")).toString());
        rule.targetId = object.value(QStringLiteral("target")).toString();
        rule.privateWindow = object.value(QStringLiteral("private")).toBool();
        rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        rule.remembered = object.value(QStringLiteral("remembered")).toBool();

        if (!rule.pattern.isEmpty()) {
            m_rules.append(rule);
        }
    }
}

bool RuleStore::save()
{
    QJsonArray rules;
    for (const Rule &rule : std::as_const(m_rules)) {
        QJsonObject object;
        object.insert(QStringLiteral("match"), matchKindToString(rule.matchKind));
        object.insert(QStringLiteral("pattern"), rule.pattern);
        object.insert(QStringLiteral("action"), actionToString(rule.action));
        if (!rule.targetId.isEmpty()) {
            object.insert(QStringLiteral("target"), rule.targetId);
        }
        if (rule.privateWindow) {
            object.insert(QStringLiteral("private"), true);
        }
        if (!rule.enabled) {
            object.insert(QStringLiteral("enabled"), false);
        }
        if (rule.remembered) {
            object.insert(QStringLiteral("remembered"), true);
        }
        rules.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);
    root.insert(QStringLiteral("holdMs"), m_holdMs);
    if (!m_fallbackTargetId.isEmpty()) {
        root.insert(QStringLiteral("fallbackTarget"), m_fallbackTargetId);
    }
    if (!m_enabledOtherHandlers.isEmpty()) {
        root.insert(QStringLiteral("enabledOtherHandlers"), QJsonArray::fromStringList(m_enabledOtherHandlers));
    }
    root.insert(QStringLiteral("rules"), rules);

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    m_writing = true;
    QSaveFile file(path);
    bool ok = file.open(QIODevice::WriteOnly);
    if (ok) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        ok = file.commit();
    }
    m_writing = false;

    if (!ok) {
        qCWarning(LOG_RULES) << "could not write" << path << ":" << file.errorString();
        return false;
    }

    watchFile();
    return true;
}

const QList<Rule> &RuleStore::rules() const
{
    return m_rules;
}

void RuleStore::setRules(const QList<Rule> &rules)
{
    m_rules = rules;
    save();
    Q_EMIT changed();
}

void RuleStore::remember(const QString &host, const QString &targetId, bool privateWindow)
{
    if (host.isEmpty()) {
        return;
    }

    m_rules.removeIf([&host](const Rule &rule) {
        return rule.remembered && rule.matchKind == MatchKind::Host
            && rule.pattern.compare(host, Qt::CaseInsensitive) == 0;
    });

    Rule rule;
    rule.matchKind = MatchKind::Host;
    rule.pattern = host;
    rule.action = RuleAction::Open;
    rule.targetId = targetId;
    rule.privateWindow = privateWindow;
    rule.remembered = true;
    m_rules.append(rule);

    save();
    Q_EMIT changed();
}

void RuleStore::forget(const QString &host)
{
    const auto removed = m_rules.removeIf([&host](const Rule &rule) {
        return rule.remembered && rule.pattern.compare(host, Qt::CaseInsensitive) == 0;
    });
    if (removed > 0) {
        save();
        Q_EMIT changed();
    }
}

bool RuleStore::hasMemory(const QString &host) const
{
    return std::any_of(m_rules.cbegin(), m_rules.cend(), [&host](const Rule &rule) {
        return rule.remembered && rule.pattern.compare(host, Qt::CaseInsensitive) == 0;
    });
}

QString RuleStore::fallbackTargetId() const
{
    return m_fallbackTargetId;
}

void RuleStore::setFallbackTargetId(const QString &targetId)
{
    m_fallbackTargetId = targetId;
    save();
    Q_EMIT changed();
}

int RuleStore::holdMs() const
{
    return m_holdMs;
}

void RuleStore::setHoldMs(int ms)
{
    m_holdMs = qMax(0, ms);
    save();
    Q_EMIT changed();
}

QStringList RuleStore::enabledOtherHandlers() const
{
    return m_enabledOtherHandlers;
}

void RuleStore::setOtherHandlerEnabled(const QString &targetId, bool enabled)
{
    if (enabled) {
        if (!m_enabledOtherHandlers.contains(targetId)) {
            m_enabledOtherHandlers << targetId;
        }
    } else {
        m_enabledOtherHandlers.removeAll(targetId);
    }
    save();
    Q_EMIT changed();
}

} // namespace Lob
