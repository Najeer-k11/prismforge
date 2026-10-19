#include "graph/NodeRegistry.h"

#include "diagnostics/Logging.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace pf::graph {

const InputDefinition* NodeDefinition::input(const QString& id) const
{
    for (const InputDefinition& i : inputs)
        if (i.id == id)
            return &i;
    return nullptr;
}

const OutputDefinition* NodeDefinition::output(const QString& id) const
{
    for (const OutputDefinition& o : outputs)
        if (o.id == id)
            return &o;
    return nullptr;
}

bool NodeDefinition::hasGenericPorts() const
{
    return std::any_of(inputs.begin(), inputs.end(), [](const auto& i) { return i.type.generic; }) ||
           std::any_of(outputs.begin(), outputs.end(), [](const auto& o) { return o.type.generic; });
}

namespace {

const QRegularExpression kIdentifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
const QRegularExpression kPlaceholder(QStringLiteral(R"(\{(\w+)\})"));

std::optional<PortType> parsePortType(const QString& name)
{
    if (name == u"T")
        return PortType{true, DataType::Float};
    if (auto t = dataTypeFromString(name))
        return PortType{false, *t};
    return std::nullopt;
}

// Checks that every {placeholder} in `text` is in `allowed`.
QString checkPlaceholders(const QString& text, const QSet<QString>& allowed)
{
    auto it = kPlaceholder.globalMatch(text);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (!allowed.contains(name))
            return QStringLiteral("unknown placeholder {%1}").arg(name);
    }
    return {};
}

std::optional<NodeDefinition> parseDefinition(const QJsonObject& json, const QString& origin, QString* error)
{
    NodeDefinition def;
    def.origin = origin;
    def.id = json.value("id").toString();
    def.title = json.value("title").toString(def.id);
    def.category = json.value("category").toString(QStringLiteral("Custom"));
    def.description = json.value("description").toString();
    def.functions = json.value("functions").toString();
    def.mainTemplate = json.value("main").toString();

    auto fail = [&](const QString& message) {
        *error = QStringLiteral("node '%1': %2").arg(def.id.isEmpty() ? QStringLiteral("?") : def.id, message);
        return std::nullopt;
    };
    if (def.id.isEmpty())
        return fail(QStringLiteral("missing 'id'"));

    const QString role = json.value("role").toString(QStringLiteral("function"));
    if (role == u"function")
        def.role = NodeRole::Function;
    else if (role == u"parameter")
        def.role = NodeRole::Parameter;
    else if (role == u"output")
        def.role = NodeRole::Output;
    else
        return fail(QStringLiteral("unknown role '%1'").arg(role));

    QSet<QString> placeholders{QStringLiteral("T")};
    for (const QJsonValue& v : json.value("inputs").toArray()) {
        const QJsonObject o = v.toObject();
        InputDefinition in;
        in.id = o.value("id").toString();
        in.label = o.value("label").toString(in.id);
        in.connectable = o.value("connectable").toBool(true);
        in.color = o.value("color").toBool(false);
        in.defaultExpression = o.value("defaultExpr").toString();
        if (!kIdentifier.match(in.id).hasMatch())
            return fail(QStringLiteral("invalid input id '%1'").arg(in.id));
        const auto type = parsePortType(o.value("type").toString());
        if (!type)
            return fail(QStringLiteral("input '%1' has invalid type").arg(in.id));
        in.type = *type;
        const QJsonArray dv = o.value("default").toArray();
        if (dv.size() == 1) {
            in.defaultValue.fill(float(dv[0].toDouble()));
        } else if (dv.size() > 1 && dv.size() <= 4) {
            for (int i = 0; i < dv.size(); ++i)
                in.defaultValue[std::size_t(i)] = float(dv[i].toDouble());
        } else if (!dv.isEmpty()) {
            return fail(QStringLiteral("input '%1' default must have 1-4 components").arg(in.id));
        }
        if (placeholders.contains(in.id))
            return fail(QStringLiteral("duplicate input '%1'").arg(in.id));
        placeholders.insert(in.id);
        def.inputs.append(in);
    }
    if (def.role == NodeRole::Parameter)
        placeholders.insert(QStringLiteral("name"));

    bool hasGenericInput = std::any_of(def.inputs.begin(), def.inputs.end(),
                                       [](const InputDefinition& i) { return i.type.generic; });
    QSet<QString> outputIds;
    for (const QJsonValue& v : json.value("outputs").toArray()) {
        const QJsonObject o = v.toObject();
        OutputDefinition out;
        out.id = o.value("id").toString();
        out.label = o.value("label").toString(out.id);
        out.expression = o.value("expr").toString();
        if (!kIdentifier.match(out.id).hasMatch() || outputIds.contains(out.id))
            return fail(QStringLiteral("invalid or duplicate output id '%1'").arg(out.id));
        outputIds.insert(out.id);
        const auto type = parsePortType(o.value("type").toString());
        if (!type)
            return fail(QStringLiteral("output '%1' has invalid type").arg(out.id));
        out.type = *type;
        if (out.type.generic && !hasGenericInput)
            return fail(QStringLiteral("generic output '%1' needs a generic input").arg(out.id));
        if (out.expression.trimmed().isEmpty())
            return fail(QStringLiteral("output '%1' has no 'expr'").arg(out.id));
        if (const QString e = checkPlaceholders(out.expression, placeholders); !e.isEmpty())
            return fail(QStringLiteral("output '%1': %2").arg(out.id, e));
        def.outputs.append(out);
    }
    if (const QString e = checkPlaceholders(def.mainTemplate, placeholders); !e.isEmpty())
        return fail(QStringLiteral("main: %1").arg(e));

    switch (def.role) {
    case NodeRole::Function:
        if (def.outputs.isEmpty())
            return fail(QStringLiteral("function nodes need at least one output"));
        break;
    case NodeRole::Parameter: {
        const InputDefinition* d = def.input(QStringLiteral("default"));
        if (def.outputs.size() != 1 || def.outputs[0].type.generic)
            return fail(QStringLiteral("parameter nodes need exactly one concrete output"));
        if (!d || d->connectable || d->type.generic || d->type.concrete != def.outputs[0].type.concrete)
            return fail(QStringLiteral("parameter nodes need a non-connectable 'default' input "
                                       "of the output's type"));
        break;
    }
    case NodeRole::Output:
        if (!def.outputs.isEmpty() || def.mainTemplate.trimmed().isEmpty())
            return fail(QStringLiteral("output nodes need a 'main' template and no outputs"));
        break;
    }
    return def;
}

} // namespace

