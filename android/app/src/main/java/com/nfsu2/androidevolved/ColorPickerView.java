package com.nfsu2.androidevolved;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ComposeShader;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.MotionEvent;
import android.view.View;

/** A colour picker: saturation and value in the square, hue in the bar beside it (Peinture perso). */
final class ColorPickerView extends View {
    interface Listener { void onColor(int argb); }
    private final float[] hsv={30f,0.9f,1f};
    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG),ring=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF square=new RectF(),bar=new RectF();
    private Listener listener;
    private int dragging; // 0 none, 1 the square, 2 the bar
    private Shader squareShader;private float shaderHue=-1;

    ColorPickerView(Context c){super(c);ring.setStyle(Paint.Style.STROKE);}
    void setListener(Listener l){listener=l;}
    int getColor(){return Color.HSVToColor(hsv);}
    void setColor(int argb){Color.colorToHSV(argb,hsv);invalidate();if(listener!=null)listener.onColor(getColor());}

    @Override protected void onSizeChanged(int w,int h,int ow,int oh){
        float d=getResources().getDisplayMetrics().density,gap=12*d,bw=26*d;
        square.set(0,0,w-gap-bw,h);bar.set(w-bw,0,w,h);shaderHue=-1;
    }
    @Override protected void onDraw(Canvas cv){
        float d=getResources().getDisplayMetrics().density;
        if(shaderHue!=hsv[0]){
            shaderHue=hsv[0];
            Shader sat=new LinearGradient(square.left,0,square.right,0,0xFFFFFFFF,Color.HSVToColor(new float[]{hsv[0],1f,1f}),Shader.TileMode.CLAMP);
            Shader val=new LinearGradient(0,square.top,0,square.bottom,0x00000000,0xFF000000,Shader.TileMode.CLAMP);
            squareShader=new ComposeShader(sat,val,PorterDuff.Mode.SRC_OVER);
        }
        fill.setShader(squareShader);cv.drawRoundRect(square,6*d,6*d,fill);
        int[] hues=new int[7];for(int i=0;i<7;++i)hues[i]=Color.HSVToColor(new float[]{(i*60)%360,1f,1f});
        fill.setShader(new LinearGradient(0,bar.top,0,bar.bottom,hues,null,Shader.TileMode.CLAMP));cv.drawRoundRect(bar,6*d,6*d,fill);
        fill.setShader(null);
        float x=square.left+hsv[1]*square.width(),y=square.top+(1-hsv[2])*square.height();
        ring.setStrokeWidth(3*d);ring.setColor(0xFFFFFFFF);cv.drawCircle(x,y,9*d,ring);
        ring.setStrokeWidth(1.5f*d);ring.setColor(0xFF000000);cv.drawCircle(x,y,11*d,ring);
        float hy=bar.top+hsv[0]/360f*bar.height();
        ring.setStrokeWidth(3*d);ring.setColor(0xFFFFFFFF);cv.drawRect(bar.left-2*d,hy-4*d,bar.right+2*d,hy+4*d,ring);
    }
    @Override public boolean onTouchEvent(MotionEvent e){
        float x=e.getX(),y=e.getY();
        if(e.getActionMasked()==MotionEvent.ACTION_DOWN){dragging=x>=bar.left-8*getResources().getDisplayMetrics().density?2:1;getParent().requestDisallowInterceptTouchEvent(true);}
        if(dragging==1){hsv[1]=clamp((x-square.left)/square.width());hsv[2]=1-clamp((y-square.top)/square.height());}
        else if(dragging==2){hsv[0]=Math.min(359.9f,clamp((y-bar.top)/bar.height())*360f);}
        if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL)dragging=0;
        invalidate();if(listener!=null)listener.onColor(getColor());
        return true;
    }
    private static float clamp(float v){return v<0?0:v>1?1:v;}
}
