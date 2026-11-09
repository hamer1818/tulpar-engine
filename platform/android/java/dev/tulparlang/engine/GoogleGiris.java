// Tulpar Engine — Android'de yerli "Google ile giris" (Credential Manager; KOPRU §16).
//
// NativeActivity'nin Java'si yok; bu sinif APK'ya ancak oyun Google paketini isterse girer
// (tools/android_google.py: androidx.credentials + play-services-auth + googleid, R8 ile kucultulur).
// Kopru (bridge/android_host.cpp) sinifi ETKINLIGIN sinif yukleyicisinden bulur ve statik yontemleri
// yoklar — yerel (native) geri cagri kaydi yok, JNI yuzeyi dar: basla / durum / jeton / hata / sifirla.
//
// Akis: basla(etkinlik, sunucuIstemciKimligi) ana is parcaciginda CredentialManager.getCredentialAsync'i
// "Sign in with Google" secenegiyle cagirir; sistem alttan hesap seciciyi acar (Chrome YOK). Sonuc bir
// id_token'dir (JWT, aud = sunucu istemci kimligi); oyun onu sunucuya yollar, sunucu Google'da dogrular.
// Android 14+ platform Credential Manager'i, daha eskilerde Play Hizmetleri yolu (HiddenActivity) kullanilir.
package dev.tulparlang.engine;

import android.app.Activity;
import android.os.Bundle;
import android.os.CancellationSignal;

import androidx.credentials.Credential;
import androidx.credentials.CredentialManager;
import androidx.credentials.CredentialManagerCallback;
import androidx.credentials.CustomCredential;
import androidx.credentials.GetCredentialRequest;
import androidx.credentials.GetCredentialResponse;
import androidx.credentials.exceptions.GetCredentialCancellationException;
import androidx.credentials.exceptions.GetCredentialException;
import androidx.credentials.exceptions.NoCredentialException;

import com.google.android.libraries.identity.googleid.GetSignInWithGoogleOption;
import com.google.android.libraries.identity.googleid.GoogleIdTokenCredential;

public final class GoogleGiris {
    // 0 bos, 1 suruyor, 2 tamam (jeton hazir), 3 oyuncu vazgecti, 4 hata, 5 hesap yok
    private static volatile int durum = 0;
    private static volatile String jeton = "";
    private static volatile String hata = "";

    private GoogleGiris() {}

    public static int durum() { return durum; }
    public static String jeton() { return jeton; }
    public static String hata() { return hata; }
    public static void sifirla() { if (durum != 1) { durum = 0; jeton = ""; hata = ""; } }

    /** 1: basladi, 0: zaten suruyor. Sonuc durum()/jeton()/hata() ile yoklanir. */
    public static int basla(final Activity etkinlik, final String sunucuIstemci) {
        if (durum == 1) return 0;
        jeton = "";
        hata = "";
        durum = 1;
        etkinlik.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    GetSignInWithGoogleOption secenek = new GetSignInWithGoogleOption.Builder(sunucuIstemci).build();
                    GetCredentialRequest istek = new GetCredentialRequest.Builder().addCredentialOption(secenek).build();
                    CredentialManager cm = CredentialManager.Companion.create(etkinlik);
                    cm.getCredentialAsync(etkinlik, istek, new CancellationSignal(), etkinlik.getMainExecutor(),
                        new CredentialManagerCallback<GetCredentialResponse, GetCredentialException>() {
                            @Override public void onResult(GetCredentialResponse yanit) { sonuc(yanit.getCredential()); }
                            @Override public void onError(GetCredentialException e) {
                                hata = e.getClass().getSimpleName() + ": " + e.getMessage();
                                durum = (e instanceof GetCredentialCancellationException) ? 3 : (e instanceof NoCredentialException) ? 5 : 4;
                            }
                        });
                } catch (Throwable t) {
                    hata = t.toString();
                    durum = 4;
                }
            }
        });
        return 1;
    }

    private static void sonuc(Credential c) {
        try {
            if (c instanceof CustomCredential) {
                Bundle veri = ((CustomCredential) c).getData();
                jeton = GoogleIdTokenCredential.createFrom(veri).getIdToken();
                durum = jeton.isEmpty() ? 4 : 2;
                if (durum == 4) hata = "bos id_token";
                return;
            }
            hata = "beklenmeyen kimlik turu: " + c.getType();
        } catch (Throwable t) {
            hata = t.toString();
        }
        durum = 4;
    }
}
