#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>				// GTK+

#include <gdk/gdkkeysyms.h>

#include "types.h"
#include "format.h"
#include "log.h"
#include "filement.h"
#include "io.h"
#include "device/startup.h"

// GTK+ 3 compatibility
#define gtk_box_new(o, s) (((o) == GTK_ORIENTATION_VERTICAL) ? gtk_vbox_new(TRUE, (s)) : gtk_hbox_new(TRUE, (s)))

#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 300

#define STATUS_WIDTH 240
#define STATUS_HEIGHT 150

#define DIALOG_WIDTH 400
#define DIALOG_HEIGHT 150

#define ICON_SMALL "/usr/share/icons/hicolor/48x48/apps/filement.png"
#define SHARE_LOGO (STARTUP_PREFIX "share/filement/logo.png")
#define SHARE_BACKGROUND (STARTUP_PREFIX "share/filement/background.png")

#define STARTUP_PREFIX "/.config/autostart/"
#define STARTUP_SUFFIX ".desktop"

#define STARTUP_HEAD \
	"[Desktop Entry]\n" \
	"Type=Application\n" \
	"Terminal=false\n" \
	"Exec="
#define STARTUP_TAIL "\n"

struct info
{
	struct string email;
	struct string devname;
	struct string password;
};

static bool registered = false;

static GtkWidget *window, *status, *progress, *button, *menu;
static GtkStatusIcon *icon;
static GtkEntryBuffer *buffer_email, *buffer_devname, *buffer_password;

static void *main_register(void *arg);

static gboolean enter(GtkWidget *widget, GdkEventKey *event, gpointer callback)
{
	if (event->keyval == GDK_Return)
	{
		((void (*)(GtkWidget *, gpointer))callback)(widget, 0);
		return TRUE;
	}
	else return FALSE;
}

static gboolean escape(GtkWidget *widget, GdkEventKey *event, gpointer callback)
{
	if (event->keyval == GDK_Escape)
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
	if (!info)
	{
		error(logs("Memory allocation error."));
		_exit(1);
	}

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

static char *startup_filename(char *command, size_t command_size)
{
	char *home = getenv("HOME");
	size_t home_size = strlen(home);

	char *filename = malloc(home_size + sizeof(STARTUP_PREFIX) - 1 + command_size + sizeof(STARTUP_SUFFIX)), *end;
	format_bytes(format_bytes(format_bytes(format_bytes(filename, home, home_size), STARTUP_PREFIX, sizeof(STARTUP_PREFIX) - 1), command, command_size), STARTUP_SUFFIX, sizeof(STARTUP_SUFFIX));

	return filename;
}

// TODO show error messages with the GUI?
static bool startup_gtk_add(const struct string *command)
{
	char *filename;
	int file;

	char *buffer;
	size_t buffer_size;

	bool success;

	filename = startup_filename(command->data, command->length);
	if (!filename)
		return false;

	buffer = malloc(sizeof(STARTUP_HEAD) - 1 + command->length + sizeof(STARTUP_TAIL));
	if (buffer < 0)
	{
		free(filename);
		return false;
	}
	format_bytes(format_bytes(format_bytes(buffer, STARTUP_HEAD, sizeof(STARTUP_HEAD) - 1), command->data, command->length), STARTUP_TAIL, sizeof(STARTUP_TAIL));

	// TODO create directory if it doesn't exist
	file = creat(filename, 0644);
	if (file < 0)
	{
		error(logs("Cannot create startup item for "), logs(command->data, command->length), logs("."));
		free(buffer);
		free(filename);
		return false;
	}

	success = writeall(file, buffer, buffer_size);

	close(file);
	free(buffer);

	if (!success)
	{
		error(logs("Cannot write to startup item for "), logs(command->data, command->length), logs("."));
		unlink(filename);
	}

	free(filename);

	return success;
}

static bool startup_gtk_remove(const struct string *command)
{
	char *filename = startup_filename(command->data, command->length);
	if (filename)
	{
		bool success = (unlink(filename) == 0);
		free(filename);
		return (success ? success : (errno == ENOENT));
	}
	return false;
}

static void register_finish(GtkWidget *widget, gpointer data)
{
	gtk_widget_hide(status);
	gtk_widget_destroy(status);
	gtk_widget_set_sensitive(button, TRUE);

	if (registered)
	{
		struct string filement_gtk = string("filement-gtk");

		// Registration is successful. Filement will now work as a daemon.
		// Destroy the interface and add startup item.

		gtk_widget_hide(window);
		gtk_widget_destroy(window);
		gtk_main_quit();

		startup_gtk_add(&filement_gtk);
	}
}

static void *main_register(void *arg)
{
	struct info *info = arg;

	registered = filement_register(&info->email, &info->devname, &info->password);

	gdk_threads_enter(); // TODO is this sufficient to prevent race conditions?

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
	// Create main interface window.
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(ICON_SMALL, 0);
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

static void menu_cancel(GtkWidget *widget, gpointer data)
{
	gtk_widget_hide(status);
	gtk_widget_destroy(status);
}

static void menu_reset(GtkWidget *widget, gpointer data)
{
	struct string filement_gtk = string("filement-gtk");

	gtk_widget_hide(status);
	gtk_widget_destroy(status);

	gtk_main_quit();

	startup_gtk_remove(&filement_gtk);
	filement_reset(); // terminates the program
}

static void if_menu_reset(GtkMenuItem *item, gpointer data)
{
	status = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(status), DIALOG_WIDTH, DIALOG_HEIGHT);
	gtk_widget_set_size_request(status, DIALOG_WIDTH, DIALOG_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(status), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(status), FALSE);
	gtk_window_set_deletable(GTK_WINDOW(status), FALSE);
	gtk_window_set_position(GTK_WINDOW(status), GTK_WIN_POS_CENTER);
	gtk_window_set_modal(GTK_WINDOW(status), TRUE);

	GtkWidget *rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(status), rows);

	GtkWidget *label = gtk_label_new("Resetting Filement will delete your local device settings.\nDo you want to reset Filement?");
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(rows), label, TRUE, TRUE, 0);

	GtkWidget *row = gtk_fixed_new();
	gtk_container_add(GTK_CONTAINER(rows), row);

	GtkWidget *cancel = gtk_button_new_with_label("Cancel");
	position(row, cancel, 60, 10, 80, 24);
	g_signal_connect(cancel, "clicked", G_CALLBACK(menu_cancel), 0);

	GtkWidget *reset = gtk_button_new_with_label("Reset");
	position(row, reset, 250, 10, 80, 24);
	g_signal_connect(reset, "clicked", G_CALLBACK(menu_reset), 0);

	g_signal_connect(status, "key-press-event", G_CALLBACK(escape), menu_cancel);

	gtk_widget_show_all(status);
}

