#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>				// GTK+

#include <gdk/gdkkeysyms.h>

#include "types.h"
#include "format.h"
#include "filement.h"

#include "log.h"

// GTK+ 3 compatibility
#define gtk_box_new(o, s) (((o) == GTK_ORIENTATION_VERTICAL) ? gtk_vbox_new(TRUE, (s)) : gtk_hbox_new(TRUE, (s)))

#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 300

#define STATUS_WIDTH 240
#define STATUS_HEIGHT 150

#define SHARE_FILEMENT "/usr/share/icons/hicolor/256x256/apps/filement.png"
#define SHARE_LOGO (PREFIX "share/filement/logo.png")
#define SHARE_BACKGROUND (PREFIX "share/filement/background.png")

#define PATH_RELATIVE "/.config/autostart/filement.desktop"

#define BUFFER_SIZE 4096

// TODO make this work on FreeBSD as well (sendfile() is linux-specific)

// TODO: icon in doc that can be used to reset or stop the device

struct info
{
	struct string email;
	struct string devname;
	struct string password;
};

int create_directory(struct string *restrict filename);

static bool registered = false;

static GtkWidget *window, *status, *progress, *button;
static GtkEntryBuffer *buffer_email, *buffer_devname, *buffer_password;

static void register_start(GtkWidget *widget, gpointer data);
static void *main_register(void *arg);
static void register_finish(GtkWidget *widget, gpointer success);

static gboolean enter(GtkWidget *widget, GdkEventKey *event, gpointer callback)
{
	if (event->keyval == GDK_Return)
	{
		((void (*)(GtkWidget *, gpointer))callback)(widget, 0);
		return TRUE;
	}
	else return FALSE;
}

static gboolean interface_quit(GtkWidget *window, gpointer data)
{
	gtk_main_quit();
	return FALSE;
}

static void position(GtkWidget *container, GtkWidget *widget, gint x, gint y, gint w, gint h)
{
	gtk_fixed_put(GTK_FIXED(container), widget, x, y);
	gtk_widget_set_size_request(widget, w, h);
}

static void register_start(GtkWidget *widget, gpointer data)
{
	struct info *info = malloc(sizeof(struct info));
	if (!info) ; // TODO

	gtk_spinner_start(GTK_SPINNER(progress));
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(progress);

	info->email = string((char *)gtk_entry_buffer_get_text(buffer_email), gtk_entry_buffer_get_bytes(buffer_email));
	info->devname = string((char *)gtk_entry_buffer_get_text(buffer_devname), gtk_entry_buffer_get_bytes(buffer_devname));
	info->password = string((char *)gtk_entry_buffer_get_text(buffer_password), gtk_entry_buffer_get_bytes(buffer_password));

	pthread_t thread_id;
	pthread_create(&thread_id, 0, &main_register, info);
	pthread_detach(thread_id);
}

static void register_finish(GtkWidget *widget, gpointer data)
{
	gtk_widget_hide(status);
	gtk_widget_destroy(status);
	gtk_widget_set_sensitive(button, TRUE);

	// TODO show error messages with the GUI

	if (registered)
	{
		// Registration is successful. Filement will now work as a daemon.
		// Destroy the interface and add startup item.

		gtk_widget_hide(window);
		gtk_widget_destroy(window);
		gtk_main_quit();

		// Open source file and collect data about it.
		struct stat info;
		int src = open("/usr/share/applications/filement.desktop", O_RDONLY);
		if (src < 0)
		{
			_exit(1); // TODO print error
		}
		if (fstat(src, &info) < 0)
		{
			_exit(1); // TODO print error
		}

		// Generate destination file name.
		char *home = getenv("HOME");
		size_t home_length = strlen(home);
		char startup[BUFFER_SIZE];
		if ((home_length + sizeof(PATH_RELATIVE) - 1) >= BUFFER_SIZE)
		{
			_exit(1); // TODO print error
		}
		*format_bytes(format_bytes(startup, home, home_length), PATH_RELATIVE, sizeof(PATH_RELATIVE) - 1) = 0;

		// Write the destination file.
		// TODO create directory if it doesn't exist
		int dest = creat(startup, 0644);
		if (dest < 0)
			error(logs("Cannot create startup item for filement."));
		if (sendfile(dest, src, 0, (size_t)info.st_size) < 0)
			error(logs("Cannot add startup information for filement."));

		close(dest);
		close(src);
	}
}

