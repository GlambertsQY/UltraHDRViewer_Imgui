package com.uhdrviewer;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "UHDRViewer";
    private static final int REQ_PICK_IMAGE = 100;

    private SurfaceView surfaceView;
    private long nativeHandle = 0;
    private String pendingImagePath = null;
    private boolean resumed = false;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private float[] touchX = new float[2];
    private float[] touchY = new float[2];

    static {
        System.loadLibrary("uhdrviewer");
    }

    private final Runnable openRequestPoller = new Runnable() {
        @Override
        public void run() {
            if (!resumed) return;
            if (nativeHandle != 0 && nativeConsumeOpenRequest(nativeHandle)) {
                openImagePicker();
            }
            handler.postDelayed(this, 100);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // The SurfaceView renders behind the activity window; keep the window
        // transparent so its theme background does not cover the GL surface.
        getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));

        surfaceView = new SurfaceView(this);
        // Do not set a view background: the activity window is above the
        // SurfaceView and would cover the GL-rendered content.
        surfaceView.getHolder().addCallback(this);
        surfaceView.setOnTouchListener((View v, MotionEvent event) -> {
            forwardTouch(event);
            return true;
        });
        setContentView(surfaceView);

        // The decor view must exist before querying the insets controller.
        applyImmersiveMode();

        handleIntent(getIntent());
    }

    private void applyImmersiveMode() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getDecorView().getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    // ---------------------------------------------------------------- surface

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        int width = holder.getSurfaceFrame().width();
        int height = holder.getSurfaceFrame().height();
        float density = getResources().getDisplayMetrics().density;

        Display display = getWindowManager().getDefaultDisplay();
        boolean hdr10 = false;
        float maxLuminance = 1000.0f;
        Display.HdrCapabilities caps = display.getHdrCapabilities();
        if (caps != null) {
            for (int type : caps.getSupportedHdrTypes()) {
                if (type == Display.HdrCapabilities.HDR_TYPE_HDR10) {
                    hdr10 = true;
                    break;
                }
            }
            float desired = caps.getDesiredMaxLuminance();
            if (desired > 0.0f) maxLuminance = desired;
        }

        Log.i(TAG, "surfaceCreated " + width + "x" + height
                + " density=" + density + " hdr10=" + hdr10
                + " maxLum=" + maxLuminance);

        nativeHandle = nativeOnSurfaceCreated(nativeHandle, holder.getSurface(),
                width, height, density, hdr10, maxLuminance);

        if (pendingImagePath != null && nativeHandle != 0) {
            nativeOnLoadImage(nativeHandle, pendingImagePath);
            pendingImagePath = null;
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (nativeHandle != 0) {
            nativeOnSurfaceChanged(nativeHandle, holder.getSurface(), width, height);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (nativeHandle != 0) {
            nativeOnSurfaceDestroyed(nativeHandle);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        applyImmersiveMode();
        if (nativeHandle != 0) nativeOnResume(nativeHandle);
        handler.removeCallbacks(openRequestPoller);
        handler.postDelayed(openRequestPoller, 100);
    }

    @Override
    protected void onPause() {
        resumed = false;
        handler.removeCallbacks(openRequestPoller);
        if (nativeHandle != 0) nativeOnPause(nativeHandle);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        resumed = false;
        handler.removeCallbacks(openRequestPoller);
        if (nativeHandle != 0) {
            nativeOnDestroy(nativeHandle);
            nativeHandle = 0;
        }
        super.onDestroy();
    }

    // ---------------------------------------------------------------- intents

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    private void handleIntent(Intent intent) {
        if (intent == null) return;
        Uri uri = intent.getData();
        if (uri == null && Intent.ACTION_SEND.equals(intent.getAction())) {
            uri = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        }
        if (uri != null) {
            importUri(uri);
        }
    }

    private void openImagePicker() {
        Log.i(TAG, "Opening image picker");
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        try {
            startActivityForResult(intent, REQ_PICK_IMAGE);
        } catch (Exception e) {
            Log.e(TAG, "No document picker available", e);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_IMAGE && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) importUri(uri);
        }
    }

    /**
     * Copies the picked document into the app cache (scoped storage safe) and
     * hands the local path to the native decoder.
     */
    private void importUri(Uri uri) {
        File output = new File(getCacheDir(), "import_" + System.currentTimeMillis() + ".jpg");
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(output)) {
            if (in == null) {
                Log.e(TAG, "Cannot open " + uri);
                return;
            }
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to import " + uri, e);
            return;
        }

        String path = output.getAbsolutePath();
        Log.i(TAG, "Imported image: " + path);
        if (nativeHandle != 0) {
            nativeOnLoadImage(nativeHandle, path);
        } else {
            pendingImagePath = path;
        }
    }

    // ----------------------------------------------------------------- touch

    private void forwardTouch(MotionEvent event) {
        if (nativeHandle == 0) return;
        int count = Math.min(event.getPointerCount(), 2);
        if (touchX.length < count) {
            touchX = new float[count];
            touchY = new float[count];
        }
        for (int i = 0; i < count; i++) {
            touchX[i] = event.getX(i);
            touchY[i] = event.getY(i);
        }
        nativeOnTouch(nativeHandle, event.getActionMasked(), count, touchX, touchY);
    }

    // ------------------------------------------------------------------ jni

    private native long nativeOnSurfaceCreated(long handle, android.view.Surface surface,
                                               int width, int height, float density,
                                               boolean hdr10, float maxLuminance);

    private native void nativeOnSurfaceChanged(long handle, android.view.Surface surface,
                                               int width, int height);

    private native void nativeOnSurfaceDestroyed(long handle);

    private native void nativeOnResume(long handle);

    private native void nativeOnPause(long handle);

    private native void nativeOnDestroy(long handle);

    private native void nativeOnLoadImage(long handle, String path);

    private native void nativeOnTouch(long handle, int action, int pointerCount,
                                      float[] xs, float[] ys);

    private native boolean nativeConsumeOpenRequest(long handle);
}
