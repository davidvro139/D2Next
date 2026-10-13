package org.d2next.game;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.system.Os;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public final class D2NextActivity extends SDLActivity {
    private static final int REQUEST_STORAGE_TREE = 0xD212;
    private static final int REQUEST_SAVE_TREE = 0xD213;
    private static final String PREFS = "d2next_storage";
    private static final String TREE_URI = "tree_uri";
    private static final String SAVE_URI = "save_uri";
    private static final String VIEW_ZOOM = "view_zoom";
    private static final String CHEATS = "cheats";
    private static final String[] ZOOM_LABELS = {"1X", "1.25X", "1.5X", "2X", "2.5X", "3X"};
    private static final int COLOR_BG = 0xFF120C0A;
    private static final int COLOR_CARD = 0xFF1C1612;
    private static final int COLOR_GOLD = 0xFFC7B377;
    private static final int COLOR_TEXT = 0xFFE8DCC0;
    private static final int COLOR_MUTED = 0xFF9A8F7A;
    private static final int COLOR_READY = 0xFF6BBF7A;
    private static final int COLOR_MISSING = 0xFFFF6B4A;
    private static final Set<String> MPQ_FILES = new HashSet<>(Arrays.asList(
        "d2data.mpq", "d2exp.mpq", "d2char.mpq", "d2sfx.mpq", "patch_d2.mpq",
        "d2speech.mpq", "d2xtalk.mpq", "d2music.mpq", "d2xmusic.mpq",
        "d2video.mpq", "d2xvideo.mpq"));
    private static final Set<String> SAVE_FILES = new HashSet<>(Arrays.asList(
        "settings.ini", "lasttcpip.txt"));
    private static final String[] REQUIRED_MPQS = {
        "d2data.mpq", "d2exp.mpq", "d2char.mpq", "d2sfx.mpq", "patch_d2.mpq"};
    private static final String ROOT_README =
        "D2Next game folder\n"
        + "==================\n\n"
        + "Copy your legally owned Diablo II 1.10f / Lord of Destruction files here.\n\n"
        + "  MPQ/     Put the game archives in this folder (see MPQ/README.txt)\n"
        + "  Save/    Optional: copy .d2s character files here (see Save/README.txt)\n"
        + "  Logs/    D2Next writes logs here after you play\n\n"
        + "The usual place for this folder is:\n"
        + "  Internal storage/Documents/D2Next\n\n"
        + "Leave the archives in this folder. D2Next opens them from here and does not\n"
        + "copy them. After you copy the MPQ files, return to D2Next and tap Launch.\n";
    private static final String MPQ_README =
        "Put your legally owned Diablo II 1.10f / Lord of Destruction MPQ files in this folder.\n\n"
        + "Required:\n"
        + "  d2data.mpq\n"
        + "  d2exp.mpq\n"
        + "  d2char.mpq\n"
        + "  d2sfx.mpq\n"
        + "  Patch_D2.mpq\n\n"
        + "Optional:\n"
        + "  d2speech.mpq\n"
        + "  d2xtalk.mpq\n"
        + "  d2music.mpq\n"
        + "  d2xmusic.mpq\n"
        + "  d2video.mpq\n"
        + "  d2xvideo.mpq\n\n"
        + "Letter case does not matter (d2data.mpq and D2DATA.MPQ are the same).\n"
        + "Leave the files in this folder. After you copy them, return to D2Next and tap\n"
        + "Launch.\n"
        + "Do not include these archives when sharing the D2Next project.\n";
    private static final String SAVE_README =
        "Player files are stored in this folder.\n\n"
        + "To import an existing Diablo II 1.10f character, copy its .d2s file here.\n"
        + "The character will appear on the character-selection screen and progress will\n"
        + "be saved back to this copy. Keep a backup of your original character.\n\n"
        + "These player files are local data and should not be included when distributing\n"
        + "the project.\n";
    private static final String LOGS_README =
        "D2Next writes diagnostic logs here after you play.\n"
        + "d2next.log is the current session; d2next.prev.log is the previous one.\n"
        + "You do not need to put anything in this folder.\n";

    private String privateRoot;
    private volatile boolean importedThisProcess;
    private volatile boolean syncing;
    private ScheduledExecutorService folderSyncExecutor;
    private final List<ParcelFileDescriptor> mpqFds = new ArrayList<>();
    private View launcherView, checklistPage, settingsPage;
    private TextView subtitleView, mpqPathView, mpqStatusView, savePathView, saveStatusView, zoomValueView;
    private CheckBox folderCheck, saveCheck, zoomCheck, cheatCheck, cheatSettingCheck;
    private CheckBox[] mpqChecks;
    private Button launchButton, changeButton;
    private boolean mpqsReady, showingSettings, launcherCheats;
    private int launcherZoom = 2;

    public void requestLauncher(String privateDirectory) {
        privateRoot = privateDirectory;
        runOnUiThread(this::showLauncher);
    }

    public void syncStorageFolder() {
        runOnUiThread(() -> {
            if (syncing) {
                nativeFolderSyncFinished(false, "A D2Next folder sync is already running.");
                return;
            }
            Uri tree = storedTree();
            if (tree == null) {
                nativeFolderSyncFinished(false, "The D2Next shared folder has not been selected.");
                return;
            }
            syncing = true;
            new Thread(() -> {
                try {
                    exportPrivateFiles(tree, new File(privateRoot));
                    runOnUiThread(() -> Toast.makeText(this, "D2Next saves and logs copied to the selected folder.",
                        Toast.LENGTH_LONG).show());
                    nativeFolderSyncFinished(true, "D2Next files synchronized.");
                } catch (Exception e) {
                    runOnUiThread(() -> Toast.makeText(this,
                        "Could not copy saves or logs to the selected D2Next folder: " + e.getMessage(),
                        Toast.LENGTH_LONG).show());
                    nativeFolderSyncFinished(false, "Could not copy D2Next files to the selected folder: " + e.getMessage());
                } finally {
                    syncing = false;
                }
            }, "D2Next folder export").start();
        });
    }

    // OPTIONS: drop the saved folder so the next launch asks again. Archives already open stay open this session.
    public void forgetSharedGameFolder() {
        runOnUiThread(() -> {
            forgetStoredTree();
            Toast.makeText(this, "Game folder cleared. Choose a new folder the next time you open D2Next.",
                Toast.LENGTH_LONG).show();
        });
    }

    private void showLauncher() {
        launcherZoom = loadLauncherZoom();
        launcherCheats = loadLauncherCheats();
        if (launcherView == null) {
            launcherView = buildLauncher();
            RelativeLayout.LayoutParams fill = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            mLayout.addView(launcherView, fill);
        }
        launcherView.setVisibility(View.VISIBLE);
        launcherView.bringToFront();
        launcherView.requestApplyInsets();
        showChecklist();
        updateZoomLabel();
        setLauncherCheats(launcherCheats);
        refreshLauncher();
        if (storedTree() == null) {
            openTreePicker(REQUEST_STORAGE_TREE);
        }
    }

    private void hideLauncher() {
        if (launcherView != null) {
            launcherView.setVisibility(View.GONE);
        }
    }

    private void quitLauncher() {
        hideLauncher();
        nativeFolderSyncFinished(false, "Folder access was cancelled.");
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (launcherView != null && launcherView.getVisibility() == View.VISIBLE
            && event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP) {
                if (showingSettings) {
                    showChecklist();
                } else {
                    quitLauncher();
                }
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private View buildLauncher() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(COLOR_BG);
        root.setClickable(true);
        applyLauncherInsets(root);

        TextView title = new TextView(this);
        title.setText("D2NEXT");
        title.setTextColor(COLOR_GOLD);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        title.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        title.setGravity(Gravity.CENTER);
        title.setIncludeFontPadding(true);
        title.setPadding(0, dp(8), 0, dp(4));
        root.addView(title, matchWrap());

        subtitleView = new TextView(this);
        subtitleView.setTextColor(COLOR_MUTED);
        subtitleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        subtitleView.setGravity(Gravity.CENTER);
        subtitleView.setPadding(0, dp(4), 0, dp(12));
        root.addView(subtitleView, matchWrap());

        ScrollView scroll = new ScrollView(this);
        LinearLayout pages = new LinearLayout(this);
        pages.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(pages, matchWrap());
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1);
        root.addView(scroll, scrollParams);

        checklistPage = buildChecklist();
        pages.addView(checklistPage, matchWrap());
        settingsPage = buildSettings();
        pages.addView(settingsPage, matchWrap());

        changeButton = smallButton("CHANGE", v -> {
            if (showingSettings) {
                showChecklist();
            } else {
                showSettings();
            }
        });
        LinearLayout.LayoutParams changeParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(44));
        changeParams.topMargin = dp(8);
        root.addView(changeButton, changeParams);

        launchButton = new Button(this);
        launchButton.setText("LAUNCH");
        launchButton.setTextColor(COLOR_BG);
        launchButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        launchButton.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        launchButton.setAllCaps(true);
        GradientDrawable launchBg = new GradientDrawable();
        launchBg.setColor(COLOR_GOLD);
        launchBg.setCornerRadius(dp(8));
        launchButton.setBackground(launchBg);
        launchButton.setEnabled(false);
        launchButton.setAlpha(0.45f);
        launchButton.setOnClickListener(v -> completeLaunch());
        LinearLayout.LayoutParams launchParams = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(64));
        launchParams.topMargin = dp(8);
        root.addView(launchButton, launchParams);

        TextView quit = new TextView(this);
        quit.setText("QUIT");
        quit.setTextColor(COLOR_MUTED);
        quit.setGravity(Gravity.CENTER);
        quit.setPadding(0, dp(10), 0, dp(4));
        quit.setClickable(true);
        quit.setOnClickListener(v -> quitLauncher());
        root.addView(quit, matchWrap());
        return root;
    }

    private View buildChecklist() {
        LinearLayout card = card();
        folderCheck = checkItem("Game files — No folder selected");
        card.addView(folderCheck);
        mpqChecks = new CheckBox[REQUIRED_MPQS.length];
        for (int i = 0; i < REQUIRED_MPQS.length; i++) {
            String name = REQUIRED_MPQS[i].equals("patch_d2.mpq") ? "Patch_D2.mpq" : REQUIRED_MPQS[i];
            mpqChecks[i] = checkItem(name);
            mpqChecks[i].setPadding(dp(28), dp(2), 0, dp(2));
            card.addView(mpqChecks[i]);
        }
        saveCheck = checkItem("Saves — Not set");
        card.addView(saveCheck);
        zoomCheck = checkItem("View zoom — " + ZOOM_LABELS[launcherZoom]);
        zoomCheck.setChecked(true);
        card.addView(zoomCheck);
        cheatCheck = cheatToggle("Cheats — off");
        card.addView(cheatCheck);
        return card;
    }

    private View buildSettings() {
        LinearLayout settings = new LinearLayout(this);
        settings.setOrientation(LinearLayout.VERTICAL);

        settings.addView(sectionLabel("GAME FILES"));
        LinearLayout mpqCard = card();
        mpqPathView = bodyText("No folder selected");
        mpqStatusView = bodyText("Choose a folder that holds your Diablo II MPQ archives.");
        mpqCard.addView(mpqPathView);
        mpqCard.addView(mpqStatusView);
        LinearLayout mpqButtons = new LinearLayout(this);
        mpqButtons.setOrientation(LinearLayout.HORIZONTAL);
        mpqButtons.addView(smallButton("CHANGE FOLDER", v -> openTreePicker(REQUEST_STORAGE_TREE)), buttonSlot());
        mpqButtons.addView(smallButton("REFRESH", v -> refreshLauncher()), buttonSlot());
        mpqCard.addView(mpqButtons);
        settings.addView(mpqCard);

        settings.addView(sectionLabel("SAVES"));
        LinearLayout saveCard = card();
        savePathView = bodyText("Same as the game folder / Save");
        saveStatusView = bodyText("Characters are stored here.");
        saveCard.addView(savePathView);
        saveCard.addView(saveStatusView);
        LinearLayout saveButtons = new LinearLayout(this);
        saveButtons.setOrientation(LinearLayout.HORIZONTAL);
        saveButtons.addView(smallButton("CHANGE FOLDER", v -> openTreePicker(REQUEST_SAVE_TREE)), buttonSlot());
        saveButtons.addView(smallButton("USE GAME FOLDER", v -> {
            forgetStoredSaveTree();
            refreshLauncher();
        }), buttonSlot());
        saveCard.addView(saveButtons);
        settings.addView(saveCard);

        settings.addView(sectionLabel("VIEW ZOOM"));
        LinearLayout zoomCard = card();
        TextView zoomHint = bodyText("How large the world is drawn. The interface stays the same size.");
        zoomCard.addView(zoomHint);
        LinearLayout zoomRow = new LinearLayout(this);
        zoomRow.setOrientation(LinearLayout.HORIZONTAL);
        zoomRow.setGravity(Gravity.CENTER_VERTICAL);
        zoomRow.addView(smallButton("−", v -> {
            launcherZoom = Math.max(0, launcherZoom - 1);
            updateZoomLabel();
        }), buttonSlot());
        zoomValueView = bodyText(ZOOM_LABELS[launcherZoom]);
        zoomValueView.setTextColor(COLOR_GOLD);
        zoomValueView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        zoomValueView.setGravity(Gravity.CENTER);
        zoomValueView.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        LinearLayout.LayoutParams zoomValueParams = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        zoomRow.addView(zoomValueView, zoomValueParams);
        zoomRow.addView(smallButton("+", v -> {
            launcherZoom = Math.min(ZOOM_LABELS.length - 1, launcherZoom + 1);
            updateZoomLabel();
        }), buttonSlot());
        zoomCard.addView(zoomRow);
        settings.addView(zoomCard);

        settings.addView(sectionLabel("CHEATS"));
        LinearLayout cheatCard = card();
        cheatCard.addView(bodyText("When on, the Esc menu has Cheats: gold, scrolls, and other save edits."));
        cheatSettingCheck = cheatToggle("Enable cheats");
        cheatCard.addView(cheatSettingCheck);
        settings.addView(cheatCard);
        return settings;
    }

    private void showChecklist() {
        showingSettings = false;
        if (checklistPage != null) {
            checklistPage.setVisibility(View.VISIBLE);
        }
        if (settingsPage != null) {
            settingsPage.setVisibility(View.GONE);
        }
        if (subtitleView != null) {
            subtitleView.setText("Ready when every required file is checked. Archives are not opened until you launch.");
        }
        if (changeButton != null) {
            changeButton.setText("CHANGE");
        }
    }

    private void showSettings() {
        showingSettings = true;
        if (checklistPage != null) {
            checklistPage.setVisibility(View.GONE);
        }
        if (settingsPage != null) {
            settingsPage.setVisibility(View.VISIBLE);
        }
        if (subtitleView != null) {
            subtitleView.setText("Choose folders and settings, then launch.");
        }
        if (changeButton != null) {
            changeButton.setText("DONE");
        }
    }

    private void updateZoomLabel() {
        String label = ZOOM_LABELS[Math.max(0, Math.min(ZOOM_LABELS.length - 1, launcherZoom))];
        if (zoomValueView != null) {
            zoomValueView.setText(label);
        }
        if (zoomCheck != null) {
            zoomCheck.setChecked(true);
            zoomCheck.setText("View zoom — " + label);
            zoomCheck.setTextColor(COLOR_GOLD);
            if (Build.VERSION.SDK_INT >= 21) {
                zoomCheck.setButtonTintList(ColorStateList.valueOf(COLOR_GOLD));
            }
        }
    }

    private void refreshLauncher() {
        mpqsReady = false;
        if (launchButton != null) {
            launchButton.setEnabled(false);
            launchButton.setAlpha(0.45f);
            launchButton.setText("CHECKING…");
        }
        if (mpqStatusView != null) {
            mpqStatusView.setText("Checking folders…");
            mpqStatusView.setTextColor(COLOR_MUTED);
        }
        new Thread(() -> {
            String mpqPath = "No folder selected";
            String mpqStatus = "Choose a folder. Suggested: Documents / D2Next";
            int mpqColor = COLOR_MISSING;
            boolean ready = false;
            boolean folderOk = false;
            String savePath = "Not set";
            String saveStatus = "Characters will be stored in the game folder's Save directory.";
            boolean[] found = new boolean[REQUIRED_MPQS.length];
            try {
                Uri tree = storedTree();
                if (tree != null) {
                    Uri game = gameFolder(tree);
                    Uri mpqFolder = ensureDirectory(tree, game, "MPQ");
                    Uri defaultSave = ensureDirectory(tree, game, "Save");
                    Uri logsFolder = ensureDirectory(tree, game, "Logs");
                    writeTextIfMissing(tree, game, "README.txt", ROOT_README);
                    writeTextIfMissing(tree, mpqFolder, "README.txt", MPQ_README);
                    writeTextIfMissing(tree, defaultSave, "README.txt", SAVE_README);
                    writeTextIfMissing(tree, logsFolder, "README.txt", LOGS_README);
                    mpqPath = friendlyPath(tree, mpqFolder);
                    folderOk = true;
                    Set<String> names = listedMpqNames(tree, game, mpqFolder);
                    for (int i = 0; i < REQUIRED_MPQS.length; i++) {
                        found[i] = names.contains(REQUIRED_MPQS[i]);
                    }
                    String missing = missingRequiredMpqs(names);
                    if (missing.isEmpty()) {
                        mpqStatus = "Ready — all required archives found.";
                        mpqColor = COLOR_READY;
                        ready = true;
                    } else {
                        mpqStatus = "Missing:" + missing + "\nCopy them into this MPQ folder, then tap Refresh.";
                        mpqColor = COLOR_MISSING;
                    }
                    Uri save = storedSaveTree();
                    if (save != null) {
                        savePath = friendlyPath(save, save);
                        saveStatus = characterCount(save, save) + "  (separate save folder)";
                    } else {
                        savePath = friendlyPath(tree, defaultSave);
                        saveStatus = characterCount(tree, defaultSave) + "  (in the game folder)";
                    }
                }
            } catch (Exception e) {
                mpqStatus = "Could not read the folder: " + e.getMessage();
                mpqColor = COLOR_MISSING;
                folderOk = false;
            }
            final String shownMpqPath = mpqPath;
            final String shownMpqStatus = mpqStatus;
            final int shownMpqColor = mpqColor;
            final boolean shownReady = ready;
            final boolean shownFolder = folderOk;
            final String shownSavePath = savePath;
            final String shownSaveStatus = saveStatus;
            final boolean[] shownFound = found;
            runOnUiThread(() -> {
                mpqsReady = shownReady;
                if (mpqPathView != null) {
                    mpqPathView.setText(shownMpqPath);
                }
                if (mpqStatusView != null) {
                    mpqStatusView.setText(shownMpqStatus);
                    mpqStatusView.setTextColor(shownMpqColor);
                }
                if (savePathView != null) {
                    savePathView.setText(shownSavePath);
                }
                if (saveStatusView != null) {
                    saveStatusView.setText(shownSaveStatus);
                }
                if (folderCheck != null) {
                    folderCheck.setChecked(shownFolder);
                    folderCheck.setText("Game files — " + shownMpqPath);
                    tintCheck(folderCheck, shownFolder);
                }
                if (mpqChecks != null) {
                    for (int i = 0; i < mpqChecks.length; i++) {
                        String name = REQUIRED_MPQS[i].equals("patch_d2.mpq") ? "Patch_D2.mpq" : REQUIRED_MPQS[i];
                        mpqChecks[i].setChecked(shownFound[i]);
                        mpqChecks[i].setText(name);
                        tintCheck(mpqChecks[i], shownFound[i]);
                    }
                }
                if (saveCheck != null) {
                    saveCheck.setChecked(shownFolder);
                    saveCheck.setText("Saves — " + shownSaveStatus);
                    tintCheck(saveCheck, shownFolder);
                }
                updateZoomLabel();
                if (launchButton != null) {
                    launchButton.setEnabled(shownReady);
                    launchButton.setAlpha(shownReady ? 1f : 0.45f);
                    launchButton.setText("LAUNCH");
                }
            });
        }, "D2Next launcher status").start();
    }

    private void completeLaunch() {
        if (syncing) {
            return;
        }
        if (!mpqsReady) {
            Toast.makeText(this, "Copy the required MPQ files into the game folder first.", Toast.LENGTH_LONG).show();
            return;
        }
        Uri tree = storedTree();
        if (tree == null) {
            Toast.makeText(this, "Choose a game files folder first.", Toast.LENGTH_LONG).show();
            return;
        }
        syncing = true;
        launchButton.setEnabled(false);
        launchButton.setAlpha(0.45f);
        launchButton.setText("OPENING…");
        new Thread(() -> {
            String error = null;
            try {
                importFromTree(tree);
                writeViewZoom();
            } catch (Exception e) {
                error = e.getMessage();
            } finally {
                syncing = false;
            }
            final String fail = error;
            runOnUiThread(() -> {
                if (fail == null) {
                    hideLauncher();
                    nativeFolderSyncFinished(true, "D2Next folder synchronized.");
                } else {
                    showSettings();
                    if (mpqStatusView != null) {
                        mpqStatusView.setText("Could not open archives: " + fail);
                        mpqStatusView.setTextColor(COLOR_MISSING);
                    }
                    launchButton.setEnabled(true);
                    launchButton.setAlpha(1f);
                    launchButton.setText("LAUNCH");
                }
            });
        }, "D2Next launch").start();
    }

    private void openTreePicker(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        if (Build.VERSION.SDK_INT >= 26) {
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:Documents"));
        }
        try {
            startActivityForResult(intent, requestCode);
        } catch (ActivityNotFoundException | SecurityException e) {
            Toast.makeText(this, "Android could not open the folder picker: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_STORAGE_TREE && requestCode != REQUEST_SAVE_TREE) {
            return;
        }
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            refreshLauncher();
            return;
        }
        Uri tree = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(tree, flags);
            if (requestCode == REQUEST_SAVE_TREE) {
                preferences().edit().putString(SAVE_URI, tree.toString()).apply();
            } else {
                preferences().edit().putString(TREE_URI, tree.toString()).apply();
            }
        } catch (Exception e) {
            Toast.makeText(this, "Could not keep access to the selected folder: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        refreshLauncher();
    }

    private void importFromTree(Uri tree) throws Exception {
        File root = new File(privateRoot);
        File mpq = new File(root, "MPQ");
        File saves = new File(root, "Save");
        if (!mpq.exists() && !mpq.mkdirs()) {
            throw new IllegalStateException("Cannot create " + mpq);
        }
        if (!saves.exists() && !saves.mkdirs()) {
            throw new IllegalStateException("Cannot create " + saves);
        }
        Uri game = gameFolder(tree);
        Uri mpqFolder = ensureDirectory(tree, game, "MPQ");
        Uri logsFolder = ensureDirectory(tree, game, "Logs");
        writeTextIfMissing(tree, game, "README.txt", ROOT_README);
        writeTextIfMissing(tree, mpqFolder, "README.txt", MPQ_README);
        writeTextIfMissing(tree, logsFolder, "README.txt", LOGS_README);
        closeMpqFds();
        clearPrivateMpqs(mpq);
        linkMpqFiles(tree, game, mpq);
        linkMpqFiles(tree, mpqFolder, mpq);
        if (!missingRequiredMpqs(mpq).isEmpty()) {
            throw new IllegalStateException("Required Diablo II archives are missing from the MPQ folder.");
        }
        Uri save = storedSaveTree();
        if (save != null) {
            copySaveFiles(save, save, saves);
        } else {
            Uri saveFolder = ensureDirectory(tree, game, "Save");
            writeTextIfMissing(tree, saveFolder, "README.txt", SAVE_README);
            copySaveFiles(tree, game, saves);
            copySaveFiles(tree, saveFolder, saves);
        }
        importedThisProcess = true;
        startPeriodicSync(tree);
    }

    private synchronized void startPeriodicSync(Uri tree) {
        if (folderSyncExecutor != null) {
            return;
        }
        folderSyncExecutor = Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "D2Next shared-folder sync");
            thread.setDaemon(true);
            return thread;
        });
        folderSyncExecutor.scheduleAtFixedRate(() -> {
            try {
                exportPrivateFiles(tree, new File(privateRoot));
            } catch (Exception e) {
                Log.e("D2Next", "Could not update saves and logs in the selected D2Next folder", e);
            }
        }, 5, 10, TimeUnit.SECONDS);
    }

    private void linkMpqFiles(Uri tree, Uri source, File destination) throws Exception {
        for (Uri child : children(tree, source)) {
            String name = new File(displayName(tree, child)).getName();
            if (MPQ_FILES.contains(name.toLowerCase(Locale.ROOT))) {
                linkMpqDocument(tree, child, new File(destination, canonicalMpqName(name)));
            }
        }
    }

    private static String canonicalMpqName(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        if (lower.equals("patch_d2.mpq")) {
            return "Patch_D2.mpq";
        }
        return lower;
    }

    // StormLib needs a filesystem path. Keep the SAF descriptor alive and point a private name at /proc/self/fd/N
    // so Fog and the archives open d2data.mpq as usual while the real file stays in the shared folder.
    private void linkMpqDocument(Uri tree, Uri source, File destination) throws Exception {
        if (destination.exists() && !destination.delete()) {
            throw new IllegalStateException("Cannot replace " + destination.getName());
        }
        ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(source, "r");
        if (pfd == null) {
            throw new IllegalStateException("Cannot open " + displayName(tree, source));
        }
        try {
            Os.symlink("/proc/self/fd/" + pfd.getFd(), destination.getAbsolutePath());
            synchronized (mpqFds) {
                mpqFds.add(pfd);
            }
            pfd = null;
        } catch (Exception e) {
            Log.w("D2Next", "Could not use " + destination.getName() + " from the shared folder in place; copying it.", e);
            copyFromDocument(tree, source, destination);
        } finally {
            if (pfd != null) {
                try {
                    pfd.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private void closeMpqFds() {
        synchronized (mpqFds) {
            for (ParcelFileDescriptor fd : mpqFds) {
                try {
                    fd.close();
                } catch (Exception ignored) {
                }
            }
            mpqFds.clear();
        }
    }

    private void clearPrivateMpqs(File folder) {
        File[] files = folder.listFiles();
        if (files == null) {
            return;
        }
        for (File file : files) {
            if (!file.isDirectory()) {
                file.delete();
            }
        }
    }

    private void copySaveFiles(Uri tree, Uri source, File destination) throws Exception {
        for (Uri child : children(tree, source)) {
            String name = new File(displayName(tree, child)).getName();
            String lower = name.toLowerCase(Locale.ROOT);
            if (lower.endsWith(".d2s") || lower.endsWith(".look") || SAVE_FILES.contains(lower)) {
                copyFromDocument(tree, child, new File(destination, name));
            }
        }
    }

    private synchronized void exportPrivateFiles(Uri tree, File root) throws Exception {
        Uri saveTree = storedSaveTree();
        Uri saveParent = saveTree != null ? saveTree : tree;
        Uri sharedSave = saveTree != null ? saveTree : ensureDirectory(tree, gameFolder(tree), "Save");
        File privateSave = new File(root, "Save");
        File[] saves = privateSave.listFiles();
        if (saves != null) {
            for (File file : saves) {
                if (file.isFile()) {
                    String name = file.getName().toLowerCase(Locale.ROOT);
                    if (name.endsWith(".d2s") || name.endsWith(".look") || SAVE_FILES.contains(name)) {
                        copyToDocument(saveParent, sharedSave, file);
                    }
                }
            }
        }
        exportLogs(tree, root);
    }

    private synchronized void exportLogs(Uri tree, File root) throws Exception {
        Uri game = gameFolder(tree);
        File gameLog = new File(root, "d2next.log");
        if (gameLog.isFile()) {
            copyToDocument(tree, game, gameLog);
        }
        File previous = new File(root, "d2next.prev.log");
        if (previous.isFile()) {
            copyToDocument(tree, game, previous);
        }
        File privateLogs = new File(root, "Logs");
        File[] logs = privateLogs.listFiles();
        if (logs != null && logs.length > 0) {
            Uri sharedLogs = ensureDirectory(tree, game, "Logs");
            for (File log : logs) {
                if (log.isFile()) {
                    copyToDocument(tree, sharedLogs, log);
                }
            }
        }
    }

    private void copyFromDocument(Uri tree, Uri source, File destination) throws Exception {
        long size = documentSize(source);
        if (destination.exists() && size >= 0 && destination.length() == size && size > 0) {
            return;
        }
        File temporary = new File(destination.getPath() + ".importing");
        try (InputStream input = getContentResolver().openInputStream(source);
             FileOutputStream output = new FileOutputStream(temporary, false)) {
            if (input == null) {
                throw new IllegalStateException("Cannot open " + displayName(tree, source));
            }
            byte[] buffer = new byte[128 * 1024];
            for (int count; (count = input.read(buffer)) != -1; ) {
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        } catch (Exception e) {
            temporary.delete();
            throw e;
        }
        if (destination.exists() && !destination.delete()) {
            temporary.delete();
            throw new IllegalStateException("Cannot replace " + destination.getName());
        }
        if (!temporary.renameTo(destination)) {
            temporary.delete();
            throw new IllegalStateException("Cannot save " + destination.getName());
        }
    }

    private void copyToDocument(Uri tree, Uri parent, File source) throws Exception {
        Uri existing = findChild(tree, parent, source.getName());
        if (existing != null && documentSize(existing) == source.length() && source.length() > 0) {
            return;
        }
        if (existing != null && !DocumentsContract.deleteDocument(getContentResolver(), existing)) {
            throw new IllegalStateException("Cannot replace " + source.getName());
        }
        Uri target = DocumentsContract.createDocument(getContentResolver(), asDocumentUri(tree, parent), "application/octet-stream",
            source.getName());
        if (target == null) {
            throw new IllegalStateException("Cannot create " + source.getName() + " in the selected folder");
        }
        try (InputStream input = new java.io.FileInputStream(source);
             OutputStream output = getContentResolver().openOutputStream(target, "wt")) {
            if (output == null) {
                throw new IllegalStateException("Cannot write " + source.getName());
            }
            byte[] buffer = new byte[128 * 1024];
            for (int count; (count = input.read(buffer)) != -1; ) {
                output.write(buffer, 0, count);
            }
            output.flush();
        }
    }

    private Uri ensureDirectory(Uri tree, Uri parent, String name) throws Exception {
        Uri existing = findChild(tree, parent, name);
        if (existing != null) {
            return existing;
        }
        Uri created = DocumentsContract.createDocument(getContentResolver(), asDocumentUri(tree, parent),
            DocumentsContract.Document.MIME_TYPE_DIR, name);
        if (created == null) {
            throw new IllegalStateException("Cannot create folder " + name);
        }
        return created;
    }

    // Documents, Download, or the storage root: create/use a D2Next child so students can tap
    // "Use this folder" without making the subfolder themselves.
    private Uri gameFolder(Uri tree) throws Exception {
        String id = DocumentsContract.getTreeDocumentId(tree);
        String path = id.contains(":") ? id.substring(id.indexOf(':') + 1) : id;
        boolean generic = path.isEmpty()
            || path.equalsIgnoreCase("Documents")
            || path.equalsIgnoreCase("Download")
            || path.equalsIgnoreCase("Downloads");
        if (!generic) {
            return tree;
        }
        if (findChild(tree, tree, "MPQ") != null || findChild(tree, tree, "Save") != null) {
            return tree;
        }
        return ensureDirectory(tree, tree, "D2Next");
    }

    private void writeTextIfMissing(Uri tree, Uri parent, String name, String text) throws Exception {
        if (findChild(tree, parent, name) != null) {
            return;
        }
        Uri target = DocumentsContract.createDocument(getContentResolver(), asDocumentUri(tree, parent),
            "text/plain", name);
        if (target == null) {
            throw new IllegalStateException("Cannot create " + name);
        }
        try (OutputStream output = getContentResolver().openOutputStream(target, "wt")) {
            if (output == null) {
                throw new IllegalStateException("Cannot write " + name);
            }
            output.write(text.getBytes(StandardCharsets.UTF_8));
            output.flush();
        }
    }

    private String missingRequiredMpqs(File folder) {
        StringBuilder missing = new StringBuilder();
        File[] files = folder.listFiles();
        for (String name : REQUIRED_MPQS) {
            boolean found = false;
            if (files != null) {
                for (File file : files) {
                    if (file.isFile() && file.getName().equalsIgnoreCase(name)) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                missing.append("\n  ").append(name.equals("patch_d2.mpq") ? "Patch_D2.mpq" : name);
            }
        }
        return missing.toString();
    }

    private String friendlyPath(Uri tree, Uri folder) {
        String id = folder.equals(tree)
            ? DocumentsContract.getTreeDocumentId(tree)
            : DocumentsContract.getDocumentId(folder);
        String volume = "primary";
        String path = id;
        int colon = id.indexOf(':');
        if (colon >= 0) {
            volume = id.substring(0, colon);
            path = id.substring(colon + 1);
        }
        String prefix = volume.equalsIgnoreCase("primary") ? "Internal storage" : "Shared storage";
        if (path.isEmpty()) {
            return prefix;
        }
        return prefix + "/" + path.replace('\\', '/');
    }

    private Uri findChild(Uri tree, Uri parent, String name) throws Exception {
        for (Uri child : children(tree, parent)) {
            if (displayName(tree, child).equalsIgnoreCase(name)) {
                return child;
            }
        }
        return null;
    }

    private Uri[] children(Uri tree, Uri parent) throws Exception {
        String documentId = parent.equals(tree)
            ? DocumentsContract.getTreeDocumentId(tree) : DocumentsContract.getDocumentId(parent);
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID};
        ArrayList<Uri> result = new ArrayList<>();
        try (Cursor cursor = getContentResolver().query(childrenUri, columns, null, null, null)) {
            if (cursor == null) {
                throw new IllegalStateException("Cannot list the selected folder");
            }
            while (cursor.moveToNext()) {
                result.add(DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0)));
            }
        }
        return result.toArray(new Uri[0]);
    }

    private Uri asDocumentUri(Uri tree, Uri parent) {
        return parent.equals(tree)
            ? DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            : parent;
    }

    private String displayName(Uri tree, Uri document) throws Exception {
        try (Cursor cursor = getContentResolver().query(document,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                if (name != null) {
                    return name;
                }
            }
        }
        throw new IllegalStateException("A file in the selected folder has no name");
    }

    private long documentSize(Uri document) {
        try (Cursor cursor = getContentResolver().query(document,
                new String[]{OpenableColumns.SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getLong(0);
            }
        } catch (Exception ignored) {
        }
        return -1;
    }

    private Uri storedTree() {
        String value = preferences().getString(TREE_URI, null);
        return value == null ? null : Uri.parse(value);
    }

    private void forgetStoredTree() {
        Uri tree = storedTree();
        if (tree != null) {
            try {
                getContentResolver().releasePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (Exception e) {
                Log.w("D2Next", "Could not release the previous D2Next folder permission", e);
            }
        }
        preferences().edit().remove(TREE_URI).apply();
    }

    private Uri storedSaveTree() {
        String value = preferences().getString(SAVE_URI, null);
        return value == null ? null : Uri.parse(value);
    }

    private void forgetStoredSaveTree() {
        Uri save = storedSaveTree();
        Uri game = storedTree();
        if (save != null && (game == null || !save.equals(game))) {
            try {
                getContentResolver().releasePersistableUriPermission(save,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (Exception e) {
                Log.w("D2Next", "Could not release the previous save folder permission", e);
            }
        }
        preferences().edit().remove(SAVE_URI).apply();
    }

    private SharedPreferences preferences() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    private int loadLauncherZoom() {
        File settings = new File(new File(privateRoot, "Save"), "settings.ini");
        Integer fromFile = readViewZoom(settings);
        if (fromFile != null) {
            return fromFile;
        }
        int stored = preferences().getInt(VIEW_ZOOM, -1);
        if (stored >= 0 && stored < ZOOM_LABELS.length) {
            return stored;
        }
        return 2;
    }

    private Integer readViewZoom(File settings) {
        if (!settings.isFile()) {
            return null;
        }
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(new FileInputStream(settings), StandardCharsets.UTF_8))) {
            for (String line; (line = reader.readLine()) != null; ) {
                if (line.startsWith("viewzoom=")) {
                    int value = Integer.parseInt(line.substring("viewzoom=".length()).trim());
                    return Math.max(0, Math.min(ZOOM_LABELS.length - 1, value));
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private boolean loadLauncherCheats() {
        File settings = new File(new File(privateRoot, "Save"), "settings.ini");
        if (settings.isFile()) {
            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(new FileInputStream(settings), StandardCharsets.UTF_8))) {
                for (String line; (line = reader.readLine()) != null; ) {
                    if (line.startsWith("cheats=")) {
                        return Integer.parseInt(line.substring("cheats=".length()).trim()) != 0;
                    }
                }
            } catch (Exception ignored) {
            }
        }
        return preferences().getBoolean(CHEATS, false);
    }

    private void setLauncherCheats(boolean on) {
        launcherCheats = on;
        if (cheatCheck != null && cheatCheck.isChecked() != on) {
            cheatCheck.setChecked(on);
        }
        if (cheatSettingCheck != null && cheatSettingCheck.isChecked() != on) {
            cheatSettingCheck.setChecked(on);
        }
        if (cheatCheck != null) {
            cheatCheck.setText(on ? "Cheats — on (Esc menu)" : "Cheats — off");
            cheatCheck.setTextColor(on ? COLOR_GOLD : COLOR_MUTED);
            if (Build.VERSION.SDK_INT >= 21) {
                cheatCheck.setButtonTintList(ColorStateList.valueOf(on ? COLOR_GOLD : COLOR_MUTED));
            }
        }
        if (cheatSettingCheck != null) {
            cheatSettingCheck.setText(on ? "Cheats on" : "Enable cheats");
            cheatSettingCheck.setTextColor(on ? COLOR_GOLD : COLOR_TEXT);
            if (Build.VERSION.SDK_INT >= 21) {
                cheatSettingCheck.setButtonTintList(ColorStateList.valueOf(on ? COLOR_GOLD : COLOR_MUTED));
            }
        }
    }

    private CheckBox cheatToggle(String label) {
        CheckBox box = new CheckBox(this);
        box.setText(label);
        box.setTextColor(COLOR_MUTED);
        box.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        box.setChecked(launcherCheats);
        box.setMinHeight(0);
        box.setPadding(0, dp(4), 0, dp(4));
        if (Build.VERSION.SDK_INT >= 21) {
            box.setButtonTintList(ColorStateList.valueOf(COLOR_MUTED));
        }
        box.setOnCheckedChangeListener((ignored, checked) -> setLauncherCheats(checked));
        return box;
    }

    private void writeViewZoom() {
        int zoom = Math.max(0, Math.min(ZOOM_LABELS.length - 1, launcherZoom));
        preferences().edit().putInt(VIEW_ZOOM, zoom).putBoolean(CHEATS, launcherCheats).apply();
        File settings = new File(new File(privateRoot, "Save"), "settings.ini");
        Map<String, String> values = new LinkedHashMap<>();
        if (settings.isFile()) {
            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(new FileInputStream(settings), StandardCharsets.UTF_8))) {
                for (String line; (line = reader.readLine()) != null; ) {
                    int equals = line.indexOf('=');
                    if (equals > 0) {
                        values.put(line.substring(0, equals), line.substring(equals + 1));
                    }
                }
            } catch (Exception ignored) {
            }
        }
        File parent = settings.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            Log.w("D2Next", "Could not create the private Save folder for view zoom");
            return;
        }
        values.put("viewzoom", Integer.toString(zoom));
        values.put("cheats", launcherCheats ? "1" : "0");
        try (PrintWriter writer = new PrintWriter(
                new OutputStreamWriter(new FileOutputStream(settings), StandardCharsets.UTF_8))) {
            for (Map.Entry<String, String> entry : values.entrySet()) {
                writer.println(entry.getKey() + "=" + entry.getValue());
            }
        } catch (Exception e) {
            Log.w("D2Next", "Could not write view zoom to settings.ini", e);
        }
    }

    private Set<String> listedMpqNames(Uri tree, Uri game, Uri mpqFolder) throws Exception {
        Set<String> names = new HashSet<>();
        collectMpqNames(tree, game, names);
        collectMpqNames(tree, mpqFolder, names);
        return names;
    }

    private void collectMpqNames(Uri tree, Uri folder, Set<String> names) throws Exception {
        for (Uri child : children(tree, folder)) {
            String name = new File(displayName(tree, child)).getName().toLowerCase(Locale.ROOT);
            if (MPQ_FILES.contains(name)) {
                names.add(name);
            }
        }
    }

    private String missingRequiredMpqs(Set<String> names) {
        StringBuilder missing = new StringBuilder();
        for (String name : REQUIRED_MPQS) {
            if (!names.contains(name)) {
                missing.append("\n  ").append(name.equals("patch_d2.mpq") ? "Patch_D2.mpq" : name);
            }
        }
        return missing.toString();
    }

    private String characterCount(Uri tree, Uri folder) throws Exception {
        int characters = 0;
        for (Uri child : children(tree, folder)) {
            String name = new File(displayName(tree, child)).getName().toLowerCase(Locale.ROOT);
            if (name.endsWith(".d2s")) {
                characters++;
            }
        }
        if (characters == 0) {
            return "No character files yet";
        }
        if (characters == 1) {
            return "1 character";
        }
        return characters + " characters";
    }

    private void applyLauncherInsets(View view) {
        int pad = dp(16);
        view.setOnApplyWindowInsetsListener((v, insets) -> {
            int top;
            int left;
            int right;
            int bottom;
            if (Build.VERSION.SDK_INT >= 30) {
                int types = WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout();
                android.graphics.Insets bars = insets.getInsetsIgnoringVisibility(types);
                top = bars.top;
                left = bars.left;
                right = bars.right;
                bottom = bars.bottom;
            } else {
                top = Math.max(insets.getSystemWindowInsetTop(), insets.getStableInsetTop());
                left = Math.max(insets.getSystemWindowInsetLeft(), insets.getStableInsetLeft());
                right = Math.max(insets.getSystemWindowInsetRight(), insets.getStableInsetRight());
                bottom = Math.max(insets.getSystemWindowInsetBottom(), insets.getStableInsetBottom());
            }
            v.setPadding(pad + left, pad + top, pad + right, pad + bottom);
            return insets;
        });
        view.requestApplyInsets();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams buttonSlot() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(40), 1);
        params.setMargins(0, dp(4), dp(8), 0);
        return params;
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(12);
        card.setPadding(pad, pad, pad, pad);
        GradientDrawable background = new GradientDrawable();
        background.setColor(COLOR_CARD);
        background.setCornerRadius(dp(8));
        background.setStroke(dp(1), 0x33C7B377);
        card.setBackground(background);
        LinearLayout.LayoutParams params = matchWrap();
        params.bottomMargin = dp(12);
        card.setLayoutParams(params);
        return card;
    }

    private TextView sectionLabel(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(COLOR_GOLD);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        view.setTypeface(Typeface.SANS_SERIF, Typeface.BOLD);
        view.setPadding(0, dp(4), 0, dp(6));
        view.setLetterSpacing(0.08f);
        return view;
    }

    private CheckBox checkItem(String label) {
        CheckBox box = new CheckBox(this);
        box.setText(label);
        box.setTextColor(COLOR_TEXT);
        box.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        box.setClickable(false);
        box.setFocusable(false);
        box.setChecked(false);
        box.setMinHeight(0);
        box.setPadding(0, dp(4), 0, dp(4));
        tintCheck(box, false);
        return box;
    }

    private void tintCheck(CheckBox box, boolean ready) {
        box.setTextColor(ready ? COLOR_READY : COLOR_MISSING);
        if (Build.VERSION.SDK_INT >= 21) {
            box.setButtonTintList(ColorStateList.valueOf(ready ? COLOR_READY : COLOR_MISSING));
        }
    }

    private TextView bodyText(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(COLOR_TEXT);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        view.setPadding(0, 0, 0, dp(6));
        return view;
    }

    private Button smallButton(String text, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(COLOR_GOLD);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        button.setAllCaps(true);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        button.setMinWidth(0);
        button.setPadding(dp(8), dp(4), dp(8), dp(4));
        button.setOnClickListener(listener);
        GradientDrawable background = new GradientDrawable();
        background.setColor(0xFF2A221C);
        background.setCornerRadius(dp(6));
        background.setStroke(dp(1), COLOR_GOLD);
        button.setBackground(background);
        return button;
    }

    private static native void nativeFolderSyncFinished(boolean success, String message);
}
