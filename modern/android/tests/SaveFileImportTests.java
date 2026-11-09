package org.d2next.game;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.ByteArrayInputStream;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;

public final class SaveFileImportTests {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static void write(File file, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) { out.write(text.getBytes(StandardCharsets.UTF_8)); }
    }
    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }
    public static void main(String[] args) throws Exception {
        byte[] bytes = new byte[131079];
        for (int i = 0; i < bytes.length; ++i) bytes[i] = (byte) i;
        ByteArrayInputStream shortReads = new ByteArrayInputStream(bytes) {
            @Override public synchronized int read(byte[] b, int off, int len) {
                return super.read(b, off, Math.min(7, len));
            }
        };
        require(SaveFileImport.sameContent(shortReads, new ByteArrayInputStream(bytes)), "short read boundaries changed equality");
        byte[] changed = bytes.clone(); changed[changed.length - 1] ^= 1;
        require(!SaveFileImport.sameContent(new ByteArrayInputStream(bytes), new ByteArrayInputStream(changed)), "same-length changed tail was skipped");
        require(!SaveFileImport.sameContent(new ByteArrayInputStream(bytes), new ByteArrayInputStream(new byte[0])), "different lengths equal");
        require(SaveFileImport.sameContent(new ByteArrayInputStream(new byte[0]), new ByteArrayInputStream(new byte[0])), "empty streams differ");
        require(args.length == 1, "NEW_FIXTURE_DIR required");
        File root = new File(args[0]);
        require(!root.exists() && root.mkdirs(), "fixture must be new");
        File destination = new File(root, "hero.d2s"), staged = new File(root, "hero.d2s.importing");
        write(destination, "old-save"); write(staged, "");
        require(!SaveFileImport.finish(staged, destination, -1, true), "unknown-size empty character was accepted");
        require(read(destination).equals("old-save") && !staged.exists(), "empty import damaged old save");
        write(staged, "cut");
        boolean rejected = false;
        try { SaveFileImport.finish(staged, destination, 10, true); } catch (IOException expected) { rejected = true; }
        require(rejected && read(destination).equals("old-save"), "short transfer replaced old save");
        write(staged, "new-save");
        require(SaveFileImport.finish(staged, destination, 8, true), "valid staged copy rejected");
        require(read(destination).equals("new-save") && !staged.exists(), "replacement failed");
        write(staged, "unknown-length");
        require(SaveFileImport.finish(staged, destination, -1, true) && read(destination).equals("unknown-length"), "nonempty unknown-size copy rejected");
        write(staged, "");
        require(!SaveFileImport.finish(staged, destination, 0, true) && read(destination).equals("unknown-length"), "known empty copy damaged save");
        rejected = false;
        try { SaveFileImport.finish(staged, destination, -1, true); } catch (IOException expected) { rejected = true; }
        require(rejected && read(destination).equals("unknown-length"), "missing stage damaged save");
        File directory = new File(root, "destination-directory"); require(directory.mkdir(), "directory fixture failed");
        File kept = new File(directory, "kept"); write(kept, "retained"); write(staged, "future");
        rejected = false;
        try { SaveFileImport.finish(staged, directory, 6, false); } catch (IOException expected) { rejected = true; }
        require(rejected && read(kept).equals("retained") && staged.exists(), "rename failure destroyed old destination");
        File look = new File(root, "hero.look"); write(look, "appearance"); write(staged, "");
        require(SaveFileImport.finish(staged, look, 0, false) && look.length() == 0, "non-character empty file policy changed");
        System.out.println("PASS: content equality with short reads, same-length changed tail, unequal lengths, empty streams; empty character, short transfer, atomic replacement, unknown-size copy, missing stage and rename failure preservation");
    }
}
