package com.filement.filement;

//import com.filement.filement.util.SystemUiHider;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.Thread.UncaughtExceptionHandler;

import android.annotation.TargetApi;
import android.app.Activity;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.widget.RadioButton;
import android.widget.TextView;
import android.widget.Toast;
import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;




/**
 * An example full-screen activity that shows and hides the system UI (i.e.
 * status bar and navigation/system bar) with user interaction.
 * 
 * @see SystemUiHider
 */
public class Filement extends Activity {
	/**
	 * Whether or not the system UI should be auto-hidden after
	 * {@link #AUTO_HIDE_DELAY_MILLIS} milliseconds.
	 */
	//private static final boolean AUTO_HIDE = true;

	/**
	 * If {@link #AUTO_HIDE} is set, the number of milliseconds to wait after
	 * user interaction before hiding the system UI.
	 */
	//private static final int AUTO_HIDE_DELAY_MILLIS = 3000;

	/**
	 * If set, will toggle the system UI visibility upon interaction. Otherwise,
	 * will show the system UI visibility upon interaction.
	 */
	//private static final boolean TOGGLE_ON_CLICK = true;

	/**
	 * The flags to pass to {@link SystemUiHider#getInstance}.
	 */
	//private static final int HIDER_FLAGS = SystemUiHider.FLAG_HIDE_NAVIGATION;

	/**
	 * The instance of the {@link SystemUiHider} for this activity.
	 */
	//private SystemUiHider mSystemUiHider;
	 TextView tv;
	 boolean mIsBound;
	 boolean registered_layout;
	 private FilementService mBoundService;
	 private Intent FilementServiceIntent;
	 
	 private boolean isNetworkAvailable() {
		    ConnectivityManager connectivityManager 
		          = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
		    NetworkInfo activeNetworkInfo = connectivityManager.getActiveNetworkInfo();
		    return activeNetworkInfo != null;
		}
	 
	 private ServiceConnection mConnection = new ServiceConnection() {
		    public void onServiceConnected(ComponentName className, IBinder service) {
		        // This is called when the connection with the service has been
		        // established, giving us the service object we can use to
		        // interact with the service.  Because we have bound to a explicit
		        // service that we know is running in our own process, we can
		        // cast its IBinder to a concrete class and directly access it.
		        mBoundService = ((FilementService.FilementBinder)service).getService();
		    }

		    public void onServiceDisconnected(ComponentName className) {
		        // This is called when the connection with the service has been
		        // unexpectedly disconnected -- that is, its process crashed.
		        // Because it is running in our same process, we should never
		        // see this happen.
		        mBoundService = null;
		    }
		};
		

		void doBindService() {
		    // Establish a connection with the service.  We use an explicit
		    // class name because we want a specific service implementation that
		    // we know will be running in our own process (and thus won't be
		    // supporting component replacement by other applications).
		    bindService(new Intent(Filement.this, 
		            FilementService.class), mConnection, Context.BIND_AUTO_CREATE);
		    mIsBound = true;
		}
		
		void doUnbindService() {
		    if (mIsBound) {
		        // Detach our existing connection.
		        unbindService(mConnection);
		        mIsBound = false;
		    }
		}
		
	@Override
	protected void onDestroy() {
		    super.onDestroy();
		    doUnbindService();
		}
	 
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		
		Thread.setDefaultUncaughtExceptionHandler(new UncaughtExceptionHandler() {
			@Override
			         public void uncaughtException(Thread t, Throwable e) {
				
					killpid(0);
					stopService(FilementServiceIntent);
					doUnbindService();
			       Filement.this.finish();
			       android.os.Process.killProcess(android.os.Process.myPid());
			        
			       }
			     });
		
		if(!isNetworkAvailable())
		{
			Toast.makeText(Filement.this,"No Internet Connection",      Toast.LENGTH_SHORT).show();
		}
		
