package com.uhdrviewer;

import android.app.Activity;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.content.Intent;
import android.net.Uri;
import android.provider.MediaStore;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private SurfaceView surfaceView;
    private long nativeHandle = 0;

    static {
        System.loadLibrary("uhdrviewer");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);

        surfaceView.getHolder().addCallback(this);
        surfaceView.setOnClickListener(v -> openImagePicker());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        nativeHandle = nativeOnCreate(holder.getSurface());
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
            nativeOnDestroy(nativeHandle);
            nativeHandle = 0;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (nativeHandle != 0) nativeOnResume(nativeHandle);
    }

    @Override
    protected void onPause() {
        if (nativeHandle != 0) nativeOnPause(nativeHandle);
        super.onPause();
    }

    private void openImagePicker() {
        Intent intent = new Intent(Intent.ACTION_PICK, MediaStore.Images.Media.EXTERNAL_CONTENT_URI);
        startActivityForResult(intent, 100);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == 100 && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null && nativeHandle != 0) {
                String path = getPathFromUri(uri);
                if (path != null) {
                    nativeOnLoadImage(nativeHandle, path);
                }
            }
        }
    }

    private String getPathFromUri(Uri uri) {
        String path = null;
        String[] proj = { MediaStore.Images.Media.DATA };
        android.database.Cursor cursor = getContentResolver().query(uri, proj, null, null, null);
        if (cursor != null) {
            if (cursor.moveToFirst()) {
                int col = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATA);
                path = cursor.getString(col);
            }
            cursor.close();
        }
        return path;
    }

    private native long nativeOnCreate(android.view.Surface surface);
    private native void nativeOnSurfaceChanged(long handle, android.view.Surface surface, int width, int height);
    private native void nativeOnResume(long handle);
    private native void nativeOnPause(long handle);
    private native void nativeOnDestroy(long handle);
    private native void nativeOnLoadImage(long handle, String path);
}
