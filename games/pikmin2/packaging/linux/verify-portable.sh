#!/usr/bin/env bash
# verify-portable.sh BINARIO [BINARIO...]
#
# Comprueba que los binarios son ejecutables en cualquier Linux x86-64
# razonablemente moderno, no solo en la máquina que los compiló:
#
#   1. Sin instrucciones por encima del x86-64 base (nada de AVX/AVX2/
#      AVX-512/FMA/BMI2: causarían "Illegal instruction" en otras CPUs).
#   2. glibc y libstdc++ requeridas por debajo de un techo configurable
#      (por defecto Ubuntu 22.04: glibc 2.35, GLIBCXX 3.4.30).
#   3. Sin dependencias dinámicas no resueltas en este sistema.
#
# Techos configurables con PIKMIN_MAX_GLIBC y PIKMIN_MAX_GLIBCXX.
# Si se establece PIKMIN_SKIP_LIBC_CHECK=1, se omite el techo de glibc
# (para paquetes autocontenidos que incluyen su propia glibc).
# Código de salida 0 si todos los binarios pasan todas las comprobaciones.

set -euo pipefail

max_glibc="${PIKMIN_MAX_GLIBC:-2.35}"
max_glibcxx="${PIKMIN_MAX_GLIBCXX:-3.4.30}"
skip_libc="${PIKMIN_SKIP_LIBC_CHECK:-0}"
failures=0

if (($# == 0)); then
    printf 'Uso: %s BINARIO [BINARIO...]\n' "$0" >&2
    exit 2
fi

version_le() {
    # version_le A B -> cierto si A <= B
    [ "$1" = "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n1)" ]
}

report() {
    local status="$1" binary="$2" detail="$3"
    printf '  [%s] %s: %s\n' "$status" "$binary" "$detail"
    if [ "$status" = FAIL ]; then failures=$((failures + 1)); fi
}

check_binary() {
    local binary="$1"

    if [ ! -f "$binary" ]; then
        report FAIL "$binary" "el archivo no existe"
        return
    fi

    # 1. ISA: registros vectoriales AVX/AVX-512 y mnemónicos AVX2/FMA/BMI2.
    # \b evita confundir pextrw (SSE2, portable) con pext (BMI2).
    local bad_isa
    bad_isa="$(objdump -d "$binary" \
        | grep -oE '\b(ymm|zmm)[0-9]+\b|\bvfmadd[0-9]+(ss|sd|ps|pd)\b|\bvperm[[:alnum:]]+\b|\bvgather[[:alnum:]]+\b|\bvbroadcast(ss|sd|f128)\b|\bshlx\b|\bshrx\b|\bsarx\b|\bmulx\b|\bpdep\b|\bpext\b' \
        | sort -u | tr '\n' ' ' || true)"
    if [ -n "$bad_isa" ]; then
        report FAIL "$binary" "instrucciones no portables: ${bad_isa}(compila con -DPIKMIN_NATIVE_OPTIMIZE=OFF)"
    else
        report PASS "$binary" "ISA dentro del x86-64 base (sin AVX/FMA/BMI2)"
    fi

    # 2. Techos de glibc y libstdc++.
    if [ "$skip_libc" = 1 ]; then
        report PASS "$binary" "verificación de glibc omitida (paquete autocontenido)"
    else
        local glibc_req glibcxx_req
        glibc_req="$(objdump -T "$binary" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' \
            | sed 's/^GLIBC_//' | sort -Vu | tail -n1 || true)"
        glibcxx_req="$(objdump -T "$binary" 2>/dev/null | grep -oE 'GLIBCXX_[0-9]+\.[0-9]+\.[0-9]+' \
            | sed 's/^GLIBCXX_//' | sort -Vu | tail -n1 || true)"

        if [ -z "$glibc_req" ]; then
            report FAIL "$binary" "no se pudieron leer los símbolos de glibc"
        elif version_le "$glibc_req" "$max_glibc"; then
            report PASS "$binary" "requiere glibc ${glibc_req} (techo ${max_glibc})"
        else
            report FAIL "$binary" "requiere glibc ${glibc_req}, por encima del techo ${max_glibc} (paquete autocontenido: usa package-standalone.sh)"
        fi

        if [ -n "$glibcxx_req" ]; then
            if version_le "$glibcxx_req" "$max_glibcxx"; then
                report PASS "$binary" "requiere GLIBCXX ${glibcxx_req} (techo ${max_glibcxx})"
            else
                report FAIL "$binary" "requiere GLIBCXX ${glibcxx_req}, por encima del techo ${max_glibcxx}"
            fi
        fi
    fi

    # 3. Dependencias dinámicas resolubles en este sistema.
    local missing
    missing="$(ldd "$binary" 2>/dev/null | grep 'not found' || true)"
    if [ -n "$missing" ]; then
        report FAIL "$binary" "dependencias no resueltas: ${missing}"
    else
        report PASS "$binary" "dependencias dinámicas resueltas"
    fi
}

printf 'Verificación de portabilidad (glibc <= %s, GLIBCXX <= %s, ISA x86-64 base)\n' \
    "$max_glibc" "$max_glibcxx"
for binary in "$@"; do
    check_binary "$binary"
done

if ((failures > 0)); then
    printf '\n%d comprobaciones fallaron.\n' "$failures" >&2
    exit 1
fi
printf '\nTodos los binarios son portables.\n'
