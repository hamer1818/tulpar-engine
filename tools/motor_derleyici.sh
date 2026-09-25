#!/usr/bin/env bash
# Tulpar Engine — motoru TANIYAN bir Tulpar derleyicisi kur (Linux / macOS).
#
#   tools/motor_derleyici.sh                     ../Tulpar kopyasindan kur
#   tools/motor_derleyici.sh --tulpar /yol/Tulpar
#   tools/motor_derleyici.sh --dogrula           yalniz mevcut derleyiciyi dene
#
# Cikti: yapi/tulpar-motor/tulpar. Editorun "Oyunu calistir"i bunu arar
# (TULPAR_MOTOR_DERLEYICI ile ezilebilir).
#
# NEDEN VAR: TulparLang derleyicisi motoru 2026-09-20'den beri TANIMIYOR
# (derleyici deposu 86e2c4e: motor ayri depoya tasindi, kopru dilden cikti).
# Kurulu `tulpar` `import "engine"` eden bir oyunu DERLEYEMEZ. Motor oyunu
# calistirmanin tek yolu bu kopruyu bir derleyici kopyasina geri baglamak —
# bu betik o tarifi otomatiklestiriyor.
#
# KULLANICININ KOPYASINA DOKUNMAZ: is `git worktree` ile yapi/tulpar-motor/
# kaynak altinda ayri bir calisma agacinda yapilir; dal acilmaz, dosya
# degismez. `derle.sh --temiz` yapi/'yi silerse worktree kaydi kalir, bu betik
# ilk is `git worktree prune` yapar.
#
# KIRILGAN NOKTA (BILEREK SOYLENIYOR): kopru 86e2c4e'nin TERSI uygulanarak
# geri geliyor. Derleyici o dosyalara dokundukca ters yama tutmayabilir;
# tutmazsa betik burada ve adiyla durur. Kalici cozum derleyiciye motor icin
# bir eklenti noktasi — bu betigin kapsami degil.
set -uo pipefail

K='\033[0;31m'; Y='\033[0;32m'; S='\033[0;33m'; M='\033[0;36m'; N='\033[0m'
[ -t 1 ] || { K=''; Y=''; S=''; M=''; N=''; }
bilgi() { printf "${M}»${N} %s\n" "$*"; }
iyi()   { printf "${Y}✓${N} %s\n" "$*"; }
uyar()  { printf "${S}!${N} %s\n" "$*"; }
hata()  { printf "${K}✗${N} %s\n" "$*" >&2; }

kok="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$kok"
hedef="$kok/yapi/tulpar-motor"
wt="$hedef/kaynak"
ayrilma=86e2c4e
DOSYALAR=(CMakeLists.txt cmake/EmbedLibraries.cmake src/embedded_libs.h.in src/aot/aot_pipeline.cpp
          src/aot/llvm_backend.cpp src/aot/llvm_backend.hpp src/typeinfer/typeinfer.cpp src/lsp/builtins.cpp
          lib/engine.tpr runtime/engine_bindings.cpp src/aot/engine_builtins_table.inc
          src/typeinfer/engine_builtins_sigs.inc src/lsp/engine_builtins.inc)

tulpar_src="${TULPAR_ROOT:-}"; sadece_dogrula=0
while [ $# -gt 0 ]; do case "$1" in
  --tulpar) [ $# -ge 2 ] || { hata "--tulpar bir yol ister"; exit 2; }; tulpar_src="$2"; shift ;;
  --dogrula) sadece_dogrula=1 ;;
  -h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *) hata "bilinmeyen secenek: $1 (--help)"; exit 2 ;;
esac; shift; done

if [ -n "${MSYSTEM:-}" ]; then
  # MSYS2'de `ln -s` varsayilan olarak KOPYALAR (motor deposunun tamami
  # derleyici agacina kopyalanirdi) ve Windows yolu burada OLCULMEDI.
  hata "Windows (MSYS2) henuz desteklenmiyor: bu tarif yalniz Linux/macOS'ta olculdu."
  exit 1
