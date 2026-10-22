# Seven Tools of the Bandit — PC Edition

Puerto para Windows del firmware de M5Stack Cardputer "Seven Tools of the
Bandit". Misma pantalla 240×135, mismo menú de 7 categorías, mismos sonidos
y controles (`;` `.` mover, `ENTER` elegir, `` ` `` volver, `i` info,
`M` silenciar) — pero corriendo como una app de escritorio.

**Las herramientas de red son reales**: usan la tarjeta de red de tu PC
(WiFi scan vía WLAN API de Windows, ping/portscan/HTTP/DNS vía Winsock,
traceroute vía ICMP). No hay datos simulados.

## Qué es físicamente imposible en una PC (y se avisa en pantalla)

- **Monitor de paquetes en modo promiscuo**: exclusivo del chip WiFi del
  ESP32. Una tarjeta WiFi normal de PC no puede capturar tráfico ajeno sin
  un adaptador/driver especial de monitor mode.
- **Teclado/Mouse USB real**: el Cardputer se anuncia como dispositivo USB
  ante *otro* equipo. Una PC no puede declararse a sí misma como HID USB.
  En su lugar, "Combos de teclado" envía los mismos atajos (Win+L, Alt+Tab,
  Ctrl+Alt+Supr, etc.) directamente a esta PC vía `SendInput` — mismo
  resultado práctico, mecanismo distinto.
- **Modo Access Point propio** del servidor de archivos: el servidor PC
  solo opera en modo "en mi red" (STA), compartiendo la carpeta
  `cardputer_sd/` por HTTP en el puerto 8080.

## Compilar (dos opciones)

### Opción A — GitHub Actions (recomendada, no instalas nada)

1. Crea un repositorio nuevo en GitHub y sube esta carpeta completa
   (incluye `.github/workflows/build.yml`).
2. En GitHub, pestaña **Actions** → el workflow "Build Windows EXE" corre
   solo. Tarda ~3-5 minutos.
3. Al terminar, abre el run y descarga el artifact `SevenToolsBandit-windows`
   (zip con el `.exe`, sus DLLs y la carpeta `assets/`).
4. Descomprime todo junto en una carpeta y ejecuta `SevenToolsBandit.exe`.

### Opción B — Compilar en tu propia PC con MSYS2

1. Instala [MSYS2](https://www.msys2.org/) (sigue los pasos de su web).
2. Abre la terminal **MSYS2 MINGW64** (no la MSYS normal) y ejecuta:
   ```bash
   pacman -Syu   # puede pedir reabrir la terminal, vuelve a ejecutar si lo pide
   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
             mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 \
             mingw-w64-x86_64-SDL2_ttf
   ```
3. Dentro de la carpeta del proyecto:
   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```
4. El `.exe` queda en `build/SevenToolsBandit.exe`. Cópialo junto con la
   carpeta `assets/` a donde quieras usarlo (doble clic para correrlo fuera
   de MSYS2 — si se queja de DLLs faltantes, cópialas desde
   `C:\msys64\mingw64\bin\` o enlaza estático, ver nota abajo).

### Nota sobre DLLs

El CMakeLists ya pide enlace estático de libgcc/libstdc++, pero SDL2 y
SDL2_ttf en MSYS2 suelen venir como `.dll`. Si al ejecutar el `.exe` fuera
de la terminal MSYS2 te falta alguna DLL (`SDL2.dll`, `SDL2_ttf.dll`,
`libfreetype-6.dll`, `zlib1.dll`, etc.), cópialas desde
`C:\msys64\mingw64\bin\` a la misma carpeta del `.exe`. El workflow de
GitHub Actions ya hace esta copia automáticamente.

## Estructura del proyecto

```
src/main.cpp         - todo el código (framework SDL2 + herramientas)
assets/font.ttf       - DejaVu Sans Mono (licencia libre, incluida)
assets/font_bold.ttf
CMakeLists.txt         - build con CMake/Ninja
.github/workflows/     - compila el .exe automáticamente en la nube
```

## Si el compilador marca errores

Escribí y revisé el código con cuidado pero **no pude compilarlo para
Windows en este entorno** (sin acceso a internet para instalar el
compilador cruzado ni a los headers reales del SDK de Windows). Es
esperable que aparezcan 1-2 errores menores de compilación (typos en
nombres de structs/campos de la API de Windows, por ejemplo). Si te pasa,
copia el mensaje de error completo y te lo arreglo.
