/*
 * GTK 3 pieces Transmission uses, headless: built-in icons in the icon
 * theme (GtkIconInfo), tooltips from "query-tooltip" and a tree view's
 * tooltip column, the tree view's type-ahead search with the program's
 * equal function, combo box row separators, a file chooser button's
 * "selection-changed" after gtk_file_chooser_set_filename, action
 * sensitivity on tool items, and a message dialog sized to its text.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit (1); } } while (0)

static int queries, selection_changes, searches;

static gboolean on_query (GtkWidget *w, gint x, gint y, gboolean kb, GtkTooltip *tip, gpointer d)
{
  queries++;
  gtk_tooltip_set_markup (tip, "<b>hello</b>");
  return TRUE;
}

static gboolean is_sep (GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
  gchar *s;
  gtk_tree_model_get (m, it, 0, &s, -1);
  gboolean sep = s && !strcmp (s, "-");
  g_free (s);
  return sep;
}

/* FALSE: the row matches; here: the key is a suffix of the text */
static gboolean suffix_equal (GtkTreeModel *m, gint col, const gchar *key, GtkTreeIter *it, gpointer d)
{
  gchar *s;
  searches++;
  gtk_tree_model_get (m, it, col, &s, -1);
  gboolean match = s && g_str_has_suffix (s, key);
  g_free (s);
  return !match;
}

static void on_selection_changed (GtkFileChooser *c, gpointer d) { selection_changes++; }

static void send_key (GtkWidget *w, guint keyval, const char *text)
{
  GdkEvent *e = gdk_event_new (GDK_KEY_PRESS);
  e->key.window = gtk_widget_get_window (w);
  e->key.keyval = keyval;
  e->key.string = g_strdup (text);
  e->key.length = strlen (text);
  gtk_widget_event (w, e);
  gdk_event_free (e);
}

