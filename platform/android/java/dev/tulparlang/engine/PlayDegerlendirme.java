// Tulpar Engine — Android'de Play'in uygulama ici degerlendirme penceresi (In-App Review; KOPRU §17.1).
//
// Oyuncu oyundan cikmadan yildiz verir/yorum yazar. Pencereyi Play cizer; gosterilip gosterilmedigini, oyuncunun
// puan verip vermedigini API SOYLEMEZ (kota: Play ayni oyuncuya kisa surede bir kez gosterir, gerisini sessizce
// yutar). Bu yuzden durum yalniz "akis bitti" der; oyun bir kez ister ve unutur.
//
// Sinif APK'ya PlayOyunlar ile ayni yoldan girer (tools/android_google.py --profil oyunlar: com.google.android.play
// :review, R8 ile kucultulur). Kopru (bridge/android_host.cpp) sinifi etkinligin sinif yukleyicisinden bulur ve
// statik yontemleri yoklar — yerel geri cagri kaydi yok.
package dev.tulparlang.engine;

import android.app.Activity;

import com.google.android.gms.tasks.OnCompleteListener;
import com.google.android.gms.tasks.Task;
import com.google.android.play.core.review.ReviewInfo;
import com.google.android.play.core.review.ReviewManager;
import com.google.android.play.core.review.ReviewManagerFactory;

public final class PlayDegerlendirme {
    // 0 bos, 1 suruyor, 2 akis bitti (pencere gosterildi YA DA kota yuzunden gosterilmedi), 4 hata
    private static volatile int durum = 0;
    private static volatile String hata = "";

    private PlayDegerlendirme() {}

    public static int durum() { return durum; }
    public static String hata() { return hata; }

    /** 1: basladi, 0: zaten suruyor. */
    public static int iste(final Activity a) {
        if (durum == 1) return 0;
        durum = 1;
        hata = "";
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    final ReviewManager m = ReviewManagerFactory.create(a);
                    m.requestReviewFlow().addOnCompleteListener(new OnCompleteListener<ReviewInfo>() {
                        @Override public void onComplete(Task<ReviewInfo> t) {
                            if (!t.isSuccessful()) { hata = "istek: " + t.getException(); durum = 4; return; }
                            try {
                                m.launchReviewFlow(a, t.getResult()).addOnCompleteListener(new OnCompleteListener<Void>() {
                                    @Override public void onComplete(Task<Void> b) {
                                        if (!b.isSuccessful()) { hata = "pencere: " + b.getException(); durum = 4; return; }
                                        durum = 2;
                                    }
                                });
                            } catch (Throwable e) {
                                hata = "pencere: " + e;
                                durum = 4;
                            }
                        }
                    });
                } catch (Throwable e) {
                    hata = "istek: " + e;
                    durum = 4;
                }
            }
        });
        return 1;
    }
}
