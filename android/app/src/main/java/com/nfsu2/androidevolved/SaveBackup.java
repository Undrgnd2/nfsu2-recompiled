package com.nfsu2.androidevolved;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * The launcher's profiles: "Télécharger une copie" zips the game's save folder (AppData/Local/NFS Underground 2: each
 * profile, its GARAGE.DAT and cover shoots) as undrgnd2.eu's page does (nfsu2-web saves-ui.ts exportSaves: entries
 * "NFS Underground 2/<NAME>/..."), into Téléchargements/UNDRGND2; "Restaurer" takes such a zip, undrgnd2.eu's backup,
 * a zip of the PC game's folder or a profile file alone, and puts each profile found by what it holds (54,966 bytes,
 * "20CM", its name inside: saves-ui.ts profilesIn) in its own folder, with the GARAGE.DAT and MODPARTS.DAT beside it
 * in the zip and its cover shoots. A copy of the saves as they were is downloaded first. Everything else in the zip
 * (undrgnd2.eu's settings.ini, registry.txt, screenshots) is left out.
 */
final class SaveBackup {
    static final String PROFILES_DIR = "NFS Underground 2";
    private static final int PROFILE_SIZE = 54966, NAME_AT = 36 + 53761;
    private static final long ENTRY_MAX = 16L << 20, TOTAL_MAX = 256L << 20;
    private static final String[] SIDES = {"GARAGE.DAT", "MODPARTS.DAT"};

    private SaveBackup() {}

    static File savesDir(File gameRoot) {
        File dir = new File(new File(new File(gameRoot, "AppData"), "Local"), PROFILES_DIR);
        File local = new File(new File(gameRoot, "AppData"), "Local");
        File[] in = local.listFiles();
        if (in != null) for (File f : in) if (f.isDirectory() && f.getName().equalsIgnoreCase(PROFILES_DIR)) return f;
        return dir;
    }

    /** the profiles here: each folder whose own file is a profile */
    static List<String> profiles(File gameRoot) {
        List<String> out = new ArrayList<>();
        File[] dirs = savesDir(gameRoot).listFiles();
        if (dirs == null) return out;
        for (File d : dirs) {
            File own = new File(d, d.getName());
            if (d.isDirectory() && own.isFile() && own.length() == PROFILE_SIZE) out.add(d.getName());
        }
        java.util.Collections.sort(out, String.CASE_INSENSITIVE_ORDER);
        return out;
    }

    /** the save folder zipped into Téléchargements/UNDRGND2; its place for the player, or null with nothing to save */
    static String export(Context context, File gameRoot, String prefix) throws IOException {
        File dir = savesDir(gameRoot);
        List<File> files = new ArrayList<>();
        collect(dir, files);
        if (files.isEmpty()) return null;
        String name = prefix + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date()) + ".zip";
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        values.put(MediaStore.MediaColumns.MIME_TYPE, "application/zip");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/UNDRGND2");
        ContentResolver resolver = context.getContentResolver();
        Uri target = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (target == null) throw new IOException("impossible de créer le fichier dans Téléchargements");
        String base = dir.getAbsolutePath();
        try (OutputStream out = resolver.openOutputStream(target, "w")) {
            if (out == null) throw new IOException("impossible d'écrire dans Téléchargements");
            try (ZipOutputStream zip = new ZipOutputStream(out)) {
                byte[] buffer = new byte[65536];
                for (File f : files) {
                    String rel = f.getAbsolutePath().substring(base.length() + 1).replace(File.separatorChar, '/');
                    ZipEntry e = new ZipEntry(PROFILES_DIR + "/" + rel);
                    e.setTime(f.lastModified());
                    zip.putNextEntry(e);
                    try (InputStream in = new FileInputStream(f)) { for (int n; (n = in.read(buffer)) != -1;) zip.write(buffer, 0, n); }
                    zip.closeEntry();
                }
            }
        } catch (IOException error) {
            resolver.delete(target, null, null);
            throw error;
        }
        return "Téléchargements/UNDRGND2/" + name;
    }
    private static void collect(File dir, List<File> out) {
        File[] in = dir.listFiles();
        if (in == null) return;
        for (File f : in) {
            if (f.isDirectory()) collect(f, out);
            else if (f.isFile() && !f.getName().equals("autotest-script.txt")) out.add(f);
        }
    }

    /** What a restore found: the profiles put in, and what it left out. */
    static final class Result { final List<String> profiles = new ArrayList<>(); int skipped; }

    /** a zip (or a profile file alone) restored into the save folder; the profiles of the same names here replaced */
    static Result restore(Context context, Uri source, File gameRoot) throws IOException {
        Map<String, byte[]> files = read(context, source);
        Result r = new Result();
        File dir = savesDir(gameRoot);
        // the profiles, by what they hold
        Map<String, String> profileDirOf = new LinkedHashMap<>();   // zip folder (lower case) -> profile name
        Map<String, byte[]> found = new LinkedHashMap<>();
        for (Map.Entry<String, byte[]> e : files.entrySet()) {
            String name = profileName(e.getValue());
            if (name == null) continue;
            if (found.containsKey(name.toLowerCase(Locale.ROOT))) { r.skipped++; continue; }
            found.put(name.toLowerCase(Locale.ROOT), e.getValue());
            String path = e.getKey(), folder = path.contains("/") ? path.substring(0, path.lastIndexOf('/')).toLowerCase(Locale.ROOT) : null;
            if (folder != null) profileDirOf.put(folder, name);
            r.profiles.add(name);
        }
        if (r.profiles.isEmpty()) return r;
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("impossible de créer " + dir);
        for (String name : r.profiles) write(new File(new File(dir, name), name), found.get(name.toLowerCase(Locale.ROOT)));
        for (Map.Entry<String, byte[]> e : files.entrySet()) {
            String path = e.getKey(), file = path.substring(path.lastIndexOf('/') + 1);
            String folder = path.contains("/") ? path.substring(0, path.lastIndexOf('/')) : "";
            String folderName = folder.substring(folder.lastIndexOf('/') + 1);
            if (profileName(e.getValue()) != null) continue;
            String owner = profileDirOf.get(folder.toLowerCase(Locale.ROOT));
            boolean side = false;
            for (String s : SIDES) if (s.equalsIgnoreCase(file)) side = true;
            if (side && owner != null) { write(new File(new File(dir, owner), file.toUpperCase(Locale.ROOT)), e.getValue()); continue; }
            // a cover shoot: "<NAME> <shoot>/<NAME> <shoot>", of a profile restored
            String coverOf = null;
            if (file.equalsIgnoreCase(folderName))
                for (String name : r.profiles) if (folderName.toLowerCase(Locale.ROOT).startsWith(name.toLowerCase(Locale.ROOT) + " ")) coverOf = name;
            if (coverOf != null) { String to = coverOf + folderName.substring(coverOf.length()); write(new File(new File(dir, to), to), e.getValue()); continue; }
            r.skipped++;
        }
        return r;
    }

    /** a profile's name, from what it holds: 54,966 bytes, "20CM", 1 to 15 letters, digits, spaces, - and _ at its payload's
     *  name; null for anything else */
    static String profileName(byte[] data) {
        if (data == null || data.length != PROFILE_SIZE || data[0] != '2' || data[1] != '0' || data[2] != 'C' || data[3] != 'M') return null;
        int n = 0;
        while (n < 32 && data[NAME_AT + n] != 0) n++;
        String name = new String(data, NAME_AT, n, StandardCharsets.ISO_8859_1);
        return name.matches("[A-Za-z0-9 _\\-]{1,15}") && !name.startsWith(" ") && !name.endsWith(" ") ? name : null;
    }

    private static Map<String, byte[]> read(Context context, Uri source) throws IOException {
        Map<String, byte[]> out = new LinkedHashMap<>();
        byte[] head;
        try (InputStream in = context.getContentResolver().openInputStream(source)) {
            if (in == null) throw new IOException("fichier illisible");
            head = readAll(in, TOTAL_MAX);
        }
        if (head.length >= 4 && head[0] == 'P' && head[1] == 'K') {
            long total = 0;
            try (ZipInputStream zip = new ZipInputStream(new java.io.ByteArrayInputStream(head))) {
                for (ZipEntry e; (e = zip.getNextEntry()) != null;) {
                    if (e.isDirectory()) continue;
                    String name = e.getName().replace('\\', '/');
                    if (name.contains("../") || name.startsWith("/")) continue;
                    byte[] data = readAll(zip, ENTRY_MAX);
                    total += data.length;
                    if (total > TOTAL_MAX) throw new IOException("archive trop grande");
                    out.put(name, data);
                }
            }
        } else out.put("profile", head);
        return out;
    }
    private static byte[] readAll(InputStream in, long max) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        for (int n; (n = in.read(buffer)) != -1;) {
            out.write(buffer, 0, n);
            if (out.size() > max) throw new IOException("fichier trop grand");
        }
        return out.toByteArray();
    }
    private static void write(File file, byte[] data) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("impossible de créer " + parent);
        File tmp = new File(file.getPath() + ".tmp");
        try (OutputStream out = new FileOutputStream(tmp)) { out.write(data); }
        if (file.exists() && !file.delete()) { tmp.delete(); throw new IOException("impossible de remplacer " + file.getName()); }
        if (!tmp.renameTo(file)) throw new IOException("impossible d'écrire " + file.getName());
    }
}