fi

dogrula() {
  local d="$hedef/tulpar"
  [ -x "$d" ] || { hata "derleyici yok: $d"; return 1; }
  bilgi "dogrulama: engine_ilk_oyun.tpr penceresiz 3 kare"
  local log="$hedef/dogrulama.log"
  if ! (cd "$kok/tulpar" && TULPAR_ENGINE_HEADLESS=3 DISPLAY= "$d" examples/engine_ilk_oyun.tpr) >"$log" 2>&1; then
    hata "derleyici motor oyununu kosturamadi — gunluk: $log"; tail -15 "$log" >&2; return 1
  fi
  # "kapanis:" satiri motorun kendi kapanis raporu: yalniz eng_init + eng_shutdown
  # gercekten kostuysa basilir. Cikis kodu 0 tek basina yetmez (derleyici
  # hata basip 0 ile de donebilir — TulparLang'da bir kez oldu).
  if ! grep -q "kapanis:" "$log"; then hata "oyun bitti ama motor kapanis raporu YOK — gunluk: $log"; return 1; fi
  iyi "motoru taniyan derleyici calisiyor: $d"
  grep "kapanis:" "$log" | head -1 | sed 's/^/    /'
  # Dil sondasi: motorun Tulpar kitapligi ve ornek oyunlar struct alanina
  # bilesik atama kullaniyor (`dusmanlar[i].can -= 50`). TulparLang #344'ten
  # (2026-09-24) once bu SESSIZCE hicbir sey yapmiyordu: oyun derlenir ve
  # calisir, ama dusmanlar olmez. Hata vermeyen bir derleyiciyle kurulum
  # "tamam" dememeli.
  local sonda="$hedef/dil_sondasi.tpr"
  printf 'struct D { int can; }\nD[] d = [];\nfunc main() { push(d, { can: 100 }); d[0].can -= 30; print("sonda " + toString(d[0].can)); }\nmain();\n' > "$sonda"
  local cevap
  cevap="$(cd "$hedef" && "$d" "$sonda" 2>&1 | tail -1)"
  if [ "$cevap" != "sonda 70" ]; then
    hata "derleyici struct alanina bilesik atamayi yanlis yapiyor ('d[0].can -= 30' -> '$cevap', beklenen 'sonda 70')"
    echo "  TulparLang #344 oncesi bir kopya: $tulpar_src icinde 'git pull' yapip bu betigi yeniden calistirin." >&2
    return 1
  fi
  iyi "dil sondasi: struct alanina bilesik atama dogru ($cevap)"
  # Ikinci sonda: import edilen modulun struct'lari. engine.tpr'nin `Vec3`'u
  # import edilen modulde tanimli; TulparLang #345'ten (2026-09-25) once bu tur
  # hic kaydedilmiyordu: her `v3()` malloc'lanan, hic birakilmayan bir nesneydi
  # (10M cagri 1013 ms / 4.65 GB; ana programdaki esi 37 ms / 2.5 MB) ve modulde
  # `D[]` derlenmiyordu. Eski derleyicide bu sonda DERLENMEZ.
  printf 'struct M { int can; }\nM[] ms = [];\nfunc m_ekle() { push(ms, { can: 5 }); ms[0].can -= 2; }\nfunc m_can(): int { return ms[0].can; }\n' > "$hedef/sonda_modul.tpr"
  printf 'import "sonda_modul.tpr";\nfunc main() { m_ekle(); print("modul " + toString(m_can())); }\nmain();\n' > "$hedef/dil_sondasi2.tpr"
  cevap="$(cd "$hedef" && "$d" dil_sondasi2.tpr 2>&1 | tail -1)"
  if [ "$cevap" != "modul 3" ]; then
    hata "derleyici import edilen modulun struct'larini tanimiyor ('$cevap', beklenen 'modul 3') — Vec3 her cagrida bellek ayirir"
    echo "  TulparLang #345 oncesi bir kopya: $tulpar_src icinde 'git pull' yapip bu betigi yeniden calistirin." >&2
    return 1
  fi
  iyi "dil sondasi: import edilen struct kutusuz ($cevap)"
  # Ucuncu sonda: kare bellegi (engine.tpr her kareyi arena_save/arena_drop ile
  # sariyor, Tuzaklar 8ch). TulparLang #347'den (2026-09-25) once global dizgiye
  # `+=` kalici kopyaya gitmiyordu: geri sarimdan sonra deger cop ("<object>")
  # olur, oyun hata vermez. Olculdu: eski derleyici 'arena <object>', yenisi
  # 'arena k0k1k2'.
  printf 'str g = "";\nfunc main() {\n    for (int k = 0; k < 3; k++) {\n        var wm = arena_save();\n        g += "k" + toString(k);\n        arena_drop(wm);\n        var wz = arena_save();\n        array cop = [];\n        for (int j = 0; j < 3000; j++) { push(cop, "COPCOPCOP" + toString(j)); }\n        arena_drop(wz);\n    }\n    print("arena " + g);\n}\nmain();\n' > "$hedef/dil_sondasi3.tpr"
  cevap="$(cd "$hedef" && "$d" dil_sondasi3.tpr 2>&1 | tail -1)"
  if [ "$cevap" != "arena k0k1k2" ]; then
    hata "derleyici kare belleginde global dizgiye '+=' degerini koruyamiyor ('$cevap', beklenen 'arena k0k1k2') — oyun metinleri sessizce bozulur"
    echo "  TulparLang #347 oncesi bir kopya: $tulpar_src icinde 'git pull' yapip bu betigi yeniden calistirin." >&2
    return 1
  fi
  iyi "dil sondasi: kare belleginde global '+=' kalici ($cevap)"
}
[ "$sadece_dogrula" = 1 ] && { dogrula; exit $?; }

