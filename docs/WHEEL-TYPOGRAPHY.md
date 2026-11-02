# Tipografía y fuentes (Rockbox Y1 Wheel)

Ajustes › Apariencia › **Tipografía y fuentes**. Este documento es a la vez el análisis técnico,
la arquitectura, la guía de instalación y conversión de fuentes, la evaluación de compatibilidad
con el hardware y el informe de implementación y pruebas.

## 1. Análisis técnico (fase 1)

| Limitación del Y1 / Rockbox | Consecuencia | Solución |
|---|---|---|
| Rockbox solo dibuja fuentes **bitmap** `.fnt` (1 bit, o 4 bits con suavizado). Sin TrueType/OpenType en tiempo real, sin kerning, ligaduras ni hinting. | Cada familia + grosor + tamaño + espaciado es un archivo distinto. | **Conversor en el propio reproductor** (`wheel_fontforge.c`, con stb_truetype: dominio público/MIT; lee `.ttf`, `.otf` CFF y `.ttc`). Los resultados quedan en caché. |
| El motor no tiene espaciado entre letras, interlineado ni cifras tabulares. | No se pueden cambiar "en vivo". | Se **hornean** en el `.fnt`: avance de cada glifo ±px, altura de línea = alto de la fuente con las mayúsculas centradas, dígitos con el ancho del más ancho. |
| Máx. 12 fuentes cargadas a la vez (`MAXUSERFONTS`). | Los temas Wheel + vista previa no cabían. | En builds *hosted* (Y1, simulador) sube a 32; la memoria no es problema (512 MB). |
| Los temas Wheel tienen alturas fijas (barra 38 px, filas 40 px). | Un tamaño grande se cortaría. | Cada rol tiene una **banda** fija (títulos 32, artista 26, detalles/números 22, filas 34/40/46). Los tamaños XS–XL se eligen para que quepan; los temas Wheel son plantillas que se regeneran con cada perfil. |
| Rockbox no sustituía glifos entre fuentes. | Una fuente latina no podía mostrar chino, japonés o coreano. | **Respaldo de glifos en el núcleo** (`font_set_fallbacks`): si una fuente no tiene un carácter (fuera de rango o con ancho 0), se dibuja con la siguiente de la cadena, alineado a la línea base y recortado a su alto. Cadena: Noto Sans JP/SC/KR (el idioma de la interfaz primero, por las formas de los kanji/hanzi) y después Inter para latín/griego/cirílico. Además el conversor puede completar huecos con Inter al generar. |
| Fuentes variables. | stb_truetype solo dibuja la instancia por defecto. | Las incluidas se entregan como instancias estáticas por grosor (fontTools, en la compilación). Una variable importada usa su estilo base (se avisa). |
| Fuentes propietarias (Chicago, Geneva, SF Pro, Avenir Next). | No se pueden redistribuir. | **No se incluyen.** Si copias tus archivos con licencia a `/.rockbox/fonts/import/`, los perfiles los usan solos; si no, alternativas OFL. |
| Árabe, hebreo, escrituras índicas. | Necesitan *shaping*, que Rockbox no hace. | Fuera de alcance; se indica en Compatibilidad. |

**Rendimiento:** las fuentes solo se generan al aplicar o previsualizar (CPU en *boost*), nunca
durante la reproducción; el audio sigue sonando. El arranque no cambia: el perfil activo ya está
convertido y los ajustes apuntan a esos archivos. Si faltan (tarjeta borrada), se vuelve sola a las
fuentes estándar.

## 2. Arquitectura

```
fonts/typo/presets.cfg ─┐                       ┌─ fonts/cache/<uso>-<hash>.fnt
                        ▼                       │
  Font Theme Manager (wheel_typo.c) ── build ───┤─ wps/Wheel(-Dark)-Type.sbs/.wps
   │  presets, perfiles, aplicar               │   (plantillas Wheel.* con "#@")
   │  Profile Persistence: typography/*.cfg    └─ config: sbs/wps/font
   │  Font Role Configuration: 5 roles
   │  Font Fallback Manager: candidatos → Inter; glifos de Inter
   ├─ Font Registry: fonts/typo + fonts/import (familia, grosor, cobertura)
   └─ Font Forge (wheel_fontforge.c): TTF/OTF/.fnt → .fnt
  Typography Settings UI + Preview Renderer (wheel_typo_ui.c)
```

| Componente | Archivo | Qué hace |
|---|---|---|
| Font Registry | `wheel_typo.c` (`typo_registry_*`) | Lee `fonts/typo` (incluidas) y `fonts/import` (tuyas): familia y grosor de la tabla `name`/`OS/2`, o del nombre en `.fnt`. |
| Font Role Configuration | `struct typo_role_cfg` | Títulos (Display), Listas (Primary), Artista y álbum (Secondary), Detalles (Metadata), Números (Numeric): familia(s), grosor, tamaño. |
| Font Theme Manager | `typo_build`, `typo_apply` | Convierte cada uso (título de menú, título en reproducción, fila, fila seleccionada, fuente de UI, artista, detalles, números, letras, letra grande), genera los temas y cambia los ajustes. |
| Font Fallback Manager | `typo_resolve_family`, `pick_face` | Primer candidato instalado; si no, Inter. Grosor más cercano (negrita sintética si falta uno 200 más grueso). Glifos que faltan, de Inter. |
| Profile Persistence | `typo_profile_*` | `/.rockbox/typography/<nombre>.cfg`, el activo en `active.cfg`. Texto legible y editable. |
| Preview Renderer | `wheel_typo_ui.c` (`draw_preview`) | Dibuja con las fuentes candidatas sin aplicarlas: filas de biblioteca, lista de álbumes, Reproduciendo, artista, controles, tiempos y metadatos. |
| Typography Settings UI | `wheel_typo_ui.c` | Galería, Personalizar, Vista previa, Perfiles, Gestión de fuentes. |

