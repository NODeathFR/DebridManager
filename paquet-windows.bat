@echo off
rem ==========================================================================
rem  Debrid Manager - fabrication du paquet Windows portable
rem
rem  Double-cliquer sur ce fichier, ou le lancer depuis une invite de commandes.
rem    paquet-windows.bat             compile en Release et fabrique le paquet
rem    paquet-windows.bat /verifier   affiche seulement les outils trouves
rem
rem  Resultat : dist\DebridManager-<version>-windows-x64\      (dossier portable)
rem             dist\DebridManager-<version>-windows-x64.zip  (le meme, zippe)
rem
rem  Le script trouve seul Qt 6 (C:\Qt, D:\Qt, E:\Qt, dossier utilisateur\Qt),
rem  le compilateur (Visual Studio ou MinGW fourni avec Qt), CMake et Ninja.
rem  Pour imposer un kit Qt precis, avant de lancer :
rem    set QT_DIR=C:\Qt\6.8.1\msvc2022_64
rem
rem  Le paquet ne contient jamais les donnees du programme (data\ avec
rem  config.json et session.json, downloads\) : elles sont creees au premier lancement.
rem ==========================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "ROOT=%CD%"
set "BUILD=%ROOT%\build-windows"
set "MODE=paquet"
if /i "%~1"=="/verifier" set "MODE=verifier"

echo.
echo  === Debrid Manager : paquet Windows ===
echo.

if not exist "%ROOT%\CMakeLists.txt" goto :err_projet

rem --------------------------------------------------------------------------
rem  1. Trouver Qt 6
rem --------------------------------------------------------------------------
set "KIT="
set "KIT_TYPE="
set "VSPATH="
set "BEST_MSVC="
set "KEY_MSVC=-1"
set "BEST_MINGW="
set "KEY_MINGW=-1"

if defined QT_DIR (
    if exist "!QT_DIR!\bin\windeployqt.exe" (
        set "KIT=!QT_DIR!"
    ) else (
        echo  ERREUR : QT_DIR ne contient pas bin\windeployqt.exe :
        echo           !QT_DIR!
        goto :fail
    )
)

if not defined KIT (
    for %%R in ("C:\Qt" "D:\Qt" "E:\Qt" "%USERPROFILE%\Qt") do (
        if exist "%%~R\" call :scan_root "%%~R"
    )
    call :find_vs
    if defined BEST_MSVC if defined VSPATH set "KIT=!BEST_MSVC!"
    if not defined KIT if defined BEST_MINGW set "KIT=!BEST_MINGW!"
    rem Kit MSVC sans Visual Studio : le choisir quand meme, pour un message clair.
    if not defined KIT if defined BEST_MSVC set "KIT=!BEST_MSVC!"
)

rem Dernier recours : un windeployqt deja accessible (invite de commandes Qt).
if not defined KIT (
    for /f "delims=" %%W in ('where windeployqt.exe 2^>nul') do (
        if not defined KIT if exist "%%~dpW..\lib\cmake\Qt6\Qt6Config.cmake" (
            for %%D in ("%%~dpW..") do set "KIT=%%~fD"
        )
    )
)

if not defined KIT goto :err_qt

for %%D in ("%KIT%\.") do set "KIT=%%~fD"
for %%D in ("%KIT%\..") do set "QTVER=%%~nxD"
for %%D in ("%KIT%\..\..") do set "QTROOT=%%~fD"
if exist "%KIT%\lib\Qt6Core.lib" set "KIT_TYPE=msvc"
if exist "%KIT%\lib\libQt6Core.a" set "KIT_TYPE=mingw"
if not defined KIT_TYPE goto :err_kit_type

rem --------------------------------------------------------------------------
rem  2. Compilateur
rem --------------------------------------------------------------------------
if "%KIT_TYPE%"=="msvc" goto :tool_msvc
goto :tool_mingw

:tool_msvc
rem Deja dans une invite "Developer Command Prompt" ? Sinon, charger vcvars64.
if not defined VCToolsRedistDir goto :msvc_vcvars
where cl.exe >nul 2>nul
if not errorlevel 1 goto :tool_common
:msvc_vcvars
if not defined VSPATH call :find_vs
if not defined VSPATH goto :err_vs
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 goto :err_vs
goto :tool_common

:tool_mingw
set "MINGWBIN="
for /f "delims=" %%M in ('dir /b /ad /on "%QTROOT%\Tools\mingw*_64" 2^>nul') do (
    if exist "!QTROOT!\Tools\%%M\bin\g++.exe" set "MINGWBIN=!QTROOT!\Tools\%%M\bin"
)
if defined MINGWBIN set "PATH=%MINGWBIN%;%PATH%"
where g++.exe >nul 2>nul
if errorlevel 1 goto :err_mingw
where mingw32-make.exe >nul 2>nul
if errorlevel 1 goto :err_mingw
goto :tool_common

