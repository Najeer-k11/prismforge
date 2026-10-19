#pragma once

#include <QMatrix4x4>
#include <QVector3D>

namespace pf {

// Turntable camera orbiting a target point. Angles are in degrees.
class OrbitCamera {
public:
    static constexpr float kMinDistance = 0.5f;
    static constexpr float kMaxDistance = 50.0f;
    static constexpr float kMaxPitch = 89.0f;

    void set(float yawDegrees, float pitchDegrees, float distance);
    void orbit(float deltaYawDegrees, float deltaPitchDegrees);
    // Positive steps move closer; each step scales distance by ~10%.
    void zoom(float steps);

    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    float distance() const { return m_distance; }

    QVector3D position() const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(float aspect) const;

private:
    float m_yaw = 30.0f;
    float m_pitch = 20.0f;
    float m_distance = 3.5f;
    QVector3D m_target{0, 0, 0};
    float m_fovY = 45.0f;
};

} // namespace pf