# --- Kaynak kopya -------------------------------------------------------------
if [ -z "$tulpar_src" ]; then
  for aday in "$kok/../Tulpar" "$kok/../TulparLang"; do
    [ -f "$aday/src/vm/vm.hpp" ] && { tulpar_src="$(cd "$aday" && pwd)"; break; }
  done
fi
if [ -z "$tulpar_src" ] || [ ! -f "$tulpar_src/src/vm/vm.hpp" ]; then
  hata "TulparLang kopyasi bulunamadi${tulpar_src:+: $tulpar_src}"
  echo "  --tulpar /yol/TulparLang ya da TULPAR_ROOT verin."; exit 1
fi
git -C "$tulpar_src" cat-file -e "$ayrilma^{commit}" 2>/dev/null \
  || { hata "$tulpar_src icinde $ayrilma commit'i yok (sig klon mu? git fetch --unshallow)"; exit 1; }
bilgi "TulparLang: $tulpar_src ($(git -C "$tulpar_src" rev-parse --short HEAD))"
# Uretilmis baglama betik kancalarini TulparLang'in aot_func_lookup'iyla cozuyor
# (yuklemede bir kez; kare icinde adsiz cagri). Eski bir kopyada bu, LLVM arka
# ucu dakikalarca derlendikten SONRA "undefined reference" ile duserdi — derleme
# HEAD'den yapiliyor, soru da HEAD'e. `grep -q` DEGIL (Tuzaklar 8ci): ilk
# eslesmede cikar, `git show` SIGPIPE alir ve `pipefail` ile boru hatti "yok"
# der (olculdu: bu satirin ilk hali aot_func_lookup'LI kopyayi da reddetti).
if ! git -C "$tulpar_src" show HEAD:src/vm/runtime_bindings.cpp 2>/dev/null | grep 'aot_func_lookup(const char' >/dev/null; then
  hata "$tulpar_src (HEAD $(git -C "$tulpar_src" rev-parse --short HEAD)) aot_func_lookup icermiyor — TulparLang'ı güncelleyin (git pull)"
  echo "  Motor betik kancalarini yuklemede bu islevle cozuyor; onsuz uretilmis baglama linklenemez." >&2
  exit 1