		/*
		final View controlsView = findViewById(R.id.fullscreen_content_controls);
		final View contentView = findViewById(R.id.fullscreen_content);

		// Set up an instance of SystemUiHider to control the system UI for
		// this activity.
		mSystemUiHider = SystemUiHider.getInstance(this, contentView,
				HIDER_FLAGS);
		mSystemUiHider.setup();
		mSystemUiHider
				.setOnVisibilityChangeListener(new SystemUiHider.OnVisibilityChangeListener() {
					// Cached values.
					int mControlsHeight;
					int mShortAnimTime;

					@Override
					@TargetApi(Build.VERSION_CODES.HONEYCOMB_MR2)
					public void onVisibilityChange(boolean visible) {
						if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB_MR2) {
							// If the ViewPropertyAnimator API is available
							// (Honeycomb MR2 and later), use it to animate the
							// in-layout UI controls at the bottom of the
							// screen.
							if (mControlsHeight == 0) {
								mControlsHeight = controlsView.getHeight();
							}
							if (mShortAnimTime == 0) {
								mShortAnimTime = getResources().getInteger(
										android.R.integer.config_shortAnimTime);
							}
							controlsView
									.animate()
									.translationY(visible ? 0 : mControlsHeight)
									.setDuration(mShortAnimTime);
						} else {
							// If the ViewPropertyAnimator APIs aren't
							// available, simply show or hide the in-layout UI
							// controls.
							controlsView.setVisibility(visible ? View.VISIBLE
									: View.GONE);
						}

						if (visible && AUTO_HIDE) {
							// Schedule a hide().
							delayedHide(AUTO_HIDE_DELAY_MILLIS);
						}
					}
				});

		// Set up the user interaction to manually show or hide the system UI.
		contentView.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View view) {
				if (TOGGLE_ON_CLICK) {
					mSystemUiHider.toggle();
				} else {
					mSystemUiHider.show();
				}
			}
		});

		// Upon interacting with UI controls, delay any scheduled hide()
		// operations to prevent the jarring behavior of controls going away
		// while interacting with the UI.
		findViewById(R.id.dummy_button).setOnTouchListener(
				mDelayHideTouchListener);
		
	//	TextView  tv = new TextView(this);
		   */
		
		
		
		
		
		
        String abspath=getBaseContext().getFilesDir().getAbsolutePath();
        String magic_path=abspath+"/filement.mgc";
        String db_path = abspath+"/filement.db";
        if (!new File(magic_path).exists()){
            try
            {
              InputStream localInputStream = getAssets().open("filement.mgc");
              FileOutputStream localFileOutputStream = getBaseContext().openFileOutput("filement.mgc", MODE_PRIVATE);

              byte[] arrayOfByte = new byte[1024];
              int offset;
              while ((offset = localInputStream.read(arrayOfByte))>0)
              {
                localFileOutputStream.write(arrayOfByte, 0, offset);
              }
              localFileOutputStream.close();
              localInputStream.close();
              
            }
            catch (IOException localIOException)
            {
                localIOException.printStackTrace();
                return;
            }
        }
        
        
         registered_layout=false;
       // tv = (TextView) findViewById(R.id.textView1);
        