:tool_common
set "PATH=%KIT%\bin;%PATH%"

rem --------------------------------------------------------------------------
rem  3. CMake et Ninja
rem --------------------------------------------------------------------------
set "CMAKE_EXE="
if exist "%QTROOT%\Tools\CMake_64\bin\cmake.exe" set "CMAKE_EXE=%QTROOT%\Tools\CMake_64\bin\cmake.exe"
if not defined CMAKE_EXE for /f "delims=" %%C in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%C"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE goto :err_cmake

set "NINJA_EXE="
if exist "%QTROOT%\Tools\Ninja\ninja.exe" set "NINJA_EXE=%QTROOT%\Tools\Ninja\ninja.exe"
if not defined NINJA_EXE for /f "delims=" %%N in ('where ninja.exe 2^>nul') do if not defined NINJA_EXE set "NINJA_EXE=%%N"

if defined NINJA_EXE (
    set "GEN=Ninja"
) else (
    if "%KIT_TYPE%"=="msvc" (set "GEN=NMake Makefiles") else (set "GEN=MinGW Makefiles")
)

echo  Qt         : !QTVER!  [!KIT_TYPE!]
echo               !KIT!
if "%KIT_TYPE%"=="msvc" echo  Compilateur: Visual Studio
if "%KIT_TYPE%"=="mingw" echo  Compilateur: MinGW
if defined VSPATH echo               !VSPATH!
if defined MINGWBIN echo               !MINGWBIN!
echo  CMake      : !CMAKE_EXE!
echo  Generateur : %GEN%
if defined NINJA_EXE echo               !NINJA_EXE!
echo.

if "%MODE%"=="verifier" (
    echo  Verification terminee : tout est en place pour fabriquer le paquet.
    goto :end
)

rem --------------------------------------------------------------------------
rem  4. Compilation Release (dossier de compilation repris a zero)
rem --------------------------------------------------------------------------
echo  [1/4] Configuration...
if exist "%BUILD%" rd /s /q "%BUILD%"
if defined NINJA_EXE for %%N in ("%NINJA_EXE%") do set "PATH=%%~dpN;%PATH%"
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%KIT%" > "%ROOT%\build-windows.log" 2>&1
if errorlevel 1 goto :err_build

echo  [2/4] Compilation en Release (une a deux minutes)...
"%CMAKE_EXE%" --build "%BUILD%" --config Release --parallel %NUMBER_OF_PROCESSORS% >> "%ROOT%\build-windows.log" 2>&1
if errorlevel 1 goto :err_build
if not exist "%BUILD%\DebridManager.exe" goto :err_build

set "VER="
for /f "tokens=2 delims==" %%V in ('findstr /b /c:"CMAKE_PROJECT_VERSION:STATIC=" "%BUILD%\CMakeCache.txt"') do set "VER=%%V"
if not defined VER set "VER=inconnue"

rem --------------------------------------------------------------------------
rem  5. Dossier portable : exe + fichiers Qt strictement necessaires
rem --------------------------------------------------------------------------
echo  [3/4] Rassemblement des fichiers Qt...
set "NAME=DebridManager-%VER%-windows-x64"
set "OUT=%ROOT%\dist\%NAME%"
rem Si le programme a deja ete lance depuis ce dossier, il contient des donnees
rem personnelles (cles API...) : ne jamais les effacer sans prevenir.
if exist "%OUT%\data\" goto :err_data
if exist "%OUT%\config.json" goto :err_data
if exist "%OUT%\session.json" goto :err_data
if exist "%OUT%\downloads\" goto :err_data
if exist "%OUT%" rd /s /q "%OUT%"
if exist "%OUT%.zip" del /q "%OUT%.zip"
if exist "%OUT%" goto :err_locked
mkdir "%OUT%"
copy /y "%BUILD%\DebridManager.exe" "%OUT%\" >nul

set "WDQ_OPT=--release --no-translations --no-system-d3d-compiler --no-opengl-sw"
if "%KIT_TYPE%"=="msvc" set "WDQ_OPT=%WDQ_OPT% --no-compiler-runtime"
"%KIT%\bin\windeployqt.exe" %WDQ_OPT% "%OUT%\DebridManager.exe" >> "%ROOT%\build-windows.log" 2>&1
if errorlevel 1 goto :err_build

