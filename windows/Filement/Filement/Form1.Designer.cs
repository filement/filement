namespace Filement
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(Form1));
            this.dname_box = new System.Windows.Forms.TextBox();
            this.email_box = new System.Windows.Forms.TextBox();
            this.pin_box = new System.Windows.Forms.TextBox();
            this.TrayIcon = new System.Windows.Forms.NotifyIcon(this.components);
            this.TrayMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.ctx_add_new_device = new System.Windows.Forms.ToolStripMenuItem();
            this.ctx_stop_server = new System.Windows.Forms.ToolStripMenuItem();
            this.aboutToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.AboutVersion = new System.Windows.Forms.ToolStripMenuItem();
            this.filementcomToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.Close = new System.Windows.Forms.PictureBox();
            this.PasswordBox = new System.Windows.Forms.TextBox();
            this.PairButton = new System.Windows.Forms.PictureBox();
            this.Link = new System.Windows.Forms.PictureBox();
            this.ShowPasswordBox = new System.Windows.Forms.PictureBox();
            this.TrayMenu.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.Close)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.PairButton)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.Link)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.ShowPasswordBox)).BeginInit();
            this.SuspendLayout();
            // 
            // dname_box
            // 
            this.dname_box.BorderStyle = System.Windows.Forms.BorderStyle.None;
            resources.ApplyResources(this.dname_box, "dname_box");
            this.dname_box.Name = "dname_box";
            // 
            // email_box
            // 
            this.email_box.BorderStyle = System.Windows.Forms.BorderStyle.None;
            resources.ApplyResources(this.email_box, "email_box");
            this.email_box.Name = "email_box";
            // 
            // pin_box
            // 
            this.pin_box.BorderStyle = System.Windows.Forms.BorderStyle.None;
            resources.ApplyResources(this.pin_box, "pin_box");
            this.pin_box.Name = "pin_box";
            // 
            // TrayIcon
            // 
            this.TrayIcon.ContextMenuStrip = this.TrayMenu;
            resources.ApplyResources(this.TrayIcon, "TrayIcon");
            this.TrayIcon.Click += new System.EventHandler(this.TrayIcon_Click);
            this.TrayIcon.MouseClick += new System.Windows.Forms.MouseEventHandler(this.TrayIcon_MouseClick);
            this.TrayIcon.MouseDoubleClick += new System.Windows.Forms.MouseEventHandler(this.TrayIcon_MouseDoubleClick);
            // 
            // TrayMenu
            // 
            this.TrayMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.ctx_add_new_device,
            this.ctx_stop_server,
            this.aboutToolStripMenuItem});
            this.TrayMenu.Name = "TrayMenu";
            resources.ApplyResources(this.TrayMenu, "TrayMenu");
            // 
            // ctx_add_new_device
            // 
            this.ctx_add_new_device.Name = "ctx_add_new_device";
            resources.ApplyResources(this.ctx_add_new_device, "ctx_add_new_device");
            this.ctx_add_new_device.Click += new System.EventHandler(this.ctx_add_new_device_Click);
            // 
            // ctx_stop_server
            // 
            this.ctx_stop_server.Name = "ctx_stop_server";
            resources.ApplyResources(this.ctx_stop_server, "ctx_stop_server");
            this.ctx_stop_server.Click += new System.EventHandler(this.ctx_stop_server_Click);
            // 
            // aboutToolStripMenuItem
            // 
            this.aboutToolStripMenuItem.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.AboutVersion,
            this.filementcomToolStripMenuItem});
            this.aboutToolStripMenuItem.Name = "aboutToolStripMenuItem";
            resources.ApplyResources(this.aboutToolStripMenuItem, "aboutToolStripMenuItem");
            // 
            // AboutVersion
            // 
            this.AboutVersion.ForeColor = System.Drawing.Color.FromArgb(((int)(((byte)(64)))), ((int)(((byte)(64)))), ((int)(((byte)(64)))));
            this.AboutVersion.Name = "AboutVersion";
            resources.ApplyResources(this.AboutVersion, "AboutVersion");
            // 
            // filementcomToolStripMenuItem
            // 
            this.filementcomToolStripMenuItem.ForeColor = System.Drawing.Color.DodgerBlue;
            this.filementcomToolStripMenuItem.Name = "filementcomToolStripMenuItem";
            resources.ApplyResources(this.filementcomToolStripMenuItem, "filementcomToolStripMenuItem");
            this.filementcomToolStripMenuItem.Click += new System.EventHandler(this.Link_Click);
            // 
            // Close
            // 
            this.Close.BackColor = System.Drawing.Color.Transparent;
            this.Close.Cursor = System.Windows.Forms.Cursors.Hand;
            this.Close.Image = global::Filement.Properties.Resources.close;
            resources.ApplyResources(this.Close, "Close");
            this.Close.Name = "Close";
            this.Close.TabStop = false;
            this.Close.Click += new System.EventHandler(this.Close_Click);
            // 
            // PasswordBox
            // 
            this.PasswordBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            resources.ApplyResources(this.PasswordBox, "PasswordBox");
            this.PasswordBox.Name = "PasswordBox";
            this.PasswordBox.UseSystemPasswordChar = true;
            // 
            // PairButton
            // 
            this.PairButton.BackColor = System.Drawing.Color.Transparent;
            this.PairButton.Cursor = System.Windows.Forms.Cursors.Hand;
            this.PairButton.Image = global::Filement.Properties.Resources.button;
            resources.ApplyResources(this.PairButton, "PairButton");
            this.PairButton.Name = "PairButton";
            this.PairButton.TabStop = false;
            this.PairButton.Click += new System.EventHandler(this.button1_Click);
            this.PairButton.MouseDown += new System.Windows.Forms.MouseEventHandler(this.PairButton_MouseDown);
            this.PairButton.MouseUp += new System.Windows.Forms.MouseEventHandler(this.PairButton_MouseUp);
            // 
            // Link
            // 
            this.Link.BackColor = System.Drawing.Color.Transparent;
            this.Link.Cursor = System.Windows.Forms.Cursors.Hand;
            resources.ApplyResources(this.Link, "Link");
            this.Link.Name = "Link";
            this.Link.TabStop = false;
            this.Link.Click += new System.EventHandler(this.Link_Click);
            // 
            // ShowPasswordBox
            // 
            this.ShowPasswordBox.BackColor = System.Drawing.Color.Transparent;
            this.ShowPasswordBox.Cursor = System.Windows.Forms.Cursors.Hand;
            this.ShowPasswordBox.Image = global::Filement.Properties.Resources.view;
            resources.ApplyResources(this.ShowPasswordBox, "ShowPasswordBox");
            this.ShowPasswordBox.Name = "ShowPasswordBox";
            this.ShowPasswordBox.TabStop = false;
            this.ShowPasswordBox.Click += new System.EventHandler(this.ShowPasswordBox_Click);
            // 
            // Form1
            // 
            resources.ApplyResources(this, "$this");
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.BackColor = System.Drawing.Color.White;
            this.BackgroundImage = global::Filement.Properties.Resources.app;
            this.Controls.Add(this.ShowPasswordBox);
            this.Controls.Add(this.Link);
            this.Controls.Add(this.PairButton);
            this.Controls.Add(this.PasswordBox);
            this.Controls.Add(this.Close);
            this.Controls.Add(this.pin_box);
            this.Controls.Add(this.email_box);
            this.Controls.Add(this.dname_box);
            this.ForeColor = System.Drawing.SystemColors.ControlText;
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.None;
            this.MaximizeBox = false;
            this.Name = "Form1";
            this.ShowInTaskbar = false;
            this.Deactivate += new System.EventHandler(this.Form1_Deactivate);
            this.Load += new System.EventHandler(this.Form1_Load);
            this.Shown += new System.EventHandler(this.Form1_Shown);
            this.Leave += new System.EventHandler(this.Form1_Leave);
            this.Resize += new System.EventHandler(this.Form1_Resize);
            this.TrayMenu.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.Close)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.PairButton)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.Link)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.ShowPasswordBox)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.TextBox dname_box;
        private System.Windows.Forms.TextBox email_box;
        private System.Windows.Forms.TextBox pin_box;
        private System.Windows.Forms.NotifyIcon TrayIcon;
        private System.Windows.Forms.ContextMenuStrip TrayMenu;
        private System.Windows.Forms.ToolStripMenuItem ctx_add_new_device;
        private System.Windows.Forms.ToolStripMenuItem ctx_stop_server;
        private System.Windows.Forms.PictureBox Close;
        private System.Windows.Forms.TextBox PasswordBox;
        private System.Windows.Forms.PictureBox PairButton;
        private System.Windows.Forms.PictureBox Link;
        private System.Windows.Forms.ToolStripMenuItem aboutToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem AboutVersion;
        private System.Windows.Forms.ToolStripMenuItem filementcomToolStripMenuItem;
        private System.Windows.Forms.PictureBox ShowPasswordBox;
    }
}

