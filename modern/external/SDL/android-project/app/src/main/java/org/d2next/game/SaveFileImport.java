package org.d2next.game;

import android.system.ErrnoException;
import android.system.Os;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

// Private files only: staged beside the destination, so rename is on one filesystem.
final class SaveFileImport {
    private static int readChunk(InputStream input, byte[] buffer) throws IOException {
        int total = 0;
        while (total < buffer.length) {
            int n = input.read(buffer, total, buffer.length - total);
            if (n < 0) break;
            if (n == 0) {
                int next = input.read();
                if (next < 0) break;
                buffer[total++] = (byte) next;
            } else total += n;
        }
        return total;
    }

    static boolean sameContent(InputStream a, InputStream b) throws IOException {
        byte[] x = new byte[64 * 1024], y = new byte[64 * 1024];
        for (;;) {
            int n = readChunk(a, x), m = readChunk(b, y);
            if (n != m) return false;
            for (int i = 0; i < n; ++i) if (x[i] != y[i]) return false;
            if (n == 0) return true;
        }
    }

    static boolean finish(File staged, File destination, long expectedSize, boolean character) throws IOException {
        if (!staged.isFile()) {
            throw new IOException("Missing imported file " + staged.getName());
        }
        long actualSize = staged.length();
        if (expectedSize >= 0 && actualSize != expectedSize) {
            throw new IOException("Incomplete import of " + destination.getName());
        }
        // Some document providers do not report a size. Check the bytes actually copied.
        if (character && actualSize == 0) {
            if (!staged.delete()) {
                throw new IOException("Cannot discard empty import " + staged.getName());
            }
            return false;
        }
        try {
            // Android's rename(2) replaces an existing file without a delete-before-rename gap.
            // A failed rename leaves the destination in place.
            Os.rename(staged.getPath(), destination.getPath());
        } catch (ErrnoException error) {
            throw new IOException("Cannot save " + destination.getName(), error);
        }
        return true;
    }

    private SaveFileImport() {}
}
