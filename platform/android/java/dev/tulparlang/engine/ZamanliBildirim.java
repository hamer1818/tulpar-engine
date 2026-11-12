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
//
// ARKA PLAN YOKLAMASI (KOPRU §15.2, WoE 2026-11-12): uzak (push) bildirim yok; sunucuda olup istemcinin bilmedigi
// olay (gelen saldiri) icin alici, oyunun kayit dizinine yazdigi "tulpar_yoklama.json" ayariyla sunucuyu araliklarla
// yoklar: {"url": "https://...", "govde": {...}, "aralik_sn": 900}. Yanit {"bildirimler": [{id, baslik, metin}]}.
// Ayni id 24 saat icinde bir kez gosterilir. Oyun on plandayken gosterilmez (oyun kendisi yazar). Ayar dosyasi
// yoksa/bossa yoklama durur. Kurulum: her kur()'da (oyun arka plana gecince zamanli bildirimleri birakir) ve
// BOOT_COMPLETED'da; zincir her yoklamada bir sonrakini kurar. https HttpsURLConnection ile; http (yerel sunucu,
// emulator 10.0.2.2) duz soketle — Android'in cleartext yasagi HttpURLConnection'a uygulanir, yerel test sunucusu
// TLS'siz.
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

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Map;

import org.json.JSONArray;
import org.json.JSONObject;

public final class ZamanliBildirim extends BroadcastReceiver {
    static final String TERCIH = "tulpar_zamanli";
    static final String EYLEM = "dev.tulparlang.engine.ZAMANLI_BILDIRIM";
    static final String KANAL = "tulpar_oyun"; // teng_notify ile ayni kanal (android_host.cpp kBildirimKanal)
    static final String YOKLA = "dev.tulparlang.engine.YOKLAMA";
    static final String YOKLAMA_DOSYA = "tulpar_yoklama.json";
    static final int YOKLAMA_ISTEK = 2000000001;   // PendingIntent istek kodu (bildirim kimlikleriyle cakismaz)

