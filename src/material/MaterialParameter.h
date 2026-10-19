#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <array>
#include <optional>

namespace pf {

// Uniform types that can be edited as material parameters.
enum class ParamType { Float, Vec2, Vec3, Vec4, Int, Bool };

int componentCount(ParamType type);
QString toString(ParamType type);
std::optional<ParamType> paramTypeFromString(const QString& name);

// Editor hints written in a trailing comment on the uniform declaration:
//   uniform float uRoughness = 0.5; // @range(0, 1) @label(Roughness)
//   uniform vec3  uTint = vec3(1);  // @color
struct ParamHints {
    std::optional<float> min;
    std::optional<float> max;
    bool color = false;
    QString label;
    bool operator==(const ParamHints&) const = default;
};

using ParamValue = std::array<float, 4>; // unused components are 0; int/bool stored as float

// A float as the shortest double that round-trips to it (0.05f -> 0.05, not
// 0.0500000007...). Keeps JSON files readable and diffs small.
double toJsonNumber(float value);

struct MaterialParameter {
    QString name;
    ParamType type = ParamType::Float;
    ParamValue value{};        // current value
    ParamValue defaultValue{}; // value the program had right after linking
    ParamHints hints;

    QString displayName() const { return hints.label.isEmpty() ? name : hints.label; }
    bool operator==(const MaterialParameter&) const = default;
};

// Uniforms PrismForge drives itself; they never appear as parameters.
bool isBuiltinUniform(const QString& name);

QHash<QString, ParamHints> parseParameterHints(const QString& source);

// Saved overrides: { "uTint": { "type": "vec3", "value": [1, 0.5, 0] } }
QJsonObject parameterValuesToJson(const QHash<QString, MaterialParameter>& values);
QHash<QString, MaterialParameter> parameterValuesFromJson(const QJsonObject& json);

} // namespace pf
