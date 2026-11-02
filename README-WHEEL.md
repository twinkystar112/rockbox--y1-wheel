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
**Para ti**, Música, Cover Flow, Canciones aleatorias, Listas, Reproduciendo, Bluetooth, Ajustes y
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
* **Apariencia** clara / oscura / automática (oscura de 21:00 a 7:00), **color de acento**
  (7), **% de batería**, **clic de la rueda** y **salto por letras**.

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
Como la ROM Rockbox-Y1 (Innioasis Updater / Y1 Helper). Si ya tenías Rockbox, borra
`.rockbox/config.cfg` para aplicar el tema y los ajustes nuevos. La biblioteca se crea sola la
primera vez.

## Para desarrolladores
| Área | Archivos |
|---|---|
| Menú principal, Extras | `apps/root_menu.c` |
| Para ti, mixes | `apps/wheel/wheel_foryou.c`, `wheel_library.c` (índice de la base de datos, familias de géneros) |
| Estadísticas e historial | `apps/wheel/wheel_stats.c` |
| Vista de lista, playlists, cola | `apps/wheel/wheel_listview.c` |
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
| Pruebas | `utils/wheel/tests/` |
