#!/usr/bin/env bash
# Comprueba que la build está compilada para ir rápida.
#
# Existe porque el rendimiento se puede perder sin que nada se vea en
# pantalla: no hay defecto gráfico, no hay error de compilación, sólo el
# juego yendo más lento. Las causas conocidas son de configuración de la
# build, no de código, y todas se detectan aquí en un segundo.
#
#   tools/check-rendimiento.sh [directorio-de-build]    (por defecto: build)
#
# Salida 0 = todo correcto. Salida 1 = hay algo que arreglar; cada fallo
# imprime el comando exacto que lo corrige.
#
# Detalle y contexto: RENDIMIENTO.md

set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${repo}/build}"

rojo=$'\e[31m'; verde=$'\e[32m'; amarillo=$'\e[33m'; normal=$'\e[0m'
fallos=0

ok()    { printf '%s  OK  %s %s\n' "$verde" "$normal" "$1"; }
mal()   { printf '%s FALLO%s %s\n' "$rojo" "$normal" "$1"; fallos=$((fallos + 1)); }
aviso() { printf '%s AVISO%s %s\n' "$amarillo" "$normal" "$1"; }
pista() { printf '        %s\n' "$1"; }

printf 'Comprobando %s\n\n' "$build"

if [[ ! -f "${build}/CMakeCache.txt" ]]; then
    mal "no hay ninguna build configurada ahí."
    pista "cmake -S '${repo}' -B '${build}' -DCMAKE_BUILD_TYPE=RelWithDebInfo"
    exit 1
fi

# ── 1. Tipo de build ────────────────────────────────────────────────────────
# Un tipo vacío no pone ninguna -O, que es -O0. Debug tampoco optimiza.
tipo="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${build}/CMakeCache.txt")"
case "${tipo}" in
    Release|RelWithDebInfo)
        ok "tipo de build: ${tipo}" ;;
    Debug)
        mal "tipo de build: Debug. Sin optimización: el juego irá a una fracción de su velocidad."
        pista "cmake -S '${repo}' -B '${build}' -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build '${build}' -j\$(nproc)" ;;
    "")
        mal "tipo de build vacío, que significa -O0."
        pista "cmake -S '${repo}' -B '${build}' -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build '${build}' -j\$(nproc)" ;;
    *)
        aviso "tipo de build poco habitual: ${tipo}" ;;
esac

# ── 2. Opciones que llegan a cada objetivo ──────────────────────────────────
# Ésta es la comprobación que importa, y la razón de que este guion exista.
# Leer el CMakeLists.txt no sirve: lo que cuenta es qué recibió cada objetivo.
# El fallo de 2026-09-03 fue exactamente éste: al separar las fuentes
# decompiladas a su propia biblioteca, las opciones se quedaron en el otro
# objetivo y nadie lo notó, porque no hay síntoma salvo el tiempo de frame.
comprobar_objetivo() {
    local objetivo="$1" descripcion="$2"
    local flags="${build}/CMakeFiles/${objetivo}.dir/flags.make"

    if [[ ! -f "${flags}" ]]; then
        aviso "${objetivo}: aún no compilado, no se puede comprobar."
        return
    fi

    local linea
    linea="$(grep -m1 '^CXX_FLAGS' "${flags}")"

    if grep -q -- '-O3' <<<"${linea}"; then
        ok "${objetivo} (${descripcion}): -O3"
    else
        mal "${objetivo} (${descripcion}): SIN -O3."
        if [[ "${tipo}" == "Debug" || -z "${tipo}" ]]; then
            pista "Consecuencia del tipo de build de arriba; se arregla con el mismo comando."
        else
            pista "Las opciones de optimización no llegan a este objetivo. Mira el bloque"
            pista "PIKMIN_OPTIMIZED_TARGETS en CMakeLists.txt: debe incluirlo."
        fi
    fi

    if grep -q -- '-flto' <<<"${linea}"; then
        ok "${objetivo}: LTO"
    else
        aviso "${objetivo}: sin LTO (correcto sólo si PIKMIN_ENABLE_IPO=OFF a propósito)."
    fi

    if grep -q -- '-march=native' <<<"${linea}"; then
        ok "${objetivo}: -march=native"
    else
        aviso "${objetivo}: sin -march=native (correcto en builds portables)."
    fi
}

echo
comprobar_objetivo pikmin_legacy "todo el juego decompilado: renderall, escena, IA, colisiones"
echo
comprobar_objetivo pikmin_pc "traductor GX, audio, ventana"

# ── 3. Interruptores de rendimiento en el entorno ───────────────────────────
# Todos tienen su valor rápido por defecto. Sólo molestan si alguien los dejó
# puestos en la sesión, que es fácil después de depurar.
echo
for var in PIKMIN_BATCH PIKMIN_UBERSHADER PIKMIN_TEV_SPECIALIZE PIKMIN_WILD_VERTS \
           PIKMIN_TICK_STATS PIKMIN_PERF_STATS PIKMIN_GL_CHECK PIKMIN_NO_UNIFORM_CACHE \
           PIKMIN_DUMP_SHADERS PIKMIN_AUDIO_TRACE_ALL PIKMIN_REPLAY_TEST; do
    if [[ -n "${!var:-}" ]]; then
        aviso "${var}=${!var} está puesto en el entorno y frena el juego."
        pista "unset ${var}"
    fi
done

# ── 4. Ajustes del jugador ──────────────────────────────────────────────────
conf="${repo}/pikmin_settings.conf"
if [[ -f "${conf}" ]]; then
    escala="$(sed -n 's/^renderScale *= *//p' "${conf}")"
    if [[ -n "${escala}" ]] && awk "BEGIN{exit !(${escala} > 1)}" 2>/dev/null; then
        aviso "renderScale = ${escala}: se renderiza por encima de la resolución de pantalla."
        pista "Se cambia con F1 dentro del juego."
    fi
fi

echo
if ((fallos == 0)); then
    printf '%sLa build está configurada para ir rápida.%s\n' "$verde" "$normal"
    printf 'Si aun así va lenta, mide: PIKMIN_TICK_STATS=1 %s/bin/nectar 2>&1 | grep -A9 "PC tick"\n' "${build}"
    exit 0
fi

printf '%s%d problema(s). Arriba está el comando que corrige cada uno.%s\n' "$rojo" "$fallos" "$normal"
exit 1
