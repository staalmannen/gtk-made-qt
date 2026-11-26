/*
 * GTK made Qt - the GTK C API implemented on top of Qt
 * Tree models as in GTK: GtkTreePath, the GtkTreeModel and GtkTreeSortable
 * interfaces (with their signals), GtkListStore / GtkTreeStore implementing
 * them on rows of GValues, row references that follow the model's signals,
 * and GtkTreeModelFilter / GtkTreeModelSort over any child model.
 *
 * Views see models only through the interface and its signals, so a
 * program's own GtkTreeModel works wherever a store does.
 * Licensed under the LGPL 2.1 or later; see COPYING.
 */
#include "gtkqt-private.h"

#include <gobject/gvaluecollector.h>
#include <QtCore>
#include <algorithm>
#include <functional>
#include <vector>

/* ======================================================================
 * GtkTreePath, GtkTreeIter
 * ====================================================================== */
struct _GtkTreePath { std::vector<gint> idx; };

extern "C" {
GType gtk_tree_path_get_type (void)
{
  static GType t = g_boxed_type_register_static ("GtkTreePath", (GBoxedCopyFunc) gtk_tree_path_copy, (GBoxedFreeFunc) gtk_tree_path_free);
  return t;
}
GType gtk_tree_iter_get_type (void)
{
  static GType t = g_boxed_type_register_static ("GtkTreeIter", (GBoxedCopyFunc) gtk_tree_iter_copy, (GBoxedFreeFunc) gtk_tree_iter_free);
  return t;
}
GtkTreePath *gtk_tree_path_new (void) { return new GtkTreePath; }
GtkTreePath *gtk_tree_path_new_first (void) { auto *p = new GtkTreePath; p->idx.push_back (0); return p; }

GtkTreePath *gtk_tree_path_new_from_string (const gchar *path)
{
  if (!path) return nullptr;
  auto *p = new GtkTreePath;
  for (const QByteArray &s : QByteArray (path).split (':')) {
    bool ok;
    int i = s.trimmed ().toInt (&ok);
    if (!ok || i < 0) { delete p; return nullptr; }
    p->idx.push_back (i);
  }
  if (p->idx.empty ()) { delete p; return nullptr; }
  return p;
}

GtkTreePath *gtk_tree_path_new_from_indices (gint first_index, ...)
{
  auto *p = new GtkTreePath;
  va_list ap;
  va_start (ap, first_index);
  for (gint i = first_index; i != -1; i = va_arg (ap, gint)) p->idx.push_back (i);
  va_end (ap);
  return p;
}

GtkTreePath *gtk_tree_path_new_from_indicesv (gint *indices, gsize length)
{
  auto *p = new GtkTreePath;
  for (gsize i = 0; i < length; i++) p->idx.push_back (indices[i]);
  return p;
}

GtkTreePath *gtk_tree_path_copy (const GtkTreePath *path) { return path ? new GtkTreePath (*path) : nullptr; }
void gtk_tree_path_free (GtkTreePath *path) { delete path; }

gchar *gtk_tree_path_to_string (GtkTreePath *path)
{
  if (!path || path->idx.empty ()) return nullptr;
  QByteArray s;
  for (size_t i = 0; i < path->idx.size (); i++) {
    if (i) s += ':';
    s += QByteArray::number (path->idx[i]);
  }
  return g_strdup (s.constData ());
}

void gtk_tree_path_append_index (GtkTreePath *path, gint i) { path->idx.push_back (i); }
void gtk_tree_path_prepend_index (GtkTreePath *path, gint i) { path->idx.insert (path->idx.begin (), i); }
gint gtk_tree_path_get_depth (GtkTreePath *path) { return path ? (gint) path->idx.size () : 0; }
gint *gtk_tree_path_get_indices (GtkTreePath *path) { return path && !path->idx.empty () ? path->idx.data () : nullptr; }
gint *gtk_tree_path_get_indices_with_depth (GtkTreePath *path, gint *depth) { if (depth) *depth = path->idx.size (); return gtk_tree_path_get_indices (path); }

gint gtk_tree_path_compare (const GtkTreePath *a, const GtkTreePath *b)
{
  return a->idx < b->idx ? -1 : a->idx == b->idx ? 0 : 1;
}

void gtk_tree_path_next (GtkTreePath *path) { if (!path->idx.empty ()) path->idx.back ()++; }
gboolean gtk_tree_path_prev (GtkTreePath *path) { if (path->idx.empty () || path->idx.back () == 0) return FALSE; path->idx.back ()--; return TRUE; }
gboolean gtk_tree_path_up (GtkTreePath *path) { if (path->idx.empty ()) return FALSE; path->idx.pop_back (); return !path->idx.empty (); }
void gtk_tree_path_down (GtkTreePath *path) { path->idx.push_back (0); }
gboolean gtk_tree_path_is_ancestor (GtkTreePath *path, GtkTreePath *descendant)
{
  return path->idx.size () < descendant->idx.size () && std::equal (path->idx.begin (), path->idx.end (), descendant->idx.begin ());
}
gboolean gtk_tree_path_is_descendant (GtkTreePath *path, GtkTreePath *ancestor) { return gtk_tree_path_is_ancestor (ancestor, path); }

GtkTreeIter *gtk_tree_iter_copy (GtkTreeIter *iter) { return (GtkTreeIter *) g_memdup2 (iter, sizeof (GtkTreeIter)); }
void gtk_tree_iter_free (GtkTreeIter *iter) { g_free (iter); }
} /* extern "C" */

/* ======================================================================
 * The interfaces
 * ====================================================================== */
static void tree_model_default_init (GtkTreeModelIface *iface)
{
  GType t = G_TYPE_FROM_INTERFACE (iface);
  GType path = GTK_TYPE_TREE_PATH | G_SIGNAL_TYPE_STATIC_SCOPE, iter = GTK_TYPE_TREE_ITER | G_SIGNAL_TYPE_STATIC_SCOPE;
  g_signal_new ("row-changed", t, G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GtkTreeModelIface, row_changed), nullptr, nullptr, nullptr,
                G_TYPE_NONE, 2, path, iter);
  g_signal_new ("row-inserted", t, G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GtkTreeModelIface, row_inserted), nullptr, nullptr, nullptr,
                G_TYPE_NONE, 2, path, iter);
  g_signal_new ("row-has-child-toggled", t, G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GtkTreeModelIface, row_has_child_toggled),
                nullptr, nullptr, nullptr, G_TYPE_NONE, 2, path, iter);
  g_signal_new ("row-deleted", t, G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GtkTreeModelIface, row_deleted), nullptr, nullptr, nullptr,
                G_TYPE_NONE, 1, path);
  g_signal_new ("rows-reordered", t, G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GtkTreeModelIface, rows_reordered), nullptr, nullptr, nullptr,
                G_TYPE_NONE, 3, path, iter, G_TYPE_POINTER);
}