Las pantallas dibujadas en C (letras, vistas de lista, estadísticas, salto por letras) piden su
fuente con `typo_font(TU_…)`; los temas Wheel la reciben en la plantilla.

### Plantillas de tema
`wps/Wheel.sbs` y `Wheel.wps` siguen siendo temas válidos (con las fuentes estándar). Una línea
`#@ viejo=nuevo` adapta la línea siguiente al generar `Wheel-Type.*`: `${title}`, `${display}`,
`${row}`, `${rowsel}`, `${secondary}`, `${meta}`, `${numeric}` son las fuentes del perfil,
`${row_h}` la altura de fila y `${pad}`/`${rpad}` el margen en espacios de la fuente de fila;
`\s` es un espacio.

Las paletas (Ajustes › Apariencia › Paleta) usan las mismas plantillas: cada color de
`Wheel.sbs/.wps` es un **token de función** (barra, texto de la barra, iconos de la barra, texto
1/2/3, iconos, fondo, panel, divisores, mosaico de portada, nota, pista de progreso, sombras) y
`typo_generate_palette()` escribe `Wheel-Pal.*` con las fuentes del perfil y cada token cambiado
por el color de la paleta (`apps/wheel/wheel_palette.c`). Un color nuevo en la plantilla debe
reutilizar un token existente o añadirse a la tabla `tmpl[]` y a `make_dark.py`.

Los temas de color se generan desde esas plantillas (`wheel_typo.c`, `gen_scheme`): `Wheel-Pal`
(la paleta elegida) y, con el acabado **Brillo Aero**, `Wheel-Aero` / `Wheel-Dark-Aero` (los
colores clásicos). Empiezan con `# wheel-gen format=… skin=… palette=… aero=… typo=…` (si no
coincide con los ajustes o el perfil de tipografía, se regeneran) y, con brillo,
`%X(aero-<hash>.bmp)`: un fondo con la barra superior en gel y la página. Con fondo, Rockbox borra
cada línea copiando el fondo en lugar de pintar `%Vb`, así que lo que necesita un color propio se
pinta con `%dr` (el mosaico de la portada) y la nota encima se dibuja con `%Vs(none)`. Las líneas
`#@aero viejo=nuevo` solo se aplican a los temas con brillo (quitan la sombra de la portada y
alargan su viewport para el reflejo). Al cambiar el aspecto del gel (`wheel_gel.c`), sube
`GEN_FORMAT` para que se regeneren.

### Fuentes CJK
`fonts/cjk/CJK-{SC,JP,KR}-{17,22}.fnt` (≈10 MB): Noto Sans SC/JP/KR (SIL OFL) recortadas a GB 2312,
JIS X 0208 y el hangul de KS X 1001 (más símbolos, kana y puntuación de ancho completo), en dos
tamaños (líneas de 21 y 27 px). Se cargan con caché de glifos: en RAM solo están los que se ven.
`build_cjk.py` las regenera.

### Interlineado
Un **estilo** (compacto, normal, cómodo, espacioso) fija cuatro valores; cambiar cualquiera pasa a
"personalizado":

| Valor | Rango | Dónde |
|---|---|---|
| Menús (alto de fila) | 30–56 px | filas de todas las listas del tema y fuente de UI (regenera las fuentes de fila y el tema) |
| Letras | 100–200 % | separación entre versos y entre líneas de un mismo verso |
| Textos de Reproduciendo | 80–135 % | número de pista, título, artista, álbum y detalles (se regenera el tema; nunca se solapan) |
| Listas de dos líneas | 80–140 % | cola, playlists, álbumes (alto de fila y separación título/artista) |

## 3. Los 8 perfiles (`fonts/typo/presets.cfg`)

| Perfil | Con tus fuentes (opcional) | Incluidas (OFL) | Rasgos |
|---|---|---|---|
| Classic iPod | Chicago / ChicagoFLF + Geneva | **Classic Bitmap** (bitmaps X11 que ya trae Rockbox) | píxel, cifras tabulares |
| Apple Music | SF Pro Display/Text | Inter | negrita en títulos, suave |
| Spotify Modern | — | Manrope 800 + Inter | compacto, títulos grandes |
| Hi‑Fi Minimal | Avenir Next | Nunito Sans + IBM Plex Mono | interlineado holgado, monoespaciada en detalles |
| Retro Tech | — | VT323 + IBM Plex Mono | píxel, filas compactas |
| Editorial | — | Playfair Display + DM Sans | serif en títulos, holgado |
| Neo Minimal | — | Manrope 300 + Inter | ligero, espaciado amplio |
| Analog Soul | — | Lora + DM Sans | serif cálida, cifras proporcionales |

