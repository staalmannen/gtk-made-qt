#ifndef __GTK__
#define __GTK__

#include "../gtk-made-qt.h" /*replaces glib */

#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QMap>
#include <QGroupBox>
#include <QActionGroup>

//#include <glib/glib.h>

typedef QWidget GtkBox; /* ugh */
typedef QWidget GtkContainer; /* ugh */
typedef QHBoxLayout GtkHBox;
typedef QVBoxLayout GtkVBox;
typedef QWidget GtkTable;

typedef QTextEdit GtkTextView;
	typedef QTextEdit GtkTextBuffer;
typedef QMenuBar GtkMenuBar;
typedef QMenu GtkMenuShell;
typedef QMenu GtkMenuItem;

typedef QLabel GtkLabel;

// XXX: will need porting for win32 (and non-gcc compilers..)
#define MARK_DEPRECATED __attribute((deprecated))


// Stupid GTK typecasting crap.

#define GTK_TEXT_VIEW(x) dynamic_cast<QTextEdit *>(x)
#define GTK_OBJECT(x) (void *)x /* obsolete */
#define GTK_MENU_BAR(x) dynamic_cast<QMenuBar *>(x)
#define GTK_LABEL(x) dynamic_cast<QLabel *>(x)
#define GTK_ABOUT_DIALOG(x) dynamic_cast<GQTAboutDialog *>(x)

// Stock stuff
#define GTK_STOCK_QUIT "gtk-quit"
#define GTK_STOCK_ABOUT "gtk-about"

enum GtkAttachOptions
{
	GTK_EXPAND,
	GTK_SHRINK,
	GTK_FILL
};

// Now, override the parts of glib that we need to.
#include <gtk/glib_signals.h>


#include <gtk/gqt_dialog.h>
#include <gtk/gtk_box.h>
#include <gtk/gtk_main.h>
#include <gtk/gtk_button.h>
#include <gtk/gtk_window.h>
#include <gtk/gtk_hbox.h>
#include <gtk/gtk_vbox.h>
#include <gtk/gtk_container.h>
#include <gtk/gtk_label.h>
#include <gtk/gtk_entry.h>
#include <gtk/gtk_table.h>
#include <gtk/gtk_combo.h>
#include <gtk/gtk_check_button.h>
#include <gtk/gtk_text_view.h>
#include <gtk/gtk_menu.h>
#include <gtk/gtk_about_dialog.h>
#include <gtk/gtk_widget.h>
#include <gtk/gtk_frame.h>
#include <gtk/gtk_action_group.h>
#include <gtk/gtk_ui_manager.h>


#endif
