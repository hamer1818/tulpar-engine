@echo off
rem ===========================================================================
rem Tulpar Engine - tek komutluk derleme betigi (Windows).
rem
rem   derle.bat                Bagimliliklari denetle, eksikse SOR, sonra derle
rem   derle.bat --otomatik     Sormadan kur ve derle
rem   derle.bat --denetle      Yalniz rapor; kurma, derleme
rem   derle.bat --temiz        Yapi dizinini sifirdan kur
rem   derle.bat --bekleme-yok  Hatada tus bekleme (betik icinden cagirirken)
rem
rem Windows'ta derleme MSYS2/MINGW64 ile yapilir. MSVC DESTEKLENMIYOR: fiber
rem gecisi GNU sozdizimli .S dosyasi ve derleme bayraklari GCC/Clang yaziminda.
rem
rem Bu dosya SAF ASCII ve CRLF'tir; cmd'nin OEM kod sayfasinda Turkce harfler
rem bozulur, o yuzden metinlerde aksan kullanilmaz.
rem ===========================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set RC=0

rem HATADA PENCEREYI ACIK TUT. Cift tiklandiginda hatada beklemezsek pencere
rem aninda kapanir ve kullanici hicbir sey goremez - kullanicinin editor
rem exe'sinde yasadigi hatanin ta kendisi.
rem
rem NEDEN ALGILAMA DEGIL DE VARSAYILAN: "cift tiklandi mi" sorusunu %cmdcmdline%
rem ile algilamak denendi; Wine'in cmd'si bu degiskeni BOS birakiyor, yani
rem algilamayi dogrulayamiyoruz. Algilama sessizce bosa cikarsa pencere yine
rem kapanir - kacinmak istedigimiz hatanin ta kendisi. O yuzden risk TERS
rem cevrildi: bekleme VARSAYILAN, cikis ACIKCA secilir. Kotu durum artik
rem "terminalde bir tusa basmak" (gorunur, zararsiz), "pencere yok oldu" degil.
set BEKLE=1
if defined CI set BEKLE=0
if /i "%TULPAR_BEKLEME%"=="0" set BEKLE=0

set OTOMATIK=0
set DENETLE=0
set TEMIZ=0
:argdongu
if "%~1"=="" goto argbitti
if /i "%~1"=="--otomatik" set OTOMATIK=1
if /i "%~1"=="--evet"     set OTOMATIK=1
if /i "%~1"=="-y"         set OTOMATIK=1
if /i "%~1"=="--denetle"  set DENETLE=1
if /i "%~1"=="--sadece-denetle" set DENETLE=1
if /i "%~1"=="--temiz"    set TEMIZ=1
if /i "%~1"=="--bekleme-yok" set BEKLE=0
if /i "%~1"=="--help"     goto yardim
if /i "%~1"=="-h"         goto yardim
shift
goto argdongu
:argbitti

echo.
echo   Tulpar Engine - Windows derleme
echo.

rem --- MSYS2 nerede? ---------------------------------------------------------
rem Sirayla: MSYS2_ROOT degiskeni, sonra yaygin kurulum yollari.
set MSYS=
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set MSYS=%MSYS2_ROOT%
if not defined MSYS if exist "C:\msys64\usr\bin\bash.exe" set MSYS=C:\msys64
if not defined MSYS if exist "%SystemDrive%\msys64\usr\bin\bash.exe" set MSYS=%SystemDrive%\msys64
if not defined MSYS if exist "%LOCALAPPDATA%\msys64\usr\bin\bash.exe" set MSYS=%LOCALAPPDATA%\msys64

if defined MSYS (
  echo   [+] MSYS2 bulundu: %MSYS%
  goto msys_var
)

echo   [X] MSYS2 BULUNAMADI - Windows'ta derleme icin gerekli.
echo.
echo   Elle kurmak icin:
echo       winget install -e --id MSYS2.MSYS2
echo   ya da: https://www.msys2.org adresinden kurulum dosyasini indirin.
echo.
if "%DENETLE%"=="1" (
  echo   ^(--denetle: kurulum yapilmadi^)
  set RC=1
  goto bitir
)
if "%OTOMATIK%"=="1" goto msys_kur
set /p CEVAP=  Otomatik kurayim mi? [e/H] 
if /i "%CEVAP%"=="e" goto msys_kur
if /i "%CEVAP%"=="evet" goto msys_kur
echo.
echo   Kurulum yapilmadi. Yukaridaki komutu calistirip tekrar deneyin.
set RC=1
goto bitir

:msys_kur
where winget >nul 2>&1
if errorlevel 1 (
  echo   [X] winget yok; MSYS2'yi otomatik kuramiyorum.
  echo       https://www.msys2.org adresinden elle kurun.
  set RC=1
  goto bitir
)
echo   [*] MSYS2 kuruluyor ^(winget^)...
winget install -e --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
  echo   [X] winget kurulumu basarisiz.
  set RC=1
  goto bitir
)
if exist "C:\msys64\usr\bin\bash.exe" set MSYS=C:\msys64
if not defined MSYS (
  echo   [X] MSYS2 kuruldu ama bulunamadi. Terminali kapatip acin, tekrar deneyin.
  set RC=1
  goto bitir
)
echo   [+] MSYS2 kuruldu: %MSYS%

:msys_var
rem --- Geri kalan is MSYS2'nin icinde: paket denetimi + derleme ---------------
rem Tek bir bash betigi ile yapilir ki mantik cmd ile bash arasinda BOLUNMESIN.
rem
rem MSYSTEM, bash BASLAMADAN once ortamda olmali: MINGW64 PATH'ini kuran sey
rem giris kabugunun okudugu /etc/profile'dir. Degiskeni ic kabukta atamak GEC
rem kalir - profil coktan kosmus olur ve MSYS PATH'i ile derlersiniz.
rem CHERE_INVOKING: profil ev dizinine gecmesin, depoda kalalim.
set BASH=%MSYS%\usr\bin\bash.exe
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
set "ARGS="
if "%OTOMATIK%"=="1" set "ARGS=%ARGS% --otomatik"
if "%DENETLE%"=="1"  set "ARGS=%ARGS% --denetle"
if "%TEMIZ%"=="1"    set "ARGS=%ARGS% --temiz"

"%BASH%" -lc "cd \"$(cygpath -u '%CD%')\" && exec bash ./tools/derle_mingw.sh%ARGS%"
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
  echo.
  echo   [X] Derleme %RC% koduyla bitti. Yukaridaki ilk hata satiri sebebi soyler.
)
goto bitir

:yardim
echo   derle.bat                Bagimliliklari denetle, eksikse SOR, sonra derle
echo   derle.bat --otomatik     Sormadan kur ve derle
echo   derle.bat --denetle      Yalniz rapor; kurma, derleme
echo   derle.bat --temiz        Yapi dizinini sifirdan kur
echo   derle.bat --bekleme-yok  Hatada tus bekleme (betik icinden cagirirken)
set RC=0
goto bitir

:bitir
rem Tek cikis noktasi: cift tiklandiysa ve is dustuyse pencereyi acik tut.
if not "%RC%"=="0" if "%BEKLE%"=="1" (
  echo.
  pause
)
exit /b %RC%
