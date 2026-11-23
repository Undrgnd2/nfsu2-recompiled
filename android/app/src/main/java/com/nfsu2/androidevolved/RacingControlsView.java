package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;
import android.app.AlertDialog;
import android.widget.EditText;
import android.widget.Toast;
import org.json.JSONObject;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * On-screen controls drawn as icons, in the style of the MW port: round buttons, pedals and a steering
 * stick, no captions. One surface tracks independent fingers so steering and pedals can be held together.
 */
final class RacingControlsView extends View {
    interface Keys { void set(int scan, boolean down); }
    /** Touches outside every control drive the game's mouse: x/y as fractions of the view, phase 0 down, 1 move, 2 up. */
    interface Pointer { void touch(float x, float y, int phase); }
    // Special controls that do not map to a DirectInput scan code.
    private static final int MODE = -1, TEXT = -2, STICK = -3, EDIT = -5, STEERMODE = -6, HYD_LIFT = -7, HYD_BOUNCE = -8;
    /** The hydraulics' keys (the game's Pressurize and Bounce: H and J), held while their button is on. */
    private static final int KEY_LIFT = 0x23, KEY_BOUNCE = 0x24;
    /** Photo mode (photo.c): its button, and its own controls while it is on. */
    private static final int PH_TOGGLE = -9, PH_EXIT = -10, PH_CAM = -11, PH_RESET = -12, PH_STICK = -13, PH_UP = -14,
            PH_DOWN = -15, PH_SHOT = -16, PH_RL = -17, PH_RR = -18, PH_PANEL = -19, PH_PATH_STOP = -20;
    /** nfs_drift.h PHOTO_* */
    private static final int PHOTO_OPEN = 1, PHOTO_MOVE = 2, PHOTO_LIFT = 3, PHOTO_DRAG = 4, PHOTO_ZOOM = 5, PHOTO_ROLL = 6,
            PHOTO_MODE = 7, PHOTO_RESET = 8, PHOTO_PATH = 9, PHOTO_CARS = 10;
    private static final int PATH_CLEAR = 0, PATH_ADD = 1, PATH_PLAY = 2, PATH_STOP = 3;
    interface Photo { void input(int cmd, int a, int b); void shot(); void look(boolean live, float[] values); }
    private Photo photo;
    private int[] photoState = new int[6];
    private boolean photoOn;
    private static final int ROUND = 0, PEDAL = 1, WHEEL = 2, RECT = 3, PILL = 4, KEY = 5;
    /** undrgnd2.eu's blue, for a pressed or lit control and the menu. */
    private static final int LIME = 0xFF3FC1F5;
    private static final int NAVY = 0xF20A1628, MENU_VALUE = 0xFFA9C7E8;
    /** How many controls hold each key: Nitro holds Gas too, and either one lifting must not let the other's go. */
    private final int[] keyCount = new int[256];
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Keys keys;
    private Pointer pointer;
    private int pointerId=-1;
    private float pointerX, pointerY;
    private final Runnable textInput;
    private final Runnable configureTilt;
    private final Runnable configureFrameLimit;
    private String style;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final ArrayList<Control> controls = new ArrayList<>();
    private final HashMap<Integer, Control> fingers = new HashMap<>();
    private boolean racing;
    private boolean lastGameMode;
    private boolean editing;
    private boolean hiddenByGamepad;
    private boolean gamepadConnected;
    private Control selected;
    private int dragPointer = -1;
    private float dragX, dragY;
    private final SharedPreferences layouts;
    private float scale;
    private static final int ACCENT = Color.rgb(255, 154, 50);
    private static final class Control {
        final RectF area;
        final String icon;
        final int scan, shape, color;
        final RectF original;
        String id;
        /** a second key held with the first (Nitro: Gas too); a group a finger slides within (1 steering, 2 pedals) */
        int scan2 = -1, group;
        /** away for now (Pressurize and Bounce without hydraulics): not drawn, no finger takes it */
        boolean hidden;
        int held;
        float steering, steerY;
        Control(float x, float y, float w, float h, int shape, String icon, int scan, int color) {
            area = new RectF(x, y, x+w, y+h); original=new RectF(area); this.shape=shape; this.icon=icon; this.scan=scan; this.color=color;
        }
    }
    RacingControlsView(Context context, Keys keys, Runnable textInput, Runnable configureTilt, Runnable configureFrameLimit) {
        super(context); this.keys=keys; this.textInput=textInput;
        layouts=context.getSharedPreferences("touch_layouts_v1",Context.MODE_PRIVATE);
        style=layouts.getString("style","undrgnd2");
        // Once: undrgnd2.eu's controls replace the style picked before they existed (Options > Style brings it back).
        if(!layouts.getBoolean("undrgnd2_default",false)) {style="undrgnd2";layouts.edit().putString("style",style).putBoolean("undrgnd2_default",true).apply();}this.configureTilt=configureTilt;this.configureFrameLimit=configureFrameLimit;
        setContentDescription("Commandes du jeu");
        setHapticFeedbackEnabled(true);
        line.setStyle(Paint.Style.STROKE);line.setStrokeCap(Paint.Cap.ROUND);line.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);text.setTypeface(Typeface.create("sans-serif-condensed",Typeface.BOLD));
        label.setTextAlign(Paint.Align.CENTER);label.setTypeface(Typeface.create(Typeface.SANS_SERIF,Typeface.BOLD_ITALIC));
    }
    private void add(float x, float y, float size, String icon, int scan) {add(x,y,size,size,ROUND,icon,scan,0);}
    private void add(float x, float y, float w, float h, int shape, String icon, int scan, int color) {
        Control c=new Control(x,y,w,h,shape,icon,scan,color);
        // v2: icon layout; positions saved for the old captioned buttons do not apply.
        c.id="v2."+(racing?"race."+(style.equals("classic")?"":style+"."):"menu.")+scan;
        float factor=layouts.getFloat(c.id+".size",1);
        float cx=layouts.getFloat(c.id+".x",c.area.centerX()/(getWidth()/scale))*(getWidth()/scale);
        float cy=layouts.getFloat(c.id+".y",c.area.centerY()/(getHeight()/scale))*(getHeight()/scale);
        c.area.set(cx-w*factor/2,cy-h*factor/2,cx+w*factor/2,cy+h*factor/2);
        // Nitro gives gas too, in every style, as on undrgnd2.eu: one thumb on N₂O drives on.
        if(racing&&scan==0x38)c.scan2=0xc8;
        clamp(c);controls.add(c);
    }
    private void addU(float x, float y, float w, float h, int shape, String icon, int scan, int scan2, int group) {
        add(x,y,w,h,shape,icon,scan,0);
        Control c=controls.get(controls.size()-1);c.scan2=scan2;c.group=group;
    }
    /**
     * undrgnd2.eu's touch controls (nfsu2-web web/src/app/touch.ts and style.css), in dp from the screen's edges:
     * the small buttons in a row at the top, the steering (◀ ▶ or a thumb stick) bottom left, Brake and Gas bottom
     * right with Nitro over Gas (Nitro holds Gas too) and the handbrake over Brake; the menus' D-pad bottom left and
     * Back over OK bottom right.  A finger slides between the steering's buttons and between the pedals.
     */
    private void layoutUndrgnd(float w, float h) {
        final float L=12,T=12,B=12,R=12;
        boolean stick=layouts.getString("steer","buttons").equals("stick");
        if(racing) {
            add(L,T,48,"settings",EDIT);
            add(L+56,T,48,"pause",1);
            add(L+112,T,48,"camera",0x2e);
            add(L+168,T,48,"reset",0x13);
            add(L+224,T,48,"map",0x32);
            add(L+280,T,48,"sms",0x0f);
            add(L+336,T,48,"drift",0xF1);   // drift.c
            add(L+392,T,48,"ghost",0xF2);   // ghost.c
            // undrgnd2.eu's Pressurize and Bounce: only with a car that has hydraulics (ghost.c hydraulics)
            add(L+448,T,48,"status",0xF3); // Statut de carrière: the pad's D-pad down (guest_input.inc)
            add(L+504,T,48,"photo",PH_TOGGLE);   // Mode photo (photo.c)
            add(L+560,T,48,"lights",0xF4);   // Phares: touché éteints / allumés, maintenu pleins phares (lights.c)
            add(L+616,T,48,"lift",HYD_LIFT);controls.get(controls.size()-1).hidden=hydState==0&&hyd==0;
            add(L+672,T,48,"bounce",HYD_BOUNCE);controls.get(controls.size()-1).hidden=hydState==0&&hyd==0;
            add(w-R-48,T,48,stick?"stick":"buttons",STEERMODE);
            add(w-R-104,T,48,"menu",MODE);
            if(stick) add(L,h-B-120,120,120,WHEEL,"stick",STICK,0);
            else {
                addU(L,h-B-104,80,104,RECT,"left",0xcb,-1,1);
                addU(L+88,h-B-104,80,104,RECT,"right",0xcd,-1,1);
            }
            if(layouts.getBoolean("gears",false)) {
                float gb=stick?132:112;
                addU(L,h-B-gb-56,80,56,RECT,"down",0x1d,-1,0);
                addU(L+88,h-B-gb-56,80,56,RECT,"up",0x2a,-1,0);
            }
            addU(w-R-84,h-B-148,84,148,RECT,"t:Gaz",0xc8,-1,2);
            addU(w-R-92-84,h-B-104,84,104,RECT,"t:Freiner",0xd0,-1,2);
            addU(w-R-12-60,h-B-160-60,60,60,ROUND,"t:N₂O",0x38,0xc8,2);
            addU(w-R-106-56,h-B-116-56,56,56,ROUND,"t:Frein\nà main",0x39,-1,0);
        } else {
            final float k=52;
            add(L,T,48,"settings",EDIT);
            add(w-R-48,T,48,"wheel",MODE);
            addU(L+k+4,h-B-2*(k+4)-k,k,k,KEY,"up",0xc8,-1,0);
            addU(L,h-B-(k+4)-k,k,k,KEY,"left",0xcb,-1,0);
            addU(L+2*(k+4),h-B-(k+4)-k,k,k,KEY,"right",0xcd,-1,0);
            addU(L+k+4,h-B-k,k,k,KEY,"down",0xd0,-1,0);
            addU(w-R-136,h-B-k-12-k,136,k,PILL,"t:Retour",1,-1,0);
            addU(w-R-136,h-B-k,136,k,PILL,"t:OK",0x1c,-1,0);
            add(w/2-23,h-B-46,46,"keyboard",TEXT);
        }
    }
    /** Photo mode's controls: leave, orbit or free camera, reset; the stick moves the camera, ▲ ▼ lift it, ↺ ↻ tilt
     *  it; PHOTO takes the picture.  A finger elsewhere turns the camera, two pinch the zoom. */
    private void layoutPhoto(float w, float h) {
        final float L=12,T=12,B=12,R=12;
        add(L,T,48,"back",PH_EXIT);
        addU(w-R-170,T,170,48,PILL,(photoState[0]&4)!=0?"t:Caméra libre":"t:Caméra orbite",PH_CAM,-1,0);
        addU(w-R-170-8-120,T,120,48,PILL,"t:Réinitialiser",PH_RESET,-1,0);
        addU(w-R-170-8-120-8-110,T,110,48,PILL,"t:Réglages",PH_PANEL,-1,0);
        if(photoState.length>5&&photoState[5]!=0)addU(w/2-90,h-B-48,180,48,PILL,"t:■ Arrêter le trajet",PH_PATH_STOP,-1,0);
        add(L,h-B-140,140,140,WHEEL,"stick",PH_STICK,0);
        addU(L+152,h-B-140,60,66,KEY,"up",PH_UP,-1,0);
        addU(L+152,h-B-66,60,66,KEY,"down",PH_DOWN,-1,0);
        add(w-R-104,h-B-104,104,"t:PHOTO",PH_SHOT);
        add(w-R-104-70,h-B-58,56,"t:↻",PH_RR);
        add(w-R-104-70,h-B-58-66,56,"t:↺",PH_RL);
    }
    void setPhotoHooks(Photo hooks) {photo=hooks;}
    // ---- photo mode's panel (undrgnd2.eu's): the picture's look, the cars shown, the camera's path ----
    private static final String[] FILTER_IDS={"none","bw","warm","cool","vivid","faded"};
    private static final String[] FILTER_NAMES={"Aucun","Noir et blanc","Chaud","Froid","Vif","Délavé"};
    /** contrast, saturation, tint r g b, fade (nfsu2-web photolook.ts FILTERS) */
    private static final float[][] FILTER_LOOK={{1,1,1,1,1,0},{1.12f,0,1,1,1,0},{1.04f,1.08f,1.08f,1,0.86f,0},
            {1.04f,0.95f,0.88f,0.98f,1.1f,0},{1.12f,1.4f,1,1,1,0},{0.88f,0.7f,1.02f,1,0.96f,0.07f}};
    private static final String[] CROP_IDS={"full","16:9","21:9","3:2","4:3","1:1","4:5","9:16"};
    private static final float[] CROP_RATIO={0,16/9f,21/9f,3/2f,4/3f,1,4/5f,9/16f};
    private static final String[] CARS_NAMES={"Toutes","Sans le trafic","Seulement la mienne","Aucune"};
    private static final int[] PATH_SECONDS={3,5,10,20,30,60};
    private android.app.Dialog photoPanel;
    private int pick(String[] ids,String id) {for(int i=0;i<ids.length;++i)if(ids[i].equals(id))return i;return 0;}
    /** the part of the picture the crop keeps, middle of it: left, top, right, bottom (photolook.ts cropRect) */
    private float[] cropRect() {
        float r=CROP_RATIO[pick(CROP_IDS,layouts.getString("photo.crop","full"))],aspect=getHeight()>0?getWidth()/(float)getHeight():16/9f;
        if(!(r>0)||Math.abs(r-aspect)<1e-3)return new float[]{0,0,1,1};
        if(r>aspect){float h=aspect/r;return new float[]{0,(1-h)/2,1,(1+h)/2};}
        float w=r/aspect;return new float[]{(1-w)/2,0,(1+w)/2,1};
    }
    private void sendLook() {
        if(photo==null)return;
        float[] f=FILTER_LOOK[pick(FILTER_IDS,layouts.getString("photo.filter","none"))],k=cropRect();
        float exposure=(float)Math.pow(2,(layouts.getInt("photo.exposure",50)-50)/50.0),vignette=layouts.getInt("photo.vignette",0)/100f;
        photo.look(photoOn,new float[]{exposure,f[0],f[1],f[2],f[3],f[4],f[5],vignette,k[0],k[1],k[2],k[3]});
    }
    private void showPhotoPanel() {
        if(photoPanel!=null&&photoPanel.isShowing()){photoPanel.dismiss();return;}
        Context ctx=getContext();
        android.widget.LinearLayout panel=new android.widget.LinearLayout(ctx);panel.setOrientation(android.widget.LinearLayout.VERTICAL);
        android.graphics.drawable.GradientDrawable bg=new android.graphics.drawable.GradientDrawable();
        bg.setColor(0xE60A1628);bg.setCornerRadius(px(14));bg.setStroke(px(1.5f),0x663FC1F5);panel.setBackground(bg);
        android.widget.LinearLayout head=new android.widget.LinearLayout(ctx);head.setGravity(android.view.Gravity.CENTER_VERTICAL);
        head.setPadding(px(20),px(12),px(20),px(12));
        head.addView(menuText("Mode photo",20,LIME,true),new android.widget.LinearLayout.LayoutParams(0,-2,1));
        android.widget.TextView close=menuText("Fermer",16,0xE6FFFFFF,false);head.addView(close);
        panel.addView(head);panel.addView(menuRule());
        android.widget.LinearLayout rows=new android.widget.LinearLayout(ctx);rows.setOrientation(android.widget.LinearLayout.VERTICAL);
        android.widget.ScrollView scroll=new android.widget.ScrollView(ctx);scroll.addView(rows);
        panel.addView(scroll,new android.widget.LinearLayout.LayoutParams(-1,0,1));
        android.app.Dialog dialog=new android.app.Dialog(ctx);photoPanel=dialog;
        dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE);
        fillPhotoPanel(rows);
        close.setOnClickListener(v->dialog.dismiss());
        dialog.setContentView(panel);dialog.setCanceledOnTouchOutside(false);
        android.view.Window win=dialog.getWindow();
        if(win!=null) {
            win.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(0));
            win.setLayout(Math.min(px(380),getWidth()/2),getHeight()-px(84));
            win.setGravity(android.view.Gravity.END|android.view.Gravity.BOTTOM);
            win.setDimAmount(0);win.clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            // the game stays in hand beside the panel: a finger there turns the camera, two zoom
            win.addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            win.setFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            win.getDecorView().setSystemUiVisibility(getSystemUiVisibility());
        }
        dialog.show();
    }
    private void refreshPhotoPanel(android.widget.LinearLayout rows) {rows.removeAllViews();fillPhotoPanel(rows);invalidate();}
    private void section(android.widget.LinearLayout rows,String name) {
        android.widget.TextView t=menuText(name.toUpperCase(java.util.Locale.FRANCE),13,LIME,true);
        t.setPadding(px(20),px(14),px(20),px(4));rows.addView(t);
    }
    /** a row whose tap does run, the panel staying open (its values again) */
    private void photoRow(android.widget.LinearLayout rows,String name,String value,Runnable run) {
        android.widget.LinearLayout row=new android.widget.LinearLayout(getContext());row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        row.setPadding(px(20),px(11),px(20),px(11));
        android.util.TypedValue ripple=new android.util.TypedValue();
        getContext().getTheme().resolveAttribute(android.R.attr.selectableItemBackground,ripple,true);
        row.setBackgroundResource(ripple.resourceId);
        row.addView(menuText(name,16,0xFFFFFFFF,false),new android.widget.LinearLayout.LayoutParams(0,-2,1));
        if(value!=null)row.addView(menuText(value,15,MENU_VALUE,false));
        row.setOnClickListener(v->{run.run();refreshPhotoPanel(rows);});
        rows.addView(row);
    }
    private void sliderRow(android.widget.LinearLayout rows,String name,String key,int start,java.util.function.IntFunction<String> shown) {
        android.widget.LinearLayout box=new android.widget.LinearLayout(getContext());box.setOrientation(android.widget.LinearLayout.VERTICAL);
        box.setPadding(px(20),px(8),px(20),px(6));
        android.widget.LinearLayout top=new android.widget.LinearLayout(getContext());
        top.addView(menuText(name,16,0xFFFFFFFF,false),new android.widget.LinearLayout.LayoutParams(0,-2,1));
        android.widget.TextView value=menuText(shown.apply(layouts.getInt(key,start)),15,MENU_VALUE,false);top.addView(value);
        android.widget.SeekBar bar=new android.widget.SeekBar(getContext());bar.setMax(100);bar.setProgress(layouts.getInt(key,start));
        bar.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(android.widget.SeekBar s,int p,boolean user){if(!user)return;
                layouts.edit().putInt(key,p).apply();value.setText(shown.apply(p));sendLook();}
            @Override public void onStartTrackingTouch(android.widget.SeekBar s){}
            @Override public void onStopTrackingTouch(android.widget.SeekBar s){}
        });
        box.addView(top);box.addView(bar);rows.addView(box);
    }
    private void fillPhotoPanel(android.widget.LinearLayout rows) {
        section(rows,"Image");
        int filter=pick(FILTER_IDS,layouts.getString("photo.filter","none"));
        photoRow(rows,"Filtre",FILTER_NAMES[filter],()->{layouts.edit().putString("photo.filter",FILTER_IDS[(filter+1)%FILTER_IDS.length]).apply();sendLook();});
        sliderRow(rows,"Luminosité","photo.exposure",50,p->String.format(java.util.Locale.FRANCE,"%+.1f IL",(p-50)/50.0));
        sliderRow(rows,"Vignettage","photo.vignette",0,p->p+" %");
        int crop=pick(CROP_IDS,layouts.getString("photo.crop","full"));
        photoRow(rows,"Cadrage",crop==0?"Image entière":CROP_IDS[crop],()->{layouts.edit().putString("photo.crop",CROP_IDS[(crop+1)%CROP_IDS.length]).apply();sendLook();});
        boolean grid=layouts.getBoolean("photo.grid",false);
        photoRow(rows,"Grille des tiers",grid?"Affichée":"Masquée",()->layouts.edit().putBoolean("photo.grid",!grid).apply());
        photoRow(rows,"Réinitialiser l'image",null,()->{layouts.edit().remove("photo.filter").remove("photo.exposure").remove("photo.vignette")
                .remove("photo.crop").remove("photo.grid").apply();sendLook();});
        section(rows,"Scène");
        int cars=layouts.getInt("photo.cars",0);
        photoRow(rows,"Voitures affichées",CARS_NAMES[cars],()->{int next=(cars+1)%CARS_NAMES.length;layouts.edit().putInt("photo.cars",next).apply();photoInput(PHOTO_CARS,next,0);});
        section(rows,"Trajet de caméra");
        int points=photoState.length>4?photoState[4]:0;boolean playing=photoState.length>5&&photoState[5]!=0;
        int sec=layouts.getInt("photo.path_s",2);
        photoRow(rows,"Ajouter un point",points+" / 8",()->photoInput(PHOTO_PATH,PATH_ADD,0));
        photoRow(rows,"Durée",PATH_SECONDS[sec]+" s",()->layouts.edit().putInt("photo.path_s",(sec+1)%PATH_SECONDS.length).apply());
        photoRow(rows,playing?"Arrêter le trajet":"Lire le trajet",points<2?"2 points minimum":null,()->{
            if(playing)photoInput(PHOTO_PATH,PATH_STOP,0);else if(points>=2)photoInput(PHOTO_PATH,PATH_PLAY,PATH_SECONDS[sec]*1000);});
        photoRow(rows,"Effacer le trajet",null,()->photoInput(PHOTO_PATH,PATH_CLEAR,0));
        android.widget.TextView help=menuText("Place la caméra, ajoute un point, recommence (jusqu'à 8), puis lis le trajet : la caméra passe d'un point à l'autre en douceur.",12,0x99FFFFFF,false);
        help.setPadding(px(20),px(6),px(20),px(14));rows.addView(help);
    }
    void setPhotoStatus(int[] status) {
        if(status==null||status.length<4)return;
        boolean on=(status[0]&2)!=0,free=(status[0]&4)!=(photoState[0]&4);
        boolean path=status.length>5&&photoState.length>5&&(status[5]!=0)!=(photoState.length>5&&photoState[5]!=0);
        int[] was=photoState;photoState=status;
        if(on!=photoOn) {
            photoOn=on;sendLook();
            if(on)photoInput(PHOTO_CARS,layouts.getInt("photo.cars",0),0);
            else if(photoPanel!=null&&photoPanel.isShowing())photoPanel.dismiss();
        }
        if(on!=((was[0]&2)!=0)||on&&(free||path)) {photoRelease();releaseAll();if(!editing)layoutControls();}
        else if(photoOn)invalidate();
    }
    private void photoInput(int cmd,int a,int b) {if(photo!=null)photo.input(cmd,a,b);}
    // ---- photo mode's touches: its controls, and a finger elsewhere turning, two pinching ----
    private final HashMap<Integer,Control> photoFingers=new HashMap<>();
    private final HashMap<Integer,float[]> freeFingers=new HashMap<>();
    private float pinchLast=-1;
    private void photoStick(Control c,float x,float y) {
        c.steering=Math.max(-1,Math.min(1,(x/scale-c.area.centerX())/(c.area.width()/2)));
        c.steerY=Math.max(-1,Math.min(1,(y/scale-c.area.centerY())/(c.area.height()/2)));
        photoInput(PHOTO_MOVE,Math.round(c.steering*1000),Math.round(-c.steerY*1000));
    }
    private void photoPress(Control c,float x,float y) {
        c.held=1;
        if(c.scan==PH_UP)photoInput(PHOTO_LIFT,1000,0);
        else if(c.scan==PH_DOWN)photoInput(PHOTO_LIFT,-1000,0);
        else if(c.scan==PH_RL)photoInput(PHOTO_ROLL,-1,0);
        else if(c.scan==PH_RR)photoInput(PHOTO_ROLL,1,0);
        else if(c.scan==PH_STICK)photoStick(c,x,y);
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }
    private void photoLift(Control c,boolean tap) {
        c.held=0;
        if(c.scan==PH_UP||c.scan==PH_DOWN)photoInput(PHOTO_LIFT,0,0);
        else if(c.scan==PH_RL||c.scan==PH_RR)photoInput(PHOTO_ROLL,0,0);
        else if(c.scan==PH_STICK){c.steering=0;c.steerY=0;photoInput(PHOTO_MOVE,0,0);}
        if(!tap)return;
        if(c.scan==PH_EXIT)photoInput(PHOTO_OPEN,2,0);
        else if(c.scan==PH_CAM)photoInput(PHOTO_MODE,0,0);
        else if(c.scan==PH_RESET)photoInput(PHOTO_RESET,0,0);
        else if(c.scan==PH_SHOT&&photo!=null)photo.shot();
        else if(c.scan==PH_PANEL)showPhotoPanel();
        else if(c.scan==PH_PATH_STOP)photoInput(PHOTO_PATH,PATH_STOP,0);
    }
    private void photoRelease() {
        for(Control c:photoFingers.values())photoLift(c,false);
        photoFingers.clear();freeFingers.clear();pinchLast=-1;
        photoInput(PHOTO_MOVE,0,0);photoInput(PHOTO_LIFT,0,0);photoInput(PHOTO_ROLL,0,0);
    }
    private boolean photoTouch(MotionEvent e) {
        int action=e.getActionMasked(),index=e.getActionIndex(),id=e.getPointerId(index);
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            if(action==MotionEvent.ACTION_DOWN)photoRelease();
            Control c=hit(e.getX(index),e.getY(index));
            if(c!=null){photoFingers.put(id,c);photoPress(c,e.getX(index),e.getY(index));}
            else {freeFingers.put(id,new float[]{e.getX(index),e.getY(index)});pinchLast=-1;}
        } else if(action==MotionEvent.ACTION_MOVE) {
            float h=Math.max(1,getHeight());
            if(freeFingers.size()>=2) {
                Integer[] ids=freeFingers.keySet().toArray(new Integer[0]);
                int a=e.findPointerIndex(ids[0]),b=e.findPointerIndex(ids[1]);
                if(a>=0&&b>=0) {
                    float d=(float)Math.hypot(e.getX(a)-e.getX(b),e.getY(a)-e.getY(b));
                    if(pinchLast>0&&d>0)photoInput(PHOTO_ZOOM,Math.round((float)Math.log(pinchLast/d)*1000),0);
                    pinchLast=d;
                }
            }
            for(int i=0;i<e.getPointerCount();++i) {
                int pid=e.getPointerId(i);float x=e.getX(i),y=e.getY(i);
                Control c=photoFingers.get(pid);
                if(c!=null){if(c.scan==PH_STICK)photoStick(c,x,y);continue;}
                float[] last=freeFingers.get(pid);
                if(last==null)continue;
                if(freeFingers.size()==1)photoInput(PHOTO_DRAG,Math.round((x-last[0])/h*10000),Math.round((y-last[1])/h*10000));
                last[0]=x;last[1]=y;
            }
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP||action==MotionEvent.ACTION_CANCEL) {
            Control c=photoFingers.remove(id);
            if(c!=null)photoLift(c,action!=MotionEvent.ACTION_CANCEL&&c==hit(e.getX(index),e.getY(index)));
            freeFingers.remove(id);pinchLast=-1;
            if(action!=MotionEvent.ACTION_POINTER_UP)photoRelease();
        }
        invalidate();return true;
    }
    private void clamp(Control c) {
        float w=getWidth()/scale,h=getHeight()/scale;
        float dx=c.area.left<4?4-c.area.left:c.area.right>w-4?w-4-c.area.right:0;
        float dy=c.area.top<4?4-c.area.top:c.area.bottom>h-4?h-4-c.area.bottom:0;
        c.area.offset(dx,dy);
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) { layoutControls(); }
    void setGameMode(boolean driving) {
        if(editing||lastGameMode==driving)return;
        lastGameMode=driving;racing=driving;layoutControls();
    }
    boolean isDriving() {return racing&&!editing&&!hiddenByGamepad;}
    /** A physical gamepad is connected: get out of the way until the screen is touched. */
    /** The pad was used while the controls were showing after a touch: get out of the way again. */
    void gamepadUsed() {
        if(!gamepadConnected||hiddenByGamepad||editing)return;
        releaseAll();hiddenByGamepad=true;invalidate();
    }
    void setGamepadConnected(boolean connected) {
        if(gamepadConnected==connected)return;
        gamepadConnected=connected;
        hiddenByGamepad=connected&&!editing;
        releaseAll();
    }
    private void layoutControls() {
        releaseAll(); controls.clear();selected=null;dragPointer=-1;
        if(getWidth()==0)return;
        scale=Math.min(getResources().getDisplayMetrics().density, getWidth()/820f);
        float w=getWidth()/scale, h=getHeight()/scale, bottom=h-20;
        if(photoOn) {layoutPhoto(w,h);invalidate();return;}
        if(style.equals("undrgnd2")) {layoutUndrgnd(w,h);invalidate();return;}
        add(w-62,16,46,"settings",EDIT);
        add(w-118,16,46,racing?"menu":"wheel",MODE);
        if(racing) {
            add(16,16,46,"pause",1);
            // Free-roam actions of the PC keyboard defaults (Opciones > Controles).
            add(72,16,46,"map",0x32);     // Mapa del mundo: M
            add(128,16,46,"sms",0x0f);    // Leer SMS: Tab
            add(184,16,46,"reset",0x13);  // Reiniciar coche: R
            add(240,16,46,"drift",0xF1);  // Mode drift en exploration (drift.c), aucune touche du jeu
            add(296,16,46,"ghost",0xF2);  // Mode fantôme en exploration (ghost.c), aucune touche du jeu
            if(style.equals("classic")) {
                add(20,bottom-92,92,"left",0xcb);
                add(124,bottom-92,92,"right",0xcd);
                add(228,bottom-66,62,"handbrake",0x39);
                add(30,bottom-160,52,"shiftUp",0x2a);
                add(92,bottom-160,52,"shiftDown",0x1d);
                add(154,bottom-160,52,"camera",0x2e);
                add(w-110,bottom-150,90,150,PEDAL,"gas",0xc8,0xFF1E7A4A);
                add(w-206,bottom-108,82,108,PEDAL,"brake",0xd0,0xFF8A2A2A);
                add(w-290,bottom-72,70,"nitro",0x38);
            } else {
                boolean xbox=style.equals("xbox");
                add(24,bottom-160,160,160,WHEEL,"stick",STICK,0);
                add(w-106,bottom-150,86,150,PEDAL,"gas",0xc8,0xFF1E7A4A);
                add(w-198,bottom-108,78,108,PEDAL,"brake",0xd0,0xFF8A2A2A);
                add(w-252,bottom-178,58,xbox?"A":"×",0x39);
                add(w-186,bottom-238,58,xbox?"B":"○",0x38);
                add(w-318,bottom-238,58,xbox?"X":"□",0x2e);
                add(w-252,bottom-298,58,xbox?"Y":"△",0x30);
                add(30,bottom-228,52,"shiftUp",0x2a);
                add(92,bottom-228,52,"shiftDown",0x1d);
            }
        } else {
            float pad=60,cx=24+pad*1.5f,cy=bottom-pad*1.5f;
            add(cx-pad/2,cy-pad*1.5f,pad,"up",0xc8);
            add(cx-pad/2,cy+pad*.5f,pad,"down",0xd0);
            add(cx-pad*1.5f,cy-pad/2,pad,"left",0xcb);
            add(cx+pad*.5f,cy-pad/2,pad,"right",0xcd);
            add(w-96,bottom-110,76,"accept",0x1c);
            add(w-176,bottom-62,56,"back",1);
            add(w/2-23,bottom-46,46,"keyboard",TEXT);
        }
        invalidate();
    }

    // ---- Drawing -----------------------------------------------------------------------------------------

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if(hiddenByGamepad&&!editing)return;
        canvas.save(); canvas.scale(scale,scale);
        if(editing)canvas.drawColor(0x55000000);
        for(Control c:controls)if(!c.hidden)drawControl(canvas,c);
        if(photoOn&&!editing) {
            float[] k=cropRect();float vw=getWidth()/scale,vh=getHeight()/scale;
            float l=k[0]*vw,t=k[1]*vh,r=k[2]*vw,b=k[3]*vh;
            fill.setColor(0xB3000000);
            if(t>0)canvas.drawRect(0,0,vw,t,fill);if(b<vh)canvas.drawRect(0,b,vw,vh,fill);
            if(l>0)canvas.drawRect(0,t,l,b,fill);if(r<vw)canvas.drawRect(r,t,vw,b,fill);
            if(layouts.getBoolean("photo.grid",false)) {
                line.setStrokeWidth(1);line.setColor(0x99FFFFFF);
                for(int i=1;i<3;++i){float x=l+(r-l)*i/3f,y=t+(b-t)*i/3f;canvas.drawLine(x,t,x,b,line);canvas.drawLine(l,y,r,y,line);}
            }
            label.setTextSize(14);label.setColor(0xE6FFFFFF);
            canvas.drawText("MODE PHOTO · glisse pour tourner · pince pour zoomer · zoom "+photoState[1]/10+"°"+(photoState[2]!=0?" · inclinaison "+photoState[2]/10+"°":""),
                    getWidth()/scale/2,78,label);
        }
        drawDrift(canvas);
        drawGhost(canvas);
        if(editing) {
            float w=getWidth()/scale;
            float tb=toolbarTop();
            drawToolbarButton(canvas,new RectF(w/2-250,tb,w/2-150,tb+40),"Défaut");
            drawToolbarButton(canvas,new RectF(w/2-142,tb,w/2-102,tb+40),"−");
            drawToolbarButton(canvas,new RectF(w/2-94,tb,w/2-54,tb+40),"+");
            drawToolbarButton(canvas,new RectF(w/2-46,tb,w/2+58,tb+40),"Valider");
            drawToolbarButton(canvas,new RectF(w/2+66,tb,w/2+170,tb+40),"Annuler");
            drawToolbarButton(canvas,new RectF(w/2-65,tb+52,w/2+65,tb+88),"Menu");
            label.setTextSize(13);label.setColor(0xCCFFFFFF);
            canvas.drawText(selected==null?"Touche un bouton pour le déplacer, − et + pour sa taille":"Glisse pour déplacer · − et + pour la taille",w/2,tb+110,label);
        }
        canvas.restore();
    }
    /** undrgnd2.eu's look: see-through black, white words, lime when pressed or lit. */
    private void drawUndrgnd(Canvas canvas,Control c) {
        boolean on=c.held>0, picked=editing&&c==selected;
        boolean lit=(c.scan==0xF4&&ghost.length>4&&ghost[4]==2)||(c.scan==0xF1&&driftOn())||(c.scan==0xF2&&(ghost[0]&1)!=0)||(c.scan==HYD_LIFT&&hyd==1)||(c.scan==HYD_BOUNCE&&hyd==2);
        boolean small=c.scan<0||c.shape==ROUND&&c.group==0&&c.area.top<40;
        float cx=c.area.centerX(),cy=c.area.centerY(),r=Math.min(c.area.width(),c.area.height())/2;
        int bg=on?0xB8000000:racing&&c.scan>=0?0x33000000:0x66000000;
        int ink=on||lit?LIME:racing&&c.scan>=0?0x8CFFFFFF:0xB8FFFFFF;
        fill.setColor(bg);
        if(c.shape==WHEEL) {
            canvas.drawCircle(cx,cy,r,fill);
            line.setStrokeWidth(2);line.setColor(picked?0xFFFFCF69:0x29FFFFFF);canvas.drawCircle(cx,cy,r-1,line);
            float knob=26,kx=cx+c.steering*(r-knob),ky=cy+c.steerY*(r-knob);
            fill.setColor(on?0xB8000000:0x33000000);canvas.drawCircle(kx,ky,knob,fill);
            line.setColor(on?LIME:0x5CFFFFFF);canvas.drawCircle(kx,ky,knob-1,line);
            return;
        }
        float corner=c.shape==RECT?16:c.shape==KEY?12:c.shape==PILL?c.area.height()/2:r;
        if(c.shape==RECT||c.shape==KEY||c.shape==PILL)canvas.drawRoundRect(c.area,corner,corner,fill);
        else canvas.drawCircle(cx,cy,r,fill);
        if(lit||picked||!racing) {
            line.setStrokeWidth(lit||picked?2:1.5f);line.setColor(picked?0xFFFFCF69:lit?LIME:0x33FFFFFF);
            if(c.shape==RECT||c.shape==KEY||c.shape==PILL)canvas.drawRoundRect(c.area,corner,corner,line);
            else canvas.drawCircle(cx,cy,r-line.getStrokeWidth()/2,line);
        }
        if(c.icon.startsWith("t:")) {
            String[] lines=c.icon.substring(2).split("\n");
            float size=lines.length>1?12:c.shape==ROUND?14:16;
            label.setTextSize(size);label.setColor(ink);
            float lh=size*1.15f,y0=cy-(lines.length-1)*lh/2;
            for(int i=0;i<lines.length;++i)canvas.drawText(lines[i],cx,y0+i*lh-(label.ascent()+label.descent())/2,label);
            return;
        }
        if(c.scan==0xF1||c.scan==0xF2) {
            label.setTextSize(r*.42f);label.setColor(ink);
            canvas.drawText(c.scan==0xF1?"DRIFT":"GHOST",cx,cy-(label.ascent()+label.descent())/2,label);
            return;
        }
        line.setColor(ink);fill.setColor(ink);line.setStrokeWidth(Math.max(2f,(small?24:32)*.09f));
        if(c.icon.equals("lights")) {   // a headlight: its lens and beams; struck through while the lights are off
            float s=r*.42f;
            path.reset();path.moveTo(cx-s*.15f,cy-s*.7f);path.cubicTo(cx-s*1.05f,cy-s*.7f,cx-s*1.05f,cy+s*.7f,cx-s*.15f,cy+s*.7f);path.close();
            canvas.drawPath(path,line);
            boolean high=ghost.length>4&&ghost[4]==2;
            for(int i=-1;i<=1;++i){float y=cy+i*s*.45f;canvas.drawLine(cx+s*.15f,y,cx+s*.95f,high?y-s*.25f:y+s*.2f,line);}
            if(ghost.length>4&&ghost[4]==1)canvas.drawLine(cx-s*.95f,cy+s*.95f,cx+s*.95f,cy-s*.95f,line);
            return;
        }
        if(c.icon.equals("photo")) {   // a still camera: body, lens, flash
            float s=r*.42f;
            canvas.drawRoundRect(cx-s,cy-s*.55f,cx+s,cy+s*.7f,s*.2f,s*.2f,line);
            canvas.drawCircle(cx,cy+s*.08f,s*.36f,line);
            canvas.drawRect(cx-s*.35f,cy-s*.8f,cx+s*.2f,cy-s*.55f,fill);
            canvas.drawCircle(cx+s*.65f,cy-s*.3f,s*.09f,fill);
            return;
        }
        if(c.icon.equals("status")) {   // bars rising, as the pause menu's statistics icon
            float s=r*.42f,base=cy+s*.75f,bw=s*.42f;
            fill.setColor(ink);
            for(int i=0;i<3;++i){float x0=cx-s*.8f+i*(bw+s*.17f),top=base-s*(.6f+.45f*i);canvas.drawRect(x0,top,x0+bw,base,fill);}
            return;
        }
        if(c.icon.equals("lift")||c.icon.equals("bounce")) {
            float s=r*.42f,base=cy+s*.35f;
            canvas.drawLine(cx-s,base,cx+s,base,line);
            path.reset();path.moveTo(cx-s*.75f,base);path.lineTo(cx-s*.45f,base-s*.4f);path.lineTo(cx+s*.45f,base-s*.4f);path.lineTo(cx+s*.75f,base);
            canvas.drawPath(path,line);
            canvas.drawCircle(cx-s*.55f,base+s*.38f,s*.17f,line);canvas.drawCircle(cx+s*.55f,base+s*.38f,s*.17f,line);
            if(c.icon.equals("lift")) {
                canvas.drawLine(cx,base-s*.55f,cx,cy-s*1.05f,line);
                path.reset();path.moveTo(cx-s*.32f,cy-s*.72f);path.lineTo(cx,cy-s*1.05f);path.lineTo(cx+s*.32f,cy-s*.72f);canvas.drawPath(path,line);
            } else {
                canvas.drawArc(cx-s*.85f,cy-s*1.05f,cx-s*.15f,cy-s*.45f,180,180,false,line);
                canvas.drawArc(cx+s*.15f,cy-s*1.05f,cx+s*.85f,cy-s*.45f,180,180,false,line);
            }
            return;
        }
        if(c.icon.equals("stick")){canvas.drawCircle(cx,cy,r*.5f,line);canvas.drawCircle(cx,cy,r*.18f,fill);return;}
        if(c.icon.equals("buttons")){chevronSide(canvas,cx-r*.22f,cy,r*.28f,true);chevronSide(canvas,cx+r*.22f,cy,r*.28f,false);return;}
        drawIcon(canvas,c,cx,cy,c.shape==RECT||c.shape==KEY?Math.min(r,34):r,on);
    }
    private void drawControl(Canvas canvas,Control c) {
        if(style.equals("undrgnd2")||photoOn){drawUndrgnd(canvas,c);return;}
        boolean on=c.held>0, picked=editing&&c==selected;
        float cx=c.area.centerX(),cy=c.area.centerY(),r=Math.min(c.area.width(),c.area.height())/2;
        int ink=on?ACCENT:0xE6FFFFFF;
        line.setStrokeWidth(Math.max(1.6f,r*.06f));
        if(c.shape==PEDAL) {
            float corner=c.area.width()*.28f;
            fill.setColor(on?0xD0FF9A32:(c.color&0x00FFFFFF)|0x99000000);
            canvas.drawRoundRect(c.area,corner,corner,fill);
            line.setColor(picked?0xFFFFCF69:on?0xFFFFD18A:0x99FFFFFF);
            canvas.drawRoundRect(c.area,corner,corner,line);
            // Grip ridges like a real pedal, then the direction it drives.
            line.setColor(on?0xFFFFE2B8:0x66FFFFFF);
            float span=c.area.width()*.28f;
            for(int i=0;i<4;++i){float y=c.area.top+c.area.height()*(.52f+i*.11f);canvas.drawLine(cx-span,y,cx+span,y,line);}
            line.setColor(ink);line.setStrokeWidth(Math.max(2.4f,c.area.width()*.06f));
            chevron(canvas,cx,c.area.top+c.area.height()*.27f,c.area.width()*.22f,c.icon.equals("gas"));
            return;
        }
        fill.setColor(0x55000000);canvas.drawCircle(cx,cy+r*.06f,r,fill);
        fill.setColor(on?0xD0FF9A32:0x8C141C26);canvas.drawCircle(cx,cy,r,fill);
        int ring=faceColor(c);
        line.setColor(picked?0xFFFFCF69:on?0xFFFFD18A:ring!=0?ring:0x8CFFFFFF);
        canvas.drawCircle(cx,cy,r-line.getStrokeWidth()/2,line);
        if(c.shape==WHEEL) {
            fill.setColor(on?ACCENT:0xCC3A4654);
            float knob=r*.4f,kx=cx+c.steering*(r-knob);
            canvas.drawCircle(kx,cy,knob,fill);line.setColor(0xB3FFFFFF);canvas.drawCircle(kx,cy,knob,line);
            line.setColor(0x80FFFFFF);chevronSide(canvas,cx-r*.78f,cy,r*.12f,true);chevronSide(canvas,cx+r*.78f,cy,r*.12f,false);
            return;
        }
        line.setColor(ink);fill.setColor(ink);line.setStrokeWidth(Math.max(2.2f,r*.1f));
        drawIcon(canvas,c,cx,cy,r,on);
    }
    private int faceColor(Control c) {
        if(!racing||style.equals("classic")||c.icon.length()!=1)return 0;
        if(c.scan==0x39)return style.equals("xbox")?0xff74df8e:0xff80b8ff;
        if(c.scan==0x38)return 0xffff7f86;
        if(c.scan==0x2e)return style.equals("xbox")?0xff80b8ff:0xffdd9cf4;
        return style.equals("xbox")?0xffffd778:0xff74dfb3;
    }
    private void drawIcon(Canvas canvas,Control c,float cx,float cy,float r,boolean on) {
        float s=r*.42f;
        switch(c.icon) {
            case "up": chevron(canvas,cx,cy,s,true);break;
            case "down": chevron(canvas,cx,cy,s,false);break;
            case "left": chevronSide(canvas,cx,cy,s,true);break;
            case "right": chevronSide(canvas,cx,cy,s,false);break;
            case "shiftUp": chevron(canvas,cx,cy-s*.3f,s*.8f,true);chevron(canvas,cx,cy+s*.35f,s*.8f,true);break;
            case "shiftDown": chevron(canvas,cx,cy-s*.35f,s*.8f,false);chevron(canvas,cx,cy+s*.3f,s*.8f,false);break;
            case "pause":
                canvas.drawRoundRect(cx-s*.6f,cy-s*.7f,cx-s*.18f,cy+s*.7f,s*.12f,s*.12f,fill);
                canvas.drawRoundRect(cx+s*.18f,cy-s*.7f,cx+s*.6f,cy+s*.7f,s*.12f,s*.12f,fill);break;
            case "settings":
                for(int i=-1;i<=1;++i){float y=cy+i*s*.62f,k=cx+(i==0?s*.35f:-s*.35f);
                    canvas.drawLine(cx-s*.85f,y,cx+s*.85f,y,line);canvas.drawCircle(k,y,s*.22f,fill);}break;
            case "menu":
                for(int i=0;i<4;++i){float x=cx+(i%2==0?-s*.5f:s*.5f),y=cy+(i<2?-s*.5f:s*.5f);
                    canvas.drawRoundRect(x-s*.36f,y-s*.36f,x+s*.36f,y+s*.36f,s*.1f,s*.1f,fill);}break;
            case "wheel":
                canvas.drawCircle(cx,cy,s*.9f,line);canvas.drawCircle(cx,cy,s*.2f,fill);
                canvas.drawLine(cx-s*.9f,cy,cx+s*.9f,cy,line);canvas.drawLine(cx,cy,cx,cy+s*.9f,line);break;
            case "camera":
                canvas.drawRoundRect(cx-s*.85f,cy-s*.45f,cx+s*.85f,cy+s*.6f,s*.2f,s*.2f,line);
                canvas.drawRect(cx-s*.35f,cy-s*.7f,cx+s*.1f,cy-s*.45f,fill);canvas.drawCircle(cx,cy+s*.08f,s*.3f,line);break;
            case "handbrake":
                canvas.drawCircle(cx,cy,s*.62f,line);
                canvas.drawArc(cx-s*.95f,cy-s*.95f,cx+s*.95f,cy+s*.95f,130,100,false,line);
                canvas.drawArc(cx-s*.95f,cy-s*.95f,cx+s*.95f,cy+s*.95f,-50,100,false,line);
                glyph(canvas,"P",cx,cy,s*.9f,on);break;
            case "nitro": glyph(canvas,"N₂O",cx,cy,s*.9f,on);break;
            case "drift": glyph(canvas,"DRIFT",cx,cy,s*.55f,on,driftOn()?0xFF3FC1F5:0);break;
            case "ghost": glyph(canvas,"GHOST",cx,cy,s*.5f,on,(ghost[0]&1)!=0?0xFF3FC1F5:0);break;
            case "accept":
                path.reset();path.moveTo(cx-s*.7f,cy);path.lineTo(cx-s*.15f,cy+s*.55f);path.lineTo(cx+s*.75f,cy-s*.5f);
                canvas.drawPath(path,line);break;
            case "map":
                path.reset();path.moveTo(cx-s*.9f,cy-s*.55f);path.lineTo(cx-s*.3f,cy-s*.8f);path.lineTo(cx+s*.3f,cy-s*.55f);
                path.lineTo(cx+s*.9f,cy-s*.8f);path.lineTo(cx+s*.9f,cy+s*.55f);path.lineTo(cx+s*.3f,cy+s*.8f);
                path.lineTo(cx-s*.3f,cy+s*.55f);path.lineTo(cx-s*.9f,cy+s*.8f);path.close();canvas.drawPath(path,line);
                canvas.drawLine(cx-s*.3f,cy-s*.8f,cx-s*.3f,cy+s*.55f,line);canvas.drawLine(cx+s*.3f,cy-s*.55f,cx+s*.3f,cy+s*.8f,line);break;
            case "sms":
                canvas.drawRoundRect(cx-s*.9f,cy-s*.7f,cx+s*.9f,cy+s*.4f,s*.25f,s*.25f,line);
                path.reset();path.moveTo(cx-s*.45f,cy+s*.4f);path.lineTo(cx-s*.6f,cy+s*.85f);path.lineTo(cx-s*.05f,cy+s*.4f);
                canvas.drawPath(path,line);
                for(int dot=-1;dot<=1;++dot)canvas.drawCircle(cx+dot*s*.4f,cy-s*.15f,s*.1f,fill);break;
            case "reset":
                canvas.drawArc(cx-s*.75f,cy-s*.75f,cx+s*.75f,cy+s*.75f,-60,290,false,line);
                path.reset();path.moveTo(cx+s*.2f,cy-s*.95f);path.lineTo(cx+s*.55f,cy-s*.62f);path.lineTo(cx+s*.08f,cy-s*.42f);
                canvas.drawPath(path,line);break;
            case "back":
                canvas.drawLine(cx-s*.5f,cy-s*.5f,cx+s*.5f,cy+s*.5f,line);canvas.drawLine(cx+s*.5f,cy-s*.5f,cx-s*.5f,cy+s*.5f,line);break;
            case "keyboard":
                canvas.drawRoundRect(cx-s*.95f,cy-s*.6f,cx+s*.95f,cy+s*.6f,s*.15f,s*.15f,line);
                for(int row=0;row<2;++row)for(int col=0;col<4;++col)
                    canvas.drawCircle(cx-s*.54f+col*s*.36f,cy-s*.22f+row*s*.3f,s*.07f,fill);
                canvas.drawLine(cx-s*.4f,cy+s*.34f,cx+s*.4f,cy+s*.34f,line);break;
            default: {
                int color=faceColor(c);
                glyph(canvas,c.icon,cx,cy,r*(c.icon.length()==1?1.0f:.6f),on,color!=0&&!on?color:0);
            }
        }
    }
    private void chevron(Canvas canvas,float x,float y,float s,boolean up) {
        float d=up?-1:1;path.reset();path.moveTo(x-s,y-d*s*.45f);path.lineTo(x,y+d*s*.45f);path.lineTo(x+s,y-d*s*.45f);
        canvas.drawPath(path,line);
    }
    private void chevronSide(Canvas canvas,float x,float y,float s,boolean left) {
        float d=left?-1:1;path.reset();path.moveTo(x-d*s*.45f,y-s);path.lineTo(x+d*s*.45f,y);path.lineTo(x-d*s*.45f,y+s);
        canvas.drawPath(path,line);
    }
    private void glyph(Canvas canvas,String value,float x,float y,float size,boolean on) {glyph(canvas,value,x,y,size,on,0);}
    private void glyph(Canvas canvas,String value,float x,float y,float size,boolean on,int color) {
        text.setTextSize(size);text.setColor(color!=0?color:on?ACCENT:0xF0FFFFFF);
        canvas.drawText(value,x,y-(text.ascent()+text.descent())/2,text);
    }
    // Drift mode (nativeDriftStatus): flags (1 chosen, 2 on, 4 pressed outside free roam), multiplier,
    // next level's points, total, best, current drift, its state.
    private int[] drift=new int[7];
    void setDrift(int[] status) {
        if(status==null||status.length<7||java.util.Arrays.equals(status,drift))return;
        drift=status;if(racing)invalidate();
    }
    private boolean driftOn() {return (drift[0]&1)!=0;}
    private void drawDrift(Canvas canvas) {
        boolean note=(drift[0]&4)!=0;
        if(!racing||editing||photoOn||(!driftOn()&&!note))return;
        float w=getWidth()/scale,cx=w/2,y=74;
        String head=note?"Mode drift : uniquement en exploration":(drift[0]&2)!=0?"DRIFT  ×"+drift[1]:"DRIFT  (en pause)";
        String body=note?"":drift[5]>0?"+"+fmt(drift[5]):"Total "+fmt(drift[3])+"   ·   Record "+fmt(drift[4]);
        text.setTextSize(note?14:20);float hw=Math.max(text.measureText(head),body.isEmpty()?0:text.measureText(body)*.8f)/2+18;
        fill.setColor(0xB00A1628);canvas.drawRoundRect(cx-hw,y-4,cx+hw,y+(body.isEmpty()?26:50),10,10,fill);
        line.setColor(0x993FC1F5);line.setStrokeWidth(1.5f);canvas.drawRoundRect(cx-hw,y-4,cx+hw,y+(body.isEmpty()?26:50),10,10,line);
        glyph(canvas,head,cx,y+11,note?14:20,false,0xFF3FC1F5);
        if(!body.isEmpty())glyph(canvas,body,cx,y+36,drift[5]>0?18:13,false,drift[5]>0?0xFFFFFFFF:0xCCFFFFFF);
    }
    // Ghost mode (nativeGhostStatus): flags (1 chosen, 2 active, 4 pressed outside free roam, 8 nitrous gained),
    // cars passed through, nitrous per car (%).
    private int[] ghost=new int[3];
    void setGhost(int[] status) {
        if(status==null||status.length<3||java.util.Arrays.equals(status,ghost))return;
        boolean lightsChanged=status.length>4&&(ghost.length<5||ghost[4]!=status[4]);
        if(lightsChanged&&racing)invalidate();
        ghost=status;
        int state=status.length>3?status[3]:0;
        if(state!=hydState) {
            int was=hydState;hydState=state;
            if(state==0)setHyd(0);
            // the game starts them only at 10 mph or less, a moment after the key goes down: pressed again then
            else if(state==2&&was!=2&&hyd!=0){int key=hyd==1?KEY_LIFT:KEY_BOUNCE;lift(key);press(key);}
            boolean show=state!=0||hyd!=0;
            for(Control c:controls)if(c.scan==HYD_LIFT||c.scan==HYD_BOUNCE)c.hidden=!show;
        }
        if(racing)invalidate();
    }
    /** Pressurize (1), Bounce (2) or neither (0): a tap turns one on and holds its key, as a keyboard player holds
     *  H or J, until a tap turns it off or the other on (nfsu2-web touch.ts setHyd). */
    private int hyd, hydState;
    private void setHyd(int to) {
        if(to==hyd)return;
        if(hyd!=0)lift(hyd==1?KEY_LIFT:KEY_BOUNCE);
        hyd=to;
        if(hyd!=0)press(hyd==1?KEY_LIFT:KEY_BOUNCE);
        invalidate();
    }
    private void drawGhost(Canvas canvas) {
        boolean note=(ghost[0]&4)!=0;
        if(!racing||editing||photoOn||((ghost[0]&1)==0&&!note))return;
        float w=getWidth()/scale,x=w-24,y=bottomOfDrift();
        String head=note?"Mode fantôme : uniquement en exploration":(ghost[0]&8)!=0?"+"+ghost[2]+" % N₂O":"FANTÔME";
        text.setTextSize(15);float tw=text.measureText(head)/2+14,cx=note?w/2:x-tw;
        fill.setColor((ghost[0]&8)!=0?0xD01E7A4A:0xB00A1628);canvas.drawRoundRect(cx-tw,y,cx+tw,y+26,10,10,fill);
        line.setColor(0x993FC1F5);line.setStrokeWidth(1.5f);canvas.drawRoundRect(cx-tw,y,cx+tw,y+26,10,10,line);
        glyph(canvas,head,cx,y+13,15,false,(ghost[0]&8)!=0?0xFFFFFFFF:0xFF3FC1F5);
    }
    private float bottomOfDrift() {return 74;}
    private static String fmt(int value) {return String.format(java.util.Locale.FRANCE,"%,d",value);}
    /** The layout editor's toolbar, in the middle of the screen's height: clear of the small buttons along the top
     *  and of the pedals and steering along the bottom, so each can be picked and moved. */
    private float toolbarTop() {return getHeight()/scale/2-80;}
    private void drawToolbarButton(Canvas canvas,RectF area,String name) {
        fill.setColor(NAVY);canvas.drawRoundRect(area,area.height()/2,area.height()/2,fill);
        line.setStrokeWidth(1.5f);line.setColor(0x663FC1F5);canvas.drawRoundRect(area,area.height()/2,area.height()/2,line);
        label.setColor(LIME);label.setTextSize(name.length()>2?15:24);
        canvas.drawText(name,area.centerX(),area.centerY()-(label.ascent()+label.descent())/2,label);
    }

    // ---- Editor ------------------------------------------------------------------------------------------

    private void resizeSelected(float change) {
        if(selected==null)return;
        float factor=Math.max(.55f,Math.min(2.5f,selected.area.width()/selected.original.width()+change));
        float cx=selected.area.centerX(),cy=selected.area.centerY();
        float w=selected.original.width()*factor,h=selected.original.height()*factor;
        selected.area.set(cx-w/2,cy-h/2,cx+w/2,cy+h/2);clamp(selected);
    }
    private void saveLayout() {
        SharedPreferences.Editor save=layouts.edit();
        for(Control c:controls)save.putFloat(c.id+".x",c.area.centerX()/(getWidth()/scale))
                .putFloat(c.id+".y",c.area.centerY()/(getHeight()/scale))
                .putFloat(c.id+".size",c.area.width()/c.original.width());
        save.apply();
    }
    private boolean editTouch(MotionEvent e) {
        int action=e.getActionMasked(),index=e.getActionIndex();
        float x=e.getX(index)/scale,y=e.getY(index)/scale,w=getWidth()/scale;
        if(action==MotionEvent.ACTION_DOWN) {
            float tb=toolbarTop();
            if(x>=w/2-65&&x<=w/2+65&&y>=tb+52&&y<=tb+88) {showMenu();return true;}
            if(y>=tb&&y<=tb+40) {
                boolean handled=true;
                if(x>=w/2-250&&x<=w/2-150) {for(Control c:controls)c.area.set(c.original);selected=null;}
                else if(x>=w/2-142&&x<=w/2-102)resizeSelected(-.1f);
                else if(x>=w/2-94&&x<=w/2-54)resizeSelected(.1f);
                else if(x>=w/2-46&&x<=w/2+58) {saveLayout();editing=false;layoutControls();}
                else if(x>=w/2+66&&x<=w/2+170) {editing=false;layoutControls();}
                else handled=false;
                if(handled) {invalidate();return true;}
            }
            selected=hit(e.getX(index),e.getY(index));
            dragPointer=e.getPointerId(index);dragX=x;dragY=y;
        } else if(action==MotionEvent.ACTION_MOVE&&selected!=null&&dragPointer>=0) {
            int i=e.findPointerIndex(dragPointer);
            if(i>=0) {x=e.getX(i)/scale;y=e.getY(i)/scale;selected.area.offset(x-dragX,y-dragY);clamp(selected);dragX=x;dragY=y;}
        } else if(action==MotionEvent.ACTION_CANCEL||action==MotionEvent.ACTION_UP||
                (action==MotionEvent.ACTION_POINTER_UP&&e.getPointerId(index)==dragPointer))dragPointer=-1;
        invalidate();return true;
    }
    // ---- Menu (undrgnd2.eu's: a navy panel, "Menu" in blue and Fermer, a row per setting with its value) ----

    private int px(float dp) {return Math.round(dp*getResources().getDisplayMetrics().density);}
    private android.app.Dialog menu;
    private void showMenu() {
        if(menu!=null&&menu.isShowing())menu.dismiss();
        Context ctx=getContext();
        android.widget.LinearLayout panel=new android.widget.LinearLayout(ctx);panel.setOrientation(android.widget.LinearLayout.VERTICAL);
        android.graphics.drawable.GradientDrawable bg=new android.graphics.drawable.GradientDrawable();
        bg.setColor(NAVY);bg.setCornerRadius(px(14));bg.setStroke(px(1.5f),0x663FC1F5);panel.setBackground(bg);
        android.widget.LinearLayout head=new android.widget.LinearLayout(ctx);head.setGravity(android.view.Gravity.CENTER_VERTICAL);
        head.setPadding(px(22),px(14),px(22),px(14));
        android.widget.TextView title=menuText("Menu",22,LIME,true);
        head.addView(title,new android.widget.LinearLayout.LayoutParams(0,-2,1));
        android.widget.TextView close=menuText("Fermer",17,0xE6FFFFFF,false);head.addView(close);
        panel.addView(head);panel.addView(menuRule());
        android.widget.LinearLayout rows=new android.widget.LinearLayout(ctx);rows.setOrientation(android.widget.LinearLayout.VERTICAL);
        android.widget.ScrollView scroll=new android.widget.ScrollView(ctx);scroll.addView(rows);
        panel.addView(scroll,new android.widget.LinearLayout.LayoutParams(-1,0,1));
        android.app.Dialog dialog=new android.app.Dialog(ctx);menu=dialog;
        dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE);
        boolean stick=layouts.getString("steer","buttons").equals("stick"),gears=layouts.getBoolean("gears",false);
        String styleName=style.equals("undrgnd2")?"undrgnd2.eu":style.equals("classic")?"Classique":style.equals("xbox")?"Xbox":"PlayStation";
        menuRow(rows,"Mode drift",driftOn()?"Activé":"Désactivé",0,()->{tap(0xF1);});
        menuRow(rows,"Mode fantôme",(ghost[0]&1)!=0?"Activé":"Désactivé",0,()->{tap(0xF2);});
        menuRow(rows,"Mode photo","En exploration ou en course",0,()->{if(photo!=null)photo.input(PHOTO_OPEN,1,0);});
        rows.addView(menuRule());
        menuRow(rows,"Direction",stick?"Joystick":"Boutons ◀ ▶",0,()->{
            layouts.edit().putString("steer",stick?"buttons":"stick").apply();layoutControls();});
        menuRow(rows,"Rapports (boîte manuelle)",gears?"Affichés":"Masqués",0,()->{
            layouts.edit().putBoolean("gears",!gears).apply();layoutControls();});
        menuRow(rows,"Inclinaison",null,0,configureTilt);
        menuRow(rows,"Style des commandes",styleName,0,this::chooseStyle);
        menuRow(rows,"Déplacer les commandes",null,0,()->{releaseAll();editing=true;selected=null;invalidate();});
        menuRow(rows,"Dispositions","Enregistrer · Charger",0,()->new AlertDialog.Builder(getContext()).setTitle("Dispositions")
                .setItems(new String[]{"Enregistrer la disposition","Charger une disposition"},(d,which)->{if(which==0)saveNamedLayout();else loadNamedLayout();}).show());
        rows.addView(menuRule());
        menuRow(rows,"Limite de FPS",null,0,configureFrameLimit);
        menuRow(rows,"Quitter le jeu",null,0xFFFF7A6E,()->{if(ctx instanceof android.app.Activity)((android.app.Activity)ctx).finish();});
        android.widget.TextView foot=menuText("undrgnd2.eu · le menu du jeu reste sur Pause",13,0x99FFFFFF,false);
        foot.setPadding(px(22),px(12),px(22),px(14));panel.addView(menuRule());panel.addView(foot);
        close.setOnClickListener(v->dialog.dismiss());
        dialog.setContentView(panel);
        android.view.Window win=dialog.getWindow();
        if(win!=null) {
            win.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(0));
            int w=Math.min(px(420),getWidth()-px(32)),h=getHeight()-px(24);
            win.setLayout(w,h);win.setDimAmount(.55f);
            // Keep the game's full screen: no system bars for the dialog's sake.
            win.setFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            win.getDecorView().setSystemUiVisibility(getSystemUiVisibility());
        }
        dialog.show();
        if(win!=null)win.clearFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
    }
    /** a press and release of one of the game's keys (or drift.c / ghost.c's buttons) */
    private void tap(int scan) {keys.set(scan,true);keys.set(scan,false);}
    private android.widget.TextView menuText(String value,float sp,int color,boolean bold) {
        android.widget.TextView v=new android.widget.TextView(getContext());v.setText(value);v.setTextSize(sp);v.setTextColor(color);
        if(bold)v.setTypeface(Typeface.DEFAULT_BOLD);return v;
    }
    private View menuRule() {
        View v=new View(getContext());v.setBackgroundColor(0x333FC1F5);
        v.setLayoutParams(new android.widget.LinearLayout.LayoutParams(-1,px(1)));return v;
    }
    /** a row: its name, its value (blue, at the right) or none; run then closes the menu */
    private void menuRow(android.widget.LinearLayout rows,String name,String value,int color,Runnable run) {
        android.widget.LinearLayout row=new android.widget.LinearLayout(getContext());row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        row.setPadding(px(22),px(13),px(22),px(13));
        android.util.TypedValue ripple=new android.util.TypedValue();
        getContext().getTheme().resolveAttribute(android.R.attr.selectableItemBackground,ripple,true);
        row.setBackgroundResource(ripple.resourceId);
        row.addView(menuText(name,18,color!=0?color:0xFFFFFFFF,false),new android.widget.LinearLayout.LayoutParams(0,-2,1));
        if(value!=null)row.addView(menuText(value,17,MENU_VALUE,false));
        row.setOnClickListener(v->{if(menu!=null)menu.dismiss();run.run();});
        rows.addView(row);
    }
    private void showOptions() {
        new AlertDialog.Builder(getContext()).setTitle("Options des commandes")
                .setItems(new String[]{"Style : undrgnd2.eu / classique / Xbox / PlayStation","Conduite par inclinaison","Limite de FPS",
                        layouts.getBoolean("gears",false)?"Masquer les rapports (boîte auto)":"Afficher les rapports (boîte manuelle)",
                        "Enregistrer la disposition","Charger une disposition"},(dialog,which)->{
                    if(which==0)chooseStyle();else if(which==1)configureTilt.run();
                    else if(which==2)configureFrameLimit.run();
                    else if(which==3){layouts.edit().putBoolean("gears",!layouts.getBoolean("gears",false)).apply();editing=false;layoutControls();}
                    else if(which==4)saveNamedLayout();else loadNamedLayout();
                }).setNegativeButton("Fermer",null).show();
    }
    private void chooseStyle() {
        String[] ids={"undrgnd2","classic","xbox","playstation"};
        new AlertDialog.Builder(getContext()).setTitle("Style de conduite")
                .setSingleChoiceItems(new String[]{"undrgnd2.eu · comme sur le site","Classique · flèches et pédales","Xbox · volant et A / B / X / Y","PlayStation · volant et × / ○ / □ / △"},java.util.Arrays.asList(ids).indexOf(style),(dialog,which)->{
                    style=ids[which];layouts.edit().putString("style",style).apply();
                    layoutControls();dialog.dismiss();
                }).setNegativeButton("Annuler",null).show();
    }
    private void saveNamedLayout() {
        EditText name=new EditText(getContext());name.setSingleLine();name.setHint("Nom de la disposition");
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(24)});
        AlertDialog dialog=new AlertDialog.Builder(getContext()).setTitle("Enregistrer la disposition "+(racing?"de conduite":"du menu"))
                .setView(name).setPositiveButton("Enregistrer",null).setNegativeButton("Annuler",null).create();
        dialog.setOnShowListener(ignored->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{
            String value=name.getText().toString().trim();if(value.isEmpty()) {name.setError("Écris un nom");return;}
            try {
                JSONObject data=new JSONObject();data.put("style",style);
                for(Control c:controls) {JSONObject item=new JSONObject();
                    item.put("x",c.area.centerX()/(getWidth()/scale));item.put("y",c.area.centerY()/(getHeight()/scale));
                    item.put("size",c.area.width()/c.original.width());data.put(c.id,item);}
                layouts.edit().putString("preset."+(racing?"race.":"menu.")+value,data.toString()).apply();
                saveLayout();Toast.makeText(getContext(),"Layout guardado: "+value,Toast.LENGTH_SHORT).show();dialog.dismiss();
            }catch(org.json.JSONException exception) {name.setError("Impossible d'enregistrer la disposition");}
        }));dialog.show();
    }
    private void loadNamedLayout() {
        String prefix="preset."+(racing?"race.":"menu.");ArrayList<String> names=new ArrayList<>();
        for(String key:layouts.getAll().keySet())if(key.startsWith(prefix))names.add(key.substring(prefix.length()));
        java.util.Collections.sort(names);
        if(names.isEmpty()) {Toast.makeText(getContext(),"Aucune disposition enregistrée pour ce mode",Toast.LENGTH_SHORT).show();return;}
        new AlertDialog.Builder(getContext()).setTitle("Charger une disposition").setItems(names.toArray(new String[0]),(dialog,which)->{
            try {
                JSONObject data=new JSONObject(layouts.getString(prefix+names.get(which),"{}"));
                style=data.getString("style");SharedPreferences.Editor save=layouts.edit().putString("style",style);
                java.util.Iterator<String> ids=data.keys();while(ids.hasNext()) {String id=ids.next();if(id.equals("style"))continue;
                    JSONObject item=data.getJSONObject(id);save.putFloat(id+".x",(float)item.getDouble("x"))
                            .putFloat(id+".y",(float)item.getDouble("y")).putFloat(id+".size",(float)item.getDouble("size"));}
                save.apply();layoutControls();
            }catch(org.json.JSONException exception) {Toast.makeText(getContext(),"Impossible de charger la disposition",Toast.LENGTH_SHORT).show();}
        }).setNegativeButton("Annuler",null).show();
    }

    // ---- Input -------------------------------------------------------------------------------------------

    /** The control under a point; round controls use their circle, with a little slack. */
    private Control hit(float x,float y) {
        x/=scale;y/=scale;Control best=null;double bestDistance=Double.MAX_VALUE;
        for(Control c:controls) {
            if(c.hidden)continue;
            float cx=c.area.centerX(),cy=c.area.centerY();
            double d;
            if(c.shape==PEDAL||c.shape>=RECT) {
                if(Math.abs(x-cx)>c.area.width()*.56f||Math.abs(y-cy)>c.area.height()*.56f)continue;
                d=Math.hypot((x-cx)/c.area.width(),(y-cy)/c.area.height());
            } else {
                float r=Math.min(c.area.width(),c.area.height())/2;
                d=Math.hypot(x-cx,y-cy)/r;if(d>(c.shape==WHEEL?1.15:1.12))continue;
            }
            if(d<bestDistance){bestDistance=d;best=c;}
        }
        return best;
    }
    private void press(int scan) { if(scan>=0&&scan<256&&keyCount[scan]++==0)keys.set(scan,true); }
    private void lift(int scan) { if(scan>=0&&scan<256&&keyCount[scan]>0&&--keyCount[scan]==0)keys.set(scan,false); }
    private void hold(Control c) {
        if(c==null||++c.held!=1)return;
        press(c.scan);press(c.scan2);
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }
    private void release(Control c) { if(c!=null && --c.held==0) {
        lift(c.scan);lift(c.scan2);
        if(c.scan==STICK) {c.steering=0;keys.set(0xcb,false);keys.set(0xcd,false);}
    } }
    private void steer(Control c,float x) {
        c.steering=Math.max(-1,Math.min(1,(x/scale-c.area.centerX())/(c.area.width()/2)));
        keys.set(0xcb,c.steering<-.18f);keys.set(0xcd,c.steering>.18f);
    }
    void setPointer(Pointer pointer) { this.pointer=pointer; }
    private void pointerTouch(float x,float y,int phase) {
        if(pointer==null||getWidth()==0||getHeight()==0)return;
        pointerX=x;pointerY=y;pointer.touch(x/getWidth(),y/getHeight(),phase);
    }
    private void pointerRelease() { if(pointerId>=0){pointerId=-1;pointerTouch(pointerX,pointerY,2);} }
    /** Releases fingers the event no longer carries (their ACTION_POINTER_UP never arrived). */
    private void releaseLiftedFingers(MotionEvent e) {
        java.util.Iterator<java.util.Map.Entry<Integer,Control>> it=fingers.entrySet().iterator();
        while(it.hasNext()) {
            java.util.Map.Entry<Integer,Control> finger=it.next();
            if(e.findPointerIndex(finger.getKey())<0){release(finger.getValue());it.remove();}
        }
        if(pointerId>=0&&e.findPointerIndex(pointerId)<0)pointerRelease();
    }
    void releaseAll() { for(Control c:fingers.values()) release(c); fingers.clear(); pointerRelease(); invalidate(); }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if(editing)return editTouch(e);
        if(photoOn)return photoTouch(e);
        int action=e.getActionMasked(),index=e.getActionIndex(),id=e.getPointerId(index);
        if(hiddenByGamepad) {
            if(action==MotionEvent.ACTION_DOWN){hiddenByGamepad=false;invalidate();}
            return true;
        }
        if(action==MotionEvent.ACTION_CANCEL) { releaseAll(); return true; }
        // A new gesture means no finger is down: anything still held lost its
        // ACTION_UP (edge gestures, palm rejection, overlays) and would stay
        // pressed forever, e.g. the left arrow steering by itself.
        if(action==MotionEvent.ACTION_DOWN)releaseAll();
        else releaseLiftedFingers(e);
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            Control c=hit(e.getX(index),e.getY(index));if(c!=null&&c.scan==STICK&&c.held>0)c=null;
            if(c==null&&pointerId<0&&hit(e.getX(index),e.getY(index))==null){pointerId=id;pointerTouch(e.getX(index),e.getY(index),0);}
            release(fingers.put(id,c));hold(c);if(c!=null&&c.scan==STICK)steer(c,e.getX(index));
        } else if(action==MotionEvent.ACTION_MOVE) {
            for(int i=0;i<e.getPointerCount();++i) {
                int pointer=e.getPointerId(i);
                if(pointer==pointerId){pointerTouch(e.getX(i),e.getY(i),1);continue;}
                Control old=fingers.get(pointer),next=hit(e.getX(i),e.getY(i));
                if(old!=null&&old.scan==STICK) {steer(old,e.getX(i));continue;}
                // Menu-style buttons do not slide; pedals and arrows follow the thumb.
                if(old!=null&&old.scan<0)continue;
                if(next!=null&&(next.scan==STICK&&next.held>0||next.scan<0&&next.scan!=STICK))next=null;
                // undrgnd2.eu: a finger slides only between the steering's buttons, or between the pedals.
                if(style.equals("undrgnd2")) {
                    if(old!=null&&old.group==0)continue;
                    if(next!=null&&(next.group==0||old!=null&&next.group!=old.group))next=null;
                }
                if(old!=next) {release(old);fingers.put(pointer,next);hold(next);if(next!=null&&next.scan==STICK)steer(next,e.getX(i));}
            }
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            if(id==pointerId){pointerId=-1;pointerTouch(e.getX(index),e.getY(index),2);}
            Control c=fingers.remove(id); release(c);
            if(c!=null && c==hit(e.getX(index),e.getY(index))) {
                if(c.scan==MODE) {racing=!racing;layoutControls();}
                else if(c.scan==TEXT) {releaseAll();textInput.run();}
                else if(c.scan==EDIT) {releaseAll();showMenu();}
                else if(c.scan==PH_TOGGLE) {if(photo!=null)photo.input(PHOTO_OPEN,1,0);}
                else if(c.scan==HYD_LIFT) setHyd(hyd==1?0:1);
                else if(c.scan==HYD_BOUNCE) setHyd(hyd==2?0:2);
                else if(c.scan==STEERMODE) {
                    boolean stick=!layouts.getString("steer","buttons").equals("stick");
                    layouts.edit().putString("steer",stick?"stick":"buttons").apply();layoutControls();
                    Toast.makeText(getContext(),stick?"Direction : joystick":"Direction : boutons ◀ ▶",Toast.LENGTH_SHORT).show();
                }
                performClick();
            }
            if(action==MotionEvent.ACTION_UP)releaseAll();  // the last finger left
        }
        invalidate();return true;
    }
    @Override public boolean performClick() { super.performClick();return true; }
}
