package com.bazzalt.runtime;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.graphics.Color;
import android.widget.ProgressBar;
import android.widget.LinearLayout;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Installs signed content into private storage before SDL creates its rendering surface. */
public final class BootstrapActivity extends Activity {
    private volatile boolean cancelled;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(android.view.Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        TextView label = new TextView(this);
        label.setText(getApplicationInfo().loadLabel(getPackageManager()));
        label.setTextColor(Color.WHITE);
        layout.addView(label);
        layout.addView(new ProgressBar(this));
        setContentView(layout);
        new Thread(() -> {
            try {
                File directory = Prepare();
                if (cancelled) return;
                Platform.Initialize(this, directory);
                runOnUiThread(() -> {
                    if (!cancelled) {
                        startActivity(new Intent(this, GameActivity.class).putExtra("content", directory.getName()));
                        finish();
                    }
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (!cancelled) new AlertDialog.Builder(this).setTitle("Game could not start")
                        .setMessage(error.getMessage()).setPositiveButton(android.R.string.ok, (dialog, which) -> finish()).show();
                });
            }
        }, "Bazzalt content loader").start();
    }

    private File Prepare() throws Exception {
        JSONObject delivery;
        try (InputStream input = getAssets().open("delivery.json")) {
            delivery = new JSONObject(new String(ReadBounded(input, 8388608), java.nio.charset.StandardCharsets.UTF_8));
        }
        String id = delivery.getString("id");
        if (!id.matches("[a-f0-9]{32}")) throw new IllegalStateException("Invalid content identity");
        File directory = new File(getNoBackupFilesDir(), "content/" + id);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("Could not create game content directory");
        String mode = delivery.getString("mode");
        ZipFile archive = null;
        if (mode.equals("obb") || mode.equals("external_data")) {
            String name = delivery.getString("archive");
            if (!new File(name).getName().equals(name)) throw new IllegalStateException("Invalid content archive name");
            File parent = mode.equals("obb") ? getObbDir() : getExternalFilesDir(null);
            if (parent == null) throw new IllegalStateException("External game storage is unavailable");
            File file = new File(parent, name);
            if (!file.isFile()) throw new IllegalStateException("Install the game's content file in " + parent.getAbsolutePath());
            archive = new ZipFile(file);
        } else if (!mode.equals("apk") && !mode.equals("split_apk")) throw new IllegalStateException("Unsupported content delivery");
        try {
            JSONArray entries = delivery.getJSONArray("files");
            if (entries.length() > 100000) throw new IllegalStateException("Content inventory exceeds limits");
            for (int i = 0; i < entries.length(); ++i) {
                if (cancelled) throw new InterruptedException("Game loading cancelled");
                JSONObject entry = entries.getJSONObject(i);
                String name = entry.getString("path");
                if (name.startsWith("/") || name.contains("\\") || name.contains(":") || name.contains("//"))
                    throw new IllegalStateException("Unsafe content path");
                File target = new File(directory, name);
                if (!target.getCanonicalPath().startsWith(directory.getCanonicalPath() + "/")) throw new IllegalStateException("Content path traversal");
                long length = entry.getLong("size");
                String hash = entry.getString("sha256");
                if (length < 0 || length > 4294967296L || !hash.matches("[a-f0-9]{64}")) throw new IllegalStateException("Invalid content inventory");
                if (target.isFile() && target.length() == length && Digest(target).equals(hash)) continue;
                File parent = target.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IllegalStateException("Could not create content directory");
                File temporary = File.createTempFile("install-", ".tmp", parent);
                try {
                    InputStream source;
                    if (archive == null) source = getAssets().open("bazzalt/" + name);
                    else {
                        ZipEntry zipped = archive.getEntry(name);
                        if (zipped == null || zipped.getSize() != length) throw new IllegalStateException("Missing content entry " + name);
                        source = archive.getInputStream(zipped);
                    }
                    try (InputStream input = source; FileOutputStream output = new FileOutputStream(temporary)) {
                        byte[] buffer = new byte[65536];
                        long total = 0;
                        int count;
                        while ((count = input.read(buffer)) != -1) {
                            total += count;
                            if (total > length || cancelled) throw new IllegalStateException("Invalid content length");
                            output.write(buffer, 0, count);
                        }
                        output.getFD().sync();
                        if (total != length) throw new IllegalStateException("Truncated game content");
                    }
                    if (!Digest(temporary).equals(hash)) throw new IllegalStateException("Game content verification failed");
                    Files.move(temporary.toPath(), target.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                        java.nio.file.StandardCopyOption.ATOMIC_MOVE);
                } finally { temporary.delete(); }
            }
        } finally { if (archive != null) archive.close(); }
        return directory;
    }
    private static byte[] ReadBounded(InputStream input, int limit) throws Exception {
        java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (output.size() + count > limit) throw new IllegalStateException("Content manifest exceeds limits");
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }
    private static String Digest(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder hex = new StringBuilder();
        for (byte value : digest.digest()) hex.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
        return hex.toString();
    }
    @Override public void onDestroy() { cancelled = true; super.onDestroy(); }
}
