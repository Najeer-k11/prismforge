#include "render/OrbitCamera.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pf {

void OrbitCamera::set(float yawDegrees, float pitchDegrees, float distance)
{
    m_yaw = std::fmod(yawDegrees, 360.0f);
    m_pitch = std::clamp(pitchDegrees, -kMaxPitch, kMaxPitch);
    m_distance = std::clamp(distance, kMinDistance, kMaxDistance);
}

void OrbitCamera::orbit(float deltaYawDegrees, float deltaPitchDegrees)
{
    set(m_yaw + deltaYawDegrees, m_pitch + deltaPitchDegrees, m_distance);
}

void OrbitCamera::zoom(float steps)
{
    set(m_yaw, m_pitch, m_distance * std::pow(0.9f, steps));
}

QVector3D OrbitCamera::position() const
{
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D offset(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                           std::cos(pitch) * std::cos(yaw));
    return m_target + offset * m_distance;
}

QMatrix4x4 OrbitCamera::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(position(), m_target, QVector3D(0, 1, 0));
    return view;
}

QMatrix4x4 OrbitCamera::projectionMatrix(float aspect) const
{
    QMatrix4x4 proj;
    proj.perspective(m_fovY, std::max(aspect, 0.01f), 0.05f, 200.0f);
    return proj;
}

} // namespace pf
