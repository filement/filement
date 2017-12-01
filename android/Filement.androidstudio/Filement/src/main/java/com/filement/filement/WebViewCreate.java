package com.filement.filement;

import android.app.Activity;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.view.KeyEvent;
import android.webkit.ValueCallback;
import android.webkit.WebView;


public class WebViewCreate extends Activity {

	
	String url = "http://www.filement.com/register-android.php";
	HTML5WebView mWebView;
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mWebView = new HTML5WebView(this);

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
