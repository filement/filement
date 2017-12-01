package com.filement.filement;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.webkit.DownloadListener;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.net.Uri;

public class WebViewLogin extends Activity {

	
	 public ValueCallback<Uri> mUploadMessage;
	 public final static int FILECHOOSER_RESULTCODE = 1;
     final public static int PICK_IMAGE = 2;
	 String url = "http://www.flmntdev.com/index-android.php";
     //String url = "http://s.flmntdev.com/upload.php";
	 HTML5WebView mWebView;

	
	@Override
	 protected void onActivityResult(int requestCode, int resultCode,
	         Intent intent) {
		 if (requestCode == FILECHOOSER_RESULTCODE) {
	            if (null == mUploadMessage)
	                return;
	            Uri result = intent == null || resultCode != RESULT_OK ? null
	                    : intent.getData();
	            mUploadMessage.onReceiveValue(result);
	            mUploadMessage = null;

	        }
	 }
	
	
	
	
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
       
        
        
        mWebView = new HTML5WebView(this);
      //remove cache for debug
        /*
        mWebView.clearHistory();
        mWebView.clearFormData();
        mWebView.clearCache(true);
        */
        /*
        mWebView.setDownloadListener(new DownloadListener() {
            public void onDownloadStart(String url, String userAgent,
                    String contentDisposition, String mimetype,
                    long contentLength) {
            	Uri uri = Uri.parse(url);
                Intent intent = new Intent(Intent.ACTION_VIEW,uri);
               // WebViewLogin.contx.startActivity(intent);
                startActivity(intent);       
            }     
        });
        */
       
        
        if (savedInstanceState != null) {
            mWebView.restoreState(savedInstanceState);
        } else {    
            mWebView.loadUrl(url);
        }
        
   

        setContentView(mWebView.getLayout());
    }

    @Override
    public void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        mWebView.saveState(outState);
    }

    @Override
    public void onStop() {
        super.onStop();
        mWebView.stopLoading();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {

        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (mWebView.inCustomView()) {
                mWebView.hideCustomView();
            //  mWebView.goBack();
                //mWebView.goBack();
                return true;
            }

        }
        return super.onKeyDown(keyCode, event);
    }
	
	@Override
	public void onConfigurationChanged(Configuration newConfig) {
	     super.onConfigurationChanged(newConfig);
	}

}
