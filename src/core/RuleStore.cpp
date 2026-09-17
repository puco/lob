#include "RuleStore.h"
#include "UrlSanitizer.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace Lob
{
namespace
{
const QStringList matchNames{QStringLiteral("host"), QStringLiteral("hostSuffix"),
                             QStringLiteral("pathPrefix"), QStringLiteral("regex")};
const QStringList actionNames{QStringLiteral("open"), QStringLiteral("ask"), QStringLiteral("copy")};

bool validate(const QJsonObject &root, QList<Rule> &rules, QString &error)
{
    const auto bad = [&error](const QString &field) {
        error = QStringLiteral("Invalid rules.json field: %1").arg(field);
        return false;
    };
    if (root.contains(QStringLiteral("version")) && root.value(QStringLiteral("version")) != QJsonValue(1)) {
        return bad(QStringLiteral("version (only version 1 is supported)"));
    }
    if (root.contains(QStringLiteral("holdMs"))) {
        const auto value = root.value(QStringLiteral("holdMs"));
        if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > 60000
            || value.toDouble() != value.toInt(-1)) {
            return bad(QStringLiteral("holdMs (integer from 0 to 60000)"));
        }
    }
    for (const auto &key : {QStringLiteral("stripTracking"), QStringLiteral("unwrapRedirects"),
                            QStringLiteral("resolveShorteners")}) {
        if (root.contains(key) && !root.value(key).isBool()) {
            return bad(key);
        }
    }
    if (root.contains(QStringLiteral("redirectWrappers"))) {
        const auto value = root.value(QStringLiteral("redirectWrappers"));
        if (!value.isObject()) {
            return bad(QStringLiteral("redirectWrappers (\"host[/path]\": \"parameter\")"));
        }
        const auto wrappers = value.toObject();
        for (auto it = wrappers.constBegin(); it != wrappers.constEnd(); ++it) {
            if (it.key().isEmpty() || !it.value().isString() || it.value().toString().isEmpty()) {
                return bad(QStringLiteral("redirectWrappers.") + it.key());
            }
        }
    }
    if (root.contains(QStringLiteral("fallbackTarget")) && !root.value(QStringLiteral("fallbackTarget")).isString()) {
        return bad(QStringLiteral("fallbackTarget"));
    }
    for (const auto &key : {QStringLiteral("trackingParameters"), QStringLiteral("enabledOtherHandlers")}) {
        if (!root.contains(key)) {
            continue;
        }
        if (!root.value(key).isArray()) {
            return bad(key);
        }
        for (const auto &value : root.value(key).toArray()) {
            if (!value.isString() || value.toString().isEmpty()) {
                return bad(key);
            }
        }
    }
    if (root.contains(QStringLiteral("rules")) && !root.value(QStringLiteral("rules")).isArray()) {
        return bad(QStringLiteral("rules"));
    }
    const auto array = root.value(QStringLiteral("rules")).toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QString location = QStringLiteral("rules[%1]").arg(i);
        if (!array.at(i).isObject()) {
            return bad(location);
        }
        const auto obj = array.at(i).toObject();
        for (const auto &key : {QStringLiteral("match"), QStringLiteral("action"), QStringLiteral("pattern"), QStringLiteral("target")}) {
            if (obj.contains(key) && !obj.value(key).isString()) {
                return bad(location + QLatin1Char('.') + key);
            }
        }
        const int match = matchNames.indexOf(obj.value(QStringLiteral("match")).toString(QStringLiteral("host")));
        const int action = actionNames.indexOf(obj.value(QStringLiteral("action")).toString(QStringLiteral("open")));
        const QString pattern = obj.value(QStringLiteral("pattern")).toString().trimmed();
        if (match < 0 || action < 0 || pattern.isEmpty()) {
            return bad(location + QStringLiteral(" (match, action or pattern)"));
        }
        // A pathPrefix pattern carries host and path in one string, and the
        // engine can do nothing with one that has no host or no path: it
        // matches nothing, without ever saying so. Someone who wrote
        // "github.com" meant a host rule, and finding that out now beats
        // wondering later why the rule never fires.
        if (match == static_cast<int>(MatchKind::PathPrefix)) {
            const int slash = pattern.indexOf(QLatin1Char('/'));
            if (slash < 0) {
                return bad(location + QStringLiteral(".pattern (pathPrefix needs a path: \"host/path\", or use match \"host\")"));
            }
            if (slash == 0) {
                return bad(location + QStringLiteral(".pattern (pathPrefix needs a host before the path)"));
            }
        }
        for (const auto &key : {QStringLiteral("private"), QStringLiteral("enabled"), QStringLiteral("remembered"), QStringLiteral("caseSensitive")}) {
            if (obj.contains(key) && !obj.value(key).isBool()) {
                return bad(location + QLatin1Char('.') + key);
            }
        }
        Rule rule;
        rule.extensions = obj;
        rule.matchKind = static_cast<MatchKind>(match);
        rule.action = static_cast<RuleAction>(action);
        rule.pattern = obj.value(QStringLiteral("pattern")).toString();
        rule.targetId = obj.value(QStringLiteral("target")).toString();
        rule.privateWindow = obj.value(QStringLiteral("private")).toBool();
        rule.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        rule.remembered = obj.value(QStringLiteral("remembered")).toBool();
        rule.caseSensitive = obj.value(QStringLiteral("caseSensitive")).toBool();
        if (obj.contains(QStringLiteral("targetVersion")) && obj.value(QStringLiteral("targetVersion")) != QJsonValue(1)
            && obj.value(QStringLiteral("targetVersion")) != QJsonValue(2)) {
            return bad(location + QStringLiteral(".targetVersion"));
        }
        rule.targetVersion = obj.value(QStringLiteral("targetVersion")).toInt(1);
        if (rule.action == RuleAction::Open && rule.targetId.isEmpty()) {
            return bad(location + QStringLiteral(".target (required for open)"));
        }
        rule.compile();
        if (rule.matchKind == MatchKind::Regex && !rule.expression.isValid()) {
            return bad(location + QStringLiteral(".pattern (invalid regular expression)"));
        }
        rules.append(rule);
    }
    return true;
}

