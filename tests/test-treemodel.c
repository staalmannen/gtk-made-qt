/*
 * Tree models through the GtkTreeModel interface, GTK 3 API level, run
 * headless: sorting a store (rows-reordered), GtkTreeModelFilter (visible
 * function and column, refilter, changes in the child, trees, conversions,
 * a modify function, a virtual root), GtkTreeModelSort (sort functions,
 * child changes, conversions), a filter under a sort model shown in a tree
 * view, a model the program writes itself, and row references.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit (1); } } while (0)

static int reordered, inserted, deleted, changed;
static void on_reordered (GtkTreeModel *m, GtkTreePath *p, GtkTreeIter *it, gint *order, gpointer d) { reordered++; }
static void on_inserted (GtkTreeModel *m, GtkTreePath *p, GtkTreeIter *it, gpointer d) { inserted++; }
static void on_deleted (GtkTreeModel *m, GtkTreePath *p, gpointer d) { deleted++; }
static void on_changed (GtkTreeModel *m, GtkTreePath *p, GtkTreeIter *it, gpointer d) { changed++; }

/* the n-th row's int column 0 */
static int nth (GtkTreeModel *m, GtkTreeIter *parent, int n)
{
  GtkTreeIter it;
  int v = -1;
  if (gtk_tree_model_iter_nth_child (m, &it, parent, n)) gtk_tree_model_get (m, &it, 0, &v, -1);
  return v;
}

static gboolean even_only (GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
  int v;
  gtk_tree_model_get (m, it, 0, &v, -1);
  return v % 2 == 0;
}

static gint by_value_desc (GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b, gpointer d)
{
  int x, y;
  gtk_tree_model_get (m, a, 0, &x, -1);
  gtk_tree_model_get (m, b, 0, &y, -1);
  return y - x;
}

static void doubled (GtkTreeModel *m, GtkTreeIter *it, GValue *value, gint column, gpointer d)
{
  GtkTreeModel *child = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (m));
  GtkTreeIter ci;
  int v;
  gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (m), &ci, it);
  gtk_tree_model_get (child, &ci, 0, &v, -1);
  if (column == 0) g_value_set_int (value, v * 2);
  else g_value_take_string (value, g_strdup_printf ("#%d", v));
}

/* ---------- a model the program writes: the squares 0..n-1 ---------- */
typedef struct { GObject parent; int n; gint stamp; } Squares;
typedef struct { GObjectClass parent_class; } SquaresClass;
static void squares_iface_init (GtkTreeModelIface *iface);
G_DEFINE_TYPE_WITH_CODE (Squares, squares, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, squares_iface_init))
static void squares_init (Squares *s) { s->n = 5; s->stamp = 42; }
static void squares_class_init (SquaresClass *k) {}
static GtkTreeModelFlags sq_flags (GtkTreeModel *m) { return GTK_TREE_MODEL_LIST_ONLY; }
static gint sq_n_columns (GtkTreeModel *m) { return 1; }
static GType sq_column_type (GtkTreeModel *m, gint i) { return G_TYPE_INT; }
static gboolean sq_set (Squares *s, GtkTreeIter *it, int i)
{
  if (i < 0 || i >= s->n) return FALSE;
  it->stamp = s->stamp;
  it->user_data = GINT_TO_POINTER (i);
  return TRUE;
}
static gboolean sq_get_iter (GtkTreeModel *m, GtkTreeIter *it, GtkTreePath *p)
{
  return gtk_tree_path_get_depth (p) == 1 && sq_set ((Squares *) m, it, gtk_tree_path_get_indices (p)[0]);
}
static GtkTreePath *sq_get_path (GtkTreeModel *m, GtkTreeIter *it) { return gtk_tree_path_new_from_indices (GPOINTER_TO_INT (it->user_data), -1); }
static void sq_get_value (GtkTreeModel *m, GtkTreeIter *it, gint col, GValue *v)
{
  int i = GPOINTER_TO_INT (it->user_data);
  g_value_init (v, G_TYPE_INT);
  g_value_set_int (v, i * i);
}
static gboolean sq_iter_next (GtkTreeModel *m, GtkTreeIter *it) { return sq_set ((Squares *) m, it, GPOINTER_TO_INT (it->user_data) + 1); }
static gboolean sq_iter_nth_child (GtkTreeModel *m, GtkTreeIter *it, GtkTreeIter *parent, gint n) { return !parent && sq_set ((Squares *) m, it, n); }
static gboolean sq_iter_children (GtkTreeModel *m, GtkTreeIter *it, GtkTreeIter *parent) { return sq_iter_nth_child (m, it, parent, 0); }
static gboolean sq_iter_has_child (GtkTreeModel *m, GtkTreeIter *it) { return FALSE; }
static gint sq_iter_n_children (GtkTreeModel *m, GtkTreeIter *it) { return it ? 0 : ((Squares *) m)->n; }
static gboolean sq_iter_parent (GtkTreeModel *m, GtkTreeIter *it, GtkTreeIter *child) { return FALSE; }
static void squares_iface_init (GtkTreeModelIface *i)
{
  i->get_flags = sq_flags; i->get_n_columns = sq_n_columns; i->get_column_type = sq_column_type;
  i->get_iter = sq_get_iter; i->get_path = sq_get_path; i->get_value = sq_get_value; i->iter_next = sq_iter_next;
  i->iter_children = sq_iter_children; i->iter_has_child = sq_iter_has_child; i->iter_n_children = sq_iter_n_children;
  i->iter_nth_child = sq_iter_nth_child; i->iter_parent = sq_iter_parent;
}