fi

# Motor arsivleri once: derleyici oyunu linklerken yapi/libengine_*.a'yi arar.
[ -f yapi/libengine_bridge.a ] || { hata "yapi/libengine_bridge.a yok — once ./derle.sh"; exit 1; }

# --- Calisma agaci ------------------------------------------------------------
mkdir -p "$hedef"
git -C "$tulpar_src" worktree prune
bas="$(git -C "$tulpar_src" rev-parse HEAD)"
if [ -e "$wt/.git" ]; then
  bilgi "calisma agaci guncelleniyor ($wt)"
  git -C "$wt" checkout -q --detach "$bas" -f && git -C "$wt" clean -qfd -e build/ || { hata "calisma agaci guncellenemedi"; exit 1; }
else
  bilgi "calisma agaci aciliyor ($wt) — kullanicinin kopyasi DEGISMEZ"
  git -C "$tulpar_src" worktree add -q --detach "$wt" "$bas" || { hata "git worktree add basarisiz"; exit 1; }
fi

# --- Kopruyu geri bagla -------------------------------------------------------
bilgi "kopru geri baglaniyor ($ayrilma tersi, ${#DOSYALAR[@]} dosya)"
if ! git -C "$tulpar_src" show "$ayrilma" -- "${DOSYALAR[@]}" | git -C "$wt" apply -R; then
  hata "$ayrilma'nin tersi artik TEMIZ UYGULANMIYOR: derleyici o dosyalari degistirmis."
  echo "  Tarifin guncellenmesi gerekiyor; ters yamayi zorlamak yanlis bir derleyici uretirdi."
  exit 1
fi
# Motor alt agaci derlenmez: hazir yapi/ arsivleri kullanilir.
sed -i.yedek 's/^\([[:space:]]*\)add_subdirectory(engine)/\1# add_subdirectory(engine)  # motor_derleyici.sh: hazir yapi\/ kullaniliyor/' "$wt/CMakeLists.txt"
rm -f "$wt/CMakeLists.txt.yedek"
ln -sfn "$kok" "$wt/engine" # basliklar: bridge/engine_api.h
python3 tools/gen_engine_bindings.py --tulpar "$wt" | sed 's/^/    /' || { hata "baglama uretimi dustu"; exit 1; }
cp tulpar/engine.tpr "$wt/lib/engine.tpr"

# --- Derle --------------------------------------------------------------------
cc=()
command -v ccache >/dev/null 2>&1 && cc=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
gen=(); command -v ninja >/dev/null 2>&1 && gen=(-G Ninja)
bilgi "yapilandiriliyor ($wt/build, Release)"
cmake -S "$wt" -B "$wt/build" ${gen[@]+"${gen[@]}"} -DCMAKE_BUILD_TYPE=Release ${cc[@]+"${cc[@]}"} >"$hedef/cmake.log" 2>&1 \
  || { hata "cmake dustu — gunluk: $hedef/cmake.log"; tail -20 "$hedef/cmake.log" >&2; exit 1; }
# Derleyici oyunu linklerken motor arsivlerini <derleyici dizini>/engine'de arar.
ln -sfn "$kok/yapi" "$wt/build/engine"
is=$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4 )
bilgi "derleniyor (-j$is; ilk seferde LLVM arka ucuyla birkac dakika)"
cmake --build "$wt/build" -j"$is" >"$hedef/derleme.log" 2>&1 \
  || { hata "derleme dustu — gunluk: $hedef/derleme.log"; grep -m5 "error" "$hedef/derleme.log" >&2; exit 1; }
[ -x "$wt/build/tulpar" ] || { hata "derleme bitti ama $wt/build/tulpar yok"; exit 1; }
ln -sfn "$wt/build/tulpar" "$hedef/tulpar"
iyi "derleyici: $hedef/tulpar ($("$hedef/tulpar" --version 2>&1 | head -1))"
dogrula
