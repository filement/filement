package com.filement.filement;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.util.Log;
import android.widget.Toast;

public class FilementService extends Service {

	private NotificationManager mNM;
	public static int startserverpid;
	public static long last_connectivity=0;
	public static long last_nonconnectivity=0;
	static boolean is_service=false;
	static String magic_path;
	static String db_path;
    //private FilementThread FilementThreadHandle;

    // Unique Identification Number for the Notification.
    // We use it on Notification start, and to cancel it.
    private int NOTIFICATION = R.string.filement_service_started;
   
    
    /*
    public class FilementThread extends Thread{
        @Override
        public void run() {
            super.run(); 
            try{
            	 String abspath=getBaseContext().getFilesDir().getAbsolutePath();
                final String magic_path=abspath+"/filement.mgc";
                final String db_path = abspath+"/filement.db";
            	if(!startserver(db_path,magic_path))
    			{
            		Toast.makeText(getBaseContext(), R.string.filement_server_problem, Toast.LENGTH_SHORT).show();
            		
    			}
            	else
            	{
            		Log.i("FilementService", "The thread has been stopped1111");
            	}
            	
            }catch(Exception e){
                e.getMessage();
            }
        }
    }
    */
    
    /*
    private Handler handler=new Handler(){
        @Override
        public void handleMessage(Message msg) {
            super.handleMessage(msg);
            //showAppNotification();
        }
       };
    */
    /*
     * Class for clients to access.  Because we know this service always
     * runs in the same process as its clients, we don't need to deal with
     * IPC.
     */
    public class FilementBinder extends Binder {
    	FilementService getService() {
            return FilementService.this;
        }
    }
    
	 private boolean isNetworkAvailable() {
		    ConnectivityManager connectivityManager 
		          = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
		    NetworkInfo activeNetworkInfo = connectivityManager.getActiveNetworkInfo();
		    return activeNetworkInfo != null;
		}
	 
	 static private boolean FilementStartServer() {
		 
		 if(db_path==null || magic_path==null)return false;
		 
	      if(startserverpid>0)killpid(startserverpid);
	      startserverpid=startserver(db_path,magic_path);
	      if(startserverpid<0)
				{
	    	  return false;
	     		
	     		
				}
	     else if(startserverpid==0)
	     	{
	     		Log.i("FilementService", "The thread has been stopped");
	     	}
	      
	      
	      return true;
		}
	 
	 static private void FilementStopServer() {
		 killpid(startserverpid);
	     startserverpid=0;
	 }
	 

    public static class ConnectionUpdater extends BroadcastReceiver{

        @Override
        public void onReceive(Context context, Intent intent) {
        	
            NetworkInfo info = (NetworkInfo) ((ConnectivityManager) context
.getSystemService(Context.CONNECTIVITY_SERVICE)).getActiveNetworkInfo();

            if (info == null || !info.isConnected()) {
            	long CurrentTS = System.currentTimeMillis()/1000;
            	long conndiff = CurrentTS - last_nonconnectivity;
            	if(conndiff<2)return ;
            	

            	Log.i("FilementService", "Connectivity change not connected");
            	
            	last_nonconnectivity=CurrentTS;
            	
            	
            	if(is_service)FilementStopServer();
            	//if(startserverpid!=0)killpid(startserverpid);
            	//startserverpid=0;
            	//if(FilementThreadHandle!=null && FilementThreadHandle.isAlive())
            	//{
            	//	FilementThreadHandle.interrupt();
               // 	FilementThreadHandle.stop();
            	//}
            	
            }
            else{
            	long CurrentTS = System.currentTimeMillis()/1000;
            	long conndiff = CurrentTS - last_connectivity;
            	if(conndiff<2)return ;
            	
            	Log.i("FilementService", "Connectivity change new internet");
            	
            	last_connectivity=CurrentTS;
            	
            	if(is_service)FilementStartServer();
            	

            	//if(FilementThreadHandle!=null && !FilementThreadHandle.isAlive())
            	// { 
            	//	FilementThreadHandle=new FilementThread();
               // 	FilementThreadHandle.start();
            	// }
            	//else
            	// {
            	//	FilementThreadHandle.interrupt();
               // 	FilementThreadHandle.stop();
               // 	FilementThreadHandle=new FilementThread();
               // 	FilementThreadHandle.start();
            	// }
            	 
            	 
            	}
            	
            	
        }	

   }
        
    
    @Override
    public void onCreate() {
    	is_service=true;
    //	FilementThreadHandle=new FilementThread();
    	String abspath=getBaseContext().getFilesDir().getAbsolutePath();
	    magic_path = abspath+"/filement.mgc";
	    db_path = abspath+"/filement.db";
    
	    if(isNetworkAvailable())
	      {
	    	if(!FilementStartServer())
	    	{
	    		Toast.makeText(getBaseContext(), R.string.filement_server_problem, Toast.LENGTH_SHORT).show();
	    	}
	      }
    	
    	mNM = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
    //	FilementThreadHandle.start();
        // Display a notification about us starting.  We put an icon in the status bar.
        showNotification(false);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i("FilementService", "Received start id " + startId + ": " + intent);
        // We want this service to continue running until it is explicitly
        // stopped, so return sticky.
        
     //   if(!FilementThreadHandle.isAlive())
     //   {
     //   FilementThreadHandle.start();
     //   }
        
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
    	is_service=false;
        // Cancel the persistent notification.
        mNM.cancel(NOTIFICATION);
        FilementStopServer();
       // mNM.cancelAll();
    //    if(FilementThreadHandle.isAlive())
    //    {
    //    	FilementThreadHandle.interrupt();
    //  //  	FilementThreadHandle.stop();
    //    }
        
        // Tell the user we stopped.
        Toast.makeText(this, R.string.filement_service_stopped, Toast.LENGTH_SHORT).show();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    // This is the object that receives interactions from clients.  See
    // RemoteService for a more complete example.
    private final IBinder mBinder = new FilementBinder();

   
    
    /**
     * Show a notification while this service is running.
     */
    
    public void showNotification(boolean is_update) {
        // In this sample, we'll use the same text for the ticker and the expanded notification
    	CharSequence text;
    	if(startserverpid>0)
    	{
    		text = getText(R.string.filement_service_started);
    	}
    	else
    	{
    		text = getText(R.string.filement_service_no_conn);
    	}
    	
        // Set the icon, scrolling text and timestamp
        Notification notification = new Notification(R.drawable.ic_launcher, text,
                System.currentTimeMillis());
        /*
        Notification noti = new Notification.Builder(mContext)
        .setContentTitle("New mail from " + sender.toString())
        .setContentText(subject)
        .setSmallIcon(R.drawable.new_mail)
        .setLargeIcon(aBitmap)
        .build();
        */
        notification.flags |= Notification.FLAG_NO_CLEAR;
        notification.flags |= Notification.FLAG_FOREGROUND_SERVICE;
        // The PendingIntent to launch our activity if the user selects this notification
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0,
                new Intent(this, Filement.class), 0);

        
        
        // Set the info for the views that show in the notification panel.
        notification.setLatestEventInfo(this, getText(R.string.filement_service_label),
                       text, contentIntent);
        final int myID = 23122;
        // Send the notification.
        if(is_update)
        {
        	mNM.notify(NOTIFICATION, notification);
        }
        else
        {
        	startForeground(myID, notification);
        }
    }
    

    
    
	
    public static native int startserver(String dbpath,String mpath);
    public static native int killpid(int pid);
    
}