static void tree_sortable_default_init (GtkTreeSortableIface *iface)
{
  g_signal_new ("sort-column-changed", G_TYPE_FROM_INTERFACE (iface), G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (GtkTreeSortableIface, sort_column_changed), nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}

extern "C" {
GType gtk_tree_model_get_type (void)
{
  static GType t = 0;
  if (!t) {
    t = g_type_register_static_simple (G_TYPE_INTERFACE, "GtkTreeModel", sizeof (GtkTreeModelIface),
                                       (GClassInitFunc) tree_model_default_init, 0, nullptr, (GTypeFlags) 0);
    g_type_interface_add_prerequisite (t, G_TYPE_OBJECT);
  }
  return t;
}

GType gtk_tree_sortable_get_type (void)
{
  static GType t = 0;
  if (!t) {
    t = g_type_register_static_simple (G_TYPE_INTERFACE, "GtkTreeSortable", sizeof (GtkTreeSortableIface),
                                       (GClassInitFunc) tree_sortable_default_init, 0, nullptr, (GTypeFlags) 0);
    g_type_interface_add_prerequisite (t, gtk_tree_model_get_type ());
  }
  return t;
}
} /* extern "C" */

static GtkTreeModelIface *miface (GtkTreeModel *m)
{
  return m && G_IS_OBJECT (m) ? (GtkTreeModelIface *) g_type_interface_peek (G_OBJECT_GET_CLASS (m), GTK_TYPE_TREE_MODEL) : nullptr;
}
static GtkTreeSortableIface *siface (GtkTreeSortable *s)
{
  return s && G_IS_OBJECT (s) ? (GtkTreeSortableIface *) g_type_interface_peek (G_OBJECT_GET_CLASS (s), GTK_TYPE_TREE_SORTABLE) : nullptr;
}

extern "C" {

/* ---------- dispatch ---------- */
GtkTreeModelFlags gtk_tree_model_get_flags (GtkTreeModel *m)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->get_flags ? i->get_flags (m) : (GtkTreeModelFlags) 0;
}
gint gtk_tree_model_get_n_columns (GtkTreeModel *m) { GtkTreeModelIface *i = miface (m); return i && i->get_n_columns ? i->get_n_columns (m) : 0; }
GType gtk_tree_model_get_column_type (GtkTreeModel *m, gint index_)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->get_column_type ? i->get_column_type (m, index_) : G_TYPE_INVALID;
}
gboolean gtk_tree_model_get_iter (GtkTreeModel *m, GtkTreeIter *iter, GtkTreePath *path)
{
  GtkTreeModelIface *i = miface (m);
  if (!i || !path || path->idx.empty ()) return FALSE;
  if (i->get_iter) return i->get_iter (m, iter, path);
  return FALSE;
}
gboolean gtk_tree_model_get_iter_from_string (GtkTreeModel *m, GtkTreeIter *iter, const gchar *path_string)
{
  GtkTreePath *p = gtk_tree_path_new_from_string (path_string);
  gboolean ok = p && gtk_tree_model_get_iter (m, iter, p);
  gtk_tree_path_free (p);
  return ok;
}
gboolean gtk_tree_model_get_iter_first (GtkTreeModel *m, GtkTreeIter *iter) { return gtk_tree_model_iter_nth_child (m, iter, nullptr, 0); }
GtkTreePath *gtk_tree_model_get_path (GtkTreeModel *m, GtkTreeIter *iter)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->get_path && iter ? i->get_path (m, iter) : nullptr;
}
gchar *gtk_tree_model_get_string_from_iter (GtkTreeModel *m, GtkTreeIter *iter)
{
  GtkTreePath *p = gtk_tree_model_get_path (m, iter);
  gchar *s = p ? gtk_tree_path_to_string (p) : nullptr;
  gtk_tree_path_free (p);
  return s;
}
void gtk_tree_model_get_value (GtkTreeModel *m, GtkTreeIter *iter, gint column, GValue *value)
{
  GtkTreeModelIface *i = miface (m);
  if (i && i->get_value) i->get_value (m, iter, column, value);
}
void gtk_tree_model_get_valist (GtkTreeModel *m, GtkTreeIter *iter, va_list ap)
{
  gint n = gtk_tree_model_get_n_columns (m);
  for (int col = va_arg (ap, gint); col != -1; col = va_arg (ap, gint)) {
    if (col < 0 || col >= n) { g_warning ("gtk_tree_model_get: invalid column number %d", col); return; }
    GValue v = G_VALUE_INIT;
    gtk_tree_model_get_value (m, iter, col, &v);
    if (!G_IS_VALUE (&v)) { g_warning ("gtk_tree_model_get: no value for column %d", col); return; }
    gchar *err = nullptr;
    G_VALUE_LCOPY (&v, ap, 0, &err);
    g_value_unset (&v);
    if (err) { g_warning ("%s", err); g_free (err); return; }
  }
}
void gtk_tree_model_get (GtkTreeModel *m, GtkTreeIter *iter, ...)
{
  va_list ap;
  va_start (ap, iter);
  gtk_tree_model_get_valist (m, iter, ap);
  va_end (ap);
}
gboolean gtk_tree_model_iter_next (GtkTreeModel *m, GtkTreeIter *iter)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->iter_next && iter ? i->iter_next (m, iter) : FALSE;
}
/* models without iter_previous: through the path */
gboolean gtk_tree_model_iter_previous (GtkTreeModel *m, GtkTreeIter *iter)
{
  GtkTreeModelIface *i = miface (m);
  if (!i || !iter) return FALSE;
  if (i->iter_previous) return i->iter_previous (m, iter);
  GtkTreePath *p = gtk_tree_model_get_path (m, iter);
  gboolean ok = p && gtk_tree_path_prev (p) && gtk_tree_model_get_iter (m, iter, p);
  gtk_tree_path_free (p);
  return ok;
}
gboolean gtk_tree_model_iter_children (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->iter_children ? i->iter_children (m, iter, parent) : FALSE;
}
gboolean gtk_tree_model_iter_has_child (GtkTreeModel *m, GtkTreeIter *iter)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->iter_has_child && iter ? i->iter_has_child (m, iter) : FALSE;
}
gint gtk_tree_model_iter_n_children (GtkTreeModel *m, GtkTreeIter *iter)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->iter_n_children ? i->iter_n_children (m, iter) : 0;
}
gboolean gtk_tree_model_iter_nth_child (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{
  GtkTreeModelIface *i = miface (m);
  if (!i || n < 0) return FALSE;
  return i->iter_nth_child ? i->iter_nth_child (m, iter, parent, n) : FALSE;
}
gboolean gtk_tree_model_iter_parent (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *child)
{
  GtkTreeModelIface *i = miface (m);
  return i && i->iter_parent && child ? i->iter_parent (m, iter, child) : FALSE;
}
void gtk_tree_model_ref_node (GtkTreeModel *m, GtkTreeIter *iter) { GtkTreeModelIface *i = miface (m); if (i && i->ref_node) i->ref_node (m, iter); }
void gtk_tree_model_unref_node (GtkTreeModel *m, GtkTreeIter *iter) { GtkTreeModelIface *i = miface (m); if (i && i->unref_node) i->unref_node (m, iter); }

static gboolean foreach_level (GtkTreeModel *m, GtkTreeIter *parent, GtkTreePath *path, GtkTreeModelForeachFunc func, gpointer data)
{
  GtkTreeIter it;
  if (!gtk_tree_model_iter_children (m, &it, parent)) return FALSE;
  gtk_tree_path_down (path);
  do {
    if (func (m, path, &it, data)) return TRUE;
    if (gtk_tree_model_iter_has_child (m, &it) && foreach_level (m, &it, path, func, data)) return TRUE;
    gtk_tree_path_next (path);
  } while (gtk_tree_model_iter_next (m, &it));
  gtk_tree_path_up (path);
  return FALSE;
}

void gtk_tree_model_foreach (GtkTreeModel *m, GtkTreeModelForeachFunc func, gpointer data)
{
  GtkTreePath *p = gtk_tree_path_new ();
  foreach_level (m, nullptr, p, func, data);
  gtk_tree_path_free (p);
}

/* ---------- signals ---------- */
void gtk_tree_model_row_changed (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter)
{
  GtkQtEmitGuard guard;
  g_signal_emit_by_name (m, "row-changed", path, iter);
}
void gtk_tree_model_row_inserted (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter)
{
  GtkQtEmitGuard guard;
  g_signal_emit_by_name (m, "row-inserted", path, iter);
}
void gtk_tree_model_row_has_child_toggled (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter)
{
  GtkQtEmitGuard guard;
  g_signal_emit_by_name (m, "row-has-child-toggled", path, iter);
}
void gtk_tree_model_row_deleted (GtkTreeModel *m, GtkTreePath *path)
{
  GtkQtEmitGuard guard;
  g_signal_emit_by_name (m, "row-deleted", path);
}
void gtk_tree_model_rows_reordered (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter, gint *new_order)
{
  GtkQtEmitGuard guard;
  GtkTreePath *root = path ? nullptr : gtk_tree_path_new ();
  g_signal_emit_by_name (m, "rows-reordered", path ? path : root, iter, new_order);
  gtk_tree_path_free (root);
}
void gtk_tree_model_rows_reordered_with_length (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter, gint *new_order, gint)
{
  gtk_tree_model_rows_reordered (m, path, iter, new_order);
}

/* ---------- GtkTreeSortable ---------- */
gboolean gtk_tree_sortable_get_sort_column_id (GtkTreeSortable *s, gint *sort_column_id, GtkSortType *order)
{
  GtkTreeSortableIface *i = siface (s);
  if (!i || !i->get_sort_column_id) {
    if (sort_column_id) *sort_column_id = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
    if (order) *order = GTK_SORT_ASCENDING;
    return FALSE;
  }
  return i->get_sort_column_id (s, sort_column_id, order);
}
void gtk_tree_sortable_set_sort_column_id (GtkTreeSortable *s, gint sort_column_id, GtkSortType order)
{
  GtkTreeSortableIface *i = siface (s);
  if (i && i->set_sort_column_id) i->set_sort_column_id (s, sort_column_id, order);
}
void gtk_tree_sortable_set_sort_func (GtkTreeSortable *s, gint id, GtkTreeIterCompareFunc func, gpointer data, GDestroyNotify destroy)
{
  GtkTreeSortableIface *i = siface (s);
  if (i && i->set_sort_func) i->set_sort_func (s, id, func, data, destroy);
}
void gtk_tree_sortable_set_default_sort_func (GtkTreeSortable *s, GtkTreeIterCompareFunc func, gpointer data, GDestroyNotify destroy)
{
  GtkTreeSortableIface *i = siface (s);
  if (i && i->set_default_sort_func) i->set_default_sort_func (s, func, data, destroy);
}
gboolean gtk_tree_sortable_has_default_sort_func (GtkTreeSortable *s)
{
  GtkTreeSortableIface *i = siface (s);
  return i && i->has_default_sort_func ? i->has_default_sort_func (s) : FALSE;
}
void gtk_tree_sortable_sort_column_changed (GtkTreeSortable *s) { g_signal_emit_by_name (s, "sort-column-changed"); }

} /* extern "C" */

/* ======================================================================
 * Sorting, shared by the stores and GtkTreeModelSort
 * ====================================================================== */
namespace {
struct SortFunc { GtkTreeIterCompareFunc func = nullptr; gpointer data = nullptr; GDestroyNotify destroy = nullptr; };

struct SortState {
  int column = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
  GtkSortType order = GTK_SORT_ASCENDING;
  QMap<int, SortFunc> funcs;
  SortFunc default_func;
  bool default_set = false;     /* set_default_sort_func was called (possibly with NULL) */
  ~SortState ()
  {
    for (SortFunc &f : funcs) if (f.destroy) f.destroy (f.data);
    if (default_func.destroy) default_func.destroy (default_func.data);
  }
  bool active () const
  {
    if (column == GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID) return false;
    if (column == GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID) return default_func.func != nullptr;
    return true;
  }
  void set_func (int id, GtkTreeIterCompareFunc f, gpointer d, GDestroyNotify destroy)
  {
    if (funcs.contains (id) && funcs[id].destroy) funcs[id].destroy (funcs[id].data);
    funcs[id] = SortFunc { f, d, destroy };
  }
  void set_default (GtkTreeIterCompareFunc f, gpointer d, GDestroyNotify destroy)
  {
    if (default_func.destroy) default_func.destroy (default_func.data);
    default_func = SortFunc { f, d, destroy };
    default_set = true;
  }
};

/* the default comparison of a column's values */
int compare_values (const GValue *va, const GValue *vb)
{
  switch (G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (va))) {
  case G_TYPE_STRING: {
    const char *a = g_value_get_string (va), *b = g_value_get_string (vb);
    return g_utf8_collate (a ? a : "", b ? b : "");
  }
  case G_TYPE_INT: return (g_value_get_int (va) > g_value_get_int (vb)) - (g_value_get_int (va) < g_value_get_int (vb));
  case G_TYPE_UINT: return (g_value_get_uint (va) > g_value_get_uint (vb)) - (g_value_get_uint (va) < g_value_get_uint (vb));
  case G_TYPE_INT64: return (g_value_get_int64 (va) > g_value_get_int64 (vb)) - (g_value_get_int64 (va) < g_value_get_int64 (vb));
  case G_TYPE_UINT64: return (g_value_get_uint64 (va) > g_value_get_uint64 (vb)) - (g_value_get_uint64 (va) < g_value_get_uint64 (vb));
  case G_TYPE_LONG: return (g_value_get_long (va) > g_value_get_long (vb)) - (g_value_get_long (va) < g_value_get_long (vb));
  case G_TYPE_ULONG: return (g_value_get_ulong (va) > g_value_get_ulong (vb)) - (g_value_get_ulong (va) < g_value_get_ulong (vb));
  case G_TYPE_DOUBLE: return (g_value_get_double (va) > g_value_get_double (vb)) - (g_value_get_double (va) < g_value_get_double (vb));
  case G_TYPE_FLOAT: return (g_value_get_float (va) > g_value_get_float (vb)) - (g_value_get_float (va) < g_value_get_float (vb));
  case G_TYPE_BOOLEAN: return g_value_get_boolean (va) - g_value_get_boolean (vb);
  case G_TYPE_CHAR: return g_value_get_schar (va) - g_value_get_schar (vb);
  case G_TYPE_UCHAR: return g_value_get_uchar (va) - g_value_get_uchar (vb);
  case G_TYPE_ENUM: return g_value_get_enum (va) - g_value_get_enum (vb);
  default: return 0;
  }
}

/* compare two rows of model m (iters of m) under the sort state */
int compare_iters (const SortState &st, GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b)
{
  const SortFunc *f = nullptr;
  if (st.column == GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID) f = &st.default_func;
  else if (st.funcs.contains (st.column)) f = &*st.funcs.constFind (st.column);
  int c;
  if (f && f->func) c = f->func (m, a, b, f->data);
  else {
    GValue va = G_VALUE_INIT, vb = G_VALUE_INIT;
    gtk_tree_model_get_value (m, a, st.column, &va);
    gtk_tree_model_get_value (m, b, st.column, &vb);
    c = G_IS_VALUE (&va) && G_IS_VALUE (&vb) ? compare_values (&va, &vb) : 0;
    if (G_IS_VALUE (&va)) g_value_unset (&va);
    if (G_IS_VALUE (&vb)) g_value_unset (&vb);
  }
  return st.order == GTK_SORT_ASCENDING ? c : -c;
}
} /* namespace */

/* ======================================================================
 * GtkListStore / GtkTreeStore
 * ====================================================================== */
namespace {
struct Row {
  std::vector<GValue> values;
  Row *parent = nullptr;
  QList<Row *> children;
  ~Row () { for (GValue &v : values) if (G_IS_VALUE (&v)) g_value_unset (&v); qDeleteAll (children); }
  int index () const { return parent ? parent->children.indexOf (const_cast<Row *> (this)) : 0; }
};

struct StoreData {
  QVector<GType> types;
  Row root;
  gint stamp = (gint) g_random_int ();
  SortState sort;
  bool is_list = true;
};
}

