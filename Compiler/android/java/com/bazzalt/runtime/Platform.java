package com.bazzalt.runtime;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import android.util.Base64;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyStore;
import java.util.ArrayList;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Private platform services. Gameplay calls the engine's public APIs. */
public final class Platform {
    private static Context context;
    private static File content;
    private Platform() {}

    public static synchronized void Initialize(Context app, File directory) {
        context = app.getApplicationContext();
        content = directory;
    }

    public static String Utf8(byte[] bytes) { return new String(bytes, StandardCharsets.UTF_8); }
    public static byte[] Bytes(String text) { return text.getBytes(StandardCharsets.UTF_8); }
    public static String ContentDirectory() { return content.getAbsolutePath(); }
    public static void ShowIntro(boolean enabled, int size, boolean allowSkip) { GameActivity.ShowIntro(enabled, size, allowSkip); }
    public static boolean IntroSkipped() { return GameActivity.IntroSkipped(); }
    public static void FinishIntro() { GameActivity.FinishIntro(); }
    public static String NativeLibrary(String name) {
        if (!name.matches("lib[A-Za-z0-9_.-]+\\.so")) throw new IllegalArgumentException("Invalid native library name");
        File library = new File(context.getApplicationInfo().nativeLibraryDir, name);
        if (!library.isFile()) throw new IllegalStateException("Native library is missing: " + name);
        return library.getAbsolutePath();
    }

    // Keystore keys are non-exportable. Wrapped content keys live in no-backup files, never preferences.
    public static synchronized byte[] ProtectKey(String id, byte[] seed) throws Exception {
        if (!id.matches("[a-f0-9]{32}") || seed.length != 32) throw new IllegalArgumentException("Invalid key provisioning request");
        String alias = "Bazzalt.Content." + id;
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        SecretKey key = (SecretKey)store.getKey(alias, null);
        File directory = new File(context.getNoBackupFilesDir(), "keys");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("Cannot create key directory");
        AtomicFile protectedFile = new AtomicFile(new File(directory, id + ".key"));
        if (key == null) {
            if (protectedFile.getBaseFile().exists()) throw new IllegalStateException("Content wrapping key is unavailable");
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
            key = generator.generateKey();
        }
        byte[] aad = Bytes(context.getPackageName() + ":" + id);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        if (!protectedFile.getBaseFile().exists()) {
            cipher.init(Cipher.ENCRYPT_MODE, key);
            cipher.updateAAD(aad);
            byte[] encrypted = cipher.doFinal(seed);
            byte[] nonce = cipher.getIV();
            if (nonce.length != 12) throw new IllegalStateException("Unsupported Keystore nonce");
            FileOutputStream output = null;
            try {
                output = protectedFile.startWrite();
                output.write(nonce);
                output.write(encrypted);
                protectedFile.finishWrite(output);
            } catch (Exception error) {
                if (output != null) protectedFile.failWrite(output);
                throw error;
            } finally { Arrays.fill(encrypted, (byte)0); }
        }
        byte[] blob = protectedFile.readFully();
        if (blob.length != 60) throw new IllegalStateException("Invalid protected content key");
        cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(128, blob, 0, 12));
        cipher.updateAAD(aad);
        return cipher.doFinal(blob, 12, blob.length - 12);
    }

    private static SharedPreferences Saves() { return context.getSharedPreferences("Bazzalt.SaveGame", Context.MODE_PRIVATE); }
    private static String SaveKey(String path) {
        // Relative app-private paths remain stable across updates and exclude other apps' files.
        String root = context.getFilesDir().getParentFile().getAbsolutePath() + "/";
        if (!path.startsWith(root) || path.contains("/../")) throw new IllegalArgumentException("Invalid save path");
        return path.substring(root.length());
    }
    public static synchronized byte[] ReadSave(String path) {
        String encoded = Saves().getString(SaveKey(path), null);
        if (encoded == null) return null;
        if (encoded.length() > 22400000) throw new IllegalStateException("Save slot exceeds its size limit");
        return Base64.decode(encoded, Base64.NO_WRAP);
    }
    public static synchronized void WriteSave(String path, byte[] data) {
        if (data.length > 16781312) throw new IllegalArgumentException("Save slot exceeds its size limit");
        if (!Saves().edit().putString(SaveKey(path), Base64.encodeToString(data, Base64.NO_WRAP)).commit())
            throw new IllegalStateException("Could not commit save slot");
    }
    public static synchronized boolean RemoveSave(String path) {
        String key = SaveKey(path);
        boolean existed = Saves().contains(key);
        if (!Saves().edit().remove(key).commit()) throw new IllegalStateException("Could not remove save slot");
        return existed;
    }
    public static synchronized String[] SaveFiles(String directory) {
        String prefix = SaveKey(directory + "/");
        ArrayList<String> names = new ArrayList<>();
        for (String key : Saves().getAll().keySet()) {
            if (key.startsWith(prefix) && key.indexOf('/', prefix.length()) == -1) names.add(key.substring(prefix.length()));
        }
        return names.toArray(new String[0]);
    }
}