static int view_rows (GtkWidget *view)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
  gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
  gtk_tree_selection_select_all (sel);
  int n = gtk_tree_selection_count_selected_rows (sel);
  gtk_tree_selection_unselect_all (sel);
  return n;
}

/* the view's text in a row of its first column */
static char *view_text (GtkWidget *view, int row)
{
  GtkTreeModel *m = gtk_tree_view_get_model (GTK_TREE_VIEW (view));
  GtkTreePath *p = gtk_tree_path_new_from_indices (row, -1);
  GtkTreeIter it;
  char *s = NULL;
  int v = -1;
  if (gtk_tree_model_get_iter (m, &it, p)) gtk_tree_model_get (m, &it, 0, &v, -1);
  gtk_tree_path_free (p);
  s = g_strdup_printf ("%d", v);
  return s;
}

int main (int argc, char **argv)
{
  gtk_init (&argc, &argv);
  GtkTreeIter it, it2;

  /* ---------- interfaces ---------- */
  GtkListStore *ls = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);
  CHECK (GTK_IS_TREE_MODEL (ls) && GTK_IS_TREE_SORTABLE (ls));
  CHECK (GTK_TREE_MODEL_GET_IFACE (ls)->get_n_columns (GTK_TREE_MODEL (ls)) == 2);
  int vals[] = { 5, 2, 8, 1, 6, 3 };
  for (int i = 0; i < 6; i++) gtk_list_store_insert_with_values (ls, NULL, -1, 0, vals[i], 1, "x", -1);
  GtkTreeModel *m = GTK_TREE_MODEL (ls);

  /* ---------- a store sorting itself ---------- */
  g_signal_connect (ls, "rows-reordered", G_CALLBACK (on_reordered), NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (ls), 0, GTK_SORT_ASCENDING);
  CHECK (reordered == 1);
  CHECK (nth (m, NULL, 0) == 1 && nth (m, NULL, 5) == 8);
  /* a changed row moves to its place */
  gtk_tree_model_iter_nth_child (m, &it, NULL, 0);
  gtk_list_store_set (ls, &it, 0, 7, -1);
  CHECK (nth (m, NULL, 0) == 2 && nth (m, NULL, 4) == 7);
  /* inserted with values: sorted in */
  gtk_list_store_insert_with_values (ls, NULL, 0, 0, 4, -1);
  CHECK (nth (m, NULL, 2) == 4);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (ls), 0, by_value_desc, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (ls), 0, GTK_SORT_ASCENDING);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (ls), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
  /* values now: 8 7 6 5 4 3 2 */
  CHECK (nth (m, NULL, 0) == 8 && nth (m, NULL, 6) == 2);

  /* ---------- the filter ---------- */
  GtkTreeModel *f = gtk_tree_model_filter_new (m, NULL);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (f), even_only, NULL, NULL);
  CHECK (gtk_tree_model_iter_n_children (f, NULL) == 4);          /* 8 6 4 2 */
  CHECK (nth (f, NULL, 1) == 6);
  inserted = deleted = changed = 0;
  g_signal_connect (f, "row-inserted", G_CALLBACK (on_inserted), NULL);
  g_signal_connect (f, "row-deleted", G_CALLBACK (on_deleted), NULL);
  g_signal_connect (f, "row-changed", G_CALLBACK (on_changed), NULL);
  /* a row becomes visible, one disappears */
  gtk_tree_model_iter_nth_child (m, &it, NULL, 1);                /* 7 */
  gtk_list_store_set (ls, &it, 0, 10, -1);
  CHECK (inserted == 1 && gtk_tree_model_iter_n_children (f, NULL) == 5);
  CHECK (nth (f, NULL, 1) == 10);
  gtk_tree_model_iter_nth_child (m, &it, NULL, 0);                /* 8 */
  gtk_list_store_set (ls, &it, 0, 9, -1);
  CHECK (deleted == 1 && gtk_tree_model_iter_n_children (f, NULL) == 4);
  gtk_tree_model_iter_nth_child (m, &it, NULL, 2);                /* 6 -> 12: stays */
  gtk_list_store_set (ls, &it, 0, 12, -1);
  CHECK (changed == 1 && nth (f, NULL, 1) == 12);
  /* conversions */
  CHECK (gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (f), &it2, &it));
  GtkTreePath *fp = gtk_tree_model_get_path (f, &it2);
  CHECK (gtk_tree_path_get_indices (fp)[0] == 1);
  GtkTreePath *cp = gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (f), fp);
  CHECK (gtk_tree_path_get_indices (cp)[0] == 2);
  gtk_tree_path_free (fp);
  gtk_tree_path_free (cp);
  gtk_tree_model_iter_nth_child (m, &it, NULL, 0);                /* 9: hidden */
  CHECK (!gtk_tree_model_filter_convert_child_iter_to_iter (GTK_TREE_MODEL_FILTER (f), &it2, &it));
  /* removing and adding rows in the child */
  gtk_list_store_remove (ls, &it);                                /* 9, hidden: nothing */
  CHECK (deleted == 1);
  gtk_list_store_append (ls, &it);
  gtk_list_store_set (ls, &it, 0, 100, -1);                       /* becomes visible when set */
  CHECK (nth (f, NULL, gtk_tree_model_iter_n_children (f, NULL) - 1) == 100);

  /* ---------- the sort model over the filter, in a view ---------- */
  GtkTreeModel *s = gtk_tree_model_sort_new_with_model (f);
  CHECK (GTK_IS_TREE_SORTABLE (s));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (s), 0, GTK_SORT_ASCENDING);
  int n = gtk_tree_model_iter_n_children (s, NULL);
  CHECK (n == gtk_tree_model_iter_n_children (f, NULL));
  for (int i = 1; i < n; i++) CHECK (nth (s, NULL, i - 1) <= nth (s, NULL, i));
  GtkWidget *win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  GtkWidget *view = gtk_tree_view_new_with_model (s);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), -1, "n", gtk_cell_renderer_text_new (), "text", 0, NULL);
  gtk_container_add (GTK_CONTAINER (win), view);
  gtk_widget_show_all (win);
  while (gtk_events_pending ()) gtk_main_iteration ();
  CHECK (view_rows (view) == n);
  /* a change down in the store reaches the view through both models */
  gtk_tree_model_iter_nth_child (m, &it, NULL, 0);
  int first;
  gtk_tree_model_get (m, &it, 0, &first, -1);
  gtk_list_store_set (ls, &it, 0, first % 2 ? first + 1 : first + 1, -1);   /* parity flips */
  CHECK (view_rows (view) == gtk_tree_model_iter_n_children (s, NULL));
  gtk_list_store_insert_with_values (ls, NULL, -1, 0, -2, -1);
  CHECK (view_rows (view) == gtk_tree_model_iter_n_children (s, NULL));
  char *t = view_text (view, 0);
  CHECK (!strcmp (t, "-2"));
  g_free (t);
  /* the view's selection gives the sort model's iters */
  GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
  gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
  GtkTreePath *p0 = gtk_tree_path_new_first ();
  gtk_tree_selection_select_path (sel, p0);
  GtkTreeModel *got;
  CHECK (gtk_tree_selection_get_selected (sel, &got, &it));
  CHECK (got == s);
  int v;
  gtk_tree_model_get (got, &it, 0, &v, -1);
  CHECK (v == -2);
  /* down to the store */
  GtkTreeIter fi, ci;
  gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (s), &fi, &it);
  gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (f), &ci, &fi);
  gtk_tree_model_get (m, &ci, 0, &v, -1);
  CHECK (v == -2);
  /* descending: the view follows */
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (s), 0, GTK_SORT_DESCENDING);
  t = view_text (view, 0);
  CHECK (atoi (t) == 100);
  g_free (t);
  CHECK (gtk_tree_selection_get_selected (sel, &got, &it));   /* the selection followed its row */
  gtk_tree_model_get (got, &it, 0, &v, -1);
  CHECK (v == -2);
  gtk_tree_path_free (p0);

  /* refilter: everything visible again */
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (f), NULL, NULL, NULL);
  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (f));

  /* ---------- a filter over a tree, by a visible column ---------- */
  GtkTreeStore *ts = gtk_tree_store_new (2, G_TYPE_INT, G_TYPE_BOOLEAN);
  GtkTreeIter a, b, c;
  gtk_tree_store_insert_with_values (ts, &a, NULL, -1, 0, 1, 1, TRUE, -1);
  gtk_tree_store_insert_with_values (ts, &b, &a, -1, 0, 11, 1, TRUE, -1);
  gtk_tree_store_insert_with_values (ts, &c, &a, -1, 0, 12, 1, FALSE, -1);
  gtk_tree_store_insert_with_values (ts, NULL, NULL, -1, 0, 2, 1, FALSE, -1);
  GtkTreeModel *tf = gtk_tree_model_filter_new (GTK_TREE_MODEL (ts), NULL);
  gtk_tree_model_filter_set_visible_column (GTK_TREE_MODEL_FILTER (tf), 1);
  CHECK (gtk_tree_model_iter_n_children (tf, NULL) == 1);
  gtk_tree_model_iter_children (tf, &it, NULL);
  CHECK (gtk_tree_model_iter_has_child (tf, &it) && gtk_tree_model_iter_n_children (tf, &it) == 1);
  CHECK (nth (tf, &it, 0) == 11);
  gtk_tree_store_set (ts, &c, 1, TRUE, -1);
  CHECK (gtk_tree_model_iter_n_children (tf, &it) == 2);
  /* a virtual root: the children of the first row */
  GtkTreePath *vr = gtk_tree_path_new_first ();
  GtkTreeModel *vf = gtk_tree_model_filter_new (GTK_TREE_MODEL (ts), vr);
  CHECK (gtk_tree_model_iter_n_children (vf, NULL) == 2 && nth (vf, NULL, 1) == 12);
  gtk_tree_path_free (vr);
  /* a modify function: other columns */
  GType types[] = { G_TYPE_INT, G_TYPE_STRING };
  GtkTreeModel *mf = gtk_tree_model_filter_new (m, NULL);
  gtk_tree_model_filter_set_modify_func (GTK_TREE_MODEL_FILTER (mf), 2, types, doubled, NULL, NULL);
  gtk_tree_model_iter_nth_child (m, &it, NULL, 0);
  gtk_tree_model_get (m, &it, 0, &first, -1);
  gtk_tree_model_iter_nth_child (mf, &it, NULL, 0);
  char *str;
  gtk_tree_model_get (mf, &it, 0, &v, 1, &str, -1);
  CHECK (v == first * 2 && str && atoi (str + 1) == first);
  g_free (str);

  /* ---------- the program's own model in a view ---------- */
  GtkTreeModel *sq = g_object_new (squares_get_type (), NULL);
  GtkWidget *v2 = gtk_tree_view_new_with_model (sq);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (v2), -1, "sq", gtk_cell_renderer_text_new (), "text", 0, NULL);
  gtk_container_remove (GTK_CONTAINER (win), view);
  gtk_container_add (GTK_CONTAINER (win), v2);
  gtk_widget_show_all (win);
  while (gtk_events_pending ()) gtk_main_iteration ();
  CHECK (view_rows (v2) == 5);
  t = view_text (v2, 4);
  CHECK (!strcmp (t, "16"));
  g_free (t);
  /* the program's model announces a new row */
  ((Squares *) sq)->n = 6;
  GtkTreePath *np = gtk_tree_path_new_from_indices (5, -1);
  gtk_tree_model_get_iter (sq, &it, np);
  gtk_tree_model_row_inserted (sq, np, &it);
  gtk_tree_path_free (np);
  CHECK (view_rows (v2) == 6);
  /* a sort model over it, descending */
  GtkTreeModel *ss = gtk_tree_model_sort_new_with_model (sq);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (ss), 0, GTK_SORT_DESCENDING);
  CHECK (nth (ss, NULL, 0) == 25 && nth (ss, NULL, 5) == 0);

  /* ---------- row references ---------- */
  GtkListStore *rs = gtk_list_store_new (1, G_TYPE_INT);
  for (int i = 0; i < 4; i++) gtk_list_store_insert_with_values (rs, NULL, -1, 0, i, -1);
  GtkTreePath *rp = gtk_tree_path_new_from_indices (2, -1);
  GtkTreeRowReference *ref = gtk_tree_row_reference_new (GTK_TREE_MODEL (rs), rp);
  gtk_tree_path_free (rp);
  gtk_list_store_insert (rs, &it, 0);                             /* before: moves down */
  rp = gtk_tree_row_reference_get_path (ref);
  CHECK (gtk_tree_path_get_indices (rp)[0] == 3);
  gtk_tree_path_free (rp);
  gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (rs), &it, NULL, 4);
  gtk_list_store_move_before (rs, &it, NULL);                     /* to the end? (NULL: last) */
  gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (rs), &it, NULL, 0);
  gtk_list_store_move_after (rs, &it, NULL);                      /* first row to the front: no change */
  int order[] = { 4, 3, 2, 1, 0 };
  gtk_list_store_reorder (rs, order);                             /* reversed */
  rp = gtk_tree_row_reference_get_path (ref);
  gtk_tree_model_get_iter (GTK_TREE_MODEL (rs), &it, rp);
  gtk_tree_model_get (GTK_TREE_MODEL (rs), &it, 0, &v, -1);
  CHECK (v == 2);
  gtk_tree_path_free (rp);
  gtk_list_store_remove (rs, &it);
  CHECK (!gtk_tree_row_reference_valid (ref));
  gtk_tree_row_reference_free (ref);

  gtk_widget_destroy (win);
  g_object_unref (ss);
  g_object_unref (s);
  g_object_unref (f);
  g_object_unref (mf);
  g_object_unref (tf);
  g_object_unref (vf);
  printf ("treemodel: all checks passed\n");
  return 0;
}
