#pragma once

#include <algorithm>

namespace pikmin::launcher {

// Reparto vertical de la lista de páginas de Ajustes. La lista tiene que
// terminar por encima de "Reset all" sea cual sea la altura de la ventana o el
// número de páginas: primero se estrecha el hueco hasta un mínimo cómodo,
// luego la altura de los botones, después el hueco hasta el mínimo y, si ni así
// cabe, la lista pasa a tener scroll propio.
struct SidebarLayout {
    float height = 0.0f;  // alto de cada botón
    float step = 0.0f;    // distancia entre el inicio de dos botones
    bool scroll = false;  // la lista no cabe: dibujarla dentro de una región con scroll
    float content = 0.0f; // alto total de la lista con este reparto
};

struct SidebarMetrics {
    float preferredHeight = 40.0f;
    float preferredGap = 10.0f;
    float comfortGap = 8.0f;
    float minHeight = 30.0f; // el texto mide 21 px
    float minGap = 5.0f;
};

inline float sidebarContent(int count, float height, float gap)
{
    return count <= 0 ? 0.0f : float(count) * height + float(count - 1) * gap;
}

inline SidebarLayout fitSidebar(int count, float available, SidebarMetrics m = {})
{
    available = std::max(0.0f, available);
    SidebarLayout out;
    out.height = m.preferredHeight;
    out.step = m.preferredHeight + m.preferredGap;
    if (count <= 0) return out;
    out.content = sidebarContent(count, m.preferredHeight, m.preferredGap);
    if (out.content <= available) return out;

    // Prueba un alto y un hueco mínimo; reparte el sobrante en el hueco.
    auto tryHeight = [&](float height, float minGap) {
        if (count == 1) {
            if (height > available) return false;
            out.height = height;
            out.step = height + m.preferredGap;
            out.content = height;
            return true;
        }
        const float gap = (available - float(count) * height) / float(count - 1);
        if (gap < minGap - 1e-4f) return false;
        out.height = height;
        out.step = height + std::min(gap, m.preferredGap);
        out.content = sidebarContent(count, height, out.step - height);
        return true;
    };
    // 1) Mismo alto, hueco hasta el mínimo cómodo.
    if (tryHeight(m.preferredHeight, m.comfortGap)) return out;
    // 2) Hueco cómodo, botones más bajos.
    const float comfortH = count == 1 ? available
                         : (available - float(count - 1) * m.comfortGap) / float(count);
    if (comfortH >= m.minHeight && tryHeight(std::min(comfortH, m.preferredHeight), m.comfortGap)) return out;
    // 3) Botones al mínimo, hueco hasta el mínimo absoluto.
    if (tryHeight(m.minHeight, m.minGap)) return out;
    // 4) No cabe ni al mínimo: tamaño mínimo y scroll.
    out.height = m.minHeight;
    out.step = m.minHeight + m.minGap;
    out.content = sidebarContent(count, m.minHeight, m.minGap);
    out.scroll = true;
    return out;
}

// Desplazamiento de la lista con scroll que deja entero a la vista el botón
// `selected`, moviéndose lo mínimo desde `scrollY`. Si ya se ve, no se mueve.
inline float sidebarScrollToShow(int selected, const SidebarLayout& l, float view, float scrollY)
{
    view = std::max(0.0f, view);
    const float maxScroll = std::max(0.0f, l.content - view);
    if (selected < 0) return std::clamp(scrollY, 0.0f, maxScroll);
    const float top = float(selected) * l.step;
    const float bottom = top + l.height;
    if (bottom > scrollY + view) scrollY = bottom - view;
    if (top < scrollY) scrollY = top; // si el botón no cabe entero, que se vea su principio
    return std::clamp(scrollY, 0.0f, maxScroll);
}

} // namespace pikmin::launcher