static void *main_register(void *arg)
{
	struct info *info = arg;

	registered = filement_register(&info->email, &info->devname, &info->password);

	gdk_threads_enter();

	status = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(status), STATUS_WIDTH, STATUS_HEIGHT);
	gtk_widget_set_size_request(status, STATUS_WIDTH, STATUS_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(status), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(status), FALSE);
	gtk_window_set_deletable(GTK_WINDOW(status), FALSE);
	gtk_window_set_position(GTK_WINDOW(status), GTK_WIN_POS_CENTER);
	gtk_window_set_modal(GTK_WINDOW(status), TRUE);

	GtkWidget *rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(status), rows);

	GtkWidget *label = gtk_label_new((registered ? "Device registered successfully." : "Registration error."));
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(rows), label, TRUE, TRUE, 0);

	GtkWidget *row = gtk_alignment_new(0.5, 0.5, 0, 0);
	gtk_container_add(GTK_CONTAINER(rows), row);

	GtkWidget *button = gtk_button_new_with_label("   OK   ");
	gtk_container_add(GTK_CONTAINER(row), button);
	g_signal_connect(button, "clicked", G_CALLBACK(register_finish), 0);
	g_signal_connect(status, "key-press-event", G_CALLBACK(enter), register_finish);

	gtk_spinner_stop(GTK_SPINNER(progress));
	gtk_widget_hide(progress);
	gtk_widget_show_all(status);

	gdk_threads_leave();

	free(info);
	return 0;
}

static bool interface_register(void)
{
	gdk_threads_init();

	// TODO: is it a good idea to do this?
	int argc = 1;
	char *args[] = {"filement", 0};
	char **argv = args;
	gtk_init(&argc, &argv);

	// Create main interface window.
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(SHARE_FILEMENT, 0);
	if (!pixbuf) ; // TODO
	gtk_window_set_icon(GTK_WINDOW(window), pixbuf);
	gtk_window_set_title(GTK_WINDOW(window), "Filement");
	gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_widget_set_size_request(window, WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	// Set window background.
	pixbuf = gdk_pixbuf_new_from_file_at_scale(SHARE_BACKGROUND, WINDOW_WIDTH, WINDOW_HEIGHT, FALSE, 0);
	GdkPixmap *background;
	if (!pixbuf) ; // TODO
	gdk_pixbuf_render_pixmap_and_mask(pixbuf, &background, 0, 0);
	GtkStyle *style = gtk_style_new();
	style->bg_pixmap[0] = background;
	gtk_widget_set_style(window, style);

	GdkColor color_text = {.red = ~0, .green = ~0, .blue = ~0};
	GtkBorder border = {.left = 0, .right = 0, .top = 0, .bottom = 0};

	GtkWidget *container = gtk_fixed_new();
	gtk_container_add(GTK_CONTAINER(window), container);

	GtkWidget *logo = gtk_image_new_from_file(SHARE_LOGO);
	gtk_fixed_put(GTK_FIXED(container), logo, 20, 20);

	// device name
	{
		char hostname[256];

		GtkWidget *label = gtk_label_new("Device name");
		gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color_text);
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		position(container, label, 80, 80, 160, 20);

		if (gethostname(hostname, sizeof(hostname)) < 0) ; // TODO

		buffer_devname = gtk_entry_buffer_new(hostname, strlen(hostname)); // TODO strlen() is slow
		GtkWidget *entry = gtk_entry_new_with_buffer(buffer_devname);
		position(container, entry, 260, 78, 200, 24);
	}

	// e-mail
	{
		GtkWidget *label = gtk_label_new("E-mail");
		gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color_text);
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		position(container, label, 80, 140, 160, 20);

		buffer_email = gtk_entry_buffer_new("", 0);
		GtkWidget *entry = gtk_entry_new_with_buffer(buffer_email);
		position(container, entry, 260, 138, 200, 24);
	}

	// device password
	{
		GtkWidget *label = gtk_label_new("Device password");
		gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color_text);
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		position(container, label, 80, 200, 160, 20);

		buffer_password = gtk_entry_buffer_new("", 0);
		GtkWidget *entry = gtk_entry_new_with_buffer(buffer_password);
		position(container, entry, 260, 198, 200, 24);

		gtk_entry_set_invisible_char(GTK_ENTRY(entry), ' ');
		gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
	}

	// connect button and progress
	{
		button = gtk_button_new_with_label("Connect");
		position(container, button, 260, 238, 80, 24);
		g_signal_connect(button, "clicked", G_CALLBACK(register_start), 0);

		progress = gtk_spinner_new();
		gtk_widget_modify_fg(progress, GTK_STATE_NORMAL, &color_text);
		gtk_widget_set_no_show_all(progress, TRUE);
		position(container, progress, 350, 240, 20, 20);
	}

	// filement domain
	{
		GtkWidget *label = gtk_label_new("");
		gtk_label_set_markup(GTK_LABEL(label), "<small>filement.com</small>");
		gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color_text);
		gtk_misc_set_alignment(GTK_MISC(label), 1, 1);
		position(container, label, 310, 265, 150, 15);
	}

	g_signal_connect(window, "key-press-event", G_CALLBACK(enter), register_start);
	g_signal_connect(window, "delete-event", G_CALLBACK(interface_quit), 0);

	gtk_widget_show_all(window);
	gtk_main();
	return registered;
}

int main(int argc, char *argv[])
{
	// Initialize the device. If it is not registered, display registration window.
	// Start serving after the initialization is complete.
	filement_daemon();
	if (filement_init() || interface_register())
	{
		// Check for new version of the Filement device software.
		//filement_upgrade(); // TODO report error here

		filement_serve();
	}

	return 0;
}
