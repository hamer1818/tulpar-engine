@echo off
rem ===========================================================================
rem Tulpar Engine - editoru tek tikla ac (Windows).
rem
rem   editor.bat                         Varsayilan sahne (tests\assets\editor.sahne)
rem   editor.bat yol\x.sahne             Verilen sahneyle ac
rem   editor.bat --derleme-yok           Derlemeyi atla, mevcut ikiliyi ac
rem   editor.bat --headless 30 --out k.ppm   Penceresiz dogrulama
rem   editor.bat --bekleme-yok           Hatada tus bekleme (betik icinden cagirirken)
rem
rem Ikili yoksa once derler (tools\derle_mingw.sh, bagimlilik denetimi dahil);
rem varsa artimli derler, kaynak degismediyse aninda gecer.
rem
rem ASIL IS editor.sh'de, MSYS2 MINGW64 icinde: derleme, sahne denetimi,
rem arguman cevirisi. Bu dosya yalniz MSYS2'yi bulur ve hatada pencereyi acik
rem tutar - mantik cmd ile bash arasinda BOLUNMESIN (derle.bat ile ayni sebep).
rem MINGW64 ayrica DLL'ler icin sart: glfw3.dll ve libstdc++ /mingw64/bin'de.
rem
rem Bu dosya SAF ASCII ve CRLF'tir; cmd'nin OEM kod sayfasinda Turkce harfler
rem bozulur, o yuzden metinlerde aksan kullanilmaz.
rem ===========================================================================
setlocal
set RC=0

rem Hatada bekleme VARSAYILAN - sebebi derle.bat'ta: cift tiklamayi guvenilir
rem algilayamiyoruz, algilama bosa cikarsa pencere aninda kapanir.
set BEKLE=1
if defined CI set BEKLE=0
if /i "%TULPAR_BEKLEME%"=="0" set BEKLE=0
rem --bekleme-yok'u %* icinden AYIKLAMAK cmd'de kirilgan (shift %*'i
rem degistirmez). Yalniz VAR MI diye bakilir; arguman editor.sh'ye aynen gider,
rem orada etkisiz.
for %%A in (%*) do (
  if /i "%%~A"=="--bekleme-yok" set BEKLE=0
  if /i "%%~A"=="--help" goto yardim
  if /i "%%~A"=="-h" goto yardim
)

rem --- MSYS2 nerede? (derle.bat ile ayni sira) -------------------------------
set MSYS=
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set MSYS=%MSYS2_ROOT%
if not defined MSYS if exist "C:\msys64\usr\bin\bash.exe" set MSYS=C:\msys64
if not defined MSYS if exist "%SystemDrive%\msys64\usr\bin\bash.exe" set MSYS=%SystemDrive%\msys64
if not defined MSYS if exist "%LOCALAPPDATA%\msys64\usr\bin\bash.exe" set MSYS=%LOCALAPPDATA%\msys64
if not defined MSYS (
  echo.
  echo   [X] MSYS2 BULUNAMADI - editoru derlemek ve calistirmak icin gerekli.
  echo       Once derle.bat'i calistirin: MSYS2'yi o kurar ve editoru derler.
  set RC=1
  goto bitir
)

rem --- editor.sh'ye devret -----------------------------------------------------
rem MSYSTEM, bash BASLAMADAN once ortamda olmali: MINGW64 PATH'ini kuran
rem /etc/profile'dir (giris kabugu). CHERE_INVOKING: profil ev dizinine
rem GECMESIN - kullanicinin bulundugu dizinde kalalim ki `editor.bat x.sahne`
rem gibi GORELI bir yol, cagrildigi dizine gore cozulsun.
rem
rem Betik yolu komut dizgisine GOMULMEZ, ortam degiskeniyle gecer: yol
rem bosluk, & ya da ' icerirse cmd'nin ve bash'in tirnak kurallari ayni anda
rem dogru olmak zorunda kalirdi. Argumanlar da bash'in konumsal parametreleri
rem olarak gider ("$@"), dizgiye yapistirilmaz.
set "BASH=%MSYS%\usr\bin\bash.exe"
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
set "TULPAR_EDITOR_SH=%~dp0editor.sh"
"%BASH%" -lc "exec bash \"$(cygpath -u \"$TULPAR_EDITOR_SH\")\" \"$@\"" editor %*
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
  echo.
  echo   [X] Editor %RC% koduyla bitti. Yukaridaki ilk hata satiri sebebi soyler.
  echo       Pencere acilamadiysa sebep yapi\engine_hata.log'da da yazili.
)
goto bitir

:yardim
echo   editor.bat                         Varsayilan sahne (tests\assets\editor.sahne)
echo   editor.bat yol\x.sahne             Verilen sahneyle ac
echo   editor.bat --derleme-yok           Derlemeyi atla, mevcut ikiliyi ac
echo   editor.bat --headless 30 --out k.ppm   Penceresiz dogrulama
echo   editor.bat --size 1600x900 --validation
echo   editor.bat --bekleme-yok           Hatada tus bekleme (betik icinden cagirirken)
set RC=0
goto bitir

:bitir
if not "%RC%"=="0" if "%BEKLE%"=="1" (
  echo.
  pause
)
exit /b %RC%