struct _GtkListStore { GObject parent_instance; gpointer priv; };
struct _GtkTreeStore { GObject parent_instance; gpointer priv; };
typedef struct { GObjectClass parent_class; } GtkListStoreClass;
typedef struct { GObjectClass parent_class; } GtkTreeStoreClass;
struct StoreObj { GObject parent_instance; gpointer priv; };
static StoreData *store (gpointer model) { return static_cast<StoreData *> (((StoreObj *) model)->priv); }

static Row *iter_row (GtkTreeModel *m, GtkTreeIter *it)
{
  if (!it || it->stamp != store (m)->stamp) return nullptr;
  return static_cast<Row *> (it->user_data);
}
static void set_iter (GtkTreeModel *m, GtkTreeIter *it, Row *r)
{
  if (!it) return;
  it->stamp = r ? store (m)->stamp : 0;
  it->user_data = r;
  it->user_data2 = it->user_data3 = nullptr;
}
static GtkTreePath *row_path (Row *r)
{
  auto *p = new GtkTreePath;
  for (Row *x = r; x && x->parent; x = x->parent) p->idx.insert (p->idx.begin (), x->index ());
  return p;
}
static Row *new_row (StoreData *d)
{
  auto *r = new Row;
  r->values.resize (d->types.size ());
  for (int i = 0; i < d->types.size (); i++) {
    r->values[i] = G_VALUE_INIT;
    g_value_init (&r->values[i], d->types[i]);
  }
  return r;
}

/* ---------- the interface ---------- */
static GtkTreeModelFlags store_get_flags (GtkTreeModel *m)
{
  return store (m)->is_list ? (GtkTreeModelFlags) (GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY) : GTK_TREE_MODEL_ITERS_PERSIST;
}
static gint store_n_columns (GtkTreeModel *m) { return store (m)->types.size (); }
static GType store_column_type (GtkTreeModel *m, gint i) { return store (m)->types.value (i, G_TYPE_INVALID); }
static gboolean store_get_iter (GtkTreeModel *m, GtkTreeIter *iter, GtkTreePath *path)
{
  Row *r = &store (m)->root;
  for (int i : path->idx) {
    if (i < 0 || i >= r->children.size ()) { set_iter (m, iter, nullptr); return FALSE; }
    r = r->children[i];
  }
  set_iter (m, iter, r);
  return TRUE;
}
static GtkTreePath *store_get_path (GtkTreeModel *m, GtkTreeIter *iter)
{
  Row *r = iter_row (m, iter);
  return r ? row_path (r) : nullptr;
}
static void store_get_value (GtkTreeModel *m, GtkTreeIter *iter, gint column, GValue *value)
{
  StoreData *d = store (m);
  Row *r = iter_row (m, iter);
  if (column < 0 || column >= d->types.size ()) { g_warning ("gtk_tree_model_get_value: invalid column %d", column); return; }
  g_value_init (value, d->types[column]);
  if (r) g_value_copy (&r->values[column], value);
}
static gboolean store_iter_next (GtkTreeModel *m, GtkTreeIter *iter)
{
  Row *r = iter_row (m, iter);
  if (!r) return FALSE;
  int i = r->index () + 1;
  Row *n = i < r->parent->children.size () ? r->parent->children[i] : nullptr;
  set_iter (m, iter, n);
  return n != nullptr;
}
static gboolean store_iter_previous (GtkTreeModel *m, GtkTreeIter *iter)
{
  Row *r = iter_row (m, iter);
  if (!r) return FALSE;
  int i = r->index () - 1;
  Row *n = i >= 0 ? r->parent->children[i] : nullptr;
  set_iter (m, iter, n);
  return n != nullptr;
}
static gboolean store_iter_nth_child (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{
  Row *p = parent ? iter_row (m, parent) : &store (m)->root;
  Row *c = p && n >= 0 && n < p->children.size () ? p->children[n] : nullptr;
  set_iter (m, iter, c);
  return c != nullptr;
}
static gboolean store_iter_children (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent) { return store_iter_nth_child (m, iter, parent, 0); }
static gboolean store_iter_has_child (GtkTreeModel *m, GtkTreeIter *iter) { Row *r = iter_row (m, iter); return r && !r->children.isEmpty (); }
static gint store_iter_n_children (GtkTreeModel *m, GtkTreeIter *iter)
{
  Row *r = iter ? iter_row (m, iter) : &store (m)->root;
  return r ? r->children.size () : 0;
}
static gboolean store_iter_parent (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *child)
{
  Row *r = iter_row (m, child);
  Row *p = r && r->parent != &store (m)->root ? r->parent : nullptr;
  set_iter (m, iter, p);
  return p != nullptr;
}

static void store_model_iface_init (gpointer g, gpointer)
{
  auto *i = (GtkTreeModelIface *) g;
  i->get_flags = store_get_flags;
  i->get_n_columns = store_n_columns;
  i->get_column_type = store_column_type;
  i->get_iter = store_get_iter;
  i->get_path = store_get_path;
  i->get_value = store_get_value;
  i->iter_next = store_iter_next;
  i->iter_previous = store_iter_previous;
  i->iter_children = store_iter_children;
  i->iter_has_child = store_iter_has_child;
  i->iter_n_children = store_iter_n_children;
  i->iter_nth_child = store_iter_nth_child;
  i->iter_parent = store_iter_parent;
}

/* ---------- sorting the store ---------- */
static void store_sort_level (GtkTreeModel *m, Row *parent, bool recurse)
{
  StoreData *d = store (m);
  QList<Row *> old = parent->children;
  QList<Row *> now = old;
  std::stable_sort (now.begin (), now.end (), [m, d] (Row *a, Row *b) {
    GtkTreeIter ia, ib;
    set_iter (m, &ia, a);
    set_iter (m, &ib, b);
    return compare_iters (d->sort, m, &ia, &ib) < 0;
  });
  if (now != old) {
    std::vector<gint> order (now.size ());
    for (int i = 0; i < now.size (); i++) order[i] = old.indexOf (now[i]);
    parent->children = now;
    GtkTreeIter it;
    set_iter (m, &it, parent == &d->root ? nullptr : parent);
    GtkTreePath *p = row_path (parent);
    gtk_tree_model_rows_reordered (m, p, parent == &d->root ? nullptr : &it, order.data ());
    gtk_tree_path_free (p);
  }
  if (recurse)
    for (Row *c : QList<Row *> (parent->children)) if (!c->children.isEmpty ()) store_sort_level (m, c, true);
}

static void store_resort (GtkTreeModel *m)
{
  if (store (m)->sort.active ()) store_sort_level (m, &store (m)->root, true);
}

static gboolean store_get_sort_column_id (GtkTreeSortable *s, gint *col, GtkSortType *order)
{
  StoreData *d = store (s);
  if (col) *col = d->sort.column;
  if (order) *order = d->sort.order;
  return d->sort.column != GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID && d->sort.column != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
}
static void store_set_sort_column_id (GtkTreeSortable *s, gint col, GtkSortType order)
{
  StoreData *d = store (s);
  if (d->sort.column == col && d->sort.order == order) return;
  d->sort.column = col;
  d->sort.order = order;
  gtk_tree_sortable_sort_column_changed (s);
  store_resort (GTK_TREE_MODEL (s));
}
static void store_set_sort_func (GtkTreeSortable *s, gint id, GtkTreeIterCompareFunc f, gpointer data, GDestroyNotify destroy)
{
  store (s)->sort.set_func (id, f, data, destroy);
  if (store (s)->sort.column == id) store_resort (GTK_TREE_MODEL (s));
}
static void store_set_default_sort_func (GtkTreeSortable *s, GtkTreeIterCompareFunc f, gpointer data, GDestroyNotify destroy)
{
  store (s)->sort.set_default (f, data, destroy);
  if (store (s)->sort.column == GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID) store_resort (GTK_TREE_MODEL (s));
}
static gboolean store_has_default_sort_func (GtkTreeSortable *s) { return store (s)->sort.default_func.func != nullptr; }

static void store_sortable_iface_init (gpointer g, gpointer)
{
  auto *i = (GtkTreeSortableIface *) g;
  i->get_sort_column_id = store_get_sort_column_id;
  i->set_sort_column_id = store_set_sort_column_id;
  i->set_sort_func = store_set_sort_func;
  i->set_default_sort_func = store_set_default_sort_func;
  i->has_default_sort_func = store_has_default_sort_func;
}

static void store_finalize_impl (GObject *o) { delete store (o); }

G_DEFINE_TYPE_WITH_CODE (GtkListStore, gtk_list_store, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, store_model_iface_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_SORTABLE, store_sortable_iface_init))
G_DEFINE_TYPE_WITH_CODE (GtkTreeStore, gtk_tree_store, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, store_model_iface_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_SORTABLE, store_sortable_iface_init))

static void gtk_list_store_init (GtkListStore *s) { s->priv = new StoreData; }
static void gtk_tree_store_init (GtkTreeStore *s) { s->priv = new StoreData; store (s)->is_list = false; }
static void list_finalize (GObject *o) { store_finalize_impl (o); G_OBJECT_CLASS (gtk_list_store_parent_class)->finalize (o); }
static void tree_finalize (GObject *o) { store_finalize_impl (o); G_OBJECT_CLASS (gtk_tree_store_parent_class)->finalize (o); }
static void gtk_list_store_class_init (GtkListStoreClass *klass) { G_OBJECT_CLASS (klass)->finalize = list_finalize; }
static void gtk_tree_store_class_init (GtkTreeStoreClass *klass) { G_OBJECT_CLASS (klass)->finalize = tree_finalize; }

/* ---------- changes, announced through the interface's signals ---------- */
static void emit_for_row (GtkTreeModel *m, Row *r, void (*emit) (GtkTreeModel *, GtkTreePath *, GtkTreeIter *))
{
  GtkTreePath *p = row_path (r);
  GtkTreeIter it;
  set_iter (m, &it, r);
  emit (m, p, &it);
  gtk_tree_path_free (p);
}

/* GTK moves a changed row to its sorted place */
static void store_row_changed (GtkTreeModel *m, Row *r)
{
  emit_for_row (m, r, gtk_tree_model_row_changed);
  if (store (m)->sort.active ()) store_sort_level (m, r->parent, false);
}

static Row *store_insert (GtkTreeModel *m, Row *parent, int pos, GtkTreeIter *iter)
{
  StoreData *d = store (m);
  if (!parent) parent = &d->root;
  if (pos < 0 || pos > parent->children.size ()) pos = parent->children.size ();
  Row *r = new_row (d);
  r->parent = parent;
  parent->children.insert (pos, r);
  set_iter (m, iter, r);
  emit_for_row (m, r, gtk_tree_model_row_inserted);
  if (parent != &d->root && parent->children.size () == 1) emit_for_row (m, parent, gtk_tree_model_row_has_child_toggled);
  return r;
}

static bool store_set_value (GtkTreeModel *m, Row *r, int col, const GValue *v)
{
  StoreData *d = store (m);
  if (!r || col < 0 || col >= d->types.size ()) { g_warning ("gtk-made-qt: invalid column %d", col); return false; }
  if (G_VALUE_TYPE (v) == d->types[col]) g_value_copy (v, &r->values[col]);
  else if (!g_value_transform (v, &r->values[col])) {
    g_warning ("gtk-made-qt: cannot store %s in column %d", G_VALUE_TYPE_NAME (v), col);
    return false;
  }
  return true;
}

