package com.bazzalt.runtime;

import android.os.Bundle;
import java.io.File;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private static volatile GameActivity instance;
    private static volatile boolean skipped;
    private android.view.View intro;
    public static void ShowIntro(boolean enabled, int size, boolean allowSkip) {
        GameActivity activity=instance;
        if(activity==null)return;
        skipped=false;
        activity.runOnUiThread(() -> {
            if(instance!=activity||activity.isFinishing()||activity.isDestroyed()||mLayout==null)return;
            android.widget.LinearLayout panel=new android.widget.LinearLayout(activity);
            panel.setOrientation(android.widget.LinearLayout.VERTICAL);
            panel.setGravity(android.view.Gravity.CENTER);
            panel.setBackgroundColor(android.graphics.Color.BLACK);
            if(enabled){
                android.widget.ImageView image=new android.widget.ImageView(activity);
                try(java.io.InputStream input=activity.getAssets().open("branding.png")){
                    image.setImageDrawable(android.graphics.drawable.Drawable.createFromStream(input,null));
                }catch(java.io.IOException error){android.util.Log.e("Bazzalt","Startup logo is unavailable",error);}
                int pixels=Math.round(size*activity.getResources().getDisplayMetrics().density);
                panel.addView(image,new android.widget.LinearLayout.LayoutParams(pixels,pixels));
                android.widget.TextView label=new android.widget.TextView(activity);
                label.setText("Powered by Bazzalt");label.setTextColor(android.graphics.Color.WHITE);
                panel.addView(label);
            }
            panel.setOnClickListener(view -> {if(allowSkip)skipped=true;});
            activity.intro=panel;
            mLayout.addView(panel,new android.view.ViewGroup.LayoutParams(-1,-1));
        });
    }
    public static boolean IntroSkipped(){return skipped;}
    public static void FinishIntro(){
        GameActivity activity=instance;
        if(activity!=null)activity.runOnUiThread(() -> {
            if(instance!=activity||activity.isDestroyed()||mLayout==null)return;
            if(activity.intro!=null){mLayout.removeView(activity.intro);activity.intro=null;}
        });
    }
    @Override protected String[] getLibraries() { return new String[]{"SDL3", "Bazzalt", "main"}; }
    @Override protected void onCreate(Bundle state) {
        String id = getIntent().getStringExtra("content");
        if (id == null || !id.matches("[a-f0-9]{32}")) {
            super.onCreate(state);
            startActivity(new android.content.Intent(this, BootstrapActivity.class));
            finish();
            return;
        }
        Platform.Initialize(this, new File(getNoBackupFilesDir(), "content/" + id));
        super.onCreate(state);
        instance=this;
    }
    @Override protected void onDestroy(){if(instance==this)instance=null;super.onDestroy();}
}
