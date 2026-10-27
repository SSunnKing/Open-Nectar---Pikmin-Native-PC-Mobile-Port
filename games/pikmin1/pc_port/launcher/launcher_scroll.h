#pragma once

#include <algorithm>
#include <cmath>

namespace pikmin {
namespace launcher {

// Scroll con la rueda: un paso de rueda mueve una fila (no las ~5 líneas de
// ImGui) y el desplazamiento se suaviza un poco en vez de saltar.
//
// El objetivo se recoloca en la posición real cuando el scroll cambia por otro
// camino (barra arrastrada, contenido que encoge, SetScrollHereY...).
struct SmoothScroll {
    float target = 0.0f;
    float lastSet = 0.0f;
    bool started = false;
};

constexpr float kScrollEaseSpeed = 20.0f; // ~0,1 s hasta el 90 %
constexpr float kScrollSnap = 0.5f;       // px: por debajo se llega de golpe

// Devuelve la posición de scroll para este fotograma. `wheel` son pasos de
// rueda (positivo = hacia arriba, como io.MouseWheel; las trackpads mandan
// fracciones) y `step` el alto de una fila en píxeles.
inline float smoothScrollStep(SmoothScroll& s, float current, float maxScroll, float wheel, float step, float dt,
                              float speed = kScrollEaseSpeed)
{
    maxScroll = std::max(0.0f, maxScroll);
    if (!s.started || std::fabs(current - s.lastSet) > kScrollSnap) s.target = current;
    s.started = true;
    if (wheel != 0.0f) s.target -= wheel * step;
    s.target = std::clamp(s.target, 0.0f, maxScroll);

    float next = current + (s.target - current) * (1.0f - std::exp(-speed * std::max(0.0f, dt)));
    if (std::fabs(s.target - next) < kScrollSnap) next = s.target;
    s.lastSet = next;
    return next;
}

} // namespace launcher
} // namespace pikmin
