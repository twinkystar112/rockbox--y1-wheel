# Rockbox Y1 Wheel

Versión de Rockbox para el **Innioasis Y1** (480×360, Android 4.2 / MT6572) centrada en la música:
menús al estilo de un reproductor de rueda clásico, recomendaciones y estadísticas hechas con tu
propia biblioteca, letras sincronizadas, Bluetooth integrado y scrobbling a Last.fm con y sin conexión.

Construida sobre el port activo **[rockbox-y1/rockbox](https://github.com/rockbox-y1/rockbox)**
(rama `rebase-upstream`, target `310|innioasisy1`, GPLv2+): se instala como la ROM Rockbox-Y1 y
respeta sus botones tipo iPod, su Bluetooth y su Wi-Fi. Sin recursos de Apple ni de otros temas:
diseño, iconos y presets son propios; las fuentes incluidas son libres (SIL OFL) y los bitmaps X11
que ya trae Rockbox.

> **Estado:** beta sin probar en hardware. El C compila para el objetivo Y1 y el Java contra la
> API 19 (y las clases Wheel contra la 17). La interfaz se ha probado en el simulador a 480×360; el
> código de Last.fm, letras, Wi-Fi bajo demanda y envío por Bluetooth pasa 52 pruebas contra
> servidores simulados (`utils/wheel/tests`) y el conversor de fuentes y las fuentes CJK 20
> (`utils/wheel/typography/test_forge.py`). Lo que depende de la radio del Y1 solo puede
> verificarse en un Y1. Haz copia de la tarjeta antes de instalar.

> **Wi-Fi:** el Y1 tiene el chip; el firmware de fábrica no lo activa y la ROM Rockbox-Y1 sí.
> Esta versión usa la red de `.rockbox/wifi.cfg` (la misma que los plugins de la ROM) y **enciende
> el Wi-Fi solo mientras envía scrobbles o descarga una letra**, y lo apaga después. También sirve
> el internet del móvil por Bluetooth.

## Controles
| En… | Acción | Resultado |
|---|---|---|
| Listas | girar rápido | **salto por letras** (A, B, C… en grande, con la fuente de títulos) |
| Listas | mantener el centro | opciones de la fila (reproducir a continuación, parecidas, enviar…) |
| Reproduciendo | pulsar el centro | letras → avance rápido con la rueda → normal |
| Reproduciendo | mantener el centro | menú de la canción |
| Reproduciendo | centro + Play | atajo (*hotkey*) de la ROM |
| Cualquier sitio | Menú | atrás |
| Cualquier sitio | mantener el centro 1 s / 5 s | apagar pantalla / apagar (como en la ROM) |

## Qué incluye

### Menú principal
**Para ti**, Música, Portadas (Album Flow), Canciones aleatorias, Listas, Reproduciendo, Bluetooth, Ajustes y
Extras (Archivos, Last.fm, Plugins, Accesos directos, Sistema…). Pantalla dividida con la carátula
de lo que suena. Los valores (estado, número de canciones, duración) van alineados a la derecha y
las filas que abren algo llevan chevrón.

### Para ti (todo se calcula en el reproductor, sin internet)
* **Seguir escuchando** y **Elegidas para hoy**: 6 canciones de tu mix diario.
* **Tu mix diario**: tus artistas más escuchados del último mes y música parecida. Cambia cada día.
* **Parecidas a…** (estilo Genius): parte de la última canción y puntúa por género, familia de
  géneros, artista, época y cuánto la escuchas, sin que un artista acapare la lista.
* **Estados de ánimo**: Energía, Calma, Fiesta, Concentración, Melancolía.
* **Mixtape**: un estado de ánimo que dura 20, 30, 45, 60 o 90 minutos.
* **Joyas olvidadas**, **Nunca escuchadas**, **Añadidas recientemente**, **Tus favoritas**, **Décadas**.
* **Lista inteligente**: combina género, década, duración y tipo de canción.
* **Álbumes escuchados hace poco** y **Tus estadísticas**.

Cualquier mix se guarda como playlist definitiva con **Guardar** (`Playlists/Nombre.m3u8`).

### Tus estadísticas
Se actualizan solas con cada escucha (`.rockbox/wheel-history.tsv`): tiempo escuchado,
reproducciones, **canciones, artistas y álbumes más escuchados** por **mes, año o desde siempre**,
mes a mes, historial reciente y un **resumen tipo Wrapped** en 4 tarjetas (minutos, top canciones
con carátula, top artistas, tu hora favorita). Desde los rankings se reproduce directamente.

### Listas y cola
* Las playlists muestran **número de canciones y duración** (`17 · 12 min`).
* **Vista de lista tipo Spotify**: carátula (o mosaico de 4 álbumes), nombre, canciones y horas,
  botones Reproducir / Aleatorio / Guardar y todas las pistas con artista y duración. La cabecera
  se pliega al bajar. La canción que suena lleva un ecualizador animado.
* **A continuación**: la cola con la misma vista; quitar, subir y bajar canciones.
* Reproducir a continuación / Añadir a la cola desde el navegador.

### Portadas (Album Flow) y vistas de listas
* **Portadas** (menú principal): todos los álbumes de la base de datos como carátulas en 3D; la
  del centro mira de frente y las demás se giran hacia los lados, con reflejo. La rueda las desliza,
  el centro abre el álbum y una pulsación larga ofrece Reproducir, Aleatorio, A continuación,
  Después y **Ordenar** (artista, título, año, añadidos recientemente). Lo dibuja el propio
  firmware; el plugin PictureFlow ya no hace falta.
* Las **playlists, los mixes y A continuación** se pueden ver de cuatro formas
  (Ajustes › Apariencia › **Vista de listas**, o pulsación larga › **Cambiar vista**):
  **Lista Wheel** (la de siempre), **Lista clásica** (una línea por canción, «Aleatorio» arriba y
  ♪ en la que suena), **Portadas** (el carrusel con las canciones) e **Historias en disco**: una
  tarjeta por canción con la carátula impresa en un disco que gira mientras suena, fondo
  desenfocado y barras de progreso arriba. En las historias el centro reproduce o pausa sin salir,
  y al acabar la canción pasa sola a la siguiente tarjeta.
* Los álbumes y carpetas siempre se abren en la Lista Wheel.

### Letras sincronizadas
Pantalla completa al estilo Spotify (fondo del color de la carátula, línea actual resaltada,
desplazamiento suave). Lee archivos `.lrc` junto a la canción. Si no hay y existe conexión, las
busca en **LRCLIB** (base de datos abierta) **solo cuando abres las letras** y las guarda como
`.lrc`, así funcionan sin conexión desde entonces.

### Sonido
Menú de la canción › **Sonido**: ecualizador on/off, **17 presets originales** (Bass Boost, Vocal,
Late Night, Small Speakers, Spoken Word…) y ajuste fino. **Temporizador de apagado** (10–120 min).

### Temas
Ajustes › Apariencia:
* **Tema**: lista de temas instalados. Se prueba en vivo y vuelve al anterior si no confirmas en
  10 s.
* **Instalar tema desde .zip**: copia el .zip de un tema de Rockbox (themes.rockbox.org) a la
  carpeta principal, `Download` o `Themes`, y se instala desde el propio Y1.
* **Escalar temas de 320×240**: los temas hechos para 320×240 se amplían 1,5× (posiciones,
  imágenes, fondos y fuentes) en lugar de verse en una esquina.
* **Apariencia** clara / oscura / automática (oscura de noche; las horas se ajustan en Looks),
  **color de acento** (7), **% de batería** y **salto por letras**.

### Paletas
Ajustes › Apariencia › **Paleta** abre "Elige una paleta": cada una con su miniatura en degradado
y sus colores, y se aplica al pulsar (barra superior, fondo, textos, iconos, selector, portada de
reserva, barra de progreso, sombras y las pantallas dibujadas en C):

| Paleta | Carácter |
|---|---|
| **Wheel** | la clásica: clara u oscura con tu color de acento |
| **Garnet Espresso** | granate profundo, lila y café crema |
| **Petal Pop** | rosa intenso, melocotón y un toque de lila |
| **Pine Nectar** | verde pino con miel dorada |
| **Midnight Iris** | violeta oscuro para la noche (paleta oscura) |
| **Citrus Sunset** | coral, tomate maduro y un poco de cielo |
| **Rose Chai** | rosa suave, palo rosa y vainilla |
| **De la portada** | los colores de la canción que suena: cambian con cada canción, en claro u oscuro según el modo |
| **Mi paleta** | la tuya: eliges barra superior, fondo y acento (tono, saturación y luminosidad con la rueda) y el resto se calcula, con vista previa |

**Acabado** (justo debajo): **Brillo Aero** (por defecto) o **Mate**, **intensidad del brillo**
(sutil / iPod / máximo), **reflejo de la portada** y **esquinas** (rectas / suaves / redondeadas,
en la selección y en las portadas). El brillo Aero da a la
interfaz el aire gel/cristal de los reproductores de mediados de los 2000, dibujado con los
colores de cada paleta, sin imágenes copiadas: barra de selección con reflejo superior, línea de
"flotación" y fondo que se ilumina hacia abajo; barra superior acristalada con una sombra suave
sobre la página; página con un degradado tenue teñido por la paleta; barras de progreso y
volumen como tubos (surco hundido + relleno gel); botones-cápsula y miniaturas brillantes; y en
Ahora suena, la portada se refleja debajo como si estuviera sobre cristal (en lugar de la sombra).
Vale para la Wheel clásica (barra plateada en el modo claro, grafito en el oscuro; el automático
cambia entre las dos) y para todas las paletas: los temas `Wheel-Aero`, `Wheel-Dark-Aero` y
`Wheel-Pal` se generan con sus fondos (`wps/<tema>/aero-*.bmp`). Con "Mate" vuelve el aspecto
plano de siempre.

### Más personalización (Ajustes › Apariencia)
* **Imagen de fondo**: una foto o dibujo tuyo (JPEG o BMP de `Pictures`, `DCIM`, `Download`…)
  detrás de menús y Ahora suena, recortada para llenar la pantalla, desenfocada o nítida y con
  intensidad suave / media / fuerte para que el texto siempre se lea; la barra superior se vuelve
  de cristal sobre ella.
* **Ahora suena**: diseño **Clásico**, **Portada grande** (centrada, estilo nano),
  **Minimalista** (solo texto) o **Vinilo** (la portada es la etiqueta redonda de un disco);
  mostrar u ocultar álbum, "año · formato · bitrate" y la **siguiente canción**; barra de
  progreso fina o gruesa.
* **Menú principal**: **panel derecho** con la portada, un **reloj** grande, **"A
  continuación"** (lo que suena y lo siguiente) o ninguno (lista a todo el ancho); **elementos y
  orden**: OK muestra u oculta, mantener OK levanta el elemento y se mueve girando la rueda.
* **Looks**: guarda la combinación actual (paleta, acabado, imagen, diseño de Ahora suena, panel,
  modo, acento y tipografía) con un nombre y aplícala cuando quieras; **looks de día y de
  noche** con la hora a la que empieza cada uno (también la usa el modo automático).
* **Rueda y clic**: volumen del clic, **sonido** (tic, clac, burbuja, gota, madera; se generan en
  el reproductor), **sensibilidad** (baja = dos clics por elemento, alta = dos elementos por clic) y
  **aceleración** al girar rápido.
* **Reposo y arranque**: **pantalla de reposo** tras 15 s – 2 min sin tocar nada (reloj grande
  sobre la portada difuminada, la canción y su progreso; cualquier botón la quita) e **imagen y
  texto de arranque** (se componen una vez y se guardan en `.rockbox/wheel/boot.bmp`).

Los colores están inspirados en las capturas de referencia, con nombres propios. Funciona junto
con la tipografía: el tema `Wheel-Pal` se genera desde las plantillas `Wheel.sbs/.wps` con tu
perfil de fuentes e interlineado y los colores de la paleta. Con una paleta activa, Apariencia y
Color de acento muestran "Paleta"; pulsar cualquiera de los dos vuelve a la Wheel clásica.

### Idiomas
Ajustes › Apariencia › **Idioma**: español, English, français, 简体中文, 日本語 y 한국어, todos con la
interfaz Wheel completa traducida (`utils/wheel/i18n/`), más el resto de idiomas de Rockbox en
"Más idiomas…". Los caracteres chinos, japoneses y coreanos (menús, títulos y artistas de tus
canciones, letras) se dibujan con fuentes Noto Sans SC/JP/KR (SIL OFL) que el firmware usa como
**respaldo glifo a glifo**: cualquier fuente o tema muestra CJK sin cambiar nada, y los kanji/hanzi
toman la forma del idioma elegido. Las letras en chino y japonés se cortan entre caracteres.

### Tipografía y fuentes
Ajustes › Apariencia › Tipografía y fuentes: 8 estilos (Classic iPod, Apple Music, Spotify Modern,
Hi‑Fi Minimal, Retro Tech, Editorial, Neo Minimal, Analog Soul) que se ven en la galería escritos
con su propia fuente, personalización por rol (títulos, listas, artista y álbum, detalles,
números: fuente, grosor y tamaño), espaciado, **interlineado** por estilos (compacto, normal, cómodo,
espacioso o personalizado: alto de las filas de los menús, letras, textos de Reproduciendo y
listas de dos líneas, cada uno por separado), cifras tabulares, renderizado suave /
nítido / píxel, tamaños por pantalla, **vista previa real** antes de aplicar, perfiles propios y
gestión de fuentes (cobertura, memoria, caché, importar tus `.ttf`/`.otf`). Las fuentes se
convierten en el propio Y1. Las propietarias (Chicago, Geneva, SF Pro, Avenir Next) no se
incluyen: si tienes las tuyas, cópialas a `/.rockbox/fonts/import/` y los estilos las usan.
Detalles técnicos: [`docs/WHEEL-TYPOGRAPHY.md`](docs/WHEEL-TYPOGRAPHY.md).

### Bluetooth
* Activar, dispositivos con estado en vivo, conectar, desconectar, olvidar, **buscar y emparejar**
  dentro de Rockbox (PIN 0000 o confirmación automáticos).
* Reconexión automática, **pausa al desconectar** y reanudar al reconectar (en el Y1 usan el
  ajuste de Rockbox "pausa al desconectar auriculares", que la ROM ya conecta al A2DP).
* **Restablecer dispositivos Bluetooth** (la herramienta de la ROM) también desde esta pantalla.
* **Enviar por Bluetooth** una canción o una lista entera a un móvil u ordenador emparejado
  (OBEX Object Push). Los móviles Android (Samsung incluidos) y los
  ordenadores lo aceptan; **los iPhone no reciben archivos por Bluetooth**.
* **Usar internet del móvil**: en un móvil emparejado, activa el anclaje por Bluetooth en el
  teléfono y el Y1 lo usa para Last.fm, letras y el inicio de sesión con QR. En Samsung: Ajustes ›
  Conexiones › Zona Wi-Fi y módem › Anclaje a red Bluetooth.

### Last.fm
* **No necesita app intermedia**: el Y1 envía los scrobbles directamente por Wi-Fi (encendido
  solo para eso, con la red de `wifi.cfg`) o por el internet del móvil. Sin conexión, se guardan y
  se envían en cuanto la hay; **Enviar ahora** enciende el Wi-Fi si hace falta.
* Lee la API key/secret y el usuario de los `scrobble-*` de `wifi.cfg` si no hay `lastfm.cfg`
  (nunca modifica `wifi.cfg`). Si usas también el plugin *Last.fm Scrobbler* de la ROM para subir
  `.scrobbler.log`, usa solo uno de los dos para no duplicar escuchas.
* Reglas de Last.fm: la pista dura más de 30 s y la escuchas la mitad o 4 minutos (tiempo real).
* Cola en `.rockbox/.lastfm-queue.tsv`, lotes de 50, reintento tras errores temporales,
  "Escuchando ahora", **Me gusta** desde la canción.
* **Inicio de sesión con QR**: escanea con el móvil y autoriza; o con `lastfm.cfg`.
* También escribe `/.scrobbler-wheel.log` (Audioscrobbler 1.1) **en UTC** como respaldo.
* Compatible con **Libre.fm** u otro servicio compatible (`api_url` y `auth_url`).

## Conflictos con el Y1 (resueltos al pasar a rockbox-y1/rockbox)
| Punto | Estado |
|---|---|
| Base | Ahora sobre la ROM activa (`innioasisy1`), no sobre el port archivado. Mismo APK de sistema firmado, mismas rutas (`/sdcard/.rockbox`), Type A y Type B como la ROM. |
| Rueda y botones | Keymap de la ROM (`Rockbox.kl`, botones tipo iPod). El centro en Reproduciendo es una acción nueva (`ACTION_WPS_CYCLE_MODE`) que no quita el menú contextual ni el atajo de la ROM. |
| Pausa al perder los auriculares BT | Un solo mecanismo: el de la ROM (A2DP → evento de auriculares). Las opciones de la pantalla Bluetooth cambian ese ajuste. |
| Wi-Fi | El de la ROM, encendido bajo demanda con `wifi.cfg`; HTTPS con Java y, si falla, con el `gocurl` de la ROM. |
| `.scrobbler.log` | El plugin de la ROM escribe ese archivo; esta versión usa `.scrobbler-wheel.log` (UTC). |
| Simulador | Corregido un `defined(PLATFORM_ANDROID)` del port que rompía las compilaciones no Android. |
| Anclaje Bluetooth (PAN) y envío de archivos (OPP) | Sin probar en el Y1: dependen de la pila Bluetooth de MediaTek. |

## Configurar Last.fm
1. Crea una API key gratuita en https://www.last.fm/api/account/create.
2. Copia `utils/wheel/lastfm.cfg.example` como `.rockbox/lastfm.cfg` y rellena `api_key` y
   `api_secret` (y, si no usas el QR, `username` y `password`).
3. Extras › Last.fm › Cuenta. Con el QR, escanéalo con el móvil y pulsa *Permitir*.

**¿Sin anclaje por Bluetooth?** Conecta el Y1 al ordenador de vez en cuando y ejecuta
`python3 utils/wheel/upload_scrobbler_log.py /ruta/a/la/tarjeta/.scrobbler-wheel.log`
(Last.fm no acepta escuchas de hace más de 14 días).

## Compilar
Igual que la ROM Rockbox-Y1 (`android/y1/building.md`): con el NDK r10e–r17,
`mkdir build && cd build && ../tools/configure --target=310 --lcdwidth=480 --lcdheight=360 --type=n`
y después los pasos de `android/y1` (o el workflow `.github/workflows/build-roms.yml`, que genera
las ROM Type A/Type B).

Pruebas en un PC: `utils/wheel/tests/run.sh` (JDK + Python 3) y
`python3 utils/wheel/typography/test_forge.py` (gcc).

## Instalar
Igual que Rockbox-Y1 y con los mismos archivos. El flujo `.github/workflows/build-wheel.yml` los
genera en GitHub con el toolchain oficial del port (NDK r10e, API 19):

* `update_360p.zip`: cópialo a `.rockbox/update.zip` y entra en **Extras › Sistema › Actualizar
  firmware**. Se instala en segundo plano y el Y1 se reinicia solo.
* `rom_360p.zip` / `rom_type_b_360p.zip`: instalación completa con MTKClient o SP Flash Tool, como
  la ROM Rockbox-Y1.

Si ya tenías Rockbox, borra `.rockbox/config.cfg` para aplicar el tema y los ajustes nuevos. La
biblioteca se crea sola la primera vez. Copia de seguridad y vuelta atrás:
`android/y1/WHEEL-INSTALL.md`.

## Para desarrolladores
| Área | Archivos |
|---|---|
| Menú principal, Extras | `apps/root_menu.c` |
| Para ti, mixes | `apps/wheel/wheel_foryou.c`, `wheel_library.c` (índice de la base de datos, familias de géneros) |
| Estadísticas e historial | `apps/wheel/wheel_stats.c` |
| Vista de lista, playlists, cola | `apps/wheel/wheel_listview.c` |
| Portadas (Album Flow), historias en disco | `apps/wheel/wheel_flow.c` |
| Letras, modos de Reproduciendo | `apps/wheel/wheel_lyrics.c` |
| Sonido, temporizador, envío BT | `apps/wheel/wheel_tools.c`, `lib/rbcodec/dsp/eqs/` |
| Temas, .zip, escalado 320×240 | `apps/wheel/wheel_themes.c`, `lib/skin_parser/skin_parser.c`, `apps/gui/skin_engine/skin_parser.c`, `apps/gui/backdrop.c`, `firmware/common/zip.c` (deflate) |
| Apariencia, salto por letras, teclado, diálogos | `apps/wheel/wheel_appearance.c`, `wheel_letterjump.c`, `wheel_keyboard.c`, `wheel_yesno.c` |
| Capa de plataforma | `apps/wheel/wheel_connect.h`, `wheel_connect_stub.c` (demo del simulador), `apps/hosted/android/wheel_connect_android.c` (JNI) |
| Lado Android | `android/y1/src/org/rockbox/Helper/Wheel*.java` (`WheelNet`: Wi-Fi bajo demanda) |
| Tipografía | `apps/wheel/wheel_fontforge.c` (conversor), `wheel_typo.c` (registro, perfiles, temas), `wheel_typo_ui.c` (pantallas, vista previa), `fonts/typo/`, `utils/wheel/typography/` |
| Respaldo de glifos / CJK | `firmware/font.c` (`font_set_fallbacks`, `font_glyph_source`), `firmware/drivers/lcd-bitmap-common.c`, `fonts/cjk/` (`build_cjk.py`) |
| Traducciones | `utils/wheel/i18n/wheel_{fr,zh,ja,ko}.tsv` → `apply.py` → `apps/lang/*.lang` |
| Etiquetas de skin nuevas | `%Lh %Lv %Wb %Wq %Wm %Wy %Wd %Wa..%Wo` en `lib/skin_parser/tag_table.*`, `apps/gui/skin_engine/skin_tokens.c` |
| Tema | `wps/Wheel*.sbs/.wps` (plantillas con `#@`), `fonts/prebuilt/` (`utils/wheel/make_fonts.sh`) |
| Paletas | `apps/wheel/wheel_palette.c` (tabla de colores por función, selector, fondo Aero) → `wheel_typo.c` (`typo_generate_palette`: genera `Wheel-Pal`) |
| Acabado Aero | `apps/wheel/wheel_gel.c` (gel, surco, reflejo, esquinas, colores dinámicos `wrNN`), ganchos en `apps/gui/line.c` (selector), `apps/gui/scrollbar.c` (barras de progreso), `apps/gui/skin_engine/skin_display.c` (reflejo, portada redonda), `apps/gui/backdrop.c` (fondo sin tramado), `apps/misc.c` (`parse_color`) |
| Paleta de la portada, Mi paleta, imagen de fondo, vinilo | `apps/wheel/wheel_palette.c` (`wheel_palette_derive`, `wheel_palette_poll`, fondos generados), `apps/gui/skin_engine/skin_backdrops.c` (`skin_backdrop_next`) |
| Ahora suena, panel, menú, looks, rueda, reposo, arranque | `apps/wheel/wheel_custom.c`, `wps/Wheel-NP-*.wps`, `apps/wheel/wheel_letterjump.c` (sensibilidad), `apps/beep.c` (sonidos del clic), `apps/menu.c` / `apps/gui/wps.c` (reposo), `apps/main.c` (arranque) |
| Pruebas | `utils/wheel/tests/` |