QJsonArray encodeRules(const QList<Rule> &rules)
{
    QJsonArray array;
    for (const auto &rule : rules) {
        QJsonObject obj = rule.extensions;
        obj.insert(QStringLiteral("match"), matchNames.at(static_cast<int>(rule.matchKind)));
        obj.insert(QStringLiteral("action"), actionNames.at(static_cast<int>(rule.action)));
        obj.insert(QStringLiteral("pattern"), rule.pattern);
        obj.insert(QStringLiteral("target"), rule.targetId);
        obj.insert(QStringLiteral("private"), rule.privateWindow);
        obj.insert(QStringLiteral("enabled"), rule.enabled);
        obj.insert(QStringLiteral("remembered"), rule.remembered);
        obj.insert(QStringLiteral("caseSensitive"), rule.caseSensitive);
        obj.insert(QStringLiteral("targetVersion"), rule.targetVersion);
        array.append(obj);
    }
    return array;
}

QStringList strings(const QJsonValue &value)
{
    QStringList result;
    for (const auto &entry : value.toArray()) {
        result.append(entry.toString());
    }
    return result;
}
} // namespace

RuleStore::RuleStore(QObject *parent)
    : QObject(parent), m_watcher(new QFileSystemWatcher(this))
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        load();
        watchFile();
    });
    const auto schedule = [this] { m_reloadTimer.start(); };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, schedule);
    load();
    watchFile();
}

QString RuleStore::filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/lob/rules.json");
}

void RuleStore::watchFile()
{
    const QString path = filePath();
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }
    // Watch the nearest existing ancestor too: even lob/ may not exist yet.
    QString dir = QFileInfo(path).absolutePath();
    while (!QFileInfo::exists(dir)) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    if (!m_watcher->directories().contains(dir)) {
        m_watcher->addPath(dir);
    }
}

bool RuleStore::fail(const QString &message)
{
    if (m_error != message) {
        m_error = message;
        Q_EMIT errorOccurred(message);
    }
    return false;
}

QString RuleStore::lastError() const { return m_error; }

void RuleStore::apply(const QJsonObject &root, const QList<Rule> &rules)
{
    m_root = root;
    m_rules = rules;
    m_fallbackTargetId = root.value(QStringLiteral("fallbackTarget")).toString();
    m_holdMs = root.value(QStringLiteral("holdMs")).toInt(600);
    m_stripTracking = root.value(QStringLiteral("stripTracking")).toBool(true);
    m_trackingParameters = root.contains(QStringLiteral("trackingParameters"))
        ? strings(root.value(QStringLiteral("trackingParameters"))) : UrlSanitizer::defaultTrackingParameters();
    m_enabledOtherHandlers = strings(root.value(QStringLiteral("enabledOtherHandlers")));
    m_unwrapRedirects = root.value(QStringLiteral("unwrapRedirects")).toBool(true);
    m_resolveShorteners = root.value(QStringLiteral("resolveShorteners")).toBool(false);
    m_redirectWrappers.clear();
    const auto wrappers = root.value(QStringLiteral("redirectWrappers")).toObject();
    for (auto it = wrappers.constBegin(); it != wrappers.constEnd(); ++it) {
        m_redirectWrappers.insert(it.key(), it.value().toString());
    }
    m_writable = true;
    m_error.clear();
    Q_EMIT changed();
}

