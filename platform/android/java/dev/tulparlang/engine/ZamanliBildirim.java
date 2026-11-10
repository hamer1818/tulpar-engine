// Tulpar Engine — Android zamanli yerel bildirim (teng_notify_at / _cancel / _due; KOPRU §15.1).
//
// teng_notify suredeki surec bildirim cubuguna yazar; surec olmusse (oyuncu uygulamayi kaydirip kapatti,
// sistem bellek icin oldurdu) hicbir sey gelmez. Bu sinif "su kadar saniye sonra" bildirimini SISTEME
// birakir: AlarmManager.setAndAllowWhileIdle (kesin olmayan; SCHEDULE_EXACT_ALARM izni ISTEMEZ, Doze'da
// birkac dakika kayabilir) suresi gelince bu alicisi uyandirir, alici bildirimi kendisi kurar. Surec
// gerekmez, Tulpar kodu kosmaz.
//
// Kayitlar SharedPreferences'ta ("tulpar_zamanli"): b<id> = "<duvar_ms>\n<baslik>\n<metin>". Boylece
//  - kalan(id) dogrulama/oyun icin sorgulanir,
//  - telefon yeniden baslarsa (AlarmManager alarmlari silinir) BOOT_COMPLETED'da hepsi yeniden kurulur;
//    suresi gecmis olanlar hemen gosterilir.
// Ayni id oncekinin yerine gecer (PendingIntent istek kodu = id; FLAG_UPDATE_CURRENT). Bildirim kimligi de
// ayni id: oyun teng_notify ile ayni id'yi kullanirsa iki yol birbirinin yerine gecer, cift bildirim olmaz.
//
// Kopru (bridge/android_host.cpp) sinifi etkinligin sinif yukleyicisinden bulur ve statik kur / iptal /
// kalan'i cagirir. Sinif APK'ya motorun Java paketiyle girer (tools/android_google.py); manifest alici
// kaydini ve RECEIVE_BOOT_COMPLETED iznini ayni arac ekler. Alici disa ACIK DEGIL (exported=false):
// AlarmManager'in PendingIntent'i ve sistemin BOOT_COMPLETED yayini (system uid) yine ulasir.
package dev.tulparlang.engine;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

import java.util.ArrayList;
import java.util.Map;

public final class ZamanliBildirim extends BroadcastReceiver {
    static final String TERCIH = "tulpar_zamanli";
    static final String EYLEM = "dev.tulparlang.engine.ZAMANLI_BILDIRIM";
    static final String KANAL = "tulpar_oyun"; // teng_notify ile ayni kanal (android_host.cpp kBildirimKanal)

    // --- kopru (oyun is parcacigi) -------------------------------------------------------------------------
    /** gecikmeMs sonra bildirim. 1 kuruldu, 0 hata. Ayni id oncekinin yerine gecer. */
    public static int kur(Context c, int id, long gecikmeMs, String baslik, String metin) {
        try {
            Context u = c.getApplicationContext();
            long an = System.currentTimeMillis() + Math.max(0L, gecikmeMs);
            tercih(u).edit().putString("b" + id, an + "\n" + baslik + "\n" + (metin == null ? "" : metin)).commit();
            alarmKur(u, id, an);
            return 1;
        } catch (Throwable t) {
            return 0;
        }
    }

    /** id < 0: hepsi. Doner: iptal edilen kayit sayisi (-1 hata). */
    public static int iptal(Context c, int id) {
        try {
            Context u = c.getApplicationContext();
            SharedPreferences sp = tercih(u);
            ArrayList<Integer> idler = new ArrayList<>();
            if (id >= 0) {
                if (sp.contains("b" + id)) idler.add(id);
            } else {
                for (String k : sp.getAll().keySet()) {
                    if (k.startsWith("b")) {
                        try { idler.add(Integer.parseInt(k.substring(1))); } catch (NumberFormatException ignored) { }
                    }
                }
            }
            AlarmManager am = (AlarmManager) u.getSystemService(Context.ALARM_SERVICE);
            SharedPreferences.Editor ed = sp.edit();
            for (int i : idler) {
                PendingIntent pi = niyet(u, i, PendingIntent.FLAG_NO_CREATE);
                if (pi != null && am != null) { am.cancel(pi); pi.cancel(); }
                ed.remove("b" + i);
            }
            ed.commit();
            return idler.size();
        } catch (Throwable t) {
            return -1;
        }
    }

