#pragma once

#include "material/MaterialParameter.h"

#include <QString>

#include <optional>

namespace pf::graph {

// Value types flowing along graph connections. The enum value is the
// component count.
enum class DataType { Float = 1, Vec2 = 2, Vec3 = 3, Vec4 = 4 };

QString glslName(DataType type);
std::optional<DataType> dataTypeFromString(const QString& name);
inline int componentCount(DataType type) { return int(type); }
ParamType toParamType(DataType type);

// A port is either a concrete type or the node's generic type "T", which is
// resolved per node instance to the widest type connected to its T inputs.
struct PortType {
    bool generic = false;
    DataType concrete = DataType::Float;
};

// Implicit conversion rules:
//   float -> vecN        splat           vec3(x)
//   vecN  -> vecM (M<N)  truncate        x.xy
//   vecN  -> vecM (M>N)  pad             vec4(x, 1.0)  (w pads with 1, others with 0)
//   vecN  -> float       not allowed: use a Split node to pick a component
bool canConvert(DataType from, DataType to);
// Returns the converted expression; `expr` must be convertible.
QString convertExpression(const QString& expr, DataType from, DataType to);

// GLSL literal of `type` built from the first components of `value`.
QString glslLiteral(DataType type, const ParamValue& value);

} // namespace pf::graph