/* set columns from (column, value) pairs; insertions sort once, after all are set */
static bool store_set_valist_quiet (GtkTreeModel *m, Row *r, va_list ap)
{
  StoreData *d = store (m);
  for (int col = va_arg (ap, gint); col != -1; col = va_arg (ap, gint)) {
    if (col < 0 || col >= d->types.size ()) { g_warning ("gtk-made-qt: invalid column %d", col); return false; }
    GValue v = G_VALUE_INIT;
    gchar *err = nullptr;
    G_VALUE_COLLECT_INIT (&v, d->types[col], ap, 0, &err);
    if (err) { g_warning ("%s", err); g_free (err); return false; }
    g_value_copy (&v, &r->values[col]);
    g_value_unset (&v);
  }
  return true;
}

static void store_set_valist (GtkTreeModel *m, GtkTreeIter *iter, va_list ap)
{
  Row *r = iter_row (m, iter);
  if (!r) return;
  store_set_valist_quiet (m, r, ap);
  store_row_changed (m, r);
}

static gboolean store_remove (GtkTreeModel *m, GtkTreeIter *iter)
{
  StoreData *d = store (m);
  Row *r = iter_row (m, iter);
  if (!r) return FALSE;
  Row *parent = r->parent;
  int pos = r->index ();
  GtkTreePath *p = row_path (r);
  parent->children.removeAt (pos);
  delete r;
  gtk_tree_model_row_deleted (m, p);
  gtk_tree_path_free (p);
  if (parent != &d->root && parent->children.isEmpty ()) emit_for_row (m, parent, gtk_tree_model_row_has_child_toggled);
  if (pos < parent->children.size ()) { set_iter (m, iter, parent->children[pos]); return TRUE; }
  set_iter (m, iter, nullptr);
  return FALSE;
}

static void store_clear (GtkTreeModel *m)
{
  StoreData *d = store (m);
  while (!d->root.children.isEmpty ()) {
    GtkTreeIter it;
    set_iter (m, &it, d->root.children.last ());
    store_remove (m, &it);
  }
  d->stamp++;   /* iters from before are invalid, as in GTK */
}

static void store_init_types (GtkTreeModel *m, gint n, GType *types)
{
  StoreData *d = store (m);
  d->types.clear ();
  for (int i = 0; i < n; i++) d->types << types[i];
}

/* new_order[new position] = old position */
static void store_reorder (GtkTreeModel *m, Row *parent, const std::vector<gint> &new_order)
{
  StoreData *d = store (m);
  if (!parent) parent = &d->root;
  QList<Row *> old = parent->children, now;
  if ((int) new_order.size () != old.size ()) return;
  for (gint o : new_order) {
    if (o < 0 || o >= old.size ()) return;
    now << old[o];
  }
  parent->children = now;
  GtkTreeIter it;
  set_iter (m, &it, parent);
  GtkTreePath *p = row_path (parent);
  gtk_tree_model_rows_reordered (m, p, parent == &d->root ? nullptr : &it, const_cast<gint *> (new_order.data ()));
  gtk_tree_path_free (p);
}

static void store_move (GtkTreeModel *m, Row *r, int pos)
{
  QList<Row *> level = r->parent->children;
  int from = level.indexOf (r);
  level.removeAt (from);
  if (pos < 0 || pos > level.size ()) pos = level.size ();
  level.insert (pos, r);
  std::vector<gint> order;
  for (Row *x : level) order.push_back (r->parent->children.indexOf (x));
  store_reorder (m, r->parent, order);
}

static void store_insert_by (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent, GtkTreeIter *sibling, bool after)
{
  Row *sib = sibling ? iter_row (m, sibling) : nullptr;
  Row *par = sib ? sib->parent : parent ? iter_row (m, parent) : nullptr;
  int pos = sib ? sib->index () + (after ? 1 : 0) : after ? 0 : -1;
  store_insert (m, par == &store (m)->root ? nullptr : par, pos, iter);
}

static void store_move_by (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *position, bool after)
{
  Row *r = iter_row (m, iter), *at = position ? iter_row (m, position) : nullptr;
  if (!r || (at && at->parent != r->parent) || at == r) return;
  int from = r->index ();
  int pos = at ? at->index () + (after ? 1 : 0) : after ? 0 : r->parent->children.size ();
  if (from < pos) pos--;
  store_move (m, r, pos);
}

static void store_swap (GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b)
{
  Row *ra = iter_row (m, a), *rb = iter_row (m, b);
  if (!ra || !rb || ra->parent != rb->parent || ra == rb) return;
  std::vector<gint> order;
  for (int i = 0; i < ra->parent->children.size (); i++) order.push_back (i);
  std::swap (order[ra->index ()], order[rb->index ()]);
  store_reorder (m, ra->parent, order);
}

extern "C" {

GtkListStore *gtk_list_store_newv (gint n_columns, GType *types)
{
  auto *s = (GtkListStore *) g_object_new (GTK_TYPE_LIST_STORE, nullptr);
  store_init_types (GTK_TREE_MODEL (s), n_columns, types);
  return s;
}
GtkListStore *gtk_list_store_new (gint n_columns, ...)
{
  QVector<GType> t;
  va_list ap;
  va_start (ap, n_columns);
  for (int i = 0; i < n_columns; i++) t << va_arg (ap, GType);
  va_end (ap);
  return gtk_list_store_newv (n_columns, t.data ());
}
void gtk_list_store_set_column_types (GtkListStore *s, gint n_columns, GType *types) { store_init_types (GTK_TREE_MODEL (s), n_columns, types); }
void gtk_list_store_set_valist (GtkListStore *s, GtkTreeIter *iter, va_list ap) { store_set_valist (GTK_TREE_MODEL (s), iter, ap); }
void gtk_list_store_set (GtkListStore *s, GtkTreeIter *iter, ...)
{
  va_list ap;
  va_start (ap, iter);
  store_set_valist (GTK_TREE_MODEL (s), iter, ap);
  va_end (ap);
}
void gtk_list_store_set_value (GtkListStore *s, GtkTreeIter *iter, gint column, GValue *value)
{
  Row *r = iter_row (GTK_TREE_MODEL (s), iter);
  if (r && store_set_value (GTK_TREE_MODEL (s), r, column, value)) store_row_changed (GTK_TREE_MODEL (s), r);
}
void gtk_list_store_set_valuesv (GtkListStore *s, GtkTreeIter *iter, gint *columns, GValue *values, gint n_values)
{
  Row *r = iter_row (GTK_TREE_MODEL (s), iter);
  if (!r) return;
  for (int i = 0; i < n_values; i++) store_set_value (GTK_TREE_MODEL (s), r, columns[i], &values[i]);
  store_row_changed (GTK_TREE_MODEL (s), r);
}
gboolean gtk_list_store_remove (GtkListStore *s, GtkTreeIter *iter) { return store_remove (GTK_TREE_MODEL (s), iter); }
void gtk_list_store_insert (GtkListStore *s, GtkTreeIter *iter, gint position) { store_insert (GTK_TREE_MODEL (s), nullptr, position, iter); }

/* the row comes in with its values, and in its sorted place */
static void insert_with_valist (GtkTreeModel *m, Row *parent, GtkTreeIter *iter, gint position, va_list ap)
{
  StoreData *d = store (m);
  if (!parent) parent = &d->root;
  Row *r = new_row (d);
  r->parent = parent;
  store_set_valist_quiet (m, r, ap);
  int pos = position < 0 || position > parent->children.size () ? parent->children.size () : position;
  if (d->sort.active ()) {
    pos = 0;
    GtkTreeIter ir, ic;
    set_iter (m, &ir, r);
    while (pos < parent->children.size ()) {
      set_iter (m, &ic, parent->children[pos]);
      if (compare_iters (d->sort, m, &ir, &ic) < 0) break;
      pos++;
    }
  }
  parent->children.insert (pos, r);
  set_iter (m, iter, r);
  emit_for_row (m, r, gtk_tree_model_row_inserted);
  if (parent != &d->root && parent->children.size () == 1) emit_for_row (m, parent, gtk_tree_model_row_has_child_toggled);
}

void gtk_list_store_insert_with_values (GtkListStore *s, GtkTreeIter *iter, gint position, ...)
{
  GtkTreeIter local;
  va_list ap;
  va_start (ap, position);
  insert_with_valist (GTK_TREE_MODEL (s), nullptr, iter ? iter : &local, position, ap);
  va_end (ap);
}
void gtk_list_store_prepend (GtkListStore *s, GtkTreeIter *iter) { store_insert (GTK_TREE_MODEL (s), nullptr, 0, iter); }
void gtk_list_store_append (GtkListStore *s, GtkTreeIter *iter) { store_insert (GTK_TREE_MODEL (s), nullptr, -1, iter); }
void gtk_list_store_clear (GtkListStore *s) { store_clear (GTK_TREE_MODEL (s)); }
gboolean gtk_list_store_iter_is_valid (GtkListStore *s, GtkTreeIter *iter) { return iter_row (GTK_TREE_MODEL (s), iter) != nullptr; }
void gtk_list_store_swap (GtkListStore *s, GtkTreeIter *a, GtkTreeIter *b) { store_swap (GTK_TREE_MODEL (s), a, b); }
void gtk_list_store_insert_before (GtkListStore *s, GtkTreeIter *iter, GtkTreeIter *sibling) { store_insert_by (GTK_TREE_MODEL (s), iter, nullptr, sibling, false); }
void gtk_list_store_insert_after (GtkListStore *s, GtkTreeIter *iter, GtkTreeIter *sibling) { store_insert_by (GTK_TREE_MODEL (s), iter, nullptr, sibling, true); }
void gtk_list_store_reorder (GtkListStore *s, gint *new_order)
{
  int n = store (s)->root.children.size ();
  store_reorder (GTK_TREE_MODEL (s), nullptr, std::vector<gint> (new_order, new_order + n));
}
void gtk_list_store_move_before (GtkListStore *s, GtkTreeIter *iter, GtkTreeIter *position) { store_move_by (GTK_TREE_MODEL (s), iter, position, false); }
void gtk_list_store_move_after (GtkListStore *s, GtkTreeIter *iter, GtkTreeIter *position) { store_move_by (GTK_TREE_MODEL (s), iter, position, true); }

GtkTreeStore *gtk_tree_store_newv (gint n_columns, GType *types)
{
  auto *s = (GtkTreeStore *) g_object_new (GTK_TYPE_TREE_STORE, nullptr);
  store_init_types (GTK_TREE_MODEL (s), n_columns, types);
  return s;
}
GtkTreeStore *gtk_tree_store_new (gint n_columns, ...)
{
  QVector<GType> t;
  va_list ap;
  va_start (ap, n_columns);
  for (int i = 0; i < n_columns; i++) t << va_arg (ap, GType);
  va_end (ap);
  return gtk_tree_store_newv (n_columns, t.data ());
}
void gtk_tree_store_set_column_types (GtkTreeStore *s, gint n_columns, GType *types) { store_init_types (GTK_TREE_MODEL (s), n_columns, types); }
void gtk_tree_store_set_valist (GtkTreeStore *s, GtkTreeIter *iter, va_list ap) { store_set_valist (GTK_TREE_MODEL (s), iter, ap); }
void gtk_tree_store_set (GtkTreeStore *s, GtkTreeIter *iter, ...)
{
  va_list ap;
  va_start (ap, iter);
  store_set_valist (GTK_TREE_MODEL (s), iter, ap);
  va_end (ap);
}
void gtk_tree_store_set_value (GtkTreeStore *s, GtkTreeIter *iter, gint column, GValue *value)
{
  gtk_list_store_set_value ((GtkListStore *) s, iter, column, value);
}
gboolean gtk_tree_store_remove (GtkTreeStore *s, GtkTreeIter *iter) { return store_remove (GTK_TREE_MODEL (s), iter); }
void gtk_tree_store_insert (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *parent, gint position)
{
  store_insert (GTK_TREE_MODEL (s), parent ? iter_row (GTK_TREE_MODEL (s), parent) : nullptr, position, iter);
}
void gtk_tree_store_prepend (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *parent) { gtk_tree_store_insert (s, iter, parent, 0); }
void gtk_tree_store_append (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *parent) { gtk_tree_store_insert (s, iter, parent, -1); }
void gtk_tree_store_clear (GtkTreeStore *s) { store_clear (GTK_TREE_MODEL (s)); }
void gtk_tree_store_insert_before (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *parent, GtkTreeIter *sibling)
{
  store_insert_by (GTK_TREE_MODEL (s), iter, parent, sibling, false);
}
void gtk_tree_store_insert_after (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *parent, GtkTreeIter *sibling)
{
  store_insert_by (GTK_TREE_MODEL (s), iter, parent, sibling, true);
}
void gtk_tree_store_insert_with_values (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *parent, gint position, ...)
{
  GtkTreeIter local;
  va_list ap;
  va_start (ap, position);
  insert_with_valist (GTK_TREE_MODEL (s), parent ? iter_row (GTK_TREE_MODEL (s), parent) : nullptr, iter ? iter : &local, position, ap);
  va_end (ap);
}
gboolean gtk_tree_store_is_ancestor (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *descendant)
{
  Row *a = iter_row (GTK_TREE_MODEL (s), iter), *r = iter_row (GTK_TREE_MODEL (s), descendant);
  for (r = r ? r->parent : nullptr; a && r; r = r->parent) if (r == a) return TRUE;
  return FALSE;
}
gint gtk_tree_store_iter_depth (GtkTreeStore *s, GtkTreeIter *iter)
{
  int depth = 0;
  Row *r = iter_row (GTK_TREE_MODEL (s), iter);
  for (; r && r->parent && r->parent != &store (s)->root; r = r->parent) depth++;
  return depth;
}
gboolean gtk_tree_store_iter_is_valid (GtkTreeStore *s, GtkTreeIter *iter) { return iter_row (GTK_TREE_MODEL (s), iter) != nullptr; }
void gtk_tree_store_reorder (GtkTreeStore *s, GtkTreeIter *parent, gint *new_order)
{
  Row *p = parent ? iter_row (GTK_TREE_MODEL (s), parent) : &store (s)->root;
  if (!p) return;
  store_reorder (GTK_TREE_MODEL (s), p, std::vector<gint> (new_order, new_order + p->children.size ()));
}
void gtk_tree_store_swap (GtkTreeStore *s, GtkTreeIter *a, GtkTreeIter *b) { store_swap (GTK_TREE_MODEL (s), a, b); }
void gtk_tree_store_move_before (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *position) { store_move_by (GTK_TREE_MODEL (s), iter, position, false); }
void gtk_tree_store_move_after (GtkTreeStore *s, GtkTreeIter *iter, GtkTreeIter *position) { store_move_by (GTK_TREE_MODEL (s), iter, position, true); }

} /* extern "C" */