Los perfiles se definen en el `.cfg`, no en el código: puedes añadir un `[bloque]` y aparece en la
galería.

## 4. Uso

* **Estilos de letra** (galería): cada perfil escrito con su propia fuente. OK abre la vista previa
  (Aplicar / Personalizar / Cancelar); mantener el centro, personalizar directamente.
* **Personalizar**: por rol, fuente / grosor / tamaño (XS–XL); espaciado (compacto, normal,
  amplio), interlineado (compacto, normal, holgado), cifras (tabulares, proporcionales),
  renderizado (suave, nítido, píxel), tamaños por pantalla (títulos de menú, título en
  reproducción, letras), usar en otros temas, completar caracteres, vista previa, aplicar, guardar
  como perfil.
* **Vista previa**: OK → Aplicar / Seguir editando / Deshacer cambios / Cancelar.
* **Perfiles**: nuevo perfil con el actual; por perfil, aplicar, vista previa, personalizar,
  renombrar, eliminar. **Restaurar fuentes estándar** en la pantalla principal.
* **Gestión de fuentes**: fuentes instaladas (origen, grosores, tamaño, cobertura por escritura,
  aviso de fuente variable), importar, volver a buscar, fuentes en memoria, caché (borrar lo que no
  se usa), fuente de respaldo, compatibilidad.

## 5. Instalar y convertir fuentes

**En el reproductor:** copia `.ttf`, `.otf` o `.fnt` a `/.rockbox/fonts/import/` (o a `Download`
/ `Fonts` y usa *Importar fuentes*). Aparecen en Fuente de cada rol y, si su nombre de familia
coincide (p. ej. "SF Pro Text", "Avenir Next", "Chicago"), los perfiles las usan solos.
Solo fuentes que tengas derecho a usar; el firmware no descarga ni redistribuye nada.

**En un PC** (mismo conversor que el reproductor):
```
gcc -O2 -I apps/wheel -o forge utils/wheel/typography/forge_cli.c apps/wheel/wheel_fontforge.c
./forge -i MiFuente.ttf                     # familia, grosor, cobertura
./forge -p 20 -b 40 -r -k 1 MiFuente.ttf 40-MiFuente-Fila.fnt
#   -p tamaño em  -b alto de línea  -t espaciado  -T cifras tabulares  -c píxel
#   -k contraste 0-3  -e negrita sintética  -l último carácter  -r chevrón/✓  -f respaldo.ttf
```
**Fuentes incluidas:** `python3 utils/wheel/typography/build_fonts.py <google/fonts> fonts/typo
<convbdf>` crea las instancias estáticas recortadas (latín, latín‑1, latín ext‑A, griego, cirílico,
puntuación) con licencia OFL al lado, y los bitmaps clásicos. `make_stock_fonts.sh` crea las
fuentes del aspecto estándar.

## 6. Compatibilidad con el hardware

| Aspecto | Evaluación |
|---|---|
| Pantalla 480×360, 16 bits | Suavizado de 4 bits con 16 niveles; "Píxel" para el aspecto bitmap. |
| CPU MT6572 (Cortex‑A7) | Convertir un perfil completo (10 fuentes, ~570 glifos cada una) es una operación puntual; en un PC tarda 0,1 s; en el Y1 se esperan unos pocos segundos. Después todo sale de la caché. |
| RAM | Las fuentes de un perfil ocupan ~1 MB entre todas (las grandes usan caché de glifos). |
| Almacenamiento | Fuentes incluidas ≈ 2 MB; cada perfil generado ≈ 1–1,5 MB en `fonts/cache` (se puede vaciar). |
| Android 4.2 | Todo es C del núcleo; no usa APIs de Android ni `libm`. |
| Temas externos | Conservan sus fuentes; opcionalmente la fuente de UI sigue al rol Listas. |

## 7. Informe de implementación y pruebas

* `utils/wheel/typography/test_forge.py` — 20 pruebas (incluye las fuentes CJK y que cada carácter
  de las traducciones fr/zh/ja/ko tenga glifo): las 42 fuentes incluidas se convierten y se
  leen como lo hace `firmware/font.c`; altura de línea y mayúsculas centradas (bandas 22–46);
  descendentes y acentos dentro de la banda; espaciado ±px en cada glifo; cifras tabulares y
  proporcionales; modo píxel sin suavizado y 16 niveles en suave; glifos de respaldo (griego de
  Inter en Lora); bitmap X11 reubicado; tamaños.
* Simulador 480×360: galería, vista previa, aplicar (Classic iPod, Retro Tech, Editorial, Analog
  Soul), reinicio con el perfil activo, Reproduciendo, Personalizar, Gestión de fuentes.
* Objetivo Y1 (`310`): compila sin errores ni símbolos sin resolver propios.
* **Sin probar en un Y1 real**: tiempos exactos de conversión y legibilidad en la pantalla física.