bool RuleStore::load()
{
    QFile file(filePath());
    if (!QFileInfo::exists(file.fileName())) {
        // No file is a valid empty configuration, including when someone
        // deletes it to start over. Forgetting what it used to hold is what
        // makes the next save possible rather than an error about a file that
        // is not there any more.
        m_hasFile = false;
        m_writable = true;
        m_snapshot.clear();
        apply({}, {});
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_writable = false;
        return fail(i18n("Cannot read rules.json; keeping the last valid configuration."));
    }
    const QByteArray bytes = file.readAll();
    if (m_hasFile && m_writable && bytes == m_snapshot) {
        return true;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    QList<Rule> rules;
    QString error;
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_writable = false;
        return fail(i18n("Invalid rules.json at byte %1; keeping the last valid configuration.", parseError.offset));
    }
    if (!validate(doc.object(), rules, error)) {
        m_writable = false;
        return fail(error);
    }
    m_snapshot = bytes;
    m_hasFile = true;
    apply(doc.object(), rules);
    return true;
}

bool RuleStore::writeRoot(const QJsonObject &root)
{
    if (!m_writable) {
        Q_EMIT errorOccurred(i18n("Cannot save until rules.json is readable and valid again."));
        return false;
    }
    QList<Rule> rules;
    QString error;
    if (!validate(root, rules, error)) {
        return fail(error);
    }
    const QString path = filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return fail(i18n("Cannot create the Lob configuration directory."));
    }
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) {
        return fail(i18n("Another process is updating rules.json; try again."));
    }
    QFile current(path);
    const bool exists = QFileInfo::exists(path);
    if (exists != m_hasFile || (exists && (!current.open(QIODevice::ReadOnly) || current.readAll() != m_snapshot))) {
        load();
        return fail(i18n("rules.json changed externally. Review the new configuration and try again."));
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return fail(i18n("Could not save rules.json: %1", file.errorString()));
    }
    m_snapshot = bytes;
    m_hasFile = true;
    apply(root, rules);
    watchFile();
    return true;
}

bool RuleStore::save() { return writeRoot(m_root); }
const QList<Rule> &RuleStore::rules() const { return m_rules; }
bool RuleStore::setRules(const QList<Rule> &rules)
{
    auto root = m_root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("rules"), encodeRules(rules));
    return writeRoot(root);
}

bool RuleStore::remember(const QString &host, const QString &targetId, bool privateWindow)
{
    if (host.isEmpty() || targetId.isEmpty()) {
        return false;
    }
    auto rules = m_rules;
    rules.removeIf([&host](const Rule &r) { return r.remembered && r.matchKind == MatchKind::Host && r.pattern.compare(host, Qt::CaseInsensitive) == 0; });
    Rule rule;
    rule.pattern = host;
    rule.targetId = targetId;
    rule.privateWindow = privateWindow;
    rule.remembered = true;
    rule.targetVersion = 2;
    rules.append(rule);
    return setRules(rules);
}

bool RuleStore::forget(const QString &host)
{
    auto rules = m_rules;
    rules.removeIf([&host](const Rule &r) { return r.remembered && r.matchKind == MatchKind::Host && r.pattern.compare(host, Qt::CaseInsensitive) == 0; });
    return setRules(rules);
}
bool RuleStore::hasMemory(const QString &host) const
{
    return std::any_of(m_rules.cbegin(), m_rules.cend(), [&host](const Rule &r) {
        return r.remembered && r.matchKind == MatchKind::Host && r.pattern.compare(host, Qt::CaseInsensitive) == 0;
    });
}

QString RuleStore::fallbackTargetId() const { return m_fallbackTargetId; }
int RuleStore::holdMs() const { return m_holdMs; }
bool RuleStore::stripTracking() const { return m_stripTracking; }
QStringList RuleStore::trackingParameters() const { return m_trackingParameters; }
bool RuleStore::unwrapRedirects() const { return m_unwrapRedirects; }
bool RuleStore::resolveShorteners() const { return m_resolveShorteners; }
QMap<QString, QString> RuleStore::redirectWrappers() const { return m_redirectWrappers; }
QStringList RuleStore::enabledOtherHandlers() const { return m_enabledOtherHandlers; }

bool RuleStore::setFallbackTargetId(const QString &id)
{
    auto root = m_root;
    root.insert(QStringLiteral("fallbackTarget"), id);
    return writeRoot(root);
}
bool RuleStore::setHoldMs(int ms)
{
    auto root = m_root;
    root.insert(QStringLiteral("holdMs"), ms);
    return writeRoot(root);
}
bool RuleStore::setStripTracking(bool strip)
{
    auto root = m_root;
    root.insert(QStringLiteral("stripTracking"), strip);
    return writeRoot(root);
}
bool RuleStore::setOtherHandlerEnabled(const QString &id, bool enabled)
{
    auto handlers = m_enabledOtherHandlers;
    handlers.removeAll(id);
    if (enabled) {
        handlers.append(id);
    }
    auto root = m_root;
    root.insert(QStringLiteral("enabledOtherHandlers"), QJsonArray::fromStringList(handlers));
    return writeRoot(root);
}
} // namespace Lob