/* ======================================================================
 * GtkTreeRowReference: a path kept up to date by the model's signals
 * ====================================================================== */
struct _GtkTreeRowReference { GtkTreeModel *model; GtkTreePath *path; };

namespace {
void ref_inserted (GtkTreeRowReference *r, GtkTreePath *path);
void ref_deleted (GtkTreeRowReference *r, GtkTreePath *path);
void ref_reordered (GtkTreeRowReference *r, GtkTreePath *path, GtkTreeIter *iter, gint *order);

QList<GtkTreeRowReference *> *model_refs (GtkTreeModel *m, bool create)
{
  auto *l = static_cast<QList<GtkTreeRowReference *> *> (g_object_get_data (G_OBJECT (m), "gtkqt-row-refs"));
  if (l || !create) return l;
  l = new QList<GtkTreeRowReference *>;
  g_object_set_data_full (G_OBJECT (m), "gtkqt-row-refs", l, [] (gpointer p) { delete static_cast<QList<GtkTreeRowReference *> *> (p); });
  g_signal_connect (m, "row-inserted", G_CALLBACK (+[] (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *) {
    for (GtkTreeRowReference *r : *model_refs (model, false)) ref_inserted (r, path);
  }), nullptr);
  g_signal_connect (m, "row-deleted", G_CALLBACK (+[] (GtkTreeModel *model, GtkTreePath *path) {
    for (GtkTreeRowReference *r : *model_refs (model, false)) ref_deleted (r, path);
  }), nullptr);
  g_signal_connect (m, "rows-reordered", G_CALLBACK (+[] (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gint *order) {
    for (GtkTreeRowReference *r : *model_refs (model, false)) ref_reordered (r, path, iter, order);
  }), nullptr);
  return l;
}

void ref_inserted (GtkTreeRowReference *r, GtkTreePath *path)
{
  if (!r->path) return;
  size_t d = path->idx.size ();
  if (d == 0 || r->path->idx.size () < d) return;
  if (!std::equal (path->idx.begin (), path->idx.end () - 1, r->path->idx.begin ())) return;
  if (path->idx[d - 1] <= r->path->idx[d - 1]) r->path->idx[d - 1]++;
}

void ref_deleted (GtkTreeRowReference *r, GtkTreePath *path)
{
  if (!r->path) return;
  size_t d = path->idx.size ();
  if (d == 0 || r->path->idx.size () < d) return;
  if (!std::equal (path->idx.begin (), path->idx.end () - 1, r->path->idx.begin ())) return;
  if (path->idx[d - 1] == r->path->idx[d - 1]) { gtk_tree_path_free (r->path); r->path = nullptr; }
  else if (path->idx[d - 1] < r->path->idx[d - 1]) r->path->idx[d - 1]--;
}

void ref_reordered (GtkTreeRowReference *r, GtkTreePath *path, GtkTreeIter *iter, gint *order)
{
  if (!r->path || !order) return;
  size_t d = path->idx.size ();
  if (r->path->idx.size () <= d || !std::equal (path->idx.begin (), path->idx.end (), r->path->idx.begin ())) return;
  int n = gtk_tree_model_iter_n_children (r->model, d ? iter : nullptr);
  for (int i = 0; i < n; i++)
    if (order[i] == r->path->idx[d]) { r->path->idx[d] = i; break; }
}
}

extern "C" {

GtkTreeRowReference *gtk_tree_row_reference_new (GtkTreeModel *model, GtkTreePath *path)
{
  GtkTreeIter it;
  if (!model || !path || !gtk_tree_model_get_iter (model, &it, path)) return nullptr;
  auto *ref = new GtkTreeRowReference { (GtkTreeModel *) g_object_ref (model), gtk_tree_path_copy (path) };
  *model_refs (model, true) << ref;
  return ref;
}
/* a proxy forwards the model's changes itself (gtk_tree_row_reference_inserted ...) */
GtkTreeRowReference *gtk_tree_row_reference_new_proxy (GObject *, GtkTreeModel *model, GtkTreePath *path)
{
  return gtk_tree_row_reference_new (model, path);
}
GtkTreeRowReference *gtk_tree_row_reference_copy (GtkTreeRowReference *ref)
{
  if (!ref) return nullptr;
  auto *c = new GtkTreeRowReference { (GtkTreeModel *) g_object_ref (ref->model), gtk_tree_path_copy (ref->path) };
  *model_refs (ref->model, true) << c;
  return c;
}
void gtk_tree_row_reference_free (GtkTreeRowReference *ref)
{
  if (!ref) return;
  if (auto *l = model_refs (ref->model, false)) l->removeOne (ref);
  gtk_tree_path_free (ref->path);
  g_object_unref (ref->model);
  delete ref;
}
GtkTreePath *gtk_tree_row_reference_get_path (GtkTreeRowReference *ref) { return ref && ref->path ? gtk_tree_path_copy (ref->path) : nullptr; }
GtkTreeModel *gtk_tree_row_reference_get_model (GtkTreeRowReference *ref) { return ref ? ref->model : nullptr; }
gboolean gtk_tree_row_reference_valid (GtkTreeRowReference *ref) { return ref && ref->path; }
GType gtk_tree_row_reference_get_type (void)
{
  static GType t = g_boxed_type_register_static ("GtkTreeRowReference", (GBoxedCopyFunc) gtk_tree_row_reference_copy,
                                                 (GBoxedFreeFunc) gtk_tree_row_reference_free);
  return t;
}
/* the model's references are updated from its signals; these are the
 * entry points a proxy calls (here: for the model given as proxy) */
void gtk_tree_row_reference_inserted (GObject *proxy, GtkTreePath *path)
{
  if (auto *l = GTK_IS_TREE_MODEL (proxy) ? model_refs (GTK_TREE_MODEL (proxy), false) : nullptr)
    for (GtkTreeRowReference *r : *l) ref_inserted (r, path);
}
void gtk_tree_row_reference_deleted (GObject *proxy, GtkTreePath *path)
{
  if (auto *l = GTK_IS_TREE_MODEL (proxy) ? model_refs (GTK_TREE_MODEL (proxy), false) : nullptr)
    for (GtkTreeRowReference *r : *l) ref_deleted (r, path);
}
void gtk_tree_row_reference_reordered (GObject *proxy, GtkTreePath *path, GtkTreeIter *iter, gint *new_order)
{
  if (auto *l = GTK_IS_TREE_MODEL (proxy) ? model_refs (GTK_TREE_MODEL (proxy), false) : nullptr)
    for (GtkTreeRowReference *r : *l) ref_reordered (r, path, iter, new_order);
}

} /* extern "C" */

/* ======================================================================
 * Proxy models over a child model: GtkTreeModelFilter, GtkTreeModelSort
 *
 * Both keep a tree of nodes, one per row of the child model, holding the
 * row's index in its child level. Iters point at nodes. Each level has the
 * nodes in child order (all) and in the proxy's order (shown): the visible
 * ones for a filter, the sorted ones for a sort model. The child's signals
 * update the tree and are passed on in the proxy's own paths.
 * ====================================================================== */
