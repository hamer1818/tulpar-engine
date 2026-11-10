// Tulpar Engine — Android'de Google Play Oyunlar (Play Games Services v2): giris + kayitli oyun (KOPRU §17).
//
// NativeActivity'nin Java'si yok; bu sinif APK'ya ancak oyun "oyunlar" profilini isterse girer
// (tools/android_google.py hazirla --profil oyunlar: play-services-games-v2, R8 ile kucultulur). Kopru
// (bridge/android_host.cpp) sinifi ETKINLIGIN sinif yukleyicisinden bulur ve statik yontemleri yoklar —
// yerel geri cagri kaydi yok (GoogleGiris ile ayni duzen).
//
// SDK kendini PlayGamesInitProvider ile baslatir (manifestte com.google.android.gms.games.APP_ID gerekir;
// android_google.py ekle --oyunlar-kimligi yazar). Play Oyunlar v2'de oturum cogu zaman KENDILIGINDEN acilir:
// giris(etkinlik, 0) yalniz sorar (isAuthenticated), giris(etkinlik, 1) Play'in giris penceresini acar.
//
// Kayitli oyun: oyuncunun Drive'indaki gizli uygulama klasoru (Play yonetir). Bir ad = bir kayit (bayt).
// Catisma politikasi EN YUKSEK ILERLEME: iki cihaz cevrimdisi yazdiysa Play setProgressValue'su buyuk olani
// secer; yine de catisma donerse (politika karar veremediyse) sunucudaki surum secilir ve islem surer.
package dev.tulparlang.engine;

import android.app.Activity;

import com.google.android.gms.games.AuthenticationResult;
import com.google.android.gms.games.GamesSignInClient;
import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.Player;
import com.google.android.gms.games.SnapshotsClient;
import com.google.android.gms.games.snapshot.Snapshot;
import com.google.android.gms.games.snapshot.SnapshotMetadataChange;
import com.google.android.gms.tasks.OnCompleteListener;
import com.google.android.gms.tasks.Task;

import java.nio.charset.StandardCharsets;

public final class PlayOyunlar {
    // giris: 0 sorulmadi, 1 suruyor, 2 girisli, 3 girissiz (oyuncu girmedi / vazgecti), 4 hata
    private static volatile int giris = 0;
    // islem (oku / yaz): 0 bos, 1 suruyor, 2 tamam, 4 hata, 5 kayit yok (bos)
    private static volatile int islem = 0;
    private static volatile String oyuncu = "";
    private static volatile String veri = "";
    private static volatile String hata = "";
    private static final int POLITIKA = SnapshotsClient.RESOLUTION_POLICY_HIGHEST_PROGRESS;

    private PlayOyunlar() {}

    public static int girisDurum() { return giris; }
    public static int islemDurum() { return islem; }
    public static String oyuncu() { return oyuncu; }
    public static String veri() { return veri; }
    public static String hata() { return hata; }
    public static void islemSifirla() { if (islem != 1) { islem = 0; veri = ""; } }

    /** 1: basladi, 0: zaten suruyor. etkilesimli != 0: Play'in giris penceresi. */
    public static int giris(final Activity a, final int etkilesimli) {
        if (giris == 1) return 0;
        giris = 1;
        hata = "";
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    GamesSignInClient c = PlayGames.getGamesSignInClient(a);
                    Task<AuthenticationResult> t = etkilesimli != 0 ? c.signIn() : c.isAuthenticated();
                    t.addOnCompleteListener(new OnCompleteListener<AuthenticationResult>() {
                        @Override public void onComplete(Task<AuthenticationResult> r) {
                            if (!r.isSuccessful()) { hata = "giris: " + r.getException(); giris = 4; return; }
                            if (!r.getResult().isAuthenticated()) { giris = 3; return; }
                            PlayGames.getPlayersClient(a).getCurrentPlayer().addOnCompleteListener(new OnCompleteListener<Player>() {
                                @Override public void onComplete(Task<Player> p) {
                                    if (p.isSuccessful() && p.getResult() != null) oyuncu = p.getResult().getDisplayName();
                                    giris = 2;
                                }
                            });
                        }
                    });
                } catch (Throwable e) {
                    hata = "giris: " + e;
                    giris = 4;
                }
            }
        });
        return 1;
    }

    /** Kaydi oku: 1 basladi, 0 mesgul ya da girissiz. Sonuc islemDurum()/veri(). */
    public static int oku(final Activity a, final String ad) {
        return ac(a, ad, null, "", 0);
    }

    /** Kaydi yaz (ustune): 1 basladi, 0 mesgul ya da girissiz. ilerleme: catisma politikasinin olcusu. */
    public static int yaz(final Activity a, final String ad, final String icerik, final String aciklama, final int ilerleme) {
        return ac(a, ad, icerik == null ? "" : icerik, aciklama, ilerleme);
    }

    private static int ac(final Activity a, final String ad, final String yazilacak, final String aciklama, final int ilerleme) {
        if (islem == 1 || giris != 2) return 0;
        islem = 1;
        veri = "";
        hata = "";
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    final SnapshotsClient s = PlayGames.getSnapshotsClient(a);
                    s.open(ad, true, POLITIKA).addOnCompleteListener(new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
                        @Override public void onComplete(Task<SnapshotsClient.DataOrConflict<Snapshot>> t) {
                            acildi(s, t, yazilacak, aciklama, ilerleme);
                        }
                    });
                } catch (Throwable e) {
                    hata = "ac: " + e;
                    islem = 4;
                }
            }
        });
        return 1;
    }

    private static void acildi(final SnapshotsClient s, Task<SnapshotsClient.DataOrConflict<Snapshot>> t,
                               final String yazilacak, final String aciklama, final int ilerleme) {
        try {
            if (!t.isSuccessful()) { hata = "ac: " + t.getException(); islem = 4; return; }
            SnapshotsClient.DataOrConflict<Snapshot> d = t.getResult();
            if (d.isConflict()) {
                // Politika karar veremedi: sunucudaki surumle coz, sonra ayni islemi o surumle surdur.
                SnapshotsClient.SnapshotConflict c = d.getConflict();
                s.resolveConflict(c.getConflictId(), c.getSnapshot()).addOnCompleteListener(
                    new OnCompleteListener<SnapshotsClient.DataOrConflict<Snapshot>>() {
                        @Override public void onComplete(Task<SnapshotsClient.DataOrConflict<Snapshot>> t2) {
                            acildi(s, t2, yazilacak, aciklama, ilerleme);
                        }
                    });
                return;
            }
            Snapshot sn = d.getData();
            if (yazilacak == null) {
                byte[] b = sn.getSnapshotContents().readFully();
                veri = new String(b, StandardCharsets.UTF_8);
                s.discardAndClose(sn);
                islem = b.length == 0 ? 5 : 2;
                return;
            }
            sn.getSnapshotContents().writeBytes(yazilacak.getBytes(StandardCharsets.UTF_8));
            SnapshotMetadataChange m = new SnapshotMetadataChange.Builder()
                .setDescription(aciklama == null ? "" : aciklama)
                .setProgressValue(Math.max(0, ilerleme))
                .build();
            s.commitAndClose(sn, m).addOnCompleteListener(new OnCompleteListener<com.google.android.gms.games.snapshot.SnapshotMetadata>() {
                @Override public void onComplete(Task<com.google.android.gms.games.snapshot.SnapshotMetadata> c) {
                    if (c.isSuccessful()) { islem = 2; } else { hata = "yaz: " + c.getException(); islem = 4; }
                }
            });
        } catch (Throwable e) {
            hata = "kayit: " + e;
            islem = 4;
        }
    }
}
