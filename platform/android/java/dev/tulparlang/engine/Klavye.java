// Tulpar Engine — Android ekran klavyesi (eng_text_input; KOPRU §14 "metin girisi").
//
// ANativeActivity_showSoftInput NativeActivity'nin kendi gorunumune (NativeContentView) klavye ister; o gorunum
// metin duzenleyici olmadigi icin (onCheckIsTextEditor false) bircok cihazda / Android surumunde klavye HIC
// ACILMAZ (Warlords of Era, 2026-11-12: emulator API 31 mInputShown=false, kullanici telefonu da ayni).
//
// Bu sinif etkinlige gorunmez 1x1 bir "metin duzenleyici" gorunum ekler, odagi ona verip klavyeyi o gorunume
// acar. Klavyenin yazdiklari (commitText, setComposingText, deleteSurroundingText, sendKeyEvent, editor eylemi)
// kod noktasi kuyruguna duser: 8 = sil, 10 = Enter. Kopru (bridge/android_host.cpp) acar/kapatir ve her karede
// kuyrugu al() ile bosaltir. Oneri (composing) yazan klavyelerde degisen kelime once silinip yeniden yazilir;
// oyun tarafi bunu duz sil + harf olarak gorur.
//
// Klavye ekrani tam kaplamasin (yatay modda "extract UI"): IME_FLAG_NO_EXTRACT_UI | IME_FLAG_NO_FULLSCREEN.
package dev.tulparlang.engine;

import android.app.Activity;
import android.content.Context;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

public final class Klavye {
    private static final int[] KUYRUK = new int[256];
    private static int adet = 0;
    private static Gorunum gorunum;

    /** Kopru (oyun is parcacigi): klavyeyi ac/kapat. 1 = istendi (UI is parcacigina gonderildi). */
    public static int goster(final Activity a, final boolean ac) {
        if (a == null) return 0;
        a.runOnUiThread(() -> {
            try {
                InputMethodManager imm = (InputMethodManager) a.getSystemService(Context.INPUT_METHOD_SERVICE);
                if (gorunum == null || gorunum.getContext() != a) {
                    gorunum = new Gorunum(a);
                    a.addContentView(gorunum, new ViewGroup.LayoutParams(1, 1));
                }
                if (ac) {
                    gorunum.besteleme = 0;
                    gorunum.setVisibility(View.VISIBLE);
                    gorunum.requestFocus();
                    if (imm != null) {
                        imm.restartInput(gorunum);
                        // odak yeni verildi: bir sonraki turda iste (ayni turda bazi IME'ler reddeder)
                        gorunum.post(() -> imm.showSoftInput(gorunum, InputMethodManager.SHOW_IMPLICIT));
                    }
                } else {
                    if (imm != null) imm.hideSoftInputFromWindow(gorunum.getWindowToken(), 0);
                    gorunum.clearFocus();
                }
            } catch (Throwable ignored) { }
        });
        return 1;
    }

    /** Kopru (oyun is parcacigi, her kare): bekleyen kod noktalarini al ve kuyrugu bosalt. */
    public static synchronized int[] al() {
        if (adet == 0) return null;
        int[] r = new int[adet];
        System.arraycopy(KUYRUK, 0, r, 0, adet);
        adet = 0;
        return r;
    }

    static synchronized void ekle(int cp) {
        if (adet < KUYRUK.length) KUYRUK[adet++] = cp;
    }

    static void metin(CharSequence s) {
        if (s == null) return;
        String t = s.toString();
        for (int i = 0; i < t.length(); ) {
            int cp = t.codePointAt(i);
            ekle(cp == '\n' ? 10 : cp);
            i += Character.charCount(cp);
        }
    }

    static void sil(int n) {
        for (int i = 0; i < n; i++) ekle(8);
    }

    /** Gorunmez metin duzenleyici: odak + IME baglantisi icin. */
    static final class Gorunum extends View {
        int besteleme = 0;   // su an ekranda duran "composing" metnin kod noktasi sayisi

        Gorunum(Context c) {
            super(c);
            setFocusable(true);
            setFocusableInTouchMode(true);
            setAlpha(0f);
        }

        @Override public boolean onCheckIsTextEditor() { return true; }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo o) {
            o.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            o.imeOptions = EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN;
            return new BaseInputConnection(this, false) {
                private void bestelemeyiSil() { sil(besteleme); besteleme = 0; }

                @Override public boolean commitText(CharSequence t, int yeni) {
                    bestelemeyiSil();
                    metin(t);
                    return true;
                }

                @Override public boolean setComposingText(CharSequence t, int yeni) {
                    bestelemeyiSil();
                    metin(t);
                    besteleme = t == null ? 0 : t.toString().codePointCount(0, t.length());
                    return true;
                }

                @Override public boolean finishComposingText() { besteleme = 0; return true; }

                @Override public boolean deleteSurroundingText(int once, int sonra) {
                    if (besteleme > 0) { int k = Math.min(once, besteleme); besteleme -= k; once -= k; sil(k); }
                    sil(once);
                    return true;
                }

                @Override public boolean sendKeyEvent(KeyEvent e) {
                    if (e.getAction() != KeyEvent.ACTION_DOWN) return true;
                    if (e.getKeyCode() == KeyEvent.KEYCODE_DEL) { if (besteleme > 0) besteleme--; ekle(8); }
                    else if (e.getKeyCode() == KeyEvent.KEYCODE_ENTER) { besteleme = 0; ekle(10); }
                    else { int cp = e.getUnicodeChar(e.getMetaState()); if (cp > 0) ekle(cp); }
                    return true;
                }

                @Override public boolean performEditorAction(int eylem) { besteleme = 0; ekle(10); return true; }
            };
        }
    }
}