namespace {
struct PLevel;
struct PNode {
  int child_index = 0;           /* index in the child model's level */
  bool visible = true;
  PNode *parent = nullptr;
  PLevel *level = nullptr;
  PLevel *children = nullptr;
  ~PNode ();
};
struct PLevel {
  PNode *owner = nullptr;        /* nullptr for the root level */
  QList<PNode *> all;            /* child order */
  QList<PNode *> shown;          /* proxy order */
  ~PLevel () { qDeleteAll (all); }
};
PNode::~PNode () { delete children; }

struct ProxyData {
  GtkTreeModel *self = nullptr;
  GtkTreeModel *child = nullptr;
  PLevel *root = nullptr;
  gint stamp = (gint) g_random_int ();
  bool is_filter = true;
  gulong handlers[5] = {};
  /* filter */
  GtkTreeModelFilterVisibleFunc visible_func = nullptr;
  gpointer visible_data = nullptr;
  GDestroyNotify visible_destroy = nullptr;
  int visible_column = -1;
  GtkTreePath *virtual_root = nullptr;
  QVector<GType> modify_types;
  GtkTreeModelFilterModifyFunc modify_func = nullptr;
  gpointer modify_data = nullptr;
  GDestroyNotify modify_destroy = nullptr;
  /* sort */
  SortState sort;
};

ProxyData *pd (gpointer o) { return static_cast<ProxyData *> (((GtkTreeModelFilter *) o)->priv); }

/* the child path of a node */
GtkTreePath *child_path_of (ProxyData *d, PNode *n)
{
  auto *p = new GtkTreePath;
  for (PNode *x = n; x; x = x->parent) p->idx.insert (p->idx.begin (), x->child_index);
  if (d->virtual_root) p->idx.insert (p->idx.begin (), d->virtual_root->idx.begin (), d->virtual_root->idx.end ());
  return p;
}
bool child_iter_of (ProxyData *d, PNode *n, GtkTreeIter *it)
{
  if (!n) return false;
  GtkTreePath *p = child_path_of (d, n);
  bool ok = gtk_tree_model_get_iter (d->child, it, p);
  gtk_tree_path_free (p);
  return ok;
}
/* the proxy path of a shown node */
GtkTreePath *proxy_path_of (PNode *n)
{
  auto *p = new GtkTreePath;
  for (PNode *x = n; x; x = x->parent) p->idx.insert (p->idx.begin (), x->level->shown.indexOf (x));
  return p;
}
bool node_shown (PNode *n)
{
  for (PNode *x = n; x; x = x->parent) if (!x->visible || !x->level->shown.contains (x)) return false;
  return true;
}
void set_piter (ProxyData *d, GtkTreeIter *it, PNode *n)
{
  if (!it) return;
  it->stamp = n ? d->stamp : 0;
  it->user_data = n;
  it->user_data2 = it->user_data3 = nullptr;
}
PNode *piter_node (ProxyData *d, GtkTreeIter *it) { return it && it->stamp == d->stamp ? static_cast<PNode *> (it->user_data) : nullptr; }

bool filter_visible (ProxyData *d, GtkTreeIter *child_iter)
{
  if (!d->is_filter) return true;
  if (d->visible_func) return d->visible_func (d->child, child_iter, d->visible_data);
  if (d->visible_column >= 0) {
    GValue v = G_VALUE_INIT;
    gtk_tree_model_get_value (d->child, child_iter, d->visible_column, &v);
    bool vis = G_IS_VALUE (&v) && G_VALUE_HOLDS_BOOLEAN (&v) && g_value_get_boolean (&v);
    if (G_IS_VALUE (&v)) g_value_unset (&v);
    return vis;
  }
  return true;
}

/* position of n in its level's shown list under the sort (binary search) */
int sorted_position (ProxyData *d, PLevel *lv, PNode *n)
{
  if (!d->sort.active ()) {
    int pos = 0;
    while (pos < lv->shown.size () && lv->shown[pos]->child_index < n->child_index) pos++;
    return pos;
  }
  GtkTreeIter in;
  child_iter_of (d, n, &in);
  int lo = 0, hi = lv->shown.size ();
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    GtkTreeIter im;
    child_iter_of (d, lv->shown[mid], &im);
    int c = compare_iters (d->sort, d->child, &in, &im);
    if (c < 0 || (c == 0 && n->child_index < lv->shown[mid]->child_index)) hi = mid; else lo = mid + 1;
  }
  return lo;
}

void sort_shown (ProxyData *d, PLevel *lv)
{
  if (d->is_filter) {
    lv->shown.clear ();
    for (PNode *n : lv->all) if (n->visible) lv->shown << n;
    return;
  }
  QList<PNode *> v = lv->all;
  if (d->sort.active ()) {
    QVector<GtkTreeIter> iters (v.size ());
    QHash<PNode *, int> at;
    for (int i = 0; i < v.size (); i++) { child_iter_of (d, v[i], &iters[i]); at.insert (v[i], i); }
    std::stable_sort (v.begin (), v.end (), [&] (PNode *a, PNode *b) {
      return compare_iters (d->sort, d->child, &iters[at[a]], &iters[at[b]]) < 0;
    });
  }
  lv->shown = v;
}

/* build a level from the child model (parent: child iter or nullptr) */
PLevel *build_level (ProxyData *d, PNode *owner, GtkTreeIter *child_parent)
{
  auto *lv = new PLevel;
  lv->owner = owner;
  GtkTreeIter it;
  int i = 0;
  if (gtk_tree_model_iter_children (d->child, &it, child_parent)) {
    do {
      auto *n = new PNode;
      n->child_index = i++;
      n->parent = owner;
      n->level = lv;
      n->visible = filter_visible (d, &it);
      lv->all << n;
      if (gtk_tree_model_iter_has_child (d->child, &it)) {
        GtkTreeIter c = it;
        n->children = build_level (d, n, &c);
      }
    } while (gtk_tree_model_iter_next (d->child, &it));
  }
  sort_shown (d, lv);
  return lv;
}

void build_root (ProxyData *d)
{
  delete d->root;
  d->root = nullptr;
  if (!d->child) return;
  if (d->virtual_root) {
    GtkTreeIter vr;
    if (!gtk_tree_model_get_iter (d->child, &vr, d->virtual_root)) { d->root = new PLevel; return; }
    d->root = build_level (d, nullptr, &vr);
  } else {
    d->root = build_level (d, nullptr, nullptr);
  }
}

/* the node for a child path (nullptr when outside the virtual root or unknown) */
PNode *node_for_child_path (ProxyData *d, GtkTreePath *cp, PLevel **level_out = nullptr, int depth_limit = -1)
{
  std::vector<gint> idx = cp->idx;
  if (d->virtual_root) {
    const auto &vr = d->virtual_root->idx;
    if (idx.size () <= vr.size () || !std::equal (vr.begin (), vr.end (), idx.begin ())) return nullptr;
    idx.erase (idx.begin (), idx.begin () + vr.size ());
  }
  int depth = depth_limit < 0 ? (int) idx.size () : depth_limit;
  PLevel *lv = d->root;
  PNode *n = nullptr;
  for (int k = 0; k < depth; k++) {
    if (!lv || idx[k] < 0 || idx[k] >= lv->all.size ()) return nullptr;
    n = lv->all[idx[k]];
    if (k + 1 < depth) lv = n->children;
  }
  if (level_out) *level_out = lv;
  return n;
}

/* relative child path: within the virtual root */
bool relative_depth (ProxyData *d, GtkTreePath *cp, int *depth)
{
  int off = d->virtual_root ? (int) d->virtual_root->idx.size () : 0;
  if (d->virtual_root && ((int) cp->idx.size () <= off || !std::equal (d->virtual_root->idx.begin (), d->virtual_root->idx.end (), cp->idx.begin ())))
    return false;
  *depth = (int) cp->idx.size () - off;
  return true;
}

void emit_inserted (ProxyData *d, PNode *n)
{
  GtkTreePath *p = proxy_path_of (n);
  GtkTreeIter it;
  set_piter (d, &it, n);
  gtk_tree_model_row_inserted (d->self, p, &it);
  gtk_tree_path_free (p);
  if (n->parent && n->level->shown.size () == 1 && node_shown (n->parent)) {
    GtkTreePath *pp = proxy_path_of (n->parent);
    GtkTreeIter pi;
    set_piter (d, &pi, n->parent);
    gtk_tree_model_row_has_child_toggled (d->self, pp, &pi);
    gtk_tree_path_free (pp);
  }
}

/* show a hidden node: into its level's shown list, announced */
void show_node (ProxyData *d, PNode *n)
{
  PLevel *lv = n->level;
  n->visible = true;
  if (lv->shown.contains (n)) return;
  lv->shown.insert (sorted_position (d, lv, n), n);
  if (!n->parent || node_shown (n->parent)) emit_inserted (d, n);
}

void hide_node (ProxyData *d, PNode *n)
{
  PLevel *lv = n->level;
  bool was_shown = node_shown (n);
  GtkTreePath *p = was_shown ? proxy_path_of (n) : nullptr;
  lv->shown.removeAll (n);
  n->visible = false;
  if (p) {
    gtk_tree_model_row_deleted (d->self, p);
    gtk_tree_path_free (p);
    if (n->parent && lv->shown.isEmpty () && node_shown (n->parent)) {
      GtkTreePath *pp = proxy_path_of (n->parent);
      GtkTreeIter pi;
      set_piter (d, &pi, n->parent);
      gtk_tree_model_row_has_child_toggled (d->self, pp, &pi);
      gtk_tree_path_free (pp);
    }
  }
}

/* ---------- the child's signals ---------- */
void child_row_inserted (GtkTreeModel *, GtkTreePath *cp, GtkTreeIter *citer, gpointer data)
{
  ProxyData *d = static_cast<ProxyData *> (data);
  int depth;
  if (!d->root || !relative_depth (d, cp, &depth)) return;
  PLevel *lv = d->root;
  PNode *owner = nullptr;
  if (depth > 1) {
    owner = node_for_child_path (d, cp, nullptr, depth - 1);
    if (!owner) return;
    if (!owner->children) { owner->children = new PLevel; owner->children->owner = owner; }
    lv = owner->children;
  }
  int ci = cp->idx.back ();
  if (ci < 0 || ci > lv->all.size ()) return;
  for (PNode *x : lv->all) if (x->child_index >= ci) x->child_index++;
  auto *n = new PNode;
  n->child_index = ci;
  n->parent = owner;
  n->level = lv;
  n->visible = false;
  lv->all.insert (ci, n);
  if (gtk_tree_model_iter_has_child (d->child, citer)) {
    GtkTreeIter c = *citer;
    n->children = build_level (d, n, &c);
  }
  if (filter_visible (d, citer)) show_node (d, n);
}

void child_row_changed (GtkTreeModel *, GtkTreePath *cp, GtkTreeIter *citer, gpointer data)
{
  ProxyData *d = static_cast<ProxyData *> (data);
  PNode *n = d->root ? node_for_child_path (d, cp) : nullptr;
  if (!n) return;
  bool vis = filter_visible (d, citer);
  if (vis && !n->visible) { show_node (d, n); return; }
  if (!vis && n->visible) { hide_node (d, n); return; }
  if (!n->visible) return;
  if (!d->is_filter && d->sort.active ()) {
    /* to its new place */
    PLevel *lv = n->level;
    QList<PNode *> before = lv->shown;
    lv->shown.removeOne (n);
    lv->shown.insert (sorted_position (d, lv, n), n);
    if (lv->shown != before && (!n->parent || node_shown (n->parent))) {
      std::vector<gint> order;
      for (PNode *x : lv->shown) order.push_back (before.indexOf (x));
      GtkTreePath *pp = n->parent ? proxy_path_of (n->parent) : gtk_tree_path_new ();
      GtkTreeIter pi;
      set_piter (d, &pi, n->parent);
      gtk_tree_model_rows_reordered (d->self, pp, n->parent ? &pi : nullptr, order.data ());
      gtk_tree_path_free (pp);
    }
  }
  if (node_shown (n)) {
    GtkTreePath *p = proxy_path_of (n);
    GtkTreeIter it;
    set_piter (d, &it, n);
    gtk_tree_model_row_changed (d->self, p, &it);
    gtk_tree_path_free (p);
  }
}

