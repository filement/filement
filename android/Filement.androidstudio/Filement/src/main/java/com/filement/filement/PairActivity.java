package com.filement.filement;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

public class PairActivity extends Activity {

	TextView status;
	EditText DevName;
	EditText PIN;
	EditText Email;
	EditText Password;
	LinearLayout PairDecriptionLayout;
	ProgressBar PairingProgress;
	boolean register_progress=false;
	
	
	/** Called when the activity is first created. */
	@Override
	public void onCreate(Bundle savedInstanceState) {
	    super.onCreate(savedInstanceState);
	
	    setContentView(R.layout.activity_pair);
	    
	    String abspath=getBaseContext().getFilesDir().getAbsolutePath();
        final String magic_path=abspath+"/filement.mgc";
        final String db_path = abspath+"/filement.db";
       
        final View SignInButtonView = findViewById(R.id.PairButton2);
        PairingProgress = (ProgressBar)findViewById(R.id.PairingProgress);
        PairDecriptionLayout = (LinearLayout)findViewById(R.id.pair_descr_layout);
        DevName   = (EditText)findViewById(R.id.DeviceNameInput);
        Email   = (EditText)findViewById(R.id.EmailInput);
        Password   = (EditText)findViewById(R.id.PasswordInput);
        final TextView status = (TextView) findViewById(R.id.PairStatus);
    	SignInButtonView.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View view) {
				if(register_progress)return;
				 if(DevName.getText().toString().length() < 1 || DevName.getText().toString().length() > 100)
				 {
					 PairDecriptionLayout.setVisibility(1);
					 status.setText( "Device Name is too short." );
					 return;
				 }
				 else if(Email.getText().toString().length() <4 || Email.getText().toString().length() > 120)
				 {
					 PairDecriptionLayout.setVisibility(1);
					 status.setText( "Incorrect Email." );
					 return;
				 }
				 else if(Password.getText().toString().length() <3 || Password.getText().toString().length() > 120)
				 {
					 PairDecriptionLayout.setVisibility(1);
					 status.setText( "Device Password is too short." );
					 return;
				 }
				 PairDecriptionLayout.setVisibility(0); 
				 status.setText( "Pairing..." );
				 PairingProgress.setVisibility(1); 
				 

	        	if(registerdevice(db_path,magic_path,Email.getText().toString().toString(),Password.getText().toString(),DevName.getText().toString()))
				 {
					 
					 status.setText( "The device has registered successfully." );
					 PairingProgress.setVisibility(0); 
					Intent myIntent = new Intent(PairActivity.this, Filement.class);
					PairActivity.this.finish();
					PairActivity.this.startActivity(myIntent);
					PairActivity.this.overridePendingTransition(R.anim.push_left_in,R.anim.push_up_out); 
					 /*
	        		tv.setText( "Successfuly registered, started!" );
	        			if(!startserver(db_path,magic_path))
	        			{
	        				tv.setText( "Can't start the server!" );
	        			}
	        		*/
	        		}
	        	else
	        		{
	        		status.setText( "Registration error! Possible reason: Wrong details." );
	        		PairingProgress.setVisibility(0); 
	        		register_progress=false;
	        		}
			}
		}); 
        
	    
	    
	    
	    
	    // TODO Auto-generated method stub
	}
	
	public native boolean  registerdevice(String dbpath,String mpath,String email,String password,String dev_name);

	static {
		System.loadLibrary("filement");
	}
}
