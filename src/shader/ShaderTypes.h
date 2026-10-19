#pragma once

#include <QString>

#include <array>
#include <optional>

namespace pf {

enum class ShaderStage { Vertex, Fragment };

inline constexpr std::array<ShaderStage, 2> kAllStages{ShaderStage::Vertex, ShaderStage::Fragment};

QString toString(ShaderStage stage);
std::optional<ShaderStage> stageFromFileSuffix(const QString& suffix);

// The complete source set for one graphics program.
struct ShaderSources {
    QString vertex;
    QString fragment;

    const QString& get(ShaderStage stage) const
    {
        return stage == ShaderStage::Vertex ? vertex : fragment;
    }
    QString& get(ShaderStage stage) { return stage == ShaderStage::Vertex ? vertex : fragment; }
};

} // namespace pf