static void if_menu_quit(GtkMenuItem *item, gpointer data)
{
	gtk_main_quit();
}

static void if_menu_open(GtkStatusIcon *icon, gpointer data)
{
	GtkWidget *item;

	menu = gtk_menu_new();

	item = gtk_menu_item_new_with_label("Reset Filement");
	g_signal_connect(item, "activate", G_CALLBACK(if_menu_reset), 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_label("Quit Filement");
	g_signal_connect(item, "activate", G_CALLBACK(if_menu_quit), 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), 0, 0, 0, 0, 0, gtk_get_current_event_time()); // TODO better positioning with argument 4
}

static void interface_wait(void)
{
	icon = gtk_status_icon_new();
	gtk_status_icon_set_from_file(icon, ICON_SMALL);
	//g_signal_connect(icon, "activate", G_CALLBACK(if_menu_open), 0);
	g_signal_connect(icon, "popup-menu", G_CALLBACK(if_menu_open), 0);

	gtk_status_icon_set_visible(icon, TRUE);

	gtk_main();
}

static void *main_server(void *arg)
{
    filement_serve();
    return 0;
}

int main(int argc, char *argv[])
{
	int registered;

	startup_add = &startup_gtk_add;
	startup_remove = &startup_gtk_remove;

	filement_daemon();
	registered = filement_init();

#if defined(FILEMENT_UPGRADE)
	// Check for new version of the Filement device software.
	// WARNING: This will not upgrade the gtk+ specific code.
	if (!filement_upgrade("filement"))
		error(logs("Upgrade failed"));
#endif

	gdk_threads_init();
	gtk_init(&argc, &argv);

	// Initialize the device. If it is not registered, display registration window.
	// Start serving after the initialization is complete.
	if (registered || interface_register())
	{
		pthread_t thread;

		pthread_create(&thread, 0, &main_server, 0);
		pthread_detach(thread);

		interface_wait();
	}

	return 0;
}