        if(checkdevice(db_path,magic_path))
        {
        	
        	registered_layout=true;
        	/*
        	if(!startserver(db_path,magic_path))
			{
				tv.setText( "Can't start the server!" );
			}
			*/
        }
        else 
        {
        	registered_layout=false;
        	
        	/*
        	if(registerdevice(db_path,magic_path,"nikolanikov@mail.bg","4760","ncn6129","androidniki"))
        		{
        		tv.setText( "Successfuly registered, started!" );
        			if(!startserver(db_path,magic_path))
        			{
        				tv.setText( "Can't start the server!" );
        			}
        		}
        	else
        		{
        		tv.setText( "Registration error!" );
        		}
        	*/
        	
        }
       
        
        
        
        if(registered_layout)
        {
        	setContentView(R.layout.activity_filement_paired);
        	
        	FilementServiceIntent=new Intent(Filement.this,FilementService.class);
            startService(FilementServiceIntent);
            doBindService();
        	final View SignInButtonView = findViewById(R.id.SignInButton);
        	SignInButtonView.setOnClickListener(new View.OnClickListener() {
    			@Override
    			public void onClick(View view) {
    				Intent myIntent = new Intent(Filement.this, WebViewLogin.class);
    				Filement.this.startActivity(myIntent);
    				Filement.this.overridePendingTransition(R.anim.push_left_in,R.anim.push_up_out);  
    			}
    		});
        	
        	
        }
        else
        {
        	setContentView(R.layout.activity_filement);
    		
    		final View PairButtonView = findViewById(R.id.PairButton);
    		final View SignInButtonView = findViewById(R.id.SignInButton);
    		final View CreateAccButtonView = findViewById(R.id.CreateAccButton);
    		
    		PairButtonView.setOnClickListener(new View.OnClickListener() {
    			@Override
    			public void onClick(View view) {
    				Intent myIntent = new Intent(Filement.this, PairActivity.class);
    				Filement.this.startActivityForResult(myIntent,1);
    				Filement.this.overridePendingTransition(R.anim.push_left_in,R.anim.push_up_out);  
    			}
    		});
    		
    		SignInButtonView.setOnClickListener(new View.OnClickListener() {
    			@Override
    			public void onClick(View view) {
    				Intent myIntent = new Intent(Filement.this, WebViewLogin.class);
    				Filement.this.startActivity(myIntent);
    				Filement.this.overridePendingTransition(R.anim.push_left_in,R.anim.push_up_out);  
    			}
    		});
    		
    		CreateAccButtonView.setOnClickListener(new View.OnClickListener() {
    			@Override
    			public void onClick(View view) {
    				Intent myIntent = new Intent(Filement.this, WebViewLogin.class);
    				Filement.this.startActivity(myIntent);
    				Filement.this.overridePendingTransition(R.anim.push_left_in,R.anim.push_up_out);  
    			}
    		});
        }
        
		
	}
	

		public static native int killpid(int pid);
	 public native boolean  checkdevice(String dbpath,String mpath);
	
	 @Override
	    public boolean onPrepareOptionsMenu(Menu menu)
	    {
		 menu.clear();
		 if(registered_layout)
		 {
			 if(mIsBound)
			 {
				 menu.add("Stop Filement Service");
			 } 
			 else
			 {
				 menu.add("Start Filement Service"); 
			 }
		 }
	     return true;
	    }
	 
	 @Override
	 public boolean onOptionsItemSelected(MenuItem item) {
		 if(registered_layout)
		 {
			 if(mIsBound)
			 {
				 stopService(FilementServiceIntent);
				 doUnbindService();
			 }
			 else
			 {
				 	FilementServiceIntent=new Intent(Filement.this,FilementService.class);
		            startService(FilementServiceIntent);
		            doBindService();
			 }
		 }
		 return true;
	 }
	 
	@Override
	protected void onPostCreate(Bundle savedInstanceState) {
		super.onPostCreate(savedInstanceState);

		// Trigger the initial hide() shortly after the activity has been
		// created, to briefly hint to the user that UI controls
		// are available.
		//delayedHide(100);
	}
	
	

	/**
	 * Touch listener to use for in-layout UI controls to delay hiding the
	 * system UI. This is to prevent the jarring behavior of controls going away
	 * while interacting with activity UI.
	 */
	/*
	View.OnTouchListener mDelayHideTouchListener = new View.OnTouchListener() {
		@Override
		public boolean onTouch(View view, MotionEvent motionEvent) {
			if (AUTO_HIDE) {
				delayedHide(AUTO_HIDE_DELAY_MILLIS);
			}
			return false;
		}
	};

	Handler mHideHandler = new Handler();
	Runnable mHideRunnable = new Runnable() {
		@Override
		public void run() {
			mSystemUiHider.hide();
		}
	};
*/
	/**
	 * Schedules a call to hide() in [delay] milliseconds, canceling any
	 * previously scheduled calls.
	 */
	/*
	private void delayedHide(int delayMillis) {
		mHideHandler.removeCallbacks(mHideRunnable);
		mHideHandler.postDelayed(mHideRunnable, delayMillis);
	}
	*/
	static {
    	System.loadLibrary("filement");
    }
}
