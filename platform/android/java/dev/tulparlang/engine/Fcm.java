// Tulpar Engine — Firebase Cloud Messaging (uzak/anlik bildirim; KOPRU §15.3).
//
// Gradle ve google-services eklentisi yok: Firebase, oyunun APK varliklarindaki "firebase.json" ile ELLE
// baslatilir: {"proje", "gonderici", "uygulama", "api_anahtari"} (google-services.json'dan; Firebase konsolu >
// Proje ayarlari > Android uygulamasi). Dosya yoksa hicbir sey yapilmaz (FCM kullanmayan oyunlar etkilenmez).
//
// Bu sinif bir ContentProvider: uygulama sureci her basladiginda (oyun, bildirim dokunusu, FCM uyandirmasi)
// Application'dan once onCreate kosar. Jeton alininca oyunun calisma dizinine (files/assets — motorun cwd'si,
// §15.2 ile ayni yer) "tulpar_fcm.txt" yazilir; oyun okuyup kendi sunucusuna kaydeder. Yeni jeton (yenileme)
// gelince dosya guncellenir (Firebase onNewToken -> getToken yine cagrilir: surec her basladiginda).
//
// Gosterim: sunucunun "notification" yuklu mesajlarini oyun arka plandayken / surec olmusken Firebase'in
// kendi FirebaseMessagingService'i bildirim cubuguna yazar (kanal: mesajdaki android.notification.channel_id,
// motorun kanali "tulpar_oyun" onerilir). Oyun on plandayken mesaj yutulur; oyun olayi zaten kendisi gosterir.
// Firebase ucretsiz (Spark) planinda FCM sinirsiz/ucretsiz.
package dev.tulparlang.engine;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.util.Log;

import com.google.firebase.FirebaseApp;
import com.google.firebase.FirebaseOptions;
import com.google.firebase.messaging.FirebaseMessaging;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public final class Fcm extends ContentProvider {
    static final String JETON_DOSYA = "tulpar_fcm.txt";

    @Override
    public boolean onCreate() {
        Context c = getContext();
        if (c != null) baslat(c.getApplicationContext());
        return true;
    }

    static JSONObject ayar(Context u) {
        for (String yol : new String[] { "firebase.json", "assets/firebase.json" }) {
            try (InputStream in = u.getAssets().open(yol)) {
                ByteArrayOutputStream b = new ByteArrayOutputStream();
                byte[] t = new byte[2048];
                int n;
                while ((n = in.read(t)) > 0) b.write(t, 0, n);
                return new JSONObject(new String(b.toByteArray(), StandardCharsets.UTF_8));
            } catch (Throwable ignored) { }
        }
        return null;
    }

    static void baslat(Context u) {
        try {
            JSONObject j = ayar(u);
            if (j == null) return;                                   // oyun FCM kullanmiyor
            if (FirebaseApp.getApps(u).isEmpty()) {
                FirebaseOptions o = new FirebaseOptions.Builder()
                        .setApplicationId(j.getString("uygulama"))
                        .setApiKey(j.getString("api_anahtari"))
                        .setProjectId(j.getString("proje"))
                        .setGcmSenderId(j.getString("gonderici"))
                        .build();
                FirebaseApp.initializeApp(u, o);
            }
            FirebaseMessaging.getInstance().getToken().addOnCompleteListener(g -> {
                if (!g.isSuccessful() || g.getResult() == null) {
                    Log.i("tulpar", "fcm: jeton alinamadi: " + g.getException());
                    return;
                }
                jetonYaz(u, g.getResult());
            });
        } catch (Throwable t) {
            Log.i("tulpar", "fcm: baslatilamadi: " + t);
        }
    }

    static void jetonYaz(Context u, String jeton) {
        try {
            File d = new File(u.getFilesDir(), "assets");
            if (!d.isDirectory() && !d.mkdirs()) return;
            try (FileOutputStream o = new FileOutputStream(new File(d, JETON_DOSYA))) {
                o.write(jeton.getBytes(StandardCharsets.UTF_8));
            }
            Log.i("tulpar", "fcm: jeton yazildi (" + jeton.length() + " karakter)");
        } catch (Throwable t) {
            Log.i("tulpar", "fcm: jeton yazilamadi: " + t);
        }
    }

    // ContentProvider sozlesmesi: veri sunmaz
    @Override public Cursor query(Uri u, String[] p, String s, String[] a, String o) { return null; }
    @Override public String getType(Uri u) { return null; }
    @Override public Uri insert(Uri u, ContentValues v) { return null; }
    @Override public int delete(Uri u, String s, String[] a) { return 0; }
    @Override public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}
