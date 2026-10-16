# plumgg

Emulador de Sega Game Gear para Windows. El objetivo a largo plazo es mostrar los juegos
en pseudo-3D; esta primera versión es la base: CPU, vídeo, cartuchos, ventana y controles.

## Qué funciona

- **CPU Z80** completa, con instrucciones y flags no documentados. Pasa ZEXDOC y ZEXALL.
- **VDP** (modo 4 del Master System con la paleta de 12 bits de la Game Gear): fondo con
  scroll y bloqueos de scroll, sprites 8x8 / 8x16 / zoom, prioridad, interrupciones de
  cuadro y de línea, contadores V/H.
- **Cartuchos** con el mapper estándar de Sega y RAM de cartucho (guarda partidas en un
  `.sav` junto a la ROM). Las ROMs pueden estar sueltas (`.gg`, `.sms`, `.bin`) o dentro de
  un `.zip`; del zip se carga la primera ROM que contenga, y la partida se guarda junto al
  zip con su mismo nombre.
- **Ventana SDL2** con barra de menús: abrir ROM (diálogo de Windows o arrastrando el
  archivo), lista de recientes, pausa y reinicio, ventana de **Controles** para asignar
  teclas y botones del mando, y ventana de **Vídeo** (vista, escala entera / ajustar /
  estirar, suavizado, tamaño de ventana, pantalla completa).
- **Sonido**: el PSG SN76489 completo (tres canales de tono y uno de ruido) con el estéreo
  propio de la Game Gear. Volumen y silencio en **Opciones > Sonido**, o `M` para silenciar.
- **Mandos y teclado** configurables (ver más abajo). Admite cualquier mando que SDL
  reconozca (Xbox, PlayStation, Switch Pro y compatibles) y se pueden conectar en caliente.

Aún no hay mappers de Codemasters.

## Controles

| Game Gear | Teclado | Mando |
|-----------|---------|-------|
| Cruceta   | Flechas | Cruceta o stick izquierdo |
| Botón 1   | Z       | A / X |
| Botón 2   | X       | B / Y |
| Start     | Intro   | Start |

Teclas del emulador: `Ctrl+O` abre una ROM, `F1` reinicia, `F2` cambia de vista (2D / capas),
`P` pausa, `M` silencia el sonido, `F11` (o `Alt+Intro`) pantalla completa, `Esc` muestra u oculta la barra de menús.
El efecto 3D se activa y desactiva con `F3` o con el botón Back/Select del mando; ambos se
pueden cambiar en Opciones > Controles. (La vista pseudo-3D aún no existe; el interruptor
queda guardado para cuando llegue.)

Lo más cómodo es cambiar los controles desde **Opciones > Controles**: pulsa la casilla y
luego la tecla o el botón. Todo se guarda en `plumgg.ini`.

La primera vez que se abre, plumgg crea `plumgg.ini` en la carpeta de ajustes del usuario
(`%APPDATA%\plumgg\plumgg\` en Windows). Si pones un `plumgg.ini` junto al ejecutable, se
usa ese (modo portátil). Cada botón admite varias teclas o botones separados por comas:

```ini
[teclado]
boton1 = Z, Space
[mando]
boton1 = a
boton2 = b
stick_como_cruceta = si
```

El mismo archivo guarda las opciones de vídeo (`[video]`) y las ROMs recientes (`[recientes]`).

## Compilar

Hace falta CMake 3.16+ y un compilador C++17. CMake descarga Dear ImGui (la interfaz de
menús) automáticamente.

**Windows (Visual Studio 2022):**

```
cmake -S . -B build -A x64
cmake --build build --config Release
build\Release\plumgg.exe ruta\al\juego.gg
```

Si SDL2 no está instalado, CMake lo descarga y lo compila automáticamente. Para usar un
SDL2 ya descargado, pasa `-DSDL2_DIR=C:\ruta\SDL2-2.30.9\cmake`. `SDL2.dll` se copia junto
al ejecutable.

**Linux:** instala `libsdl2-dev` y usa los mismos comandos de CMake.

## Tests

```
ctest --test-dir build              # mapper, VDP e interrupciones
build/zex_runner zexdoc.com         # ejercitador completo de la Z80
```

## Estructura

```
src/core/      emulación pura, sin SDL: z80, vdp, cartridge, gamegear
src/render/    renderers: convierten un VideoFrame en imagen
src/frontend/  ventana, bucle principal, controles y menús (SDL2 + Dear ImGui)
tests/         tests del núcleo y ejecutor de ZEXDOC/ZEXALL
installer/     instalador de Windows (Inno Setup)
```

### Pensado para el pseudo-3D

El VDP no dibuja en pantalla: rellena un `VideoFrame` (`src/core/video_frame.h`) con el
color final de cada píxel y, además, de qué capa viene (fondo, fondo con prioridad, sprite o
color de fondo), qué tile o sprite lo generó y el scroll de cada línea. Los renderers solo
leen esa estructura, así que un renderer pseudo-3D podrá separar las capas en profundidad
sin tocar la emulación. La vista "capas" (`F2`) muestra justo esa información.