int main (int argc, char **argv)
{
  gtk_init (&argc, &argv);

  /* ---------- the icon theme ---------- */
  GtkIconTheme *theme = gtk_icon_theme_get_default ();
  CHECK (GTK_IS_ICON_THEME (theme));
  CHECK (!gtk_icon_theme_has_icon (theme, "gtkqt-test-turtle"));
  GdkPixbuf *px = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 24, 24);
  gdk_pixbuf_fill (px, 0x00ff00ff);
  gtk_icon_theme_add_builtin_icon ("gtkqt-test-turtle", 24, px);
  CHECK (gtk_icon_theme_has_icon (theme, "gtkqt-test-turtle"));
  GtkIconInfo *info = gtk_icon_theme_lookup_icon (theme, "gtkqt-test-turtle", 24, GTK_ICON_LOOKUP_USE_BUILTIN);
  CHECK (info && gtk_icon_info_get_filename (info) == NULL);   /* built in */
  GdkPixbuf *loaded = gtk_icon_info_load_icon (info, NULL);
  CHECK (loaded && gdk_pixbuf_get_width (loaded) == 24);
  g_object_unref (loaded);
  g_object_unref (info);
  const char *names[] = { "gtkqt-no-such-icon", "gtkqt-test-turtle", NULL };
  info = gtk_icon_theme_choose_icon (theme, names, 16, 0);
  CHECK (info);
  g_object_unref (info);
  CHECK (!gtk_icon_theme_lookup_icon (theme, "gtkqt-no-such-icon", 16, 0));
  /* widgets naming the icon get it */
  GtkWidget *img = gtk_image_new_from_icon_name ("gtkqt-test-turtle", GTK_ICON_SIZE_BUTTON);
  CHECK (gtk_image_get_storage_type (GTK_IMAGE (img)) == GTK_IMAGE_ICON_NAME);

  GtkWidget *win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_container_add (GTK_CONTAINER (win), box);

  /* ---------- tooltips ---------- */
  GtkWidget *label = gtk_label_new ("tip me");
  gtk_widget_set_has_tooltip (label, TRUE);
  CHECK (gtk_widget_get_has_tooltip (label));
  g_signal_connect (label, "query-tooltip", G_CALLBACK (on_query), NULL);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  /* ---------- a tree view: tooltip column, search ---------- */
  GtkListStore *ls = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
  const char *rows[] = { "apple", "banana", "cherry", "grape" };
  for (int i = 0; i < 4; i++) gtk_list_store_insert_with_values (ls, NULL, -1, 0, rows[i], 1, "<i>fruit</i>", -1);
  GtkWidget *view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (ls));
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), -1, "fruit", gtk_cell_renderer_text_new (), "text", 0, NULL);
  CHECK (gtk_tree_view_get_search_column (GTK_TREE_VIEW (view)) == 0);   /* the first text column */
  gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (view), 1);
  CHECK (gtk_widget_get_has_tooltip (view) && gtk_tree_view_get_tooltip_column (GTK_TREE_VIEW (view)) == 1);
  gtk_box_pack_start (GTK_BOX (box), view, TRUE, TRUE, 0);

  /* ---------- a combo box with a separator row ---------- */
  GtkListStore *cs = gtk_list_store_new (1, G_TYPE_STRING);
  gtk_list_store_insert_with_values (cs, NULL, -1, 0, "one", -1);
  gtk_list_store_insert_with_values (cs, NULL, -1, 0, "-", -1);
  gtk_list_store_insert_with_values (cs, NULL, -1, 0, "two", -1);
  GtkWidget *combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (cs));
  GtkCellRenderer *cr = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cr, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), cr, "text", 0, NULL);
  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (combo), is_sep, NULL, NULL);
  GList *cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (combo));
  CHECK (g_list_length (cells) == 1 && cells->data == cr);
  g_list_free (cells);
  gtk_box_pack_start (GTK_BOX (box), combo, FALSE, FALSE, 0);

  /* ---------- a file chooser button set by the program ---------- */
  GtkWidget *fcb = gtk_file_chooser_button_new ("pick", GTK_FILE_CHOOSER_ACTION_OPEN);
  g_signal_connect (fcb, "selection-changed", G_CALLBACK (on_selection_changed), NULL);
  gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (fcb), "/etc/hostname");
  gtk_box_pack_start (GTK_BOX (box), fcb, FALSE, FALSE, 0);

  /* ---------- actions on tool items ---------- */
  GtkActionGroup *ag = gtk_action_group_new ("g");
  GtkActionEntry entries[] = { { "go", "media-playback-start", "_Go", NULL, "Go!", NULL } };
  gtk_action_group_add_actions (ag, entries, 1, NULL);
  GtkUIManager *ui = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (ui, ag, 0);
  CHECK (gtk_ui_manager_add_ui_from_string (ui, "<ui><toolbar name='tb'><toolitem action='go'/></toolbar></ui>", -1, NULL));
  CHECK (g_list_length (gtk_ui_manager_get_action_groups (ui)) == 1);
  GtkWidget *tb = gtk_ui_manager_get_widget (ui, "/tb");
  GtkWidget *item = gtk_ui_manager_get_widget (ui, "/tb/go");
  CHECK (tb && item);
  gtk_box_pack_start (GTK_BOX (box), tb, FALSE, FALSE, 0);
  GtkAction *go = gtk_action_group_get_action (ag, "go");
  g_object_set (go, "sensitive", FALSE, "is-important", TRUE, NULL);
  CHECK (!gtk_widget_get_sensitive (item));
  CHECK (gtk_tool_item_get_is_important (GTK_TOOL_ITEM (item)));
  gtk_action_set_sensitive (go, TRUE);
  CHECK (gtk_widget_get_sensitive (item));
  GList *acts = gtk_action_group_list_actions (ag);
  CHECK (g_list_length (acts) == 1);
  g_list_free (acts);

  gtk_widget_show_all (win);
  while (gtk_events_pending ()) gtk_main_iteration ();
  CHECK (selection_changes >= 1);   /* from the main loop, as GTK does */

  /* type-ahead with the program's function: "rry" finds cherry */
  gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (view), suffix_equal, NULL, NULL);
  gtk_widget_grab_focus (view);
  send_key (view, GDK_KEY_r, "r");
  send_key (view, GDK_KEY_r, "r");
  send_key (view, GDK_KEY_y, "y");
  GtkTreePath *cursor = NULL;
  gtk_tree_view_get_cursor (GTK_TREE_VIEW (view), &cursor, NULL);
  CHECK (searches > 0);
  CHECK (cursor && gtk_tree_path_get_indices (cursor)[0] == 2);
  gtk_tree_path_free (cursor);

  /* tooltip context: the row under a point */
  GtkTreeModel *m;
  GtkTreePath *p;
  GtkTreeIter it;
  GdkRectangle r;
  GtkTreePath *p1 = gtk_tree_path_new_from_indices (1, -1);
  gtk_tree_view_get_cell_area (GTK_TREE_VIEW (view), p1, NULL, &r);
  gtk_tree_path_free (p1);
  gint wx, wy;
  gtk_tree_view_convert_bin_window_to_widget_coords (GTK_TREE_VIEW (view), 5, r.y + r.height / 2, &wx, &wy);
  CHECK (gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (view), &wx, &wy, FALSE, &m, &p, &it));
  CHECK (gtk_tree_path_get_indices (p)[0] == 1);
  gchar *name;
  gtk_tree_model_get (m, &it, 0, &name, -1);
  CHECK (!strcmp (name, "banana"));
  g_free (name);
  gtk_tree_path_free (p);

  /* GTK 3 colours from the style context */
  GdkRGBA fg, sel;
  gtk_style_context_get_color (gtk_widget_get_style_context (label), GTK_STATE_FLAG_NORMAL, &fg);
  gtk_style_context_get_color (gtk_widget_get_style_context (label), GTK_STATE_FLAG_SELECTED, &sel);
  CHECK (fg.alpha > 0 && (fg.red != sel.red || fg.green != sel.green || fg.blue != sel.blue));

  /* ---------- a message dialog as wide as about 50 characters ---------- */
  GtkWidget *md = gtk_message_dialog_new (GTK_WINDOW (win), 0, GTK_MESSAGE_OTHER, GTK_BUTTONS_NONE, "%s",
    "This is a fairly long message that GTK would wrap at about fifty characters, three lines or so in all.");
  gtk_dialog_add_button (GTK_DIALOG (md), "_OK", GTK_RESPONSE_OK);
  gtk_widget_show (md);
  while (gtk_events_pending ()) gtk_main_iteration ();
  GtkWidget *msg = GTK_MESSAGE_DIALOG (md)->label;
  GtkAllocation a;
  gtk_widget_get_allocation (msg, &a);
  CHECK (a.width > 300);   /* not squeezed to the dialog's minimum */
  int lw, lh;
  gtk_widget_get_size_request (msg, &lw, &lh);
  CHECK (lh > 0 && a.height >= lh);   /* all lines fit */
  gtk_widget_destroy (md);

  CHECK (queries == 0);   /* only when a tooltip is due */
  gtk_widget_destroy (win);
  printf ("gtk3misc: all checks passed\n");
  return 0;
}
