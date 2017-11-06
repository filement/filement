using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.IO;
using System.Drawing;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using System.Threading;
using System.Diagnostics;
using Microsoft.Win32; 

namespace Filement
{
    public partial class Form1 : Form
    {
        Thread server_thread;

        private const int WM_NCHITTEST = 0x84;
        private const int HTCLIENT = 0x1;
        private const int HTCAPTION = 0x2;


        protected override void WndProc(ref Message message)
        {
            base.WndProc(ref message);

            if (message.Msg == WM_NCHITTEST && (int)message.Result == HTCLIENT)
                message.Result = (IntPtr)HTCAPTION;
        }


        [DllImport("filement_device_lib.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int filement_export_check_device(StringBuilder _loc, int _loc_len);

        [DllImport("filement_device_lib.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int filement_export_register_device(StringBuilder _loc, int _loc_len, StringBuilder _email, int _email_len, StringBuilder _pin, int _pin_len, StringBuilder _pass, int _pass_len, StringBuilder _dev_name, int _dev_name_len);

        [DllImport("filement_device_lib.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int filement_export_start_server(StringBuilder _loc, int _loc_len);

        [DllImport("filement_device_lib.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern void filement_export_exit();

        [DllImport("filement_device_lib.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern StringBuilder filement_export_get_version();

        [DllImport("filement_device_lib.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern void filement_export_exec_reinit(StringBuilder _loc);



        public Form1()
        {
            string[] args = Environment.GetCommandLineArgs();
            if (args.Length > 1 && args[1]=="reinit")
            {
                string db = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Filement");
                try
                {
                    string file = Path.Combine(db, "Filement.db");
                    File.Delete(file);
                }
                catch (Exception ex)
                {
                    MessageBox.Show("Can't delete the database file.");
                }

            }

           

            InitializeComponent();
            dname_box.Text=System.Environment.MachineName;

            string loc = System.Reflection.Assembly.GetExecutingAssembly().Location;

            if (filement_export_check_device(new StringBuilder(loc), loc.Length) == 1)
            {
                if (server_thread == null || server_thread.ThreadState != System.Threading.ThreadState.Running)
                {
                    server_thread = new Thread(new ThreadStart(Server_Start));
                    server_thread.Name = "Filement Server";
                    server_thread.Start();
                }
                Visible = false;
                WindowState = FormWindowState.Minimized;
            }
        }


        private static void RegisterInStartup(bool isChecked)
        {
            RegistryKey registryKey = Registry.CurrentUser.OpenSubKey
                    ("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", true);
            if (isChecked)
            {
                registryKey.SetValue("Filement", Application.ExecutablePath);
            }
            else
            {
                registryKey.DeleteValue("Filement");
            }
        }

        public static void Server_Start()
        {
            string loc=System.Reflection.Assembly.GetExecutingAssembly().Location;
            if (filement_export_start_server(new StringBuilder(loc),loc.Length) == 0)
            {
                MessageBox.Show("Problems with starting the server");
              //TODO: error
            }

          
           
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            base.OnFormClosing(e);

            if (e.CloseReason == CloseReason.WindowsShutDown) return;

            // Confirm user wants to close
            switch (MessageBox.Show(this, "Are you sure you want to close the application?", "Close Filement", MessageBoxButtons.YesNo))
            {
                case DialogResult.No:
                    e.Cancel = true;
                    break;
                default:
                    if (server_thread!=null && server_thread.ThreadState == System.Threading.ThreadState.Running)
                    {
                        server_thread.Abort();
                    }
                    filement_export_exit();
                    break;
            }
        }

        private void button1_Click(object sender, EventArgs e)
        {


            string loc = System.Reflection.Assembly.GetExecutingAssembly().Location;
            if (filement_export_check_device(new StringBuilder(loc), loc.Length) == 1)
            {
                
                if ( server_thread == null || server_thread.ThreadState != System.Threading.ThreadState.Running)
                {
                    server_thread = new Thread(new ThreadStart(Server_Start));
                    server_thread.Name = "Filement Server";
                    server_thread.Start();
                }

            }
            else
            {
                if (email_box.Text.Length == 0)
                {
                    MessageBox.Show("The Email is empty.");
                    return;
                }
                if (pin_box.Text.Length == 0)
                {
                    MessageBox.Show("The PIN is empty.");
                    return;
                } 
                if (dname_box.Text.Length == 0)
                {
                    MessageBox.Show("The Device Name is empty.");
                    return;
                }
                if (PasswordBox.Text.Length == 0)
                {
                    MessageBox.Show("The Device Password is empty.");
                    return;
                }
                if (filement_export_register_device(new StringBuilder(loc), loc.Length, new StringBuilder(email_box.Text), email_box.Text.Length, new StringBuilder(pin_box.Text), PasswordBox.Text.Length, new StringBuilder(pin_box.Text), PasswordBox.Text.Length, new StringBuilder(dname_box.Text), dname_box.Text.Length) == 1)
                {

                    if (server_thread == null || server_thread.ThreadState != System.Threading.ThreadState.Running)
                    {
                        server_thread = new Thread(new ThreadStart(Server_Start));
                        server_thread.Name = "Filement Server";
                        server_thread.Start();
                    }
                    RegisterInStartup(true);
                    MessageBox.Show("Your device is registered successfully.");

                    Visible = false;
                    WindowState = FormWindowState.Minimized; 
                }
                else
                {
                    MessageBox.Show("Problems with the registration.");
                }
            
            }
            
        
        }

        

        private void Form1_Resize(object sender, EventArgs e)
        {
            if (FormWindowState.Minimized == WindowState)
                Hide();
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            
        }

        private void Form1_Shown(object sender, EventArgs e)
        {
            
             
            
        }

        private void ctx_stop_server_Click(object sender, EventArgs e)
        {

            Application.Exit();
            
        }

        private void Form1_Deactivate(object sender, EventArgs e)
        {
            //server_thread.Abort();
        }

        private void Form1_Leave(object sender, EventArgs e)
        {
            

        }

        private void ctx_add_new_device_Click(object sender, EventArgs e)
        {
            switch (MessageBox.Show(this, "Are you sure? If you click \"Yes\" the settings will be removed.", "Reinit Filement", MessageBoxButtons.YesNo))
            {
                case DialogResult.No:
                   
                    break;
                default:
                    if (server_thread != null && server_thread.ThreadState == System.Threading.ThreadState.Running)
                    {
                        server_thread.Abort();
                    }
                      
                    string loc = System.Reflection.Assembly.GetExecutingAssembly().Location;
                    filement_export_exec_reinit(new StringBuilder(loc));

                    
                    Visible = true;
                    WindowState = FormWindowState.Normal;
                     
                    break;
            }
        }

        private void TrayIcon_MouseDoubleClick(object sender, MouseEventArgs e)
        {
           
        }

        private void TrayIcon_MouseClick(object sender, MouseEventArgs e)
        {
          
        }

        private void TrayIcon_Click(object sender, EventArgs e)
        {
            
        }

        private void Close_Click(object sender, EventArgs e)
        {
            Application.Exit();
        }

        private void label1_Click(object sender, EventArgs e)
        {

        }

        private void Link_Click(object sender, EventArgs e)
        {
            Process.Start("http://filement.com");
        }

        private void PairButton_MouseDown(object sender, MouseEventArgs e)
        {
            PairButton.Image = Filement.Properties.Resources.button_down;
        }

        private void PairButton_MouseUp(object sender, MouseEventArgs e)
        {
            PairButton.Image = Filement.Properties.Resources.button;
        }

        private void ShowPasswordBox_Click(object sender, EventArgs e)
        {
            if (PasswordBox.UseSystemPasswordChar)
            {
                PasswordBox.UseSystemPasswordChar = false;
                ShowPasswordBox.Image = Filement.Properties.Resources.hide;
            }
            else
            {
                PasswordBox.UseSystemPasswordChar = true;
                ShowPasswordBox.Image = Filement.Properties.Resources.view;
            }
        }

      

     
    }
}