int NodeRegistry::loadJson(const QByteArray& json, const QString& origin, QStringList* errors)
{
    auto report = [&](const QString& message) {
        if (errors)
            errors->append(QStringLiteral("%1: %2").arg(origin, message));
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        report(QStringLiteral("invalid JSON at offset %1: %2").arg(parseError.offset).arg(parseError.errorString()));
        return 0;
    }
    const QJsonObject root = doc.object();
    if (root.value("formatVersion").toInt() != 1) {
        report(QStringLiteral("unsupported or missing formatVersion (expected 1)"));
        return 0;
    }
    int loaded = 0;
    for (const QJsonValue& v : root.value("nodes").toArray()) {
        QString error;
        if (auto def = parseDefinition(v.toObject(), origin, &error)) {
            m_definitions.insert(def->id, *def);
            ++loaded;
        } else {
            report(error);
        }
    }
    return loaded;
}

int NodeRegistry::loadFile(const QString& path, QStringList* errors)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errors)
            errors->append(QStringLiteral("%1: %2").arg(path, file.errorString()));
        return 0;
    }
    return loadJson(file.readAll(), path, errors);
}

int NodeRegistry::loadDirectory(const QString& directory, QStringList* errors)
{
    int loaded = 0;
    const QDir dir(directory);
    for (const QString& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        loaded += loadFile(dir.filePath(name), errors);
    return loaded;
}

const NodeRegistry& NodeRegistry::builtin()
{
    static const NodeRegistry registry = [] {
        NodeRegistry r;
        QStringList errors;
        r.loadFile(QStringLiteral(":/nodes/builtin.json"), &errors);
        for (const QString& e : errors)
            qCCritical(lcApp).noquote() << "Built-in node library:" << e;
        return r;
    }();
    return registry;
}

const NodeDefinition* NodeRegistry::find(const QString& id) const
{
    const auto it = m_definitions.constFind(id);
    return it == m_definitions.constEnd() ? nullptr : &*it;
}

QList<const NodeDefinition*> NodeRegistry::all() const
{
    QList<const NodeDefinition*> list;
    for (const NodeDefinition& d : m_definitions)
        list.append(&d);
    std::sort(list.begin(), list.end(), [](const NodeDefinition* a, const NodeDefinition* b) {
        return std::tie(a->category, a->title, a->id) < std::tie(b->category, b->title, b->id);
    });
    return list;
}

} // namespace pf::graph
