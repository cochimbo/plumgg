# Instalador de Windows

`plumgg.iss` es un script de [Inno Setup 6](https://jrsoftware.org/isinfo.php) que genera
`plumgg-<versión>-setup.exe`. El instalador:

- Copia el emulador (`plumgg.exe`), `SDL2.dll` y cualquier otro archivo que acompañe al ejecutable.
- Crea el acceso directo en el menú Inicio y, opcionalmente, en el escritorio.
- Puede asociar los archivos `.gg` (ROMs de Game Gear) para abrirlos con doble clic, y añade
  plumgg al menú "Abrir con" de los `.zip` (sin cambiar el programa que los abre por defecto).
- Se instala sin pedir permisos de administrador (o para todos los usuarios, si se elige).
- Está en español e inglés.

## Compilación automática

El workflow `.github/workflows/windows-installer.yml` compila el emulador con CMake + MSVC,
descarga SDL2, genera el instalador y lo sube como artefacto de la ejecución
(`plumgg-windows-installer`). Al publicar un tag `vX.Y.Z` el instalador se adjunta a la release.

Mientras no exista `CMakeLists.txt` en la raíz del repositorio, el workflow se salta la compilación.

## Compilación local

1. Compila el emulador en Release y copia `plumgg.exe` y `SDL2.dll` a una carpeta `dist\`
   en la raíz del repositorio.
2. Ejecuta:

   ```bat
   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 installer\plumgg.iss
   ```

El instalador queda en `dist-installer\`. Si el ejecutable tiene otro nombre, añade `/DAppExeName=otro.exe`.