rem Modules ajoutes par windeployqt mais inutiles ici :
rem  - style Windows natif (le programme impose le style Fusion, integre a Qt)
rem  - OpenSSL et "certonly" (le HTTPS passe par schannel, integre a Windows)
rem  - images autres que SVG, saisie tactile, suivi de l'etat du reseau...
for %%P in (styles generic networkinformation platforminputcontexts qmltooling translations) do (
    if exist "!OUT!\%%P" rd /s /q "!OUT!\%%P"
)
call :keep_only tls qschannelbackend
call :keep_only imageformats qsvg
call :keep_only iconengines qsvgicon
call :keep_only platforms qwindows
if exist "%OUT%\vc_redist*.exe" del /q "%OUT%\vc_redist*.exe"

rem Visual Studio : copier les 3-4 DLL d'execution plutot que l'installateur.
if "%KIT_TYPE%"=="msvc" call :copy_msvc_runtime

rem Controle : tous les fichiers indispensables sont-ils la ?
set "MISSING="
for %%F in (DebridManager.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll Qt6Svg.dll platforms\qwindows.dll tls\qschannelbackend.dll imageformats\qsvg.dll iconengines\qsvgicon.dll) do (
    if not exist "!OUT!\%%F" (
        echo  ERREUR : fichier manquant dans le paquet : %%F
        set "MISSING=1"
    )
)
if "%KIT_TYPE%"=="msvc" if not exist "%OUT%\vcruntime140.dll" (
    echo  ERREUR : fichier manquant dans le paquet : vcruntime140.dll
    set "MISSING=1"
)
if "%KIT_TYPE%"=="mingw" if not exist "%OUT%\libstdc++-6.dll" (
    echo  ERREUR : fichier manquant dans le paquet : libstdc++-6.dll
    set "MISSING=1"
)
if defined MISSING goto :fail

rem --------------------------------------------------------------------------
rem  6. Archive .zip
rem --------------------------------------------------------------------------
echo  [4/4] Creation du .zip...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -LiteralPath $env:OUT -DestinationPath ($env:OUT + '.zip') -CompressionLevel Optimal -Force" >> "%ROOT%\build-windows.log" 2>&1
if errorlevel 1 goto :err_zip
if not exist "%OUT%.zip" goto :err_zip

rem --------------------------------------------------------------------------
rem  7. Ne garder que les 3 derniers paquets dans dist\
rem     Un ancien dossier portable qui contient data\ ou downloads\ (programme
rem     lance sur place) est garde : seul son .zip est supprime.
rem --------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath (Join-Path $env:ROOT 'dist') -Filter 'DebridManager-*-windows-x64.zip' | Sort-Object LastWriteTime -Descending | Select-Object -Skip 3 | ForEach-Object { $p = $_.FullName -replace '\.zip$', ''; Remove-Item -LiteralPath $_.FullName; if ((Test-Path -LiteralPath $p) -and -not (Test-Path -LiteralPath (Join-Path $p 'data')) -and -not (Test-Path -LiteralPath (Join-Path $p 'downloads'))) { Remove-Item -LiteralPath $p -Recurse -Force } }" >> "%ROOT%\build-windows.log" 2>&1

echo.
echo  Paquet pret, version %VER% :
echo    dist\%NAME%\
echo    dist\%NAME%.zip
echo.
echo  Pour mettre a jour une copie existante : tout remplacer SAUF
echo  les dossiers data\ et downloads\.
echo  Journal de compilation : build-windows.log
goto :end

rem ==========================================================================
rem  Sous-programmes
rem ==========================================================================

:scan_root
rem %1 = dossier racine de Qt (ex. C:\Qt). Retient le kit le plus recent
rem de chaque sorte (msvc*_64 / mingw*_64), Qt 6.4 minimum.
for /f "delims=" %%V in ('dir /b /ad "%~1\6.*" 2^>nul') do call :scan_version "%~1" "%%V"
goto :eof