void child_row_deleted (GtkTreeModel *, GtkTreePath *cp, gpointer data)
{
  ProxyData *d = static_cast<ProxyData *> (data);
  if (!d->root) return;
  if (d->virtual_root) {
    /* the virtual root itself, or one of its ancestors, went: empty */
    const auto &vr = d->virtual_root->idx;
    if (cp->idx.size () <= vr.size () && std::equal (cp->idx.begin (), cp->idx.end (), vr.begin ())) {
      if (cp->idx == vr || cp->idx.size () < vr.size ()) {
        if (cp->idx.size () == vr.size () || cp->idx.back () == vr[cp->idx.size () - 1]) {
          while (!d->root->shown.isEmpty ()) hide_node (d, d->root->shown.last ());
          qDeleteAll (d->root->all);
          d->root->all.clear ();
          return;
        }
      }
    }
    /* a sibling before the virtual root (or an ancestor of it) moves it */
    if (cp->idx.size () <= vr.size () && std::equal (cp->idx.begin (), cp->idx.end () - 1, vr.begin ())
        && cp->idx.back () < vr[cp->idx.size () - 1]) {
      d->virtual_root->idx[cp->idx.size () - 1]--;
      return;
    }
  }
  PLevel *lv = nullptr;
  PNode *n = node_for_child_path (d, cp, &lv);
  if (!n) return;
  if (n->visible && lv->shown.contains (n)) hide_node (d, n);
  int ci = n->child_index;
  lv->all.removeOne (n);
  delete n;
  for (PNode *x : lv->all) if (x->child_index > ci) x->child_index--;
}

void child_has_child_toggled (GtkTreeModel *, GtkTreePath *cp, GtkTreeIter *citer, gpointer data)
{
  ProxyData *d = static_cast<ProxyData *> (data);
  PNode *n = d->root ? node_for_child_path (d, cp) : nullptr;
  if (!n) return;
  if (!gtk_tree_model_iter_has_child (d->child, citer)) {
    if (n->children && n->children->shown.isEmpty ()) { delete n->children; n->children = nullptr; }
  } else if (!n->children) {
    GtkTreeIter c = *citer;
    n->children = build_level (d, n, &c);
  }
  if (node_shown (n)) {
    GtkTreePath *p = proxy_path_of (n);
    GtkTreeIter it;
    set_piter (d, &it, n);
    gtk_tree_model_row_has_child_toggled (d->self, p, &it);
    gtk_tree_path_free (p);
  }
}

void child_rows_reordered (GtkTreeModel *, GtkTreePath *cp, GtkTreeIter *, gint *order, gpointer data)
{
  ProxyData *d = static_cast<ProxyData *> (data);
  if (!d->root || !order) return;
  PLevel *lv;
  PNode *owner = nullptr;
  int depth;
  if (d->virtual_root && cp->idx == d->virtual_root->idx) depth = 0;
  else if (!relative_depth (d, cp, &depth)) return;
  if (depth == 0) lv = d->root;
  else {
    owner = node_for_child_path (d, cp);
    if (!owner || !owner->children) return;
    lv = owner->children;
  }
  int n = lv->all.size ();
  QVector<int> new_index (n);
  for (int i = 0; i < n; i++) if (order[i] >= 0 && order[i] < n) new_index[order[i]] = i;
  for (PNode *x : lv->all) x->child_index = new_index[x->child_index];
  std::sort (lv->all.begin (), lv->all.end (), [] (PNode *a, PNode *b) { return a->child_index < b->child_index; });
  /* the proxy order changes only where it follows the child's */
  if (!d->is_filter && d->sort.active ()) return;
  QList<PNode *> before = lv->shown;
  sort_shown (d, lv);
  if (lv->shown == before || (owner && !node_shown (owner))) return;
  std::vector<gint> porder;
  for (PNode *x : lv->shown) porder.push_back (before.indexOf (x));
  GtkTreePath *pp = owner ? proxy_path_of (owner) : gtk_tree_path_new ();
  GtkTreeIter pi;
  set_piter (d, &pi, owner);
  gtk_tree_model_rows_reordered (d->self, pp, owner ? &pi : nullptr, porder.data ());
  gtk_tree_path_free (pp);
}

void proxy_set_child (ProxyData *d, GtkTreeModel *child)
{
  d->child = (GtkTreeModel *) g_object_ref (child);
  d->handlers[0] = g_signal_connect (child, "row-inserted", G_CALLBACK (child_row_inserted), d);
  d->handlers[1] = g_signal_connect (child, "row-changed", G_CALLBACK (child_row_changed), d);
  d->handlers[2] = g_signal_connect (child, "row-deleted", G_CALLBACK (child_row_deleted), d);
  d->handlers[3] = g_signal_connect (child, "row-has-child-toggled", G_CALLBACK (child_has_child_toggled), d);
  d->handlers[4] = g_signal_connect (child, "rows-reordered", G_CALLBACK (child_rows_reordered), d);
  build_root (d);
}

void proxy_free (ProxyData *d)
{
  if (d->child) {
    for (gulong h : d->handlers) if (h) g_signal_handler_disconnect (d->child, h);
    g_object_unref (d->child);
  }
  delete d->root;
  if (d->visible_destroy) d->visible_destroy (d->visible_data);
  if (d->modify_destroy) d->modify_destroy (d->modify_data);
  if (d->virtual_root) gtk_tree_path_free (d->virtual_root);
  delete d;
}

/* ---------- the proxies' GtkTreeModel ---------- */
GtkTreeModelFlags proxy_get_flags (GtkTreeModel *m)
{
  ProxyData *d = pd (m);
  guint f = gtk_tree_model_get_flags (d->child);
  return (GtkTreeModelFlags) ((f & GTK_TREE_MODEL_LIST_ONLY) | GTK_TREE_MODEL_ITERS_PERSIST);
}
gint proxy_n_columns (GtkTreeModel *m)
{
  ProxyData *d = pd (m);
  return d->modify_func ? d->modify_types.size () : gtk_tree_model_get_n_columns (d->child);
}
GType proxy_column_type (GtkTreeModel *m, gint i)
{
  ProxyData *d = pd (m);
  return d->modify_func ? d->modify_types.value (i, G_TYPE_INVALID) : gtk_tree_model_get_column_type (d->child, i);
}
gboolean proxy_get_iter (GtkTreeModel *m, GtkTreeIter *iter, GtkTreePath *path)
{
  ProxyData *d = pd (m);
  PLevel *lv = d->root;
  PNode *n = nullptr;
  for (size_t k = 0; k < path->idx.size (); k++) {
    int i = path->idx[k];
    if (!lv || i < 0 || i >= lv->shown.size ()) { set_piter (d, iter, nullptr); return FALSE; }
    n = lv->shown[i];
    lv = n->children;
  }
  set_piter (d, iter, n);
  return n != nullptr;
}
GtkTreePath *proxy_get_path (GtkTreeModel *m, GtkTreeIter *iter)
{
  PNode *n = piter_node (pd (m), iter);
  return n && node_shown (n) ? proxy_path_of (n) : nullptr;
}
void proxy_get_value (GtkTreeModel *m, GtkTreeIter *iter, gint column, GValue *value)
{
  ProxyData *d = pd (m);
  if (d->modify_func) {
    if (column < 0 || column >= d->modify_types.size ()) return;
    g_value_init (value, d->modify_types[column]);
    d->modify_func (m, iter, value, column, d->modify_data);
    return;
  }
  GtkTreeIter ci;
  PNode *n = piter_node (d, iter);
  if (n && child_iter_of (d, n, &ci)) gtk_tree_model_get_value (d->child, &ci, column, value);
  else g_value_init (value, gtk_tree_model_get_column_type (d->child, column));
}
gboolean proxy_iter_next (GtkTreeModel *m, GtkTreeIter *iter)
{
  ProxyData *d = pd (m);
  PNode *n = piter_node (d, iter);
  if (!n) return FALSE;
  int i = n->level->shown.indexOf (n) + 1;
  PNode *x = i > 0 && i < n->level->shown.size () ? n->level->shown[i] : nullptr;
  set_piter (d, iter, x);
  return x != nullptr;
}
gboolean proxy_iter_previous (GtkTreeModel *m, GtkTreeIter *iter)
{
  ProxyData *d = pd (m);
  PNode *n = piter_node (d, iter);
  if (!n) return FALSE;
  int i = n->level->shown.indexOf (n) - 1;
  PNode *x = i >= 0 ? n->level->shown[i] : nullptr;
  set_piter (d, iter, x);
  return x != nullptr;
}
PLevel *level_of (ProxyData *d, GtkTreeIter *parent)
{
  if (!parent) return d->root;
  PNode *p = piter_node (d, parent);
  return p ? p->children : nullptr;
}
gboolean proxy_iter_nth_child (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{
  ProxyData *d = pd (m);
  PLevel *lv = level_of (d, parent);
  PNode *x = lv && n >= 0 && n < lv->shown.size () ? lv->shown[n] : nullptr;
  set_piter (d, iter, x);
  return x != nullptr;
}
gboolean proxy_iter_children (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *parent) { return proxy_iter_nth_child (m, iter, parent, 0); }
gboolean proxy_iter_has_child (GtkTreeModel *m, GtkTreeIter *iter)
{
  PNode *n = piter_node (pd (m), iter);
  return n && n->children && !n->children->shown.isEmpty ();
}
gint proxy_iter_n_children (GtkTreeModel *m, GtkTreeIter *iter)
{
  PLevel *lv = level_of (pd (m), iter);
  return lv ? lv->shown.size () : 0;
}
gboolean proxy_iter_parent (GtkTreeModel *m, GtkTreeIter *iter, GtkTreeIter *child)
{
  ProxyData *d = pd (m);
  PNode *n = piter_node (d, child);
  PNode *p = n ? n->parent : nullptr;
  set_piter (d, iter, p);
  return p != nullptr;
}

void proxy_model_iface_init (gpointer g, gpointer)
{
  auto *i = (GtkTreeModelIface *) g;
  i->get_flags = proxy_get_flags;
  i->get_n_columns = proxy_n_columns;
  i->get_column_type = proxy_column_type;
  i->get_iter = proxy_get_iter;
  i->get_path = proxy_get_path;
  i->get_value = proxy_get_value;
  i->iter_next = proxy_iter_next;
  i->iter_previous = proxy_iter_previous;
  i->iter_children = proxy_iter_children;
  i->iter_has_child = proxy_iter_has_child;
  i->iter_n_children = proxy_iter_n_children;
  i->iter_nth_child = proxy_iter_nth_child;
  i->iter_parent = proxy_iter_parent;
}

/* ---------- GtkTreeModelSort's sorting ---------- */
void resort_level (ProxyData *d, PLevel *lv, PNode *owner)
{
  QList<PNode *> before = lv->shown;
  sort_shown (d, lv);
  if (lv->shown != before && (!owner || node_shown (owner))) {
    std::vector<gint> order;
    for (PNode *x : lv->shown) order.push_back (before.indexOf (x));
    GtkTreePath *pp = owner ? proxy_path_of (owner) : gtk_tree_path_new ();
    GtkTreeIter pi;
    set_piter (d, &pi, owner);
    gtk_tree_model_rows_reordered (d->self, pp, owner ? &pi : nullptr, order.data ());
    gtk_tree_path_free (pp);
  }
  for (PNode *x : lv->all) if (x->children) resort_level (d, x->children, x);
}

gboolean sort_get_sort_column_id (GtkTreeSortable *s, gint *col, GtkSortType *order)
{
  ProxyData *d = pd (s);
  if (col) *col = d->sort.column;
  if (order) *order = d->sort.order;
  return d->sort.column != GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID && d->sort.column != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
}
void sort_set_sort_column_id (GtkTreeSortable *s, gint col, GtkSortType order)
{
  ProxyData *d = pd (s);
  if (d->sort.column == col && d->sort.order == order) return;
  d->sort.column = col;
  d->sort.order = order;
  gtk_tree_sortable_sort_column_changed (s);
  if (d->root) resort_level (d, d->root, nullptr);
}
void sort_set_sort_func (GtkTreeSortable *s, gint id, GtkTreeIterCompareFunc f, gpointer data, GDestroyNotify destroy)
{
  ProxyData *d = pd (s);
  d->sort.set_func (id, f, data, destroy);
  if (d->sort.column == id && d->root) resort_level (d, d->root, nullptr);
}
void sort_set_default_sort_func (GtkTreeSortable *s, GtkTreeIterCompareFunc f, gpointer data, GDestroyNotify destroy)
{
  ProxyData *d = pd (s);
  d->sort.set_default (f, data, destroy);
  if (d->sort.column == GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID && d->root) resort_level (d, d->root, nullptr);
}
gboolean sort_has_default_sort_func (GtkTreeSortable *s) { return pd (s)->sort.default_func.func != nullptr; }

void sort_sortable_iface_init (gpointer g, gpointer)
{
  auto *i = (GtkTreeSortableIface *) g;
  i->get_sort_column_id = sort_get_sort_column_id;
  i->set_sort_column_id = sort_set_sort_column_id;
  i->set_sort_func = sort_set_sort_func;
  i->set_default_sort_func = sort_set_default_sort_func;
  i->has_default_sort_func = sort_has_default_sort_func;
}
} /* namespace */

