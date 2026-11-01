# Rockbox Y1 Wheel

Versión de Rockbox para el **Innioasis Y1** (480×360, Android 4.2 / MT6572) centrada en la música:
menús al estilo de un reproductor de rueda clásico, recomendaciones y estadísticas hechas con tu
propia biblioteca, letras sincronizadas, Bluetooth integrado y scrobbling a Last.fm con y sin conexión.

Parte del port comunitario [multiwirth/rockbox-y1](https://github.com/multiwirth/rockbox-y1)
(GPLv2+). Sin recursos de Apple ni de otros temas: diseño, iconos y presets son propios; las fuentes
son Inter (SIL OFL).

> **Estado:** beta sin probar en hardware. El C compila para Android y el Java contra la API 17
> (Android 4.2). La interfaz se ha probado en el simulador a 480×360 y el código de Last.fm, letras
> y envío por Bluetooth pasa 38 pruebas contra servidores simulados (`utils/wheel/tests`). Lo que
> depende de la radio del Y1 (Bluetooth real) solo puede verificarse en un Y1. Haz copia de la
> tarjeta antes de instalar.

> **Wi-Fi:** el Y1 tiene el chip, pero el firmware de fábrica no lo activa. Las ROM de la comunidad
> (base `y1-ata-rom`, Rockbox-Y1) sí lo activan. Todo funciona sin conexión; para lo que necesita
> internet (enviar scrobbles al momento, descargar letras, iniciar sesión con QR) se usa la conexión
> que tenga Android: Wi-Fi si la ROM lo activa, o el internet del móvil por Bluetooth.

> **Compatibilidad:** esta versión parte del port antiguo `multiwirth/rockbox-y1` (archivado en
> enero de 2026) y se instala como allí: Y1 con root, `Generic.kl` modificado y el APK. **No es
> compatible con la ROM actual Rockbox-Y1** (`rockbox-y1/rockbox`, v0.5 y nightlies): esa es otro
> target (`innioasisy1`, botones tipo iPod, app de sistema firmada con la clave de plataforma, rutas
> `/sdcard/.rockbox`, variantes Type A/Type B). Instalar este APK encima falla por firma y, con su
> distribución de teclas, la rueda no se movería. Ver "Conflictos con el Y1" más abajo.

## Controles
| En… | Acción | Resultado |
|---|---|---|
| Listas | girar rápido | **salto por letras** (A, B, C… en grande) |
| Listas | mantener Menú | opciones de la fila (reproducir a continuación, parecidas, enviar…) |
| Reproduciendo | pulsar el centro | letras → avance rápido con la rueda → normal |
| Reproduciendo | mantener Menú | menú de la canción (A continuación, Sonido, Temporizador…) |
| Cualquier sitio | Menú | atrás |
| Cualquier sitio | mantener el centro 1 s | apagar la pantalla (igual que en el port original) |

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

### Bluetooth
* Activar, dispositivos con estado en vivo, conectar, desconectar, olvidar, **buscar y emparejar**
  dentro de Rockbox (PIN 0000 o confirmación automáticos).
* Reconexión automática, **pausa al desconectar** y reanudar al reconectar.
* **Enviar por Bluetooth** una canción o una lista entera a un móvil u ordenador emparejado
  (OBEX Object Push). Los móviles Android (Samsung incluidos) y los
  ordenadores lo aceptan; **los iPhone no reciben archivos por Bluetooth**.
* **Usar internet del móvil**: en un móvil emparejado, activa el anclaje por Bluetooth en el
  teléfono y el Y1 lo usa para Last.fm, letras y el inicio de sesión con QR. En Samsung: Ajustes ›
  Conexiones › Zona Wi-Fi y módem › Anclaje a red Bluetooth.

### Last.fm
* **No necesita app intermedia**: el Y1 envía los scrobbles directamente a través del internet
  del móvil por Bluetooth. Sin conexión, se guardan y se envían en cuanto la hay.
* Reglas de Last.fm: la pista dura más de 30 s y la escuchas la mitad o 4 minutos (tiempo real).
* Cola en `.rockbox/.lastfm-queue.tsv`, lotes de 50, reintento tras errores temporales,
  "Escuchando ahora", **Me gusta** desde la canción.
* **Inicio de sesión con QR**: escanea con el móvil y autoriza; o con `lastfm.cfg`.
* También escribe `/.scrobbler-wheel.log` (Audioscrobbler 1.1) **en UTC** como respaldo.
* Compatible con **Libre.fm** u otro servicio compatible (`api_url` y `auth_url`).

## Conflictos con el Y1 (revisados)
| Punto | Estado |
|---|---|
| Android 4.2.2 (API 17) | Compatible: el Java se compila contra la API 17. |
| Centro de la rueda | Lo gestiona Java: pulsación corta = clic; mantener 1 s = apagar pantalla. Por eso las opciones van con **mantener Menú** (corregido). |
| Rueda | Necesita el `Generic.kl` del port original (rueda = DPAD arriba/abajo). Con el `Rockbox.kl` de la ROM Rockbox-Y1 la rueda envía otros códigos y no funcionaría. |
| Firmware de fábrica 3.x / Type B | Las instrucciones `sed` del port original buscan líneas que no existen en los `.kl` de 3.x; en esos Y1 hay que revisar el `Generic.kl` a mano. Los Y1 Type B (rueda de 1,8 V, vendidos con firmware anterior a 2.0.0) usan ROM propia. |
| Wi-Fi | Solo con ROM de la comunidad que lo active; si no, internet del móvil por Bluetooth. |
| Anclaje Bluetooth (PAN) y envío de archivos (OPP) | Sin probar en el Y1: dependen de la pila Bluetooth de MediaTek. |
| `.scrobbler.log` | El plugin de Rockbox escribe ese archivo con hora local; esta versión usa `.scrobbler-wheel.log` (UTC) para no mezclarlos (corregido). |
| Instalación | Como app normal sobre un Y1 con root; no como ROM ni como app de sistema. |

## Configurar Last.fm
1. Crea una API key gratuita en https://www.last.fm/api/account/create.
2. Copia `utils/wheel/lastfm.cfg.example` como `.rockbox/lastfm.cfg` y rellena `api_key` y
   `api_secret` (y, si no usas el QR, `username` y `password`).
3. Extras › Last.fm › Cuenta. Con el QR, escanéalo con el móvil y pulsa *Permitir*.

**¿Sin anclaje por Bluetooth?** Conecta el Y1 al ordenador de vez en cuando y ejecuta
`python3 utils/wheel/upload_scrobbler_log.py /ruta/a/la/tarjeta/.scrobbler-wheel.log`
(Last.fm no acepta escuchas de hace más de 14 días).

## Compilar el APK
GitHub Actions lo compila solo (`.github/workflows/build-apk.yml`): sube la rama `y1-wheel` a tu
fork y descarga el artefacto `rockbox-y1-wheel-360p`.

A mano (Linux): `cd android && ./installToolchain.sh`, después
`mkdir build && cd build && ../../tools/configure --target=201 --lcdwidth=480 --lcdheight=360 --type=n`
y `make && make classes && make zip && make unsigned-apk && ../sign_apk_ci.sh`.

Pruebas de Last.fm, letras y envío por Bluetooth en un PC (JDK + Python 3):
`utils/wheel/tests/run.sh`.

## Instalar
Igual que el port original: Innioasis Updater / Y1 Helper, o `adb install rockbox.apk` en un Y1
rooteado. Permisos nuevos: `BLUETOOTH`, `BLUETOOTH_ADMIN`, `ACCESS_NETWORK_STATE`, `INTERNET`.
Si ya tenías Rockbox, borra `.rockbox/config.cfg` para aplicar el tema y los ajustes nuevos. La
biblioteca se crea sola la primera vez.

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
| Lado Android | `android/src/org/rockbox/Helper/Wheel*.java` |
| Etiquetas de skin nuevas | `%Lh %Lv %Wb %Wq %Wm %Wy %Wd %Wa..%Wo` en `lib/skin_parser/tag_table.*`, `apps/gui/skin_engine/skin_tokens.c` |
| Tema | `wps/Wheel*.sbs/.wps`, `fonts/prebuilt/` (`utils/wheel/make_fonts.sh`) |
| Pruebas | `utils/wheel/tests/` |