    // --- kopru (oyun is parcacigi) -------------------------------------------------------------------------
    /** gecikmeMs sonra bildirim. 1 kuruldu, 0 hata. Ayni id oncekinin yerine gecer. */
    public static int kur(Context c, int id, long gecikmeMs, String baslik, String metin) {
        try {
            Context u = c.getApplicationContext();
            long an = System.currentTimeMillis() + Math.max(0L, gecikmeMs);
            tercih(u).edit().putString("b" + id, an + "\n" + baslik + "\n" + (metin == null ? "" : metin)).commit();
            alarmKur(u, id, an);
            yoklamaKur(u, false);
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
            yoklamaKur(c.getApplicationContext(), false);
            return;
        }
        if (YOKLA.equals(eylem)) {
            final Context u = c.getApplicationContext();
            final PendingResult pr = goAsync();   // ag istegi ana is parcaciginda olmaz
            new Thread(() -> {
                try { yokla(u); } catch (Throwable ignored) { } finally { pr.finish(); }
            }, "tulpar-yoklama").start();
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
        bildirimYaz(u, id, baslik, metin);
    }

    static void bildirimYaz(Context u, int id, String baslik, String metin) {
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

    // --- arka plan yoklamasi (§15.2) ------------------------------------------------------------------------
    static JSONObject yoklamaAyar(Context u) {
        // oyunun goreli yolu motorun calisma dizinine duser: files/assets (android_host: varliklar oraya acilir, cwd)
        File[] adaylar = { new File(u.getFilesDir(), "assets"), u.getExternalFilesDir(null), u.getFilesDir() };
        for (File d : adaylar) {
            if (d == null) continue;
            File f = new File(d, YOKLAMA_DOSYA);
            if (!f.isFile() || f.length() < 10 || f.length() > 8192) continue;
            try (FileInputStream in = new FileInputStream(f)) {
                JSONObject j = new JSONObject(new String(oku(in), StandardCharsets.UTF_8));
                String url = j.optString("url", "");
                if (!(url.startsWith("https://") || url.startsWith("http://")) || url.length() > 2048) continue;
                return j;
            } catch (Throwable ignored) { }
        }
        return null;
    }

    /** Ayar varsa yoklama alarmini kurar (yeniden=false: kuruluysa dokunmaz); ayar yoksa iptal eder. */
    static void yoklamaKur(Context u, boolean yeniden) {
        try {
            AlarmManager am = (AlarmManager) u.getSystemService(Context.ALARM_SERVICE);
            if (am == null) return;
            JSONObject j = yoklamaAyar(u);
            PendingIntent var = yoklamaNiyet(u, PendingIntent.FLAG_NO_CREATE);
            if (j == null) {
                if (var != null) { am.cancel(var); var.cancel(); }
                return;
            }
            // PendingIntent alarm tetiklendikten sonra da sistemde kalir: "kurulu mu" sorusu kaydedilen zamana bakar
            // (zaman gecmis ve 2 dk icinde tetiklenmemisse zincir kopmus sayilir: surec yoklama sirasinda olduruldu,
            // veri temizlendi).
            long simdi = System.currentTimeMillis();
            long kurulu = tercih(u).getLong("yoklama_an", 0L);
            if (!yeniden && var != null && kurulu > simdi - 120000L) return;
            long an = simdi + Math.max(15, j.optInt("aralik_sn", 900)) * 1000L;
            tercih(u).edit().putLong("yoklama_an", an).commit();
            android.util.Log.i("tulpar", "yoklama: " + j.optInt("aralik_sn", 900) + " sn sonra kuruldu");
            PendingIntent pi = yoklamaNiyet(u, PendingIntent.FLAG_UPDATE_CURRENT);
            if (Build.VERSION.SDK_INT >= 23) am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, an, pi);
            else am.set(AlarmManager.RTC_WAKEUP, an, pi);
        } catch (Throwable ignored) { }
    }

    static PendingIntent yoklamaNiyet(Context u, int bayrak) {
        Intent i = new Intent(u, ZamanliBildirim.class).setAction(YOKLA);
        return PendingIntent.getBroadcast(u, YOKLAMA_ISTEK, i, PendingIntent.FLAG_IMMUTABLE | bayrak);
    }

    static boolean onPlanda() {
        android.app.ActivityManager.RunningAppProcessInfo b = new android.app.ActivityManager.RunningAppProcessInfo();
        android.app.ActivityManager.getMyMemoryState(b);
        return b.importance <= android.app.ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
    }

    static void yokla(Context u) {
        JSONObject j = yoklamaAyar(u);
        if (j == null) { android.util.Log.i("tulpar", "yoklama: ayar yok, zincir durdu"); return; }   // ayar silindi
        try {
            JSONObject govde = j.optJSONObject("govde");
            String yanit = postJson(j.getString("url"), govde == null ? "{}" : govde.toString());
            JSONArray bl = yanit == null ? null : new JSONObject(yanit).optJSONArray("bildirimler");
            SharedPreferences sp = tercih(u);
            long simdi = System.currentTimeMillis();
            SharedPreferences.Editor ed = sp.edit();
            for (Map.Entry<String, ?> g : sp.getAll().entrySet()) {   // 24 saati gecen "gosterildi" kayitlari
                if (g.getKey().startsWith("y") && g.getValue() instanceof Long && simdi - (Long) g.getValue() > 86400000L) ed.remove(g.getKey());
            }
            boolean on = onPlanda();
            android.util.Log.i("tulpar", "yoklama: " + (bl == null ? "yanit yok" : bl.length() + " bildirim") + (on ? " (oyun on planda: gosterilmez)" : ""));
            for (int i = 0; bl != null && i < bl.length() && i < 20; i++) {
                JSONObject b = bl.getJSONObject(i);
                int id = b.optInt("id", -1);
                String baslik = b.optString("baslik", "");
                if (id < 0 || baslik.isEmpty() || sp.contains("y" + id) || on) continue;
                bildirimYaz(u, id, baslik, b.optString("metin", ""));
                ed.putLong("y" + id, simdi);
            }
            ed.putLong("yoklama_son", simdi).putInt("yoklama_sayac", sp.getInt("yoklama_sayac", 0) + 1).commit();
        } catch (Throwable t) {
            android.util.Log.i("tulpar", "yoklama hatasi: " + t);
        }
        yoklamaKur(u, true);                            // zincirin sonraki halkasi
    }

    static String postJson(String url, String govde) throws Exception {
        byte[] g = govde.getBytes(StandardCharsets.UTF_8);
        if (url.startsWith("https://")) {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(15000); c.setReadTimeout(15000);
            c.setRequestMethod("POST"); c.setDoOutput(true);
            c.setRequestProperty("Content-Type", "application/json");
            try (OutputStream o = c.getOutputStream()) { o.write(g); }
            if (c.getResponseCode() != 200) return null;
            try (InputStream in = c.getInputStream()) { return new String(oku(in), StandardCharsets.UTF_8); }
        }
        URL u = new URL(url);
        int port = u.getPort() > 0 ? u.getPort() : 80;
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(u.getHost(), port), 15000);
            s.setSoTimeout(15000);
            String yol = u.getFile().isEmpty() ? "/" : u.getFile();
            String bas = "POST " + yol + " HTTP/1.0\r\nHost: " + u.getHost() + ":" + port
                       + "\r\nContent-Type: application/json\r\nContent-Length: " + g.length + "\r\nConnection: close\r\n\r\n";
            OutputStream o = s.getOutputStream();
            o.write(bas.getBytes(StandardCharsets.UTF_8)); o.write(g); o.flush();
            String y = new String(oku(s.getInputStream()), StandardCharsets.UTF_8);
            int ayr = y.indexOf("\r\n\r\n");
            if (!y.startsWith("HTTP/1.") || ayr < 0 || y.length() < 12 || !y.substring(9, 12).equals("200")) return null;
            return y.substring(ayr + 4);
        }
    }

    static byte[] oku(InputStream in) throws Exception {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        byte[] t = new byte[4096];
        int n;
        while ((n = in.read(t)) > 0) { b.write(t, 0, n); if (b.size() > 262144) break; }
        return b.toByteArray();
    }

    static SharedPreferences tercih(Context u) {
        return u.getSharedPreferences(TERCIH, Context.MODE_PRIVATE);
    }
}