#define RW (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)

G_DEFINE_TYPE_WITH_CODE (GtkTreeModelFilter, gtk_tree_model_filter, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, proxy_model_iface_init))
G_DEFINE_TYPE_WITH_CODE (GtkTreeModelSort, gtk_tree_model_sort, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, proxy_model_iface_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_SORTABLE, sort_sortable_iface_init))

static void gtk_tree_model_filter_init (GtkTreeModelFilter *f)
{
  auto *d = new ProxyData;
  d->self = GTK_TREE_MODEL (f);
  f->priv = d;
}
static void gtk_tree_model_sort_init (GtkTreeModelSort *s)
{
  auto *d = new ProxyData;
  d->self = (GtkTreeModel *) s;
  d->is_filter = false;
  s->priv = d;
}
static void filter_finalize (GObject *o) { proxy_free (pd (o)); G_OBJECT_CLASS (gtk_tree_model_filter_parent_class)->finalize (o); }
static void sort_finalize (GObject *o) { proxy_free (pd (o)); G_OBJECT_CLASS (gtk_tree_model_sort_parent_class)->finalize (o); }

enum { PROP_CHILD_MODEL = 1, PROP_VIRTUAL_ROOT, PROP_MODEL };

static void filter_set_property (GObject *o, guint id, const GValue *v, GParamSpec *)
{
  ProxyData *d = pd (o);
  if (id == PROP_CHILD_MODEL && g_value_get_object (v) && !d->child) proxy_set_child (d, (GtkTreeModel *) g_value_get_object (v));
  else if (id == PROP_VIRTUAL_ROOT && g_value_get_boxed (v)) {
    if (d->virtual_root) gtk_tree_path_free (d->virtual_root);
    d->virtual_root = gtk_tree_path_copy ((GtkTreePath *) g_value_get_boxed (v));
    if (d->child) build_root (d);
  }
}
static void filter_get_property (GObject *o, guint id, GValue *v, GParamSpec *)
{
  ProxyData *d = pd (o);
  if (id == PROP_CHILD_MODEL) g_value_set_object (v, d->child);
  else if (id == PROP_VIRTUAL_ROOT) g_value_set_boxed (v, d->virtual_root);
}
static void gtk_tree_model_filter_class_init (GtkTreeModelFilterClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  oc->finalize = filter_finalize;
  oc->set_property = filter_set_property;
  oc->get_property = filter_get_property;
  /* the virtual root comes first (construct order), so the tree is built once */
  g_object_class_install_property (oc, PROP_VIRTUAL_ROOT, g_param_spec_boxed ("virtual-root", nullptr, nullptr, GTK_TYPE_TREE_PATH,
                                                                              (GParamFlags) (RW | G_PARAM_CONSTRUCT_ONLY)));
  g_object_class_install_property (oc, PROP_CHILD_MODEL, g_param_spec_object ("child-model", nullptr, nullptr, GTK_TYPE_TREE_MODEL,
                                                                             (GParamFlags) (RW | G_PARAM_CONSTRUCT_ONLY)));
}

static void sort_set_property (GObject *o, guint id, const GValue *v, GParamSpec *)
{
  if (id == PROP_MODEL && g_value_get_object (v) && !pd (o)->child) proxy_set_child (pd (o), (GtkTreeModel *) g_value_get_object (v));
}
static void sort_get_property (GObject *o, guint id, GValue *v, GParamSpec *)
{
  if (id == PROP_MODEL) g_value_set_object (v, pd (o)->child);
}
static void gtk_tree_model_sort_class_init (GtkTreeModelSortClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  oc->finalize = sort_finalize;
  oc->set_property = sort_set_property;
  oc->get_property = sort_get_property;
  g_object_class_install_property (oc, PROP_MODEL, g_param_spec_object ("model", nullptr, nullptr, GTK_TYPE_TREE_MODEL,
                                                                       (GParamFlags) (RW | G_PARAM_CONSTRUCT_ONLY)));
}

extern "C" {

/* ---------- GtkTreeModelFilter ---------- */
GtkTreeModel *gtk_tree_model_filter_new (GtkTreeModel *child_model, GtkTreePath *root)
{
  return (GtkTreeModel *) g_object_new (GTK_TYPE_TREE_MODEL_FILTER, "child-model", child_model, "virtual-root", root, nullptr);
}

void gtk_tree_model_filter_set_visible_func (GtkTreeModelFilter *filter, GtkTreeModelFilterVisibleFunc func, gpointer data,
                                             GDestroyNotify destroy)
{
  ProxyData *d = pd (filter);
  if (d->visible_destroy) d->visible_destroy (d->visible_data);
  d->visible_func = func;
  d->visible_data = data;
  d->visible_destroy = destroy;
  build_root (d);   /* GTK: before the model is used */
}

void gtk_tree_model_filter_set_visible_column (GtkTreeModelFilter *filter, gint column)
{
  pd (filter)->visible_column = column;
  build_root (pd (filter));
}

void gtk_tree_model_filter_set_modify_func (GtkTreeModelFilter *filter, gint n_columns, GType *types,
                                            GtkTreeModelFilterModifyFunc func, gpointer data, GDestroyNotify destroy)
{
  ProxyData *d = pd (filter);
  if (d->modify_destroy) d->modify_destroy (d->modify_data);
  d->modify_types.clear ();
  for (int i = 0; i < n_columns; i++) d->modify_types << types[i];
  d->modify_func = func;
  d->modify_data = data;
  d->modify_destroy = destroy;
}

GtkTreeModel *gtk_tree_model_filter_get_model (GtkTreeModelFilter *filter) { return pd (filter)->child; }

/* visibility again for every row, as GTK's refilter */
static void refilter_level (ProxyData *d, PLevel *lv)
{
  for (PNode *n : QList<PNode *> (lv->all)) {
    GtkTreeIter ci;
    if (!child_iter_of (d, n, &ci)) continue;
    bool vis = filter_visible (d, &ci);
    if (vis && !n->visible) show_node (d, n);
    else if (!vis && n->visible) hide_node (d, n);
    if (n->children) refilter_level (d, n->children);
  }
}
void gtk_tree_model_filter_refilter (GtkTreeModelFilter *filter)
{
  ProxyData *d = pd (filter);
  if (d->root) refilter_level (d, d->root);
}
void gtk_tree_model_filter_clear_cache (GtkTreeModelFilter *) {}

gboolean gtk_tree_model_filter_convert_child_iter_to_iter (GtkTreeModelFilter *filter, GtkTreeIter *filter_iter, GtkTreeIter *child_iter)
{
  ProxyData *d = pd (filter);
  GtkTreePath *cp = gtk_tree_model_get_path (d->child, child_iter);
  PNode *n = cp ? node_for_child_path (d, cp) : nullptr;
  gtk_tree_path_free (cp);
  bool ok = n && node_shown (n);
  set_piter (d, filter_iter, ok ? n : nullptr);
  return ok;
}
void gtk_tree_model_filter_convert_iter_to_child_iter (GtkTreeModelFilter *filter, GtkTreeIter *child_iter, GtkTreeIter *filter_iter)
{
  ProxyData *d = pd (filter);
  PNode *n = piter_node (d, filter_iter);
  if (!n || !child_iter_of (d, n, child_iter)) memset (child_iter, 0, sizeof (*child_iter));
}
GtkTreePath *gtk_tree_model_filter_convert_child_path_to_path (GtkTreeModelFilter *filter, GtkTreePath *child_path)
{
  ProxyData *d = pd (filter);
  PNode *n = child_path ? node_for_child_path (d, child_path) : nullptr;
  return n && node_shown (n) ? proxy_path_of (n) : nullptr;
}
GtkTreePath *gtk_tree_model_filter_convert_path_to_child_path (GtkTreeModelFilter *filter, GtkTreePath *filter_path)
{
  ProxyData *d = pd (filter);
  GtkTreeIter it;
  if (!proxy_get_iter (GTK_TREE_MODEL (filter), &it, filter_path)) return nullptr;
  return child_path_of (d, piter_node (d, &it));
}

/* ---------- GtkTreeModelSort ---------- */
GtkTreeModel *gtk_tree_model_sort_new_with_model (GtkTreeModel *child_model)
{
  return (GtkTreeModel *) g_object_new (GTK_TYPE_TREE_MODEL_SORT, "model", child_model, nullptr);
}
GtkTreeModel *gtk_tree_model_sort_get_model (GtkTreeModelSort *s) { return pd (s)->child; }

gboolean gtk_tree_model_sort_convert_child_iter_to_iter (GtkTreeModelSort *s, GtkTreeIter *sort_iter, GtkTreeIter *child_iter)
{
  return gtk_tree_model_filter_convert_child_iter_to_iter ((GtkTreeModelFilter *) s, sort_iter, child_iter);
}
void gtk_tree_model_sort_convert_iter_to_child_iter (GtkTreeModelSort *s, GtkTreeIter *child_iter, GtkTreeIter *sorted_iter)
{
  gtk_tree_model_filter_convert_iter_to_child_iter ((GtkTreeModelFilter *) s, child_iter, sorted_iter);
}
GtkTreePath *gtk_tree_model_sort_convert_child_path_to_path (GtkTreeModelSort *s, GtkTreePath *child_path)
{
  return gtk_tree_model_filter_convert_child_path_to_path ((GtkTreeModelFilter *) s, child_path);
}
GtkTreePath *gtk_tree_model_sort_convert_path_to_child_path (GtkTreeModelSort *s, GtkTreePath *sorted_path)
{
  return gtk_tree_model_filter_convert_path_to_child_path ((GtkTreeModelFilter *) s, sorted_path);
}
void gtk_tree_model_sort_reset_default_sort_func (GtkTreeModelSort *s)
{
  ProxyData *d = pd (s);
  d->sort.set_default (nullptr, nullptr, nullptr);
  d->sort.default_set = false;
  if (d->sort.column == GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID && d->root) resort_level (d, d->root, nullptr);
}
void gtk_tree_model_sort_clear_cache (GtkTreeModelSort *) {}
gboolean gtk_tree_model_sort_iter_is_valid (GtkTreeModelSort *s, GtkTreeIter *iter)
{
  PNode *n = piter_node (pd (s), iter);
  return n && node_shown (n);
}

} /* extern "C" */