:scan_version
set "VMAJ="
set "VMIN="
set "VPAT="
for /f "tokens=1-3 delims=." %%a in ("%~2") do (
    set "VMAJ=%%a"
    set "VMIN=%%b"
    set "VPAT=%%c"
)
if not "%VMAJ%"=="6" goto :eof
if not defined VMIN goto :eof
if not defined VPAT set "VPAT=0"
rem Seulement des chiffres (sinon ce n'est pas un dossier de version Qt).
for /f "delims=0123456789" %%x in ("%VMIN%%VPAT%") do goto :eof
set /a "KEY=VMIN*1000+VPAT"
if %VMIN% LSS 4 goto :eof
set "VDIR=%~1\%~2"
for /f "delims=" %%K in ('dir /b /ad "%VDIR%\msvc*_64" 2^>nul') do (
    if exist "!VDIR!\%%K\bin\windeployqt.exe" if !KEY! GTR !KEY_MSVC! (
        set "BEST_MSVC=!VDIR!\%%K"
        set "KEY_MSVC=!KEY!"
    )
)
for /f "delims=" %%K in ('dir /b /ad "%VDIR%\mingw*_64" 2^>nul') do (
    if exist "!VDIR!\%%K\bin\windeployqt.exe" if !KEY! GTR !KEY_MINGW! (
        set "BEST_MINGW=!VDIR!\%%K"
        set "KEY_MINGW=!KEY!"
    )
)
goto :eof

:keep_only
rem %1 = sous-dossier du paquet, %2 = seul module a garder dedans.
if not exist "%OUT%\%~1\" goto :eof
for /f "delims=" %%F in ('dir /b /a-d "%OUT%\%~1\*.dll" 2^>nul') do (
    if /i not "%%~nF"=="%~2" del /q "!OUT!\%~1\%%F"
)
goto :eof

:find_vs
rem Cherche Visual Studio / Build Tools avec les outils C++ (via vswhere).
set "VSPATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :eof
rem (passe par un fichier temporaire : "Program Files (x86)" contient des
rem parentheses qui cassent un for /f)
set "VSTMP=%TEMP%\dm_vswhere_%RANDOM%.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSTMP%" 2>nul
if exist "%VSTMP%" set /p VSPATH=<"%VSTMP%"
if exist "%VSTMP%" del /q "%VSTMP%"
if not defined VSPATH goto :eof
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH="
goto :eof

:copy_msvc_runtime
set "CRT="
if not defined VCToolsRedistDir goto :crt_missing
for /f "delims=" %%C in ('dir /b /ad "%VCToolsRedistDir%x64\Microsoft.VC*.CRT" 2^>nul') do set "CRT=!VCToolsRedistDir!x64\%%C"
if not defined CRT goto :crt_missing
copy /y "%CRT%\msvcp140*.dll" "%OUT%\" >nul
copy /y "%CRT%\vcruntime140*.dll" "%OUT%\" >nul
goto :eof
:crt_missing
echo  ERREUR : DLL d'execution Visual C++ introuvables (VCToolsRedistDir).
goto :eof

rem ==========================================================================
rem  Messages d'erreur
rem ==========================================================================
:err_projet
echo  ERREUR : CMakeLists.txt introuvable. Ce script doit rester a la racine du projet.
goto :fail

:err_qt
echo  ERREUR : Qt 6 introuvable.
echo  Installer Qt 6.4 ou plus recent avec l'installateur officiel, en cochant
echo  un kit "MSVC 64-bit" ou "MinGW 64-bit" et le module "Qt SVG",
echo  ou indiquer son dossier avant de lancer ce script, par exemple :
echo    set QT_DIR=C:\Qt\6.8.1\msvc2022_64
goto :fail

:err_kit_type
echo  ERREUR : impossible de savoir si ce kit Qt est MSVC ou MinGW :
echo    !KIT!
goto :fail

:err_vs
echo  ERREUR : ce kit Qt demande Visual Studio (ou "Build Tools for Visual
echo  Studio") avec la charge "Developpement Desktop en C++", introuvable.
echo  Autre solution : installer le kit Qt "MinGW 64-bit" et son compilateur
echo  MinGW (rubrique Developer and Designer Tools de l'installateur Qt).
goto :fail

:err_mingw
echo  ERREUR : compilateur MinGW introuvable. Dans l'installateur Qt, cocher
echo  "MinGW" dans la rubrique Developer and Designer Tools.
goto :fail

:err_cmake
echo  ERREUR : CMake introuvable. Dans l'installateur Qt, cocher "CMake"
echo  dans la rubrique Developer and Designer Tools.
goto :fail

:err_build
echo.
echo  ERREUR pendant la compilation ou le rassemblement des fichiers.
echo  Les 30 dernieres lignes du journal build-windows.log :
echo  ------------------------------------------------------------------
powershell -NoProfile -Command "Get-Content -LiteralPath ($env:ROOT + '\build-windows.log') -Tail 30"
echo  ------------------------------------------------------------------
goto :fail

:err_data
echo  ERREUR : !OUT!
echo  contient des donnees du programme (data\, config.json, session.json ou downloads\) :
echo  il a ete lance depuis dist\. Deplacer ce dossier ailleurs, puis relancer.
goto :fail

:err_locked
echo  ERREUR : impossible de vider !OUT!
echo  Le programme de ce dossier est peut-etre encore ouvert : le fermer.
goto :fail

:err_zip
echo  ERREUR : la creation du .zip a echoue (voir build-windows.log).
echo  Le dossier portable est tout de meme pret : !OUT!
goto :fail

:fail
echo.
echo  ECHEC.
echo.
if not defined DM_NOPAUSE pause
endlocal
exit /b 1

:end
echo.
if not defined DM_NOPAUSE pause
endlocal
exit /b 0
