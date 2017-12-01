package com.filement.filement;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.webkit.DownloadListener;
import android.webkit.JsResult;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

public class testweb extends Activity {

	private WebView myWebView;
	 private String LOG_TAG = "AndroidWebViewActivity";

	 private ValueCallback<Uri> mUploadMessage;
	 private final static int FILECHOOSER_RESULTCODE = 1;


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
	protected void onCreate(Bundle savedInstanceState) {
	    super.onCreate(savedInstanceState);
	    setContentView(R.layout.testwebvlayout);


	        myWebView = (WebView) findViewById(R.id.testwebvlayout);

	        //enable Javascript
	        myWebView.getSettings().setJavaScriptEnabled(true);
	        myWebView.clearHistory();
	        myWebView.clearFormData();
	        myWebView.clearCache(true);

	        //loads the WebView completely zoomed out
	       // myWebView.getSettings().setLoadWithOverviewMode(true);
	        myWebView.getSettings().setLayoutAlgorithm(WebSettings.LayoutAlgorithm.NORMAL);
	        
	        //true makes the Webview have a normal viewport such as a normal desktop browser
	        //when false the webview will have a viewport constrained to it's own dimensions
	       // myWebView.getSettings().setUseWideViewPort(true);

	        //override the web client to open all links in the same webview
	        //myWebView.setWebViewClient(new MyWebViewClient());
	        myWebView.setWebChromeClient(new MyWebChromeClient());

	        //Injects the supplied Java object into this WebView. The object is injected into the
	        //JavaScript context of the main frame, using the supplied name. This allows the
	        //Java object's public methods to be accessed from JavaScript.
	        myWebView.addJavascriptInterface(new JavaScriptInterface(this), "Android");

	        //load the home page URL
	        //myWebView.loadUrl("http://demo.mysamplecode.com/Servlets_JSP/pages/androidWebView.jsp");
	        myWebView.loadUrl("http://s.flmntdev.com/upload.php");
	        //myWebView.loadUrl("http://htftp.domainbg.com/nikov/");


	        myWebView.setDownloadListener(new DownloadListener() {
	            public void onDownloadStart(String url, String userAgent,
	                    String contentDisposition, String mimetype, long contentLength) {
	                Intent intent = new Intent(Intent.ACTION_VIEW);
	                intent.setData(Uri.parse(url));
	                startActivity(intent);

	            }
	        });

	}



	 private class MyWebViewClient extends WebViewClient {
		 /*
	     @Override
	     public boolean shouldOverrideUrlLoading(WebView view, String url) {
	         if (Uri.parse(url).getHost().equals("flmntdev.com")) {
	             return false;
	         }
	         Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
	         startActivity(intent);
	         return true;
	     }
	     */
	 }


	 private class MyWebChromeClient extends WebChromeClient {

		 
		  
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
	            mUploadMessage = uploadMsg;
	            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
	            i.addCategory(Intent.CATEGORY_OPENABLE);
	            i.setType("*/*");
	            startActivityForResult(
	                    Intent.createChooser(i, "Image Browser"),
	                    FILECHOOSER_RESULTCODE);
	        }
		 
	      //display alert message in Web View
	      @Override
	         public boolean onJsAlert(WebView view, String url, String message, JsResult result) {
	             Log.d(LOG_TAG, message);
	             new AlertDialog.Builder(view.getContext())
	              .setMessage(message).setCancelable(true).show();
	             result.confirm();
	             return true;
	         }

	     }

	     public class JavaScriptInterface {
	         Context mContext;

	         // Instantiate the interface and set the context
	         JavaScriptInterface(Context c) {
	             mContext = c;
	         }

	         //using Javascript to call the finish activity
	         public void closeMyActivity() {
	             finish();
	         }

	     }
	   

	     //Web view has record of all pages visited so you can go back and forth
	     //just override the back button to go back in history if there is page
	     //available for display
	     @Override
	     public boolean onKeyDown(int keyCode, KeyEvent event) {
	         if ((keyCode == KeyEvent.KEYCODE_BACK) && myWebView.canGoBack()) {
	             myWebView.goBack();
	             return true;
	         }
	         return super.onKeyDown(keyCode, event);
	     }
	
}


