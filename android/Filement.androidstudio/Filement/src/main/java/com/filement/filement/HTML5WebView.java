package com.filement.filement;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.webkit.DownloadListener;
import android.webkit.GeolocationPermissions;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.os.Build;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;


public class HTML5WebView extends WebView {
        
        private Context                                                         mContext;
        private MyWebChromeClient                                      		    mWebChromeClient;
        private View                                                            mCustomView;
        private FrameLayout                                                     mCustomViewContainer;
        private WebChromeClient.CustomViewCallback      						mCustomViewCallback;
        public ValueCallback<Uri>												        mUploadMessage;
        private FrameLayout                                                     mContentView;
        private FrameLayout                                                     mBrowserFrameLayout;
        private FrameLayout                                                     mLayout;
        
    static final String LOGTAG = "webview";
            
        private void init(Context context) {
                mContext = context;             
                Activity a = (Activity) mContext;
               
                mLayout = new FrameLayout(context);
                
                mBrowserFrameLayout = (FrameLayout) LayoutInflater.from(a).inflate(R.layout.custom_screen, null);
                mContentView = (FrameLayout) mBrowserFrameLayout.findViewById(R.id.main_content);
                mCustomViewContainer = (FrameLayout) mBrowserFrameLayout.findViewById(R.id.fullscreen_custom_content);
                
                mLayout.addView(mBrowserFrameLayout, COVER_SCREEN_PARAMS);

                mWebChromeClient = new MyWebChromeClient();
            setWebChromeClient(mWebChromeClient);

            setWebViewClient(new MyWebViewClient());
               
            setDownloadListener(new DownloadListener() {
	            public void onDownloadStart(String url, String userAgent,
	                    String contentDisposition, String mimetype, long contentLength) {
	                Intent intent = new Intent(Intent.ACTION_VIEW);
	                intent.setData(Uri.parse(url));
	                Activity a = (Activity) mContext;
	                a.startActivity(intent);	                
	            }
            });
            
            // Configure the webview
            WebSettings s = getSettings();
            
            
            s.setBuiltInZoomControls(false);
            s.setAllowUniversalAccessFromFileURLs(true);
            //if (Build.VERSION.SDK_INT >= VERSION_CODES.JELLY_BEAN){}
            //s.setLayoutAlgorithm(WebSettings.LayoutAlgorithm.NARROW_COLUMNS);
            s.setLayoutAlgorithm(WebSettings.LayoutAlgorithm.NORMAL);
            s.setRenderPriority(WebSettings.RenderPriority.HIGH);
            s.setUseWideViewPort(true);
            s.setLoadWithOverviewMode(true);
            s.setSavePassword(true);
            s.setSaveFormData(true);
            s.setJavaScriptEnabled(true);
            
            // enable navigator.geolocation 
           // s.setGeolocationEnabled(true);
           // s.setGeolocationDatabasePath("/data/data/org.itri.html5webview/databases/");
            
            // enable Web Storage: localStorage, sessionStorage
            s.setDomStorageEnabled(true);
            
            mContentView.addView(this);
        }

        public HTML5WebView(Context context) {
                super(context);
                init(context);
        }

        public HTML5WebView(Context context, AttributeSet attrs) {
                super(context, attrs);
                init(context);
        }

        public HTML5WebView(Context context, AttributeSet attrs, int defStyle) {
                super(context, attrs, defStyle);
                init(context);
        }
        
        public FrameLayout getLayout() {
                return mLayout;
        }
        
    public boolean inCustomView() {
                return (mCustomView != null);
        }
    
    public void hideCustomView() {
                mWebChromeClient.onHideCustomView();
        }
    
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
                if ((mCustomView == null) && canGoBack()){
                        goBack();
                        return true;
                }
        }
        return super.onKeyDown(keyCode, event);
    }
    
    

     class MyWebChromeClient extends WebChromeClient {
                private Bitmap          mDefaultVideoPoster;
                private View            mVideoProgressView;
        
       
                
            
             // For Android > 4.1
    	        public void openFileChooser(ValueCallback<Uri> uploadMsg, String acceptType, String capture) {
    	            openFileChooser(uploadMsg);
    	        }


    	        // Andorid 3.0 + 
    	        public void openFileChooser(ValueCallback<Uri> uploadMsg, String acceptType) {
    	            openFileChooser(uploadMsg);
    	        }


    	        //Android 3.0
    	        public void openFileChooser(ValueCallback<Uri> uploadMsg) {
    	        	WebViewLogin a = (WebViewLogin) mContext;
    	            a.mUploadMessage = uploadMsg;
    	            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
    	            i.addCategory(Intent.CATEGORY_OPENABLE);
    	            i.setType("*/*");
                    a.startActivityForResult(
    	                    Intent.createChooser(i, "Image Browser"),
    	                    a.FILECHOOSER_RESULTCODE);
    	        }
                
                @Override
                public Bitmap getDefaultVideoPoster() {
                        //Log.i(LOGTAG, "here in on getDefaultVideoPoster");    
                        if (mDefaultVideoPoster == null) {
                                mDefaultVideoPoster = BitmapFactory.decodeResource(
                                                getResources(), R.drawable.default_video_poster);
                    }
                        return mDefaultVideoPoster;
                }
                
                @Override
                public View getVideoLoadingProgressView() {
                        //Log.i(LOGTAG, "here in on getVideoLoadingPregressView");
                        
                if (mVideoProgressView == null) {
                    LayoutInflater inflater = LayoutInflater.from(mContext);
                    mVideoProgressView = inflater.inflate(R.layout.video_loading_progress, null);
                }
                return mVideoProgressView; 
                }
                
               
        
         @Override
         public void onReceivedTitle(WebView view, String title) {
            ((Activity) mContext).setTitle(title);
         }

         @Override
         public void onProgressChanged(WebView view, int newProgress) {
                 ((Activity) mContext).getWindow().setFeatureInt(Window.FEATURE_PROGRESS, newProgress*100);
         }
         
         
         
         /*
         @Override
         public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) {
             callback.invoke(origin, true, false);
         }
         */
    }
        
        private class MyWebViewClient extends WebViewClient {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                Log.i(LOGTAG, "shouldOverrideUrlLoading: "+url);
                // don't override URL so that stuff within iframe can work properly
                // view.loadUrl(url);
                return false;
            }
            
            
            
            @Override
            public void onReceivedError (WebView view, int errorCode, 
                String description, String failingUrl) {
                if (errorCode == ERROR_TIMEOUT || errorCode == ERROR_CONNECT || errorCode == ERROR_HOST_LOOKUP || errorCode == ERROR_FAILED_SSL_HANDSHAKE) {
                    view.stopLoading();  // may not be needed
                    view.loadData("<!DOCTYPE html><html><head><title>Error</title><meta charset=\"UTF-8\" /><meta http-equiv=\"cache-control\" content=\"no-cache\" /><meta name=\"keywords\" content=\"\" /><style>body {color: #ffffff;font-family: arial, sans-serif;background-color: #0281b9;}h3 {font-weight: normal;font-size: 260%;position: fixed;top: 50%;left: 32px;right: 32px;margin: -30px auto auto auto;text-align: center;height: 60px;}</style></head> <body><h3>Filement can't be loaded.<br />Check your internet connecton.</h3></body></html>", "text/html", "utf-8");
                }
                
            }
        }
        
        static final FrameLayout.LayoutParams COVER_SCREEN_PARAMS =
        new FrameLayout.LayoutParams( ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
}
