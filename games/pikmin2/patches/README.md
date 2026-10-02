# Parches sobre el submódulo `pikmin2-decomp`

`pikmin2-decomp` apunta al upstream `projectPiki/pikmin2`. El port necesita
cambios en ese árbol (LP64, endianness de host, `PIKI_PC_PORT`/`PIKI_PC_GXREGS`)
que todavía no viven en un fork, así que se capturan aquí como parche.

Aplicar tras `git submodule update --init`:

```bash
git -C pikmin2-decomp apply ../patches/pikmin2-decomp-host.patch
```

Regenerar tras tocar el submódulo:

```bash
git -C pikmin2-decomp diff > patches/pikmin2-decomp-host.patch
```

Pendiente: mover estos cambios a un fork y apuntar el submódulo a él.