    /** Kalan milisaniye (gecmisse 0); kayit yoksa -1. */
    public static long kalan(Context c, int id) {
        try {
            String s = tercih(c.getApplicationContext()).getString("b" + id, null);
            if (s == null) return -1L;
            long an = Long.parseLong(s.substring(0, s.indexOf('\n')));
            return Math.max(0L, an - System.currentTimeMillis());
        } catch (Throwable t) {
            return -1L;
        }
    }

    // --- sistem (ana is parcacigi; surec bunun icin baslatilmis olabilir) ------------------------------------
    @Override
    public void onReceive(Context c, Intent niyet) {
        String eylem = niyet == null ? null : niyet.getAction();
        if (Intent.ACTION_BOOT_COMPLETED.equals(eylem) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(eylem)) {
            yenidenKur(c.getApplicationContext());
            return;
        }
        if (!EYLEM.equals(eylem)) return;
        int id = niyet.getIntExtra("id", -1);
        if (id < 0) return;
        goster(c.getApplicationContext(), id);
    }

    static void yenidenKur(Context u) {
        long simdi = System.currentTimeMillis();
        for (Map.Entry<String, ?> g : tercih(u).getAll().entrySet()) {
            if (!g.getKey().startsWith("b") || !(g.getValue() instanceof String)) continue;
            try {
                int id = Integer.parseInt(g.getKey().substring(1));
                String s = (String) g.getValue();
                long an = Long.parseLong(s.substring(0, s.indexOf('\n')));
                if (an <= simdi) goster(u, id); else alarmKur(u, id, an);
            } catch (Throwable ignored) { }
        }
    }

    static void goster(Context u, int id) {
        SharedPreferences sp = tercih(u);
        String s = sp.getString("b" + id, null);
        sp.edit().remove("b" + id).commit();
        if (s == null) return; // iptal edilmis
        int a = s.indexOf('\n'), b = s.indexOf('\n', a + 1);
        if (a < 0 || b < 0) return;
        String baslik = s.substring(a + 1, b), metin = s.substring(b + 1);
        if (Build.VERSION.SDK_INT >= 33
                && u.checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED) {
            return; // izin yok: Android zaten gostermezdi
        }
        NotificationManager nm = (NotificationManager) u.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        Notification.Builder nb;
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel(KANAL, "Oyun", NotificationManager.IMPORTANCE_DEFAULT));
            nb = new Notification.Builder(u, KANAL);
        } else {
            nb = new Notification.Builder(u);
        }
        int simge = u.getApplicationInfo().icon;
        nb.setSmallIcon(simge != 0 ? simge : android.R.drawable.ic_dialog_info)
          .setContentTitle(baslik).setContentText(metin).setAutoCancel(true);
        Intent ac = u.getPackageManager().getLaunchIntentForPackage(u.getPackageName());
        if (ac != null) {
            nb.setContentIntent(PendingIntent.getActivity(u, 0, ac, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
        }
        nm.notify(id, nb.build());
    }

    static void alarmKur(Context u, int id, long an) {
        AlarmManager am = (AlarmManager) u.getSystemService(Context.ALARM_SERVICE);
        if (am == null) return;
        PendingIntent pi = niyet(u, id, PendingIntent.FLAG_UPDATE_CURRENT);
        if (Build.VERSION.SDK_INT >= 23) am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, an, pi);
        else am.set(AlarmManager.RTC_WAKEUP, an, pi);
    }

    static PendingIntent niyet(Context u, int id, int bayrak) {
        Intent i = new Intent(u, ZamanliBildirim.class).setAction(EYLEM).putExtra("id", id);
        return PendingIntent.getBroadcast(u, id, i, PendingIntent.FLAG_IMMUTABLE | bayrak);
    }

    static SharedPreferences tercih(Context u) {
        return u.getSharedPreferences(TERCIH, Context.MODE_PRIVATE);
    }
}
