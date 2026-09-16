#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// A harmless browser stand-in for launch integration tests.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile output(qEnvironmentVariable("LOB_TEST_OUTPUT"));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Append)) { return 1; }
    QJsonObject record;
    record.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(app.arguments()));
    record.insert(QStringLiteral("token"), qEnvironmentVariable("XDG_ACTIVATION_TOKEN"));
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    return output.write(bytes) == bytes.size() ? 0 : 1;
}
