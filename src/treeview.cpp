/*
 * GTK made Qt - the GTK C API implemented on top of Qt
 * GtkListStore / GtkTreeStore (rows of GValues, as in GTK), GtkTreePath,
 * cell renderers, GtkTreeViewColumn, GtkTreeView and GtkTreeSelection.
 *
 * Each view gets a QAbstractItemModel adapter. For every cell the adapter
 * does what GTK does: copy the mapped model columns into the renderer's
 * properties ("text", "active", ...), run the cell data function, then read
 * the renderer's properties back as Qt item roles.
 * Licensed under the LGPL 2.1 or later; see COPYING.
 */
#include "gtkqt-private.h"

#include <gobject/gvaluecollector.h>
#include <QtWidgets>
#include <algorithm>
#include <vector>

#define RW (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)

class GtkQtItemModel;

/* paths and models are in treemodel.cpp; the view knows models only
 * through the GtkTreeModel interface */
/* ======================================================================
 * Cell renderers (properties hold the current cell's values)
 * ====================================================================== */
enum RendererKind { R_TEXT, R_TOGGLE, R_PIXBUF, R_PROGRESS, R_SPIN, R_COMBO };
struct RendererData {
  RendererKind kind;
  QString text, markup, foreground, background, icon_name;
  GdkPixbuf *pixbuf = nullptr;
  int weight = 400, value = 0;
  bool active = false, activatable = true, visible = true, editable = false, radio = false, sensitive = true;
  float xalign = 0;
  ~RendererData () { if (pixbuf) g_object_unref (pixbuf); }
};

static RendererData *rd (gpointer c) { return static_cast<RendererData *> (((GtkCellRenderer *) c)->priv); }

G_DEFINE_TYPE (GtkCellRenderer, gtk_cell_renderer, G_TYPE_INITIALLY_UNOWNED)
static void gtk_cell_renderer_init (GtkCellRenderer *c) { c->priv = new RendererData; }
static void renderer_finalize (GObject *o) { delete rd (o); G_OBJECT_CLASS (gtk_cell_renderer_parent_class)->finalize (o); }

#define RSTR(name, field) \
  gtkqt_prop (klass, g_param_spec_string (name, nullptr, nullptr, nullptr, RW), \
              [] (GObject *o, const GValue *v) { rd (o)->field = gtkqt_str (g_value_get_string (v)); }, \
              [] (GObject *o, GValue *v) { g_value_set_string (v, rd (o)->field.toUtf8 ().constData ()); })
#define RBOOL(name, field) \
  gtkqt_prop (klass, g_param_spec_boolean (name, nullptr, nullptr, FALSE, RW), \
              [] (GObject *o, const GValue *v) { rd (o)->field = g_value_get_boolean (v); }, \
              [] (GObject *o, GValue *v) { g_value_set_boolean (v, rd (o)->field); })

static void gtk_cell_renderer_class_init (GtkCellRendererClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = renderer_finalize;
  RSTR ("text", text);
  RSTR ("markup", markup);
  RSTR ("foreground", foreground);
  RSTR ("background", background);
  RSTR ("cell-background", background);
  RSTR ("icon-name", icon_name);
  RSTR ("stock-id", icon_name);
  RBOOL ("active", active);
  RBOOL ("activatable", activatable);
  RBOOL ("visible", visible);
  RBOOL ("editable", editable);
  RBOOL ("radio", radio);
  RBOOL ("sensitive", sensitive);
  gtkqt_prop (klass, g_param_spec_int ("weight", nullptr, nullptr, 0, 1000, 400, RW),
              [] (GObject *o, const GValue *v) { rd (o)->weight = g_value_get_int (v); }, nullptr);
  gtkqt_prop (klass, g_param_spec_int ("value", nullptr, nullptr, 0, 100, 0, RW),
              [] (GObject *o, const GValue *v) { rd (o)->value = g_value_get_int (v); }, nullptr);
  gtkqt_prop (klass, g_param_spec_float ("xalign", nullptr, nullptr, 0, 1, 0, RW),
              [] (GObject *o, const GValue *v) { rd (o)->xalign = g_value_get_float (v); }, nullptr);
  gtkqt_prop (klass, g_param_spec_object ("pixbuf", nullptr, nullptr, GDK_TYPE_PIXBUF, RW),
              [] (GObject *o, const GValue *v) {
                RendererData *d = rd (o);
                if (d->pixbuf) g_object_unref (d->pixbuf);
                d->pixbuf = (GdkPixbuf *) g_value_dup_object (v);
              }, nullptr);
  gtkqt_prop (klass, g_param_spec_boxed ("foreground-rgba", nullptr, nullptr, GDK_TYPE_RGBA, RW),
              [] (GObject *o, const GValue *v) {
                auto *c = (GdkRGBA *) g_value_get_boxed (v);
                rd (o)->foreground = c ? QColor::fromRgbF (c->red, c->green, c->blue, c->alpha).name (QColor::HexArgb) : QString ();
              }, nullptr);
  /* GTK 2 colours (GdkColor) */
  gtkqt_prop (klass, g_param_spec_boxed ("foreground-gdk", nullptr, nullptr, GDK_TYPE_COLOR, RW),
              [] (GObject *o, const GValue *v) {
                auto *c = (GdkColor *) g_value_get_boxed (v);
                rd (o)->foreground = c ? QColor (c->red >> 8, c->green >> 8, c->blue >> 8).name () : QString ();
              }, nullptr);
  gtkqt_prop (klass, g_param_spec_boxed ("background-gdk", nullptr, nullptr, GDK_TYPE_COLOR, RW),
              [] (GObject *o, const GValue *v) {
                auto *c = (GdkColor *) g_value_get_boxed (v);
                rd (o)->background = c ? QColor (c->red >> 8, c->green >> 8, c->blue >> 8).name () : QString ();
              }, nullptr);
  gtkqt_prop (klass, g_param_spec_boxed ("cell-background-gdk", nullptr, nullptr, GDK_TYPE_COLOR, RW),
              [] (GObject *o, const GValue *v) {
                auto *c = (GdkColor *) g_value_get_boxed (v);
                rd (o)->background = c ? QColor (c->red >> 8, c->green >> 8, c->blue >> 8).name () : QString ();
              }, nullptr);
  /* accepted; the view's rows have Qt's padding and eliding */
  for (const char *name : { "xpad", "ypad", "width", "height", "ellipsize", "wrap-width", "size", "mode" })
    gtkqt_prop (klass, g_param_spec_int (name, nullptr, nullptr, -1, G_MAXINT, 0, RW), [] (GObject *, const GValue *) {}, nullptr);
  gtkqt_prop (klass, g_param_spec_float ("yalign", nullptr, nullptr, 0, 1, 0.5, RW), [] (GObject *, const GValue *) {}, nullptr);
  for (const char *name : { "foreground-set", "background-set", "cell-background-set", "weight-set", "ellipsize-set",
                            "is-expander", "is-expanded", "strikethrough", "underline-set", "style-set" })
    gtkqt_prop (klass, g_param_spec_boolean (name, nullptr, nullptr, FALSE, RW), [] (GObject *, const GValue *) {}, nullptr);
  g_signal_new ("edited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
  g_signal_new ("toggled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static GType renderer_subtype (const char *name)
{
  return g_type_register_static_simple (GTK_TYPE_CELL_RENDERER, g_intern_static_string (name), sizeof (GtkCellRendererClass),
                                        nullptr, sizeof (GtkCellRenderer), nullptr, (GTypeFlags) 0);
}

static GtkCellRenderer *renderer_new (GType t, RendererKind kind)
{
  auto *c = (GtkCellRenderer *) g_object_new (t, nullptr);
  rd (c)->kind = kind;
  if (kind == R_PROGRESS) rd (c)->xalign = 0.5f;
  return c;
}

extern "C" {
GType gtk_cell_renderer_text_get_type (void) { static GType t = renderer_subtype ("GtkCellRendererText"); return t; }
GType gtk_cell_renderer_toggle_get_type (void) { static GType t = renderer_subtype ("GtkCellRendererToggle"); return t; }
GType gtk_cell_renderer_pixbuf_get_type (void) { static GType t = renderer_subtype ("GtkCellRendererPixbuf"); return t; }
GType gtk_cell_renderer_progress_get_type (void) { static GType t = renderer_subtype ("GtkCellRendererProgress"); return t; }
GType gtk_cell_renderer_spin_get_type (void) { static GType t = renderer_subtype ("GtkCellRendererSpin"); return t; }
GType gtk_cell_renderer_combo_get_type (void) { static GType t = renderer_subtype ("GtkCellRendererCombo"); return t; }
GtkCellRenderer *gtk_cell_renderer_text_new (void) { return renderer_new (gtk_cell_renderer_text_get_type (), R_TEXT); }
GtkCellRenderer *gtk_cell_renderer_toggle_new (void) { return renderer_new (gtk_cell_renderer_toggle_get_type (), R_TOGGLE); }
GtkCellRenderer *gtk_cell_renderer_pixbuf_new (void) { return renderer_new (gtk_cell_renderer_pixbuf_get_type (), R_PIXBUF); }
GtkCellRenderer *gtk_cell_renderer_progress_new (void) { return renderer_new (gtk_cell_renderer_progress_get_type (), R_PROGRESS); }
GtkCellRenderer *gtk_cell_renderer_spin_new (void) { return renderer_new (gtk_cell_renderer_spin_get_type (), R_SPIN); }
GtkCellRenderer *gtk_cell_renderer_combo_new (void) { return renderer_new (gtk_cell_renderer_combo_get_type (), R_COMBO); }
void gtk_cell_renderer_toggle_set_radio (GtkCellRendererToggle *t, gboolean radio) { rd (t)->radio = radio; }
void gtk_cell_renderer_toggle_set_active (GtkCellRendererToggle *t, gboolean setting) { rd (t)->active = setting; }
void gtk_cell_renderer_set_alignment (GtkCellRenderer *c, gfloat xalign, gfloat) { rd (c)->xalign = xalign; }
void gtk_cell_renderer_set_padding (GtkCellRenderer *, gint, gint) {}
void gtk_cell_renderer_set_visible (GtkCellRenderer *c, gboolean visible) { rd (c)->visible = visible; }
}

/* A column's list of cells with their attribute mappings (also used by combo boxes). */
struct Cell {
  GtkCellRenderer *renderer;
  QList<QPair<QByteArray, int>> attrs;
  GtkTreeCellDataFunc func = nullptr;
  gpointer func_data = nullptr;
  GDestroyNotify func_destroy = nullptr;
};

/* What the public GTK 2 fields (title, sort_column_id, visible ...) don't hold */
struct ColumnData {
  QString title;
  QList<Cell> cells;
  bool resizable = true;
  GtkTreeViewColumnSizing sizing = GTK_TREE_VIEW_COLUMN_GROW_ONLY;
  GtkWidget *view = nullptr;
};

/* ======================================================================
 * GtkTreeViewColumn
 * ====================================================================== */
typedef struct { GInitiallyUnownedClass parent_class; } GtkTreeViewColumnClass;
static ColumnData *cd (gpointer c) { return static_cast<ColumnData *> (((GtkTreeViewColumn *) c)->gtkqt_priv); }

static void cells_clear (QList<Cell> &cells)
{
  for (Cell &c : cells) {
    if (c.func_destroy) c.func_destroy (c.func_data);
    g_object_unref (c.renderer);
  }
  cells.clear ();
}

G_DEFINE_TYPE (GtkTreeViewColumn, gtk_tree_view_column, G_TYPE_INITIALLY_UNOWNED)
static void column_sort_clicked (GtkTreeViewColumn *c);
static void gtk_tree_view_column_init (GtkTreeViewColumn *c)
{
  c->gtkqt_priv = new ColumnData;
  c->visible = c->resizable = 1;
  c->sort_column_id = -1;
  c->fixed_width = c->min_width = c->max_width = -1;
  c->column_type = GTK_TREE_VIEW_COLUMN_GROW_ONLY;
  /* the header "button": the column's header section, as a widget programs
   * connect "clicked" and "size-allocate" to; it has no Qt widget of its own */
  c->button = gtk_button_new ();
  g_object_ref_sink (c->button);
  g_signal_connect_swapped (c->button, "clicked", G_CALLBACK (gtk_tree_view_column_clicked), c);
}
static void column_dispose (GObject *o)
{
  auto *c = (GtkTreeViewColumn *) o;
  if (c->button) { gtk_widget_destroy (c->button); g_object_unref (c->button); c->button = nullptr; }
  if (c->child) { g_object_unref (c->child); c->child = nullptr; }
  G_OBJECT_CLASS (gtk_tree_view_column_parent_class)->dispose (o);
}
static void column_finalize (GObject *o)
{
  cells_clear (cd (o)->cells);
  delete cd (o);
  g_free (((GtkTreeViewColumn *) o)->title);
  g_list_free (((GtkTreeViewColumn *) o)->cell_list);
  G_OBJECT_CLASS (gtk_tree_view_column_parent_class)->finalize (o);
}
static void gtk_tree_view_column_class_init (GtkTreeViewColumnClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = column_finalize;
  G_OBJECT_CLASS (klass)->dispose = column_dispose;
  /* GTK sorts from a handler of "clicked", after the program's own */
  g_signal_new_class_handler ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, G_CALLBACK (column_sort_clicked),
                              nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  gtkqt_prop (klass, g_param_spec_string ("title", nullptr, nullptr, nullptr, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_column_set_title ((GtkTreeViewColumn *) o, g_value_get_string (v)); }, nullptr);
  gtkqt_prop (klass, g_param_spec_boolean ("expand", nullptr, nullptr, FALSE, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_column_set_expand ((GtkTreeViewColumn *) o, g_value_get_boolean (v)); }, nullptr);
  gtkqt_prop (klass, g_param_spec_boolean ("resizable", nullptr, nullptr, FALSE, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_column_set_resizable ((GtkTreeViewColumn *) o, g_value_get_boolean (v)); }, nullptr);
  gtkqt_prop (klass, g_param_spec_int ("sort-column-id", nullptr, nullptr, -1, G_MAXINT, -1, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_column_set_sort_column_id ((GtkTreeViewColumn *) o, g_value_get_int (v)); }, nullptr);
}

/* ======================================================================
 * The Qt adapter
 * ====================================================================== */
/* The view's own tree of the model's rows, kept in step by the model's
 * signals, as GtkTreeView keeps one: Qt's indexes point at its nodes, and
 * the model is asked for the values of a node through its path (or the
 * iter it gave, when its iters persist). */
struct MNode {
  MNode *parent = nullptr;
  QList<MNode *> children;
  int row = 0;
  GtkTreeIter iter {};
  bool has_iter = false;
  ~MNode () { qDeleteAll (children); }
};

class GtkQtItemModel : public QAbstractItemModel {
public:
  GtkTreeModel *model;
  MNode root;
  bool persist = false;
  gulong handlers[5] = {};
  std::function<QList<GObject *> ()> columns;   /* GtkTreeViewColumn* list, or a single pseudo column */
  std::function<QList<Cell> *(GObject *)> cells_of;

  GtkQtItemModel (GtkTreeModel *m) : model ((GtkTreeModel *) g_object_ref (m))
  {
    persist = gtk_tree_model_get_flags (m) & GTK_TREE_MODEL_ITERS_PERSIST;
    handlers[0] = g_signal_connect (m, "row-inserted", G_CALLBACK (on_inserted), this);
    handlers[1] = g_signal_connect (m, "row-deleted", G_CALLBACK (on_deleted), this);
    handlers[2] = g_signal_connect (m, "row-changed", G_CALLBACK (on_changed), this);
    handlers[3] = g_signal_connect (m, "row-has-child-toggled", G_CALLBACK (on_child_toggled), this);
    handlers[4] = g_signal_connect (m, "rows-reordered", G_CALLBACK (on_reordered), this);
    build (&root, nullptr);
  }
  ~GtkQtItemModel () override { release (); }
  void release ()
  {
    if (!model) return;
    for (gulong h : handlers) if (h) g_signal_handler_disconnect (model, h);
    g_object_unref (model);
    model = nullptr;
  }

  /* ---------- the mirror tree ---------- */
  void build (MNode *n, GtkTreeIter *parent)
  {
    GtkTreeIter it;
    if (!gtk_tree_model_iter_children (model, &it, parent)) return;
    do {
      auto *c = new MNode;
      c->parent = n;
      c->row = n->children.size ();
      if (persist) { c->iter = it; c->has_iter = true; }
      n->children << c;
      if (gtk_tree_model_iter_has_child (model, &it)) { GtkTreeIter ci = it; build (c, &ci); }
    } while (gtk_tree_model_iter_next (model, &it));
  }
  static void renumber (MNode *parent, int from = 0)
  {
    for (int i = from; i < parent->children.size (); i++) parent->children[i]->row = i;
  }
  GtkTreePath *path_of (MNode *n) const
  {
    GtkTreePath *p = gtk_tree_path_new ();
    for (MNode *x = n; x && x->parent; x = x->parent) gtk_tree_path_prepend_index (p, x->row);
    return p;
  }
  MNode *node_at (GtkTreePath *p) const
  {
    if (!p) return nullptr;
    int depth = gtk_tree_path_get_depth (p);
    gint *idx = gtk_tree_path_get_indices (p);
    const MNode *n = &root;
    for (int k = 0; k < depth; k++) {
      if (idx[k] < 0 || idx[k] >= n->children.size ()) return nullptr;
      n = n->children[idx[k]];
    }
    return n == &root ? nullptr : const_cast<MNode *> (n);
  }
  /* the node of a model iter */
  MNode *node_of_iter (GtkTreeIter *it) const
  {
    if (!model || !it) return nullptr;
    GtkTreePath *p = gtk_tree_model_get_path (model, it);
    MNode *n = node_at (p);
    gtk_tree_path_free (p);
    return n;
  }
  bool iter_of (MNode *n, GtkTreeIter *it) const
  {
    if (!model || !n || n == &root) return false;
    if (n->has_iter) { *it = n->iter; return true; }
    GtkTreePath *p = path_of (n);
    bool ok = gtk_tree_model_get_iter (model, it, p);
    gtk_tree_path_free (p);
    return ok;
  }
  MNode *node (const QModelIndex &i) const { return i.isValid () ? static_cast<MNode *> (i.internalPointer ()) : const_cast<MNode *> (&root); }
  QModelIndex index_of (MNode *n, int col = 0) const
  {
    if (!n || n == &root) return QModelIndex ();
    return createIndex (n->row, col, n);
  }

  /* ---------- the model's signals ---------- */
  static MNode *parent_node (GtkQtItemModel *a, GtkTreePath *path)
  {
    if (gtk_tree_path_get_depth (path) <= 1) return &a->root;
    GtkTreePath *pp = gtk_tree_path_copy (path);
    gtk_tree_path_up (pp);
    MNode *n = a->node_at (pp);
    gtk_tree_path_free (pp);
    return n;
  }
  static void on_inserted (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
  {
    auto *a = static_cast<GtkQtItemModel *> (data);
    MNode *parent = parent_node (a, path);
    if (!parent) return;
    GtkTreeIter pit;
    bool has_parent = parent != &a->root && a->iter_of (parent, &pit);
    /* the parent's rows were read already (it was built after the insert) */
    if (parent->children.size () >= gtk_tree_model_iter_n_children (m, has_parent ? &pit : nullptr)) return;
    int pos = gtk_tree_path_get_indices (path)[gtk_tree_path_get_depth (path) - 1];
    if (pos < 0 || pos > parent->children.size ()) return;
    a->beginInsertRows (a->index_of (parent), pos, pos);
    auto *n = new MNode;
    n->parent = parent;
    if (a->persist) { n->iter = *iter; n->has_iter = true; }
    parent->children.insert (pos, n);
    renumber (parent, pos);
    if (gtk_tree_model_iter_has_child (m, iter)) { GtkTreeIter c = *iter; a->build (n, &c); }
    a->endInsertRows ();
  }
  static void on_deleted (GtkTreeModel *, GtkTreePath *path, gpointer data)
  {
    auto *a = static_cast<GtkQtItemModel *> (data);
    MNode *n = a->node_at (path);
    if (!n) return;
    MNode *parent = n->parent;
    int pos = n->row;
    a->beginRemoveRows (a->index_of (parent), pos, pos);
    parent->children.removeAt (pos);
    delete n;
    renumber (parent, pos);
    a->endRemoveRows ();
  }
  static void on_changed (GtkTreeModel *, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
  {
    auto *a = static_cast<GtkQtItemModel *> (data);
    MNode *n = a->node_at (path);
    if (!n) return;
    if (a->persist) { n->iter = *iter; n->has_iter = true; }
    Q_EMIT a->dataChanged (a->index_of (n, 0), a->index_of (n, a->columnCount (QModelIndex ()) - 1));
  }
  static void on_child_toggled (GtkTreeModel *m, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
  {
    auto *a = static_cast<GtkQtItemModel *> (data);
    MNode *n = a->node_at (path);
    if (!n) return;
    bool has = gtk_tree_model_iter_has_child (m, iter);
    if (has && n->children.isEmpty ()) {
      int count = gtk_tree_model_iter_n_children (m, iter);
      if (count <= 0) return;
      a->beginInsertRows (a->index_of (n), 0, count - 1);
      GtkTreeIter c = *iter;
      a->build (n, &c);
      a->endInsertRows ();
    } else if (!has && !n->children.isEmpty ()) {
      a->beginRemoveRows (a->index_of (n), 0, n->children.size () - 1);
      qDeleteAll (n->children);
      n->children.clear ();
      a->endRemoveRows ();
    }
  }
  static void on_reordered (GtkTreeModel *, GtkTreePath *path, GtkTreeIter *, gint *order, gpointer data)
  {
    auto *a = static_cast<GtkQtItemModel *> (data);
    MNode *n = gtk_tree_path_get_depth (path) == 0 ? &a->root : a->node_at (path);
    if (!n || !order) return;
    QList<MNode *> old = n->children, now;
    for (int i = 0; i < old.size (); i++) {
      if (order[i] < 0 || order[i] >= old.size ()) return;
      now << old[order[i]];
    }
    QList<QPersistentModelIndex> parents;
    if (n != &a->root) parents << QPersistentModelIndex (a->index_of (n));
    Q_EMIT a->layoutAboutToBeChanged (parents, QAbstractItemModel::VerticalSortHint);
    n->children = now;
    renumber (n);
    for (const QModelIndex &pi : a->persistentIndexList ()) {
      auto *x = static_cast<MNode *> (pi.internalPointer ());
      if (x && x->parent == n) a->changePersistentIndex (pi, a->createIndex (x->row, pi.column (), x));
    }
    Q_EMIT a->layoutChanged (parents, QAbstractItemModel::VerticalSortHint);
  }

  QModelIndex index (int r, int c, const QModelIndex &parent) const override
  {
    MNode *p = node (parent);
    if (r < 0 || r >= p->children.size () || c < 0 || c >= columnCount (parent)) return QModelIndex ();
    return createIndex (r, c, p->children[r]);
  }
  QModelIndex parent (const QModelIndex &i) const override
  {
    if (!i.isValid ()) return QModelIndex ();
    return index_of (node (i)->parent);
  }
  int rowCount (const QModelIndex &parent) const override
  {
    if (parent.column () > 0 || !model) return 0;
    return node (parent)->children.size ();
  }
  int columnCount (const QModelIndex &) const override { return qMax (1, (int) columns ().size ()); }

  /* GTK cell rendering protocol: attributes -> renderer properties -> data func */
  void prepare (GObject *column, Cell &c, GtkTreeIter *it) const
  {
    for (auto &a : c.attrs) {
      GParamSpec *ps = g_object_class_find_property (G_OBJECT_GET_CLASS (c.renderer), a.first.constData ());
      if (!ps || a.second < 0) continue;
      GValue mv = G_VALUE_INIT;
      gtk_tree_model_get_value (model, it, a.second, &mv);
      if (!G_IS_VALUE (&mv)) continue;
      GValue v = G_VALUE_INIT;
      g_value_init (&v, ps->value_type);
      if (g_value_type_transformable (G_VALUE_TYPE (&mv), ps->value_type)) {
        g_value_transform (&mv, &v);
        g_object_set_property (G_OBJECT (c.renderer), a.first.constData (), &v);
      }
      g_value_unset (&v);
      g_value_unset (&mv);
    }
    if (c.func) c.func ((GtkTreeViewColumn *) column, c.renderer, model, it, c.func_data);
  }
  /* the cells of column col prepared for row i */
  QList<Cell> *prepared (const QModelIndex &i, GObject **column = nullptr) const
  {
    QList<GObject *> cols = columns ();
    if (!model || i.column () >= cols.size ()) return nullptr;
    QList<Cell> *cells = cells_of (cols[i.column ()]);
    GtkTreeIter it;
    if (!cells || !iter_of (node (i), &it)) return nullptr;
    for (Cell &c : *cells) prepare (cols[i.column ()], c, &it);
    if (column) *column = cols[i.column ()];
    return cells;
  }

  QVariant data (const QModelIndex &i, int role) const override
  {
    if (!i.isValid ()) return QVariant ();
    QList<Cell> *cells = prepared (i);
    if (!cells) return QVariant ();
    QVariant out;
    QStringList texts;
    for (Cell &c : *cells) {
      RendererData *d = rd (c.renderer);
      if (!d->visible) continue;
      switch (d->kind) {
      case R_TOGGLE:
        if (role == Qt::CheckStateRole) out = d->active ? Qt::Checked : Qt::Unchecked;
        break;
      case R_PIXBUF:
        if (role == Qt::DecorationRole) {
          if (d->pixbuf) out = QPixmap::fromImage (gtkqt_pixbuf_image (d->pixbuf));
          else if (!d->icon_name.isEmpty ()) out = gtkqt_icon (d->icon_name.toUtf8 ().constData ());
        }
        break;
      case R_PROGRESS:
        if (role == Qt::UserRole + 1) out = d->value;
        if (role == Qt::DisplayRole) texts << (d->text.isEmpty () ? QStringLiteral ("%1 %").arg (d->value) : d->text);
        break;
      default:
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
          QString t = d->text;
          if (t.isEmpty () && !d->markup.isEmpty ())
            t = QTextDocumentFragment::fromHtml (gtkqt_markup_to_html (d->markup.toUtf8 ().constData ())).toPlainText ();
          texts << t;
        }
        if (role == Qt::ForegroundRole && !d->foreground.isEmpty ()) out = QBrush (QColor (d->foreground));
        if (role == Qt::BackgroundRole && !d->background.isEmpty ()) out = QBrush (QColor (d->background));
        if (role == Qt::FontRole && (d->weight >= 600 || d->markup.contains ("<b>"))) { QFont f; f.setBold (true); out = f; }
        if (role == Qt::TextAlignmentRole) out = int ((d->xalign > 0.75 ? Qt::AlignRight : d->xalign > 0.25 ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter);
        break;
      }
    }
    if ((role == Qt::DisplayRole || role == Qt::EditRole) && !texts.isEmpty ()) return texts.join (' ');
    return out;
  }

  Qt::ItemFlags flags (const QModelIndex &i) const override
  {
    if (!i.isValid ()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (QList<Cell> *cells = prepared (i))
        for (Cell &c : *cells) {
          if (rd (c.renderer)->kind == R_TOGGLE && rd (c.renderer)->activatable) f |= Qt::ItemIsUserCheckable;
          if (rd (c.renderer)->kind == R_TEXT && rd (c.renderer)->editable) f |= Qt::ItemIsEditable;
        }
    return f;
  }

  bool setData (const QModelIndex &i, const QVariant &v, int role) override
  {
    /* GTK never edits the model behind your back: it emits "edited"/"toggled"
     * on the renderer and the program updates the store. */
    QList<GObject *> cols = columns ();
    if (!i.isValid () || i.column () >= cols.size ()) return false;
    QList<Cell> *cells = cells_of (cols[i.column ()]);
    if (!cells) return false;
    GtkTreePath *p = path_of (node (i));
    gchar *ps = gtk_tree_path_to_string (p);
    gtk_tree_path_free (p);
    GtkQtEmitGuard guard;
    for (Cell &c : *cells) {
      if (role == Qt::CheckStateRole && rd (c.renderer)->kind == R_TOGGLE) g_signal_emit_by_name (c.renderer, "toggled", ps);
      if (role == Qt::EditRole && rd (c.renderer)->kind == R_TEXT)
        g_signal_emit_by_name (c.renderer, "edited", ps, v.toString ().toUtf8 ().constData ());
    }
    g_free (ps);
    return false;
  }

  QVariant headerData (int section, Qt::Orientation o, int role) const override
  {
    QList<GObject *> cols = columns ();
    if (o != Qt::Horizontal || section >= cols.size () || !GTK_IS_TREE_VIEW_COLUMN (cols[section])) return QVariant ();
    auto *c = (GtkTreeViewColumn *) cols[section];
    /* a header widget set with gtk_tree_view_column_set_widget: its image or text */
    QLabel *child = c->child ? qobject_cast<QLabel *> (gtkqt_qw (c->child)) : nullptr;
    switch (role) {
    case Qt::DisplayRole:
      if (c->child) return child && GTK_IS_LABEL (c->child) ? QVariant (child->text ()) : QVariant ();
      return cd (c)->title;
    case Qt::DecorationRole:
      if (child && GTK_IS_IMAGE (c->child)) {
#if QT_VERSION >= QT_VERSION_CHECK (6, 0, 0)
        QPixmap pm = child->pixmap ();
#else
        QPixmap pm = child->pixmap () ? *child->pixmap () : QPixmap ();
#endif
        if (!pm.isNull ()) return pm;
      }
      return QVariant ();
    case Qt::TextAlignmentRole:
      return int ((c->xalign >= 0.66 ? Qt::AlignRight : c->xalign >= 0.33 ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter);
    default:
      return QVariant ();
    }
  }

  void reset () { beginResetModel (); endResetModel (); }
  void detach ()
  {
    beginResetModel ();
    release ();
    qDeleteAll (root.children);
    root.children.clear ();
    endResetModel ();
  }
};

/* ======================================================================
 * Column API
 * ====================================================================== */
static void column_changed (GtkTreeViewColumn *col);

static void cells_add_attributes (QList<Cell> &cells, GtkCellRenderer *cell, va_list ap)
{
  for (Cell &c : cells) {
    if (c.renderer != cell) continue;
    for (const char *attr = va_arg (ap, const char *); attr; attr = va_arg (ap, const char *))
      c.attrs << qMakePair (QByteArray (attr), va_arg (ap, gint));
  }
}

static void cells_pack (QList<Cell> &cells, GtkCellRenderer *cell)
{
  g_object_ref_sink (cell);
  cells << Cell { cell, {} };
}

extern "C" {

GtkTreeViewColumn *gtk_tree_view_column_new (void) { return (GtkTreeViewColumn *) g_object_new (GTK_TYPE_TREE_VIEW_COLUMN, nullptr); }

GtkTreeViewColumn *gtk_tree_view_column_new_with_attributes (const gchar *title, GtkCellRenderer *cell, ...)
{
  GtkTreeViewColumn *c = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (c, title);
  cells_pack (cd (c)->cells, cell);
  va_list ap;
  va_start (ap, cell);
  cells_add_attributes (cd (c)->cells, cell, ap);
  va_end (ap);
  return c;
}

void gtk_tree_view_column_pack_start (GtkTreeViewColumn *c, GtkCellRenderer *cell, gboolean) { cells_pack (cd (c)->cells, cell); column_changed (c); }
void gtk_tree_view_column_pack_end (GtkTreeViewColumn *c, GtkCellRenderer *cell, gboolean) { cells_pack (cd (c)->cells, cell); column_changed (c); }

void gtk_tree_view_column_add_attribute (GtkTreeViewColumn *c, GtkCellRenderer *cell, const gchar *attribute, gint column)
{
  for (Cell &x : cd (c)->cells) if (x.renderer == cell) x.attrs << qMakePair (QByteArray (attribute), column);
  column_changed (c);
}

void gtk_tree_view_column_set_attributes (GtkTreeViewColumn *c, GtkCellRenderer *cell, ...)
{
  for (Cell &x : cd (c)->cells) if (x.renderer == cell) x.attrs.clear ();
  va_list ap;
  va_start (ap, cell);
  cells_add_attributes (cd (c)->cells, cell, ap);
  va_end (ap);
  column_changed (c);
}

void gtk_tree_view_column_set_cell_data_func (GtkTreeViewColumn *c, GtkCellRenderer *cell, GtkTreeCellDataFunc func,
                                              gpointer func_data, GDestroyNotify destroy)
{
  for (Cell &x : cd (c)->cells)
    if (x.renderer == cell) {
      if (x.func_destroy) x.func_destroy (x.func_data);
      x.func = func; x.func_data = func_data; x.func_destroy = destroy;
    }
  column_changed (c);
}

void gtk_tree_view_column_set_title (GtkTreeViewColumn *c, const gchar *title)
{
  cd (c)->title = gtkqt_str (title);
  g_free (c->title);
  c->title = g_strdup (title);
  column_changed (c);
}
const gchar *gtk_tree_view_column_get_title (GtkTreeViewColumn *c) { return c->title; }
void gtk_tree_view_column_set_sort_column_id (GtkTreeViewColumn *c, gint id)
{
  c->sort_column_id = id;
  if (id >= 0) c->clickable = TRUE;
  column_changed (c);
}
gint gtk_tree_view_column_get_sort_column_id (GtkTreeViewColumn *c) { return c->sort_column_id; }
void gtk_tree_view_column_set_resizable (GtkTreeViewColumn *c, gboolean v) { c->resizable = cd (c)->resizable = v; column_changed (c); }
gboolean gtk_tree_view_column_get_resizable (GtkTreeViewColumn *c) { return c->resizable; }
void gtk_tree_view_column_set_expand (GtkTreeViewColumn *c, gboolean v) { c->expand = v; column_changed (c); }
gboolean gtk_tree_view_column_get_expand (GtkTreeViewColumn *c) { return c->expand; }
void gtk_tree_view_column_set_fixed_width (GtkTreeViewColumn *c, gint w) { c->fixed_width = w; column_changed (c); }
gint gtk_tree_view_column_get_fixed_width (GtkTreeViewColumn *c) { return c->fixed_width; }
void gtk_tree_view_column_set_min_width (GtkTreeViewColumn *c, gint w) { c->min_width = w; column_changed (c); }
gint gtk_tree_view_column_get_min_width (GtkTreeViewColumn *c) { return c->min_width; }
void gtk_tree_view_column_set_max_width (GtkTreeViewColumn *c, gint w) { c->max_width = w; column_changed (c); }
gint gtk_tree_view_column_get_max_width (GtkTreeViewColumn *c) { return c->max_width; }
gint gtk_tree_view_column_get_width (GtkTreeViewColumn *c) { return c->width; }
void gtk_tree_view_column_set_sizing (GtkTreeViewColumn *c, GtkTreeViewColumnSizing type) { c->column_type = cd (c)->sizing = type; column_changed (c); }
GtkTreeViewColumnSizing gtk_tree_view_column_get_sizing (GtkTreeViewColumn *c) { return c->column_type; }
void gtk_tree_view_column_set_visible (GtkTreeViewColumn *c, gboolean v) { c->visible = v; column_changed (c); }
gboolean gtk_tree_view_column_get_visible (GtkTreeViewColumn *c) { return c->visible; }
void gtk_tree_view_column_set_clickable (GtkTreeViewColumn *c, gboolean v) { c->clickable = v; column_changed (c); }
gboolean gtk_tree_view_column_get_clickable (GtkTreeViewColumn *c) { return c->clickable; }
void gtk_tree_view_column_set_reorderable (GtkTreeViewColumn *c, gboolean v) { c->reorderable = v; }
gboolean gtk_tree_view_column_get_reorderable (GtkTreeViewColumn *c) { return c->reorderable; }
void gtk_tree_view_column_set_alignment (GtkTreeViewColumn *c, gfloat x) { c->xalign = x; column_changed (c); }
gfloat gtk_tree_view_column_get_alignment (GtkTreeViewColumn *c) { return c->xalign; }
void gtk_tree_view_column_set_spacing (GtkTreeViewColumn *c, gint spacing) { c->spacing = spacing; }
gint gtk_tree_view_column_get_spacing (GtkTreeViewColumn *c) { return c->spacing; }
void gtk_tree_view_column_set_sort_indicator (GtkTreeViewColumn *c, gboolean v) { c->show_sort_indicator = v; column_changed (c); }
gboolean gtk_tree_view_column_get_sort_indicator (GtkTreeViewColumn *c) { return c->show_sort_indicator; }
void gtk_tree_view_column_set_sort_order (GtkTreeViewColumn *c, GtkSortType order) { c->sort_order = order; column_changed (c); }
GtkSortType gtk_tree_view_column_get_sort_order (GtkTreeViewColumn *c) { return c->sort_order; }
GtkWidget *gtk_tree_view_column_get_button (GtkTreeViewColumn *c) { return c->button; }
GtkWidget *gtk_tree_view_column_get_tree_view (GtkTreeViewColumn *c) { return c->tree_view; }
GtkWidget *gtk_tree_view_column_get_widget (GtkTreeViewColumn *c) { return c->child; }

/* The header shows the widget's image or text (a QHeaderView section holds no widgets) */
void gtk_tree_view_column_set_widget (GtkTreeViewColumn *c, GtkWidget *widget)
{
  if (widget) g_object_ref_sink (widget);
  if (c->child) g_object_unref (c->child);
  c->child = widget;
  column_changed (c);
}

void gtk_tree_view_column_clear (GtkTreeViewColumn *c) { cells_clear (cd (c)->cells); column_changed (c); }

GList *gtk_tree_view_column_get_cell_renderers (GtkTreeViewColumn *c)
{
  GList *l = nullptr;
  for (const Cell &x : cd (c)->cells) l = g_list_append (l, x.renderer);
  return l;
}

void gtk_tree_view_column_clicked (GtkTreeViewColumn *c) { if (c->clickable) g_signal_emit_by_name (c, "clicked"); }
void gtk_tree_view_column_queue_resize (GtkTreeViewColumn *c) { column_changed (c); }

} /* extern "C" */

/* ======================================================================
 * GtkTreeView
 * ====================================================================== */
struct ViewData {
  GtkTreeModel *model = nullptr;
  QPointer<GtkQtItemModel> adapter;
  QList<GtkTreeViewColumn *> columns;
  GtkTreeSelection *selection = nullptr;
  bool headers_clickable = false;
  GdkWindow *bin_window = nullptr;          /* the viewport, where rows are */
  GtkTreePath *drag_dest = nullptr;
  GtkTreeViewDropPosition drag_pos = GTK_TREE_VIEW_DROP_BEFORE;
  GtkTreeViewGridLines grid_lines = GTK_TREE_VIEW_GRID_LINES_NONE;
  GtkTreeViewColumnDropFunc column_drop = nullptr;
  gpointer column_drop_data = nullptr;
  GDestroyNotify column_drop_destroy = nullptr;
  GtkTreeViewColumn *expander_column = nullptr;
  ~ViewData ()
  {
    if (drag_dest) gtk_tree_path_free (drag_dest);
    if (column_drop_destroy) column_drop_destroy (column_drop_data);
  }
};
static ViewData *vd (GtkWidget *w) { return static_cast<ViewData *> (g_object_get_data (G_OBJECT (w), "gtkqt-view")); }

/* Progress cells need painting */
class GtkQtCellDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint (QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &i) const override
  {
    paint_cell (p, opt, i);
    paint_marks (p, opt, i);
  }
  /* GTK draws grid lines and the drop position of a drag over the rows */
  void paint_marks (QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &i) const
  {
    auto *w = opt.widget ? (GtkWidget *) opt.widget->property ("_gtkqt_widget").value<void *> () : nullptr;
    ViewData *d = w ? vd (w) : nullptr;
    if (!d) return;
    p->save ();
    if (d->grid_lines & GTK_TREE_VIEW_GRID_LINES_HORIZONTAL) {
      p->setPen (opt.palette.color (QPalette::Midlight));
      p->drawLine (opt.rect.bottomLeft (), opt.rect.bottomRight ());
    }
    if (d->grid_lines & GTK_TREE_VIEW_GRID_LINES_VERTICAL) {
      p->setPen (opt.palette.color (QPalette::Midlight));
      p->drawLine (opt.rect.topRight (), opt.rect.bottomRight ());
    }
    if (d->drag_dest && d->adapter && d->adapter->node_at (d->drag_dest) == static_cast<MNode *> (i.internalPointer ())) {
      p->setPen (QPen (opt.palette.color (QPalette::Text), 2));
      QRect r = opt.rect.adjusted (0, 1, 0, -1);
      switch (d->drag_pos) {
      case GTK_TREE_VIEW_DROP_BEFORE: p->drawLine (r.topLeft (), r.topRight ()); break;
      case GTK_TREE_VIEW_DROP_AFTER: p->drawLine (r.bottomLeft (), r.bottomRight ()); break;
      default:
        p->drawLine (r.topLeft (), r.topRight ());
        p->drawLine (r.bottomLeft (), r.bottomRight ());
        if (i.column () == 0) p->drawLine (r.topLeft (), r.bottomLeft ());
        if (i.column () == i.model ()->columnCount (i.parent ()) - 1) p->drawLine (r.topRight (), r.bottomRight ());
      }
    }
    p->restore ();
  }
  void paint_cell (QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &i) const
  {
    QVariant v = i.data (Qt::UserRole + 1);
    if (!v.isValid ()) { QStyledItemDelegate::paint (p, opt, i); return; }
    QStyleOptionProgressBar bar;
    bar.initFrom (opt.widget);
    bar.state = opt.state | QStyle::State_Horizontal;
    bar.rect = opt.rect.adjusted (2, 2, -2, -2);
    bar.minimum = 0;
    bar.maximum = 100;
    bar.progress = v.toInt ();
    bar.text = i.data (Qt::DisplayRole).toString ();
    bar.textVisible = true;
    QApplication::style ()->drawControl (QStyle::CE_ProgressBar, &bar, p);
  }
};

static bool applying_columns;

static void view_apply_columns (GtkWidget *w)
{
  auto *tv = gtkqt_q<QTreeView> (w);
  ViewData *d = vd (w);
  if (!tv || !d) return;
  applying_columns = true;
  if (d->adapter) d->adapter->reset ();
  QHeaderView *h = tv->header ();
  bool any_expand = false;
  /* before a model is set the header has no sections to configure */
  int n = qMin ((int) d->columns.size (), h->count ());
  for (int i = 0; i < n; i++) {
    GtkTreeViewColumn *c = d->columns[i];
    tv->setColumnHidden (i, !c->visible);
    if (c->fixed_width > 0 && (c->column_type == GTK_TREE_VIEW_COLUMN_FIXED || h->sectionSize (i) == h->defaultSectionSize ()))
      tv->setColumnWidth (i, c->fixed_width);
    if (c->max_width == 0) h->resizeSection (i, 0);
    if (c->expand) { h->setSectionResizeMode (i, QHeaderView::Stretch); any_expand = true; }
    else h->setSectionResizeMode (i, cd (c)->resizable || c->column_type == GTK_TREE_VIEW_COLUMN_FIXED
                                       ? QHeaderView::Interactive : QHeaderView::ResizeToContents);
  }
  h->setStretchLastSection (!any_expand);
  bool clickable = d->headers_clickable;
  for (GtkTreeViewColumn *c : d->columns) clickable = clickable || c->clickable;
  h->setSectionsClickable (clickable);
  /* columns with a sort column id follow the model's sorting, as GTK's do;
   * the others show what the program set (one indicator: QHeaderView has one) */
  gint col = -1;
  GtkSortType order = GTK_SORT_ASCENDING;
  bool sorted = d->model && gtk_tree_sortable_get_sort_column_id ((GtkTreeSortable *) d->model, &col, &order);
  int shown = -1;
  for (int i = 0; i < n; i++) {
    GtkTreeViewColumn *c = d->columns[i];
    if (c->sort_column_id >= 0 && d->model) {
      c->show_sort_indicator = sorted && c->sort_column_id == col;
      if (c->show_sort_indicator) c->sort_order = order;
    }
    if (c->show_sort_indicator && shown < 0) shown = i;
  }
  h->setSortIndicatorShown (shown >= 0);
  if (shown >= 0)
    h->setSortIndicator (shown, d->columns[shown]->sort_order == GTK_SORT_ASCENDING ? Qt::AscendingOrder : Qt::DescendingOrder);
  for (int i = 0; i < n; i++) {
    GtkTreeViewColumn *c = d->columns[i];
    if (c->min_width > 0 && tv->columnWidth (i) < c->min_width) tv->setColumnWidth (i, c->min_width);
    c->width = c->visible ? tv->columnWidth (i) : 0;
  }
  applying_columns = false;
}

static void column_changed (GtkTreeViewColumn *col)
{
  if (cd (col)->view) view_apply_columns (cd (col)->view);
}

/* GTK's own "clicked" handler: sort by the column's sort column id */
static void column_sort_clicked (GtkTreeViewColumn *c)
{
  GtkWidget *vw = cd (c)->view;
  ViewData *d = vw ? vd (vw) : nullptr;
  if (!d || !d->model || c->sort_column_id < 0) return;
  gint cur;
  GtkSortType order;
  bool sorted = gtk_tree_sortable_get_sort_column_id ((GtkTreeSortable *) d->model, &cur, &order);
  order = sorted && cur == c->sort_column_id && order == GTK_SORT_ASCENDING ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
  gtk_tree_sortable_set_sort_column_id ((GtkTreeSortable *) d->model, c->sort_column_id, order);
  view_apply_columns (vw);
}

/* the header button's allocation follows its section */
static void column_allocate (GtkWidget *vw, int section)
{
  auto *tv = gtkqt_q<QTreeView> (vw);
  ViewData *d = vd (vw);
  if (!tv || !d || section < 0 || section >= d->columns.size ()) return;
  GtkTreeViewColumn *c = d->columns[section];
  QHeaderView *h = tv->header ();
  c->width = h->isSectionHidden (section) ? 0 : h->sectionSize (section);
  /* as in GTK, allocations come once the view is on screen */
  if (!c->button || !tv->isVisible ()) return;
  GtkAllocation a = { h->sectionViewportPosition (section), 0, c->width, h->height () };
  c->button->allocation = a;
  g_signal_emit_by_name (c->button, "size-allocate", &a);
}

static QObject *tree_view_create (GtkWidget *w)
{
  auto *tv = new QTreeView;
  tv->setItemDelegate (new GtkQtCellDelegate (tv));
  tv->setUniformRowHeights (true);
  tv->setAllColumnsShowFocus (true);
  tv->setEditTriggers (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
  /* tree coordinates are pixels, as in GTK */
  tv->setVerticalScrollMode (QAbstractItemView::ScrollPerPixel);
  tv->setHorizontalScrollMode (QAbstractItemView::ScrollPerPixel);
  auto *d = new ViewData;
  g_object_set_data_full (G_OBJECT (w), "gtkqt-view", d, [] (gpointer p) { delete static_cast<ViewData *> (p); });
  /* the rows' window: button presses there reach the view in its coordinates */
  d->bin_window = gtkqt_drawable_new (GTKQT_DRAWABLE_WINDOW, w);
  d->bin_window->qw = tv->viewport ();
  tv->viewport ()->setProperty ("_gtkqt_gdkwindow", QVariant::fromValue ((void *) d->bin_window));
  tv->viewport ()->installEventFilter (gtkqt_bridge (w));
  g_signal_connect (w, "map", G_CALLBACK (+[] (GtkWidget *vw) {
    if (ViewData *vdd = vd (vw)) for (int i = 0; i < vdd->columns.size (); i++) column_allocate (vw, i);
  }), nullptr);
  for (bool expanded : { true, false })
    gtkqt_connect (w, tv, expanded ? &QTreeView::expanded : &QTreeView::collapsed, [expanded] (GtkWidget *vw, const QModelIndex &i) {
      ViewData *vdd = vd (vw);
      if (!vdd->model || !vdd->adapter) return;
      MNode *r = vdd->adapter->node (i);
      GtkTreeIter it;
      if (!vdd->adapter->iter_of (r, &it)) return;
      GtkTreePath *p = vdd->adapter->path_of (r);
      g_signal_emit_by_name (vw, expanded ? "row-expanded" : "row-collapsed", &it, p);
      gtk_tree_path_free (p);
    });
  gtkqt_connect (w, tv, &QTreeView::activated, [] (GtkWidget *vw, const QModelIndex &i) {
    ViewData *d = vd (vw);
    if (!d->adapter) return;
    GtkTreePath *p = d->adapter->path_of (d->adapter->node (i));
    g_signal_emit_by_name (vw, "row-activated", p, i.column () < d->columns.size () ? d->columns[i.column ()] : nullptr);
    gtk_tree_path_free (p);
  });
  gtkqt_connect (w, tv->header (), &QHeaderView::sectionClicked, [] (GtkWidget *vw, int section) {
    ViewData *d = vd (vw);
    if (section >= d->columns.size ()) return;
    GtkTreeViewColumn *c = d->columns[section];
    if (c->button) g_signal_emit_by_name (c->button, "clicked");
  });
  gtkqt_connect (w, tv->header (), &QHeaderView::sectionResized, [] (GtkWidget *vw, int section, int, int size) {
    ViewData *d = vd (vw);
    if (section >= d->columns.size ()) return;
    GtkTreeViewColumn *c = d->columns[section];
    if (c->max_width >= 0 && size > c->max_width && !applying_columns) {
      gtkqt_q<QTreeView> (vw)->header ()->resizeSection (section, c->max_width);
      return;
    }
    column_allocate (vw, section);
  });
  return tv;
}

static void tree_view_dispose (GObject *o);
static gboolean tree_view_move_cursor (GtkTreeView *view, gint step, gint count);

G_DEFINE_TYPE (GtkTreeView, gtk_tree_view, GTK_TYPE_CONTAINER)
static void gtk_tree_view_init (GtkTreeView *) {}
static void gtk_tree_view_class_init (GtkTreeViewClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = tree_view_dispose;
  gtkqt_set_create (GTK_WIDGET_CLASS (klass), tree_view_create);
  g_signal_new ("row-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 2,
                G_TYPE_POINTER, GTK_TYPE_TREE_VIEW_COLUMN);
  g_signal_new ("cursor-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  g_signal_new_class_handler ("move-cursor", G_TYPE_FROM_CLASS (klass), (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
                              G_CALLBACK (tree_view_move_cursor), nullptr, nullptr, nullptr, G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_INT);
  g_signal_new ("columns-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  g_signal_new ("row-expanded", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 2,
                G_TYPE_POINTER, G_TYPE_POINTER);
  g_signal_new ("row-collapsed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 2,
                G_TYPE_POINTER, G_TYPE_POINTER);
  gtkqt_prop (klass, g_param_spec_object ("model", nullptr, nullptr, gtk_tree_model_get_type (), RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_set_model (GTK_TREE_VIEW (o), (GtkTreeModel *) g_value_get_object (v)); },
              [] (GObject *o, GValue *v) { g_value_set_object (v, gtk_tree_view_get_model (GTK_TREE_VIEW (o))); });
  /* GTK 2: alternating row colours */
  gtkqt_prop (klass, g_param_spec_boolean ("rules-hint", nullptr, nullptr, FALSE, RW),
              [] (GObject *o, const GValue *v) {
                if (auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (o))) tv->setAlternatingRowColors (g_value_get_boolean (v));
              },
              [] (GObject *o, GValue *v) {
                auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (o));
                g_value_set_boolean (v, tv && tv->alternatingRowColors ());
              });
  gtkqt_prop (klass, g_param_spec_boolean ("headers-visible", nullptr, nullptr, TRUE, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (o), g_value_get_boolean (v)); }, nullptr);
  gtkqt_prop (klass, g_param_spec_boolean ("reorderable", nullptr, nullptr, FALSE, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_set_reorderable (GTK_TREE_VIEW (o), g_value_get_boolean (v)); }, nullptr);
  gtkqt_prop (klass, g_param_spec_boolean ("fixed-height-mode", nullptr, nullptr, FALSE, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (o), g_value_get_boolean (v)); }, nullptr);
  gtkqt_prop (klass, g_param_spec_boolean ("enable-search", nullptr, nullptr, TRUE, RW), [] (GObject *, const GValue *) {}, nullptr);
  gtkqt_prop (klass, g_param_spec_int ("search-column", nullptr, nullptr, -1, G_MAXINT, -1, RW), [] (GObject *, const GValue *) {}, nullptr);
}

/* GtkTreeSelection */
struct _GtkTreeSelection {
  GObject parent_instance;
  GtkTreeView *view;
  GtkTreeSelectionFunc func;
  gpointer func_data;
  GDestroyNotify func_destroy;
};
typedef struct { GObjectClass parent_class; } GtkTreeSelectionClass;
G_DEFINE_TYPE (GtkTreeSelection, gtk_tree_selection, G_TYPE_OBJECT)
static void gtk_tree_selection_init (GtkTreeSelection *) {}
static void selection_finalize (GObject *o)
{
  auto *s = (GtkTreeSelection *) o;
  if (s->func_destroy) s->func_destroy (s->func_data);
  G_OBJECT_CLASS (gtk_tree_selection_parent_class)->finalize (o);
}
static void gtk_tree_selection_class_init (GtkTreeSelectionClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = selection_finalize;
  g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  gtkqt_prop (klass, g_param_spec_enum ("mode", nullptr, nullptr, gtk_selection_mode_get_type (), GTK_SELECTION_SINGLE, RW),
              [] (GObject *o, const GValue *v) { gtk_tree_selection_set_mode ((GtkTreeSelection *) o, (GtkSelectionMode) g_value_get_enum (v)); }, nullptr);
}

static void tree_view_dispose (GObject *o)
{
  GtkWidget *w = GTK_WIDGET (o);
  if (ViewData *d = vd (w)) {
    if (auto *tv = gtkqt_q<QTreeView> (w)) tv->setModel (nullptr);
    if (d->adapter) { d->adapter->detach (); delete d->adapter.data (); }
    if (d->model) { g_object_unref (d->model); d->model = nullptr; }
    for (GtkTreeViewColumn *c : d->columns) { cd (c)->view = nullptr; c->tree_view = nullptr; g_object_unref (c); }
    d->columns.clear ();
    if (d->selection) { d->selection->view = nullptr; g_object_unref (d->selection); d->selection = nullptr; }
    if (d->bin_window) { d->bin_window->qw = nullptr; d->bin_window->widget = nullptr; g_object_unref (d->bin_window); d->bin_window = nullptr; }
  }
  G_OBJECT_CLASS (gtk_tree_view_parent_class)->dispose (o);
}

static QItemSelectionModel *sel_model (GtkTreeSelection *s)
{
  auto *tv = s->view ? gtkqt_q<QTreeView> (GTK_WIDGET (s->view)) : nullptr;
  return tv ? tv->selectionModel () : nullptr;
}

/* Rows change selection only when the selection's select function agrees */
class GtkQtSelectionModel : public QItemSelectionModel {
public:
  GtkWidget *view;
  GtkQtSelectionModel (QAbstractItemModel *m, GtkWidget *v, QObject *parent) : QItemSelectionModel (m, parent), view (v) {}
  using QItemSelectionModel::select;
  void select (const QItemSelection &sel, QItemSelectionModel::SelectionFlags f) override
  {
    ViewData *d = vd (view);
    GtkTreeSelection *s = d ? d->selection : nullptr;
    if (!s || !s->func || !d->adapter || !d->model || f == QItemSelectionModel::NoUpdate) { QItemSelectionModel::select (sel, f); return; }
    QList<MNode *> cur;
    for (const QModelIndex &i : selectedRows ()) cur << d->adapter->node (i);
    QList<MNode *> want = (f & QItemSelectionModel::Clear) ? QList<MNode *> () : cur;
    for (const QModelIndex &i : sel.indexes ()) {
      if (i.column () != 0 && sel.contains (i.siblingAtColumn (0))) continue;
      MNode *r = d->adapter->node (i);
      bool in = want.contains (r);
      if ((f & QItemSelectionModel::Select) && !in) want << r;
      else if ((f & QItemSelectionModel::Deselect) && in) want.removeAll (r);
      else if (f & QItemSelectionModel::Toggle) { if (in) want.removeAll (r); else want << r; }
    }
    /* ask about each row whose state would change */
    auto ask = [&] (MNode *r, bool selected) {
      GtkTreePath *p = d->adapter->path_of (r);
      gboolean ok = s->func (s, d->model, p, selected, s->func_data);
      gtk_tree_path_free (p);
      return ok;
    };
    for (MNode *r : QList<MNode *> (want)) if (!cur.contains (r) && !ask (r, false)) want.removeAll (r);
    for (MNode *r : cur) if (!want.contains (r) && !ask (r, true)) want << r;
    if (!d->adapter) return;
    QItemSelection out;
    int last = d->adapter->columnCount (QModelIndex ()) - 1;
    for (MNode *r : want) out.select (d->adapter->index_of (r, 0), d->adapter->index_of (r, last));
    QItemSelectionModel::select (out, QItemSelectionModel::ClearAndSelect | (f & QItemSelectionModel::Current));
  }
};

static void connect_selection (GtkWidget *w)
{
  auto *tv = gtkqt_q<QTreeView> (w);
  ViewData *d = vd (w);
  if (!tv || !tv->selectionModel () || !d->selection) return;
  gtkqt_connect (w, tv->selectionModel (), &QItemSelectionModel::selectionChanged,
                 [] (GtkWidget *vw, const QItemSelection &, const QItemSelection &) { g_signal_emit_by_name (vd (vw)->selection, "changed"); });
  gtkqt_connect (w, tv->selectionModel (), &QItemSelectionModel::currentChanged,
                 [] (GtkWidget *vw, const QModelIndex &, const QModelIndex &) { g_signal_emit_by_name (vw, "cursor-changed"); });
}

extern "C" {

GtkWidget *gtk_tree_view_new (void) { return (GtkWidget *) g_object_new (GTK_TYPE_TREE_VIEW, nullptr); }

GtkWidget *gtk_tree_view_new_with_model (GtkTreeModel *model)
{
  GtkWidget *w = gtk_tree_view_new ();
  gtk_tree_view_set_model (GTK_TREE_VIEW (w), model);
  return w;
}

void gtk_tree_view_set_model (GtkTreeView *view, GtkTreeModel *model)
{
  GtkWidget *w = GTK_WIDGET (view);
  auto *tv = gtkqt_q<QTreeView> (w);
  ViewData *d = vd (w);
  if (!tv || !d || d->model == model) return;
  tv->setModel (nullptr);
  if (d->adapter) delete d->adapter.data ();
  if (d->model) g_object_unref (d->model);
  d->model = model;
  if (!model) return;
  g_object_ref (model);
  auto *a = new GtkQtItemModel (model);
  a->setParent (tv);
  a->columns = [w] { QList<GObject *> l; if (ViewData *vdd = vd (w)) for (auto *c : vdd->columns) l << G_OBJECT (c); return l; };
  a->cells_of = [] (GObject *c) -> QList<Cell> * { return &cd (c)->cells; };
  d->adapter = a;
  tv->setModel (a);
  QItemSelectionModel *old = tv->selectionModel ();
  tv->setSelectionModel (new GtkQtSelectionModel (a, w, a));
  delete old;
  tv->setRootIsDecorated (!(gtk_tree_model_get_flags (model) & GTK_TREE_MODEL_LIST_ONLY));
  gtk_tree_view_get_selection (view);
  connect_selection (w);
  view_apply_columns (w);
}

GtkTreeModel *gtk_tree_view_get_model (GtkTreeView *view) { ViewData *d = vd (GTK_WIDGET (view)); return d ? d->model : nullptr; }

gint gtk_tree_view_insert_column (GtkTreeView *view, GtkTreeViewColumn *column, gint position)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d) return 0;
  g_object_ref_sink (column);
  cd (column)->view = column->tree_view = GTK_WIDGET (view);
  if (position < 0 || position > d->columns.size ()) position = d->columns.size ();
  d->columns.insert (position, column);
  view_apply_columns (GTK_WIDGET (view));
  return d->columns.size ();
}

gint gtk_tree_view_append_column (GtkTreeView *view, GtkTreeViewColumn *column) { return gtk_tree_view_insert_column (view, column, -1); }

gint gtk_tree_view_insert_column_with_attributes (GtkTreeView *view, gint position, const gchar *title, GtkCellRenderer *cell, ...)
{
  GtkTreeViewColumn *c = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (c, title);
  cells_pack (cd (c)->cells, cell);
  va_list ap;
  va_start (ap, cell);
  cells_add_attributes (cd (c)->cells, cell, ap);
  va_end (ap);
  return gtk_tree_view_insert_column (view, c, position);
}

gint gtk_tree_view_remove_column (GtkTreeView *view, GtkTreeViewColumn *column)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (d && d->columns.removeOne (column)) {
    cd (column)->view = nullptr; column->tree_view = nullptr;
    view_apply_columns (GTK_WIDGET (view));
    g_object_unref (column);
  }
  return d ? d->columns.size () : 0;
}

GtkTreeViewColumn *gtk_tree_view_get_column (GtkTreeView *view, gint n)
{
  ViewData *d = vd (GTK_WIDGET (view));
  return d && n >= 0 && n < d->columns.size () ? d->columns[n] : nullptr;
}

guint gtk_tree_view_get_n_columns (GtkTreeView *view) { ViewData *d = vd (GTK_WIDGET (view)); return d ? d->columns.size () : 0; }

GtkTreeSelection *gtk_tree_view_get_selection (GtkTreeView *view)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d) return nullptr;
  if (!d->selection) {
    d->selection = (GtkTreeSelection *) g_object_new (gtk_tree_selection_get_type (), nullptr);
    d->selection->view = view;
    connect_selection (GTK_WIDGET (view));
  }
  return d->selection;
}

void gtk_tree_view_set_headers_visible (GtkTreeView *view, gboolean v) { if (auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view))) tv->setHeaderHidden (!v); }
void gtk_tree_view_set_headers_clickable (GtkTreeView *view, gboolean v) { if (ViewData *d = vd (GTK_WIDGET (view))) { d->headers_clickable = v; view_apply_columns (GTK_WIDGET (view)); } }
void gtk_tree_view_set_rules_hint (GtkTreeView *view, gboolean v) { if (auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view))) tv->setAlternatingRowColors (v); }
void gtk_tree_view_set_reorderable (GtkTreeView *, gboolean) {}
void gtk_tree_view_set_enable_search (GtkTreeView *, gboolean) {}
void gtk_tree_view_set_search_column (GtkTreeView *, gint) {}
void gtk_tree_view_expand_all (GtkTreeView *view) { if (auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view))) tv->expandAll (); }
void gtk_tree_view_collapse_all (GtkTreeView *view) { if (auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view))) tv->collapseAll (); }

static QModelIndex view_index (GtkTreeView *view, GtkTreePath *path, int col = 0)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d || !d->adapter || !path) return QModelIndex ();
  return d->adapter->index_of (d->adapter->node_at (path), col);
}

gboolean gtk_tree_view_expand_row (GtkTreeView *view, GtkTreePath *path, gboolean open_all)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  QModelIndex i = view_index (view, path);
  if (!tv || !i.isValid ()) return FALSE;
  if (open_all) tv->expandRecursively (i); else tv->expand (i);
  return TRUE;
}

void gtk_tree_view_set_cursor (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *focus_column, gboolean start_editing)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  ViewData *d = vd (GTK_WIDGET (view));
  int col = focus_column && d ? qMax (0, (int) d->columns.indexOf (focus_column)) : 0;
  QModelIndex i = view_index (view, path, col);
  if (!tv || !i.isValid ()) return;
  tv->setCurrentIndex (i);
  if (start_editing) tv->edit (i);
}

void gtk_tree_view_get_cursor (GtkTreeView *view, GtkTreePath **path, GtkTreeViewColumn **focus_column)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  QModelIndex i = tv ? tv->currentIndex () : QModelIndex ();
  ViewData *d = vd (GTK_WIDGET (view));
  if (path) *path = i.isValid () && d && d->adapter ? d->adapter->path_of (d->adapter->node (i)) : nullptr;
  if (focus_column) *focus_column = i.isValid () ? gtk_tree_view_get_column (view, i.column ()) : nullptr;
}

void gtk_tree_view_scroll_to_cell (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *, gboolean use_align, gfloat row_align, gfloat)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  QModelIndex i = view_index (view, path);
  if (tv && i.isValid ())
    tv->scrollTo (i, !use_align ? QAbstractItemView::EnsureVisible : row_align < 0.25 ? QAbstractItemView::PositionAtTop
                     : row_align > 0.75 ? QAbstractItemView::PositionAtBottom : QAbstractItemView::PositionAtCenter);
}

void gtk_tree_view_row_activated (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column)
{
  g_signal_emit_by_name (view, "row-activated", path, column);
}

void gtk_tree_view_columns_autosize (GtkTreeView *view)
{
  if (auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view)))
    for (int i = 0; i < tv->header ()->count (); i++) tv->resizeColumnToContents (i);
}

/* ---------- GtkTreeSelection ---------- */
void gtk_tree_selection_set_mode (GtkTreeSelection *s, GtkSelectionMode type)
{
  auto *tv = s->view ? gtkqt_q<QTreeView> (GTK_WIDGET (s->view)) : nullptr;
  if (!tv) return;
  static const QAbstractItemView::SelectionMode map[] = { QAbstractItemView::NoSelection, QAbstractItemView::SingleSelection,
                                                          QAbstractItemView::SingleSelection, QAbstractItemView::ExtendedSelection };
  tv->setSelectionMode (map[type & 3]);
  g_object_set_data (G_OBJECT (s), "gtkqt-mode", GINT_TO_POINTER (type));
}

GtkSelectionMode gtk_tree_selection_get_mode (GtkTreeSelection *s)
{
  gpointer p = g_object_get_data (G_OBJECT (s), "gtkqt-mode");
  return p ? (GtkSelectionMode) GPOINTER_TO_INT (p) : GTK_SELECTION_SINGLE;
}

static GtkQtItemModel *sel_adapter (GtkTreeSelection *s)
{
  ViewData *d = s->view ? vd (GTK_WIDGET (s->view)) : nullptr;
  return d ? d->adapter.data () : nullptr;
}

static QList<MNode *> selected_rows (GtkTreeSelection *s)
{
  QList<MNode *> rows;
  if (QItemSelectionModel *sm = sel_model (s))
    for (const QModelIndex &i : sm->selectedRows ()) rows << static_cast<MNode *> (i.internalPointer ());
  return rows;
}

gboolean gtk_tree_selection_get_selected (GtkTreeSelection *s, GtkTreeModel **model, GtkTreeIter *iter)
{
  GtkTreeModel *m = s->view ? gtk_tree_view_get_model (s->view) : nullptr;
  GtkQtItemModel *a = sel_adapter (s);
  if (model) *model = m;
  QList<MNode *> rows = selected_rows (s);
  GtkTreeIter it;
  if (!m || !a || rows.isEmpty () || !a->iter_of (rows.first (), &it)) { if (iter) memset (iter, 0, sizeof (*iter)); return FALSE; }
  if (iter) *iter = it;
  return TRUE;
}

GList *gtk_tree_selection_get_selected_rows (GtkTreeSelection *s, GtkTreeModel **model)
{
  if (model) *model = s->view ? gtk_tree_view_get_model (s->view) : nullptr;
  GtkQtItemModel *a = sel_adapter (s);
  GList *l = nullptr;
  if (a) for (MNode *r : selected_rows (s)) l = g_list_append (l, a->path_of (r));
  return l;
}

gint gtk_tree_selection_count_selected_rows (GtkTreeSelection *s) { return selected_rows (s).size (); }

void gtk_tree_selection_selected_foreach (GtkTreeSelection *s, GtkTreeSelectionForeachFunc func, gpointer data)
{
  GtkTreeModel *m = s->view ? gtk_tree_view_get_model (s->view) : nullptr;
  GtkQtItemModel *a = sel_adapter (s);
  if (!m || !a) return;
  /* paths first: the function may change the model */
  QList<GtkTreePath *> paths;
  for (MNode *r : selected_rows (s)) paths << a->path_of (r);
  for (GtkTreePath *p : paths) {
    GtkTreeIter it;
    if (gtk_tree_model_get_iter (m, &it, p)) func (m, p, &it, data);
    gtk_tree_path_free (p);
  }
}

static void select_row (GtkTreeSelection *s, MNode *r, QItemSelectionModel::SelectionFlags f)
{
  GtkQtItemModel *a = sel_adapter (s);
  QItemSelectionModel *sm = sel_model (s);
  if (!a || !sm || !r) return;
  if (gtk_tree_selection_get_mode (s) != GTK_SELECTION_MULTIPLE && (f & QItemSelectionModel::Select)) f |= QItemSelectionModel::Clear;
  sm->select (a->index_of (r), f | QItemSelectionModel::Rows);
  if (f & QItemSelectionModel::Select) sm->setCurrentIndex (a->index_of (r), QItemSelectionModel::NoUpdate);
}

void gtk_tree_selection_select_iter (GtkTreeSelection *s, GtkTreeIter *iter)
{
  if (GtkQtItemModel *a = sel_adapter (s)) select_row (s, a->node_of_iter (iter), QItemSelectionModel::Select);
}

void gtk_tree_selection_unselect_iter (GtkTreeSelection *s, GtkTreeIter *iter)
{
  if (GtkQtItemModel *a = sel_adapter (s)) select_row (s, a->node_of_iter (iter), QItemSelectionModel::Deselect);
}

void gtk_tree_selection_select_path (GtkTreeSelection *s, GtkTreePath *path)
{
  if (GtkQtItemModel *a = sel_adapter (s)) select_row (s, a->node_at (path), QItemSelectionModel::Select);
}

void gtk_tree_selection_select_all (GtkTreeSelection *s)
{
  if (auto *tv = s->view ? gtkqt_q<QTreeView> (GTK_WIDGET (s->view)) : nullptr) tv->selectAll ();
}

void gtk_tree_selection_unselect_all (GtkTreeSelection *s) { if (QItemSelectionModel *sm = sel_model (s)) sm->clearSelection (); }

gboolean gtk_tree_selection_iter_is_selected (GtkTreeSelection *s, GtkTreeIter *iter)
{
  GtkQtItemModel *a = sel_adapter (s);
  return a && selected_rows (s).contains (a->node_of_iter (iter));
}

GtkTreeView *gtk_tree_selection_get_tree_view (GtkTreeSelection *s) { return s->view; }

} /* extern "C" */

/* ======================================================================
 * GtkCellLayout + combo boxes with a model
 * ====================================================================== */
static QList<Cell> *layout_cells (GtkCellLayout *l)
{
  if (GTK_IS_TREE_VIEW_COLUMN (l)) return &cd (l)->cells;
  auto *cells = static_cast<QList<Cell> *> (g_object_get_data (G_OBJECT (l), "gtkqt-cells"));
  if (!cells) {
    cells = new QList<Cell>;
    g_object_set_data_full (G_OBJECT (l), "gtkqt-cells", cells, [] (gpointer p) {
      auto *c = static_cast<QList<Cell> *> (p);
      cells_clear (*c);
      delete c;
    });
  }
  return cells;
}

static void combo_refresh (GtkComboBox *combo)
{
  if (auto *c = gtkqt_q<QComboBox> (GTK_WIDGET (combo))) if (auto *a = dynamic_cast<GtkQtItemModel *> (c->model ())) a->reset ();
}

extern "C" {

void gtk_cell_layout_pack_start (GtkCellLayout *l, GtkCellRenderer *cell, gboolean)
{
  cells_pack (*layout_cells (l), cell);
  if (GTK_IS_COMBO_BOX (l)) combo_refresh (GTK_COMBO_BOX (l));
}

void gtk_cell_layout_pack_end (GtkCellLayout *l, GtkCellRenderer *cell, gboolean expand) { gtk_cell_layout_pack_start (l, cell, expand); }

void gtk_cell_layout_add_attribute (GtkCellLayout *l, GtkCellRenderer *cell, const gchar *attribute, gint column)
{
  for (Cell &c : *layout_cells (l)) if (c.renderer == cell) c.attrs << qMakePair (QByteArray (attribute), column);
  if (GTK_IS_COMBO_BOX (l)) combo_refresh (GTK_COMBO_BOX (l));
}

void gtk_cell_layout_set_attributes (GtkCellLayout *l, GtkCellRenderer *cell, ...)
{
  QList<Cell> *cells = layout_cells (l);
  for (Cell &c : *cells) if (c.renderer == cell) c.attrs.clear ();
  va_list ap;
  va_start (ap, cell);
  cells_add_attributes (*cells, cell, ap);
  va_end (ap);
  if (GTK_IS_COMBO_BOX (l)) combo_refresh (GTK_COMBO_BOX (l));
}

void gtk_cell_layout_clear (GtkCellLayout *l) { cells_clear (*layout_cells (l)); }

GtkWidget *gtk_combo_box_new_with_model (GtkTreeModel *model)
{
  GtkWidget *w = gtk_combo_box_new ();
  gtk_combo_box_set_model (GTK_COMBO_BOX (w), model);
  return w;
}

void gtk_combo_box_set_model (GtkComboBox *combo, GtkTreeModel *model)
{
  auto *c = gtkqt_q<QComboBox> (GTK_WIDGET (combo));
  if (!c || !model) return;
  g_object_ref (model);
  g_object_set_data_full (G_OBJECT (combo), "gtkqt-tree-model", model, g_object_unref);
  auto *a = new GtkQtItemModel (model);
  a->setParent (c);
  GObject *self = G_OBJECT (combo);
  a->columns = [self] { return QList<GObject *> { self }; };
  a->cells_of = [] (GObject *o) -> QList<Cell> * { return layout_cells ((GtkCellLayout *) o); };
  QSignalBlocker block (c);
  c->setModel (a);
  c->setCurrentIndex (-1);
}

/* A text combo's items live in the QComboBox until a program asks for its
 * model; from then on, as in GTK, they are a GtkListStore (text, id) */
GtkTreeModel *gtk_combo_box_get_model (GtkComboBox *combo)
{
  auto *m = (GtkTreeModel *) g_object_get_data (G_OBJECT (combo), "gtkqt-tree-model");
  if (m || !GTK_IS_COMBO_BOX_TEXT (combo)) return m;
  auto *c = gtkqt_q<QComboBox> (GTK_WIDGET (combo));
  if (!c) return nullptr;
  GtkListStore *store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
  for (int i = 0; i < c->count (); i++) {
    GtkTreeIter it;
    QVariant id = c->itemData (i);
    gtk_list_store_append (store, &it);
    gtk_list_store_set (store, &it, 0, c->itemText (i).toUtf8 ().constData (),
                        1, id.isValid () ? id.toString ().toUtf8 ().constData () : nullptr, -1);
  }
  int cur = c->currentIndex ();
  QString text = c->isEditable () ? c->currentText () : QString ();
  GtkCellRenderer *r = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), r, TRUE);
  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combo), r, "text", 0);
  gtk_combo_box_set_model (combo, GTK_TREE_MODEL (store));
  g_object_unref (store);
  g_object_set_data (G_OBJECT (combo), "gtkqt-text-store", store);
  QSignalBlocker block (c);
  c->setCurrentIndex (cur);
  if (c->isEditable ()) c->setEditText (text);
  return GTK_TREE_MODEL (store);
}

gboolean gtk_combo_box_get_active_iter (GtkComboBox *combo, GtkTreeIter *iter)
{
  auto *c = gtkqt_q<QComboBox> (GTK_WIDGET (combo));
  GtkTreeModel *m = gtk_combo_box_get_model (combo);
  if (!c || !m || c->currentIndex () < 0) return FALSE;
  return gtk_tree_model_iter_nth_child (m, iter, nullptr, c->currentIndex ());
}

void gtk_combo_box_set_active_iter (GtkComboBox *combo, GtkTreeIter *iter)
{
  GtkTreeModel *m = gtk_combo_box_get_model (combo);
  GtkTreePath *p = m && iter ? gtk_tree_model_get_path (m, iter) : nullptr;
  gtk_combo_box_set_active (combo, p ? gtk_tree_path_get_indices (p)[0] : -1);
  gtk_tree_path_free (p);
}

void gtk_combo_box_set_entry_text_column (GtkComboBox *combo, gint text_column)
{
  QList<Cell> *cells = layout_cells ((GtkCellLayout *) combo);
  if (cells->isEmpty ()) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new ();
    cells_pack (*cells, r);
    (*cells)[0].attrs << qMakePair (QByteArray ("text"), text_column);
  }
  combo_refresh (combo);
}

void gtk_combo_box_set_id_column (GtkComboBox *, gint) {}

} /* extern "C" */

/* ======================================================================
 * GtkEntryCompletion
 *
 * GTK's matching rules (minimum key length, case-insensitive prefix or the
 * program's match function) pick rows of any GtkTreeModel; the matches are
 * shown by a QCompleter popup in unfiltered mode, so Qt only displays and
 * navigates them. Choosing one emits match-selected, whose default handler
 * puts the row's text in the entry, so programs can take over as in GTK.
 * ====================================================================== */
struct _GtkEntryCompletion {
  GObject parent;
  GtkTreeModel *model;
  GtkWidget *entry;                      /* weak */
  gint text_column, minimum_key_length;
  gboolean inline_completion, inline_selection, popup_completion, popup_set_width, popup_single_match;
  GtkEntryCompletionMatchFunc match_func;
  gpointer match_data;
  GDestroyNotify match_notify;
  gchar *prefix;                         /* the key of the last completion */
  QPointer<QCompleter> *completer;
  QStringList *texts;                    /* the matches shown, and their rows */
  QStringList *paths;
};
typedef struct { GObjectClass parent_class; } GtkEntryCompletionClass;

G_DEFINE_TYPE (GtkEntryCompletion, gtk_entry_completion, G_TYPE_OBJECT)

static void completion_init_fields (GtkEntryCompletion *c)
{
  c->text_column = -1;
  c->minimum_key_length = 1;
  c->popup_completion = c->popup_set_width = c->popup_single_match = TRUE;
  c->completer = new QPointer<QCompleter>;
  c->texts = new QStringList;
  c->paths = new QStringList;
}
static void gtk_entry_completion_init (GtkEntryCompletion *c) { completion_init_fields (c); }

static void completion_detach (GtkEntryCompletion *c)
{
  if (*c->completer) {
    QCompleter *q = *c->completer;
    if (QAbstractItemView *p = q->popup ()) p->hide ();
    q->deleteLater ();
  }
  *c->completer = nullptr;
  if (c->entry) g_object_remove_weak_pointer (G_OBJECT (c->entry), (gpointer *) &c->entry);
  c->entry = nullptr;
}

static void completion_finalize (GObject *o)
{
  GtkEntryCompletion *c = GTK_ENTRY_COMPLETION (o);
  completion_detach (c);
  if (c->model) g_object_unref (c->model);
  if (c->match_notify) c->match_notify (c->match_data);
  g_free (c->prefix);
  delete c->completer;
  delete c->texts;
  delete c->paths;
  G_OBJECT_CLASS (gtk_entry_completion_parent_class)->finalize (o);
}

static gchar *completion_row_text (GtkEntryCompletion *c, GtkTreeIter *it)
{
  if (c->text_column < 0) return nullptr;
  gchar *s = nullptr;
  GValue v = G_VALUE_INIT;
  gtk_tree_model_get_value (c->model, it, c->text_column, &v);
  if (G_VALUE_HOLDS_STRING (&v)) s = g_value_dup_string (&v);
  g_value_unset (&v);
  return s;
}

static gboolean completion_default_match (GtkEntryCompletion *c, const gchar *key, GtkTreeIter *it)
{
  gchar *text = completion_row_text (c, it);
  if (!text) return FALSE;
  QString t = QString::fromUtf8 (text).normalized (QString::NormalizationForm_D);
  g_free (text);
  return t.startsWith (QString::fromUtf8 (key).normalized (QString::NormalizationForm_D), Qt::CaseInsensitive);
}

/* fills texts/paths with the rows that match key */
static void completion_collect (GtkEntryCompletion *c, const gchar *key)
{
  c->texts->clear ();
  c->paths->clear ();
  if (!c->model) return;
  GtkTreeIter it;
  if (!gtk_tree_model_get_iter_first (c->model, &it)) return;
  do {
    gboolean ok = c->match_func ? c->match_func (c, key, &it, c->match_data) : completion_default_match (c, key, &it);
    if (!ok) continue;
    gchar *text = completion_row_text (c, &it);
    gchar *path = gtk_tree_model_get_string_from_iter (c->model, &it);
    *c->texts << QString::fromUtf8 (text ? text : "");
    *c->paths << QString::fromUtf8 (path);
    g_free (text);
    g_free (path);
  } while (gtk_tree_model_iter_next (c->model, &it));
}

static QLineEdit *completion_line (GtkEntryCompletion *c)
{
  return c->entry ? gtkqt_q<QLineEdit> (c->entry) : nullptr;
}

static void completion_choose (GtkEntryCompletion *c, int row)
{
  if (!c->model || row < 0 || row >= c->paths->size ()) return;
  GtkTreeIter it;
  if (!gtk_tree_model_get_iter_from_string (c->model, &it, c->paths->at (row).toUtf8 ().constData ())) return;
  gboolean handled = FALSE;
  g_signal_emit_by_name (c, "match-selected", c->model, &it, &handled);
}

static void completion_run (GtkEntryCompletion *c)
{
  QLineEdit *line = completion_line (c);
  if (!line) return;
  QByteArray key = line->text ().toUtf8 ();
  if (g_utf8_strlen (key.constData (), -1) < c->minimum_key_length) {
    if (*c->completer && (*c->completer)->popup ()) (*c->completer)->popup ()->hide ();
    return;
  }
  g_free (c->prefix);
  c->prefix = g_strdup (key.constData ());
  completion_collect (c, key.constData ());
  if (c->texts->isEmpty ()) {
    g_signal_emit_by_name (c, "no-matches");
    if (*c->completer && (*c->completer)->popup ()) (*c->completer)->popup ()->hide ();
    return;
  }
  if (c->inline_completion) gtk_entry_completion_insert_prefix (c);
  if (!c->popup_completion) return;
  if (c->texts->size () == 1 && !c->popup_single_match) return;
  if (!*c->completer) {
    QCompleter *q = new QCompleter (line);
    q->setWidget (line);
    q->setCompletionMode (QCompleter::UnfilteredPopupCompletion);
    q->setModel (new QStringListModel (q));
    QObject::connect (q, QOverload<const QModelIndex &>::of (&QCompleter::activated), q, [c] (const QModelIndex &i) {
      GtkQtEmitGuard guard;
      completion_choose (c, i.row ());
    });
    *c->completer = q;
  }
  QCompleter *q = *c->completer;
  static_cast<QStringListModel *> (q->model ())->setStringList (*c->texts);
  q->complete (c->popup_set_width ? QRect () : QRect (0, 0, q->popup ()->sizeHintForColumn (0) + 24, line->height ()));
}

static gboolean completion_real_match_selected (GtkEntryCompletion *c, GtkTreeModel *, GtkTreeIter *it)
{
  if (!c->entry) return FALSE;
  gchar *text = completion_row_text (c, it);
  if (!text) return FALSE;
  gtk_entry_set_text (GTK_ENTRY (c->entry), text);
  gtk_editable_set_position (GTK_EDITABLE (c->entry), -1);
  g_free (text);
  return TRUE;
}

/* inserts the part of prefix past what was typed, selected, as GTK does */
static gboolean completion_real_insert_prefix (GtkEntryCompletion *c, const gchar *prefix)
{
  QLineEdit *line = completion_line (c);
  if (!line || !prefix) return FALSE;
  QString typed = line->text (), full = QString::fromUtf8 (prefix);
  if (full.size () <= typed.size () || !full.startsWith (typed, Qt::CaseInsensitive)) return FALSE;
  int pos = typed.size ();
  QByteArray rest = full.mid (pos).toUtf8 ();
  gint p = pos;
  gtk_editable_insert_text (GTK_EDITABLE (c->entry), rest.constData (), -1, &p);
  gtk_editable_select_region (GTK_EDITABLE (c->entry), pos, p);
  return TRUE;
}

static void gtk_entry_completion_class_init (GtkEntryCompletionClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = completion_finalize;
  GType t = G_TYPE_FROM_CLASS (klass);
  g_signal_new_class_handler ("match-selected", t, G_SIGNAL_RUN_LAST, G_CALLBACK (completion_real_match_selected),
                              g_signal_accumulator_true_handled, nullptr, nullptr,
                              G_TYPE_BOOLEAN, 2, GTK_TYPE_TREE_MODEL, G_TYPE_POINTER);
  g_signal_new_class_handler ("insert-prefix", t, G_SIGNAL_RUN_LAST, G_CALLBACK (completion_real_insert_prefix),
                              g_signal_accumulator_true_handled, nullptr, nullptr, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
  g_signal_new ("cursor-on-match", t, G_SIGNAL_RUN_LAST, 0, g_signal_accumulator_true_handled, nullptr, nullptr,
                G_TYPE_BOOLEAN, 2, GTK_TYPE_TREE_MODEL, G_TYPE_POINTER);
  g_signal_new ("action-activated", t, G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_INT);
  g_signal_new ("no-matches", t, G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
  gtkqt_prop (klass, g_param_spec_object ("model", nullptr, nullptr, GTK_TYPE_TREE_MODEL, RW),
              [] (GObject *o, const GValue *v) { gtk_entry_completion_set_model (GTK_ENTRY_COMPLETION (o), (GtkTreeModel *) g_value_get_object (v)); },
              [] (GObject *o, GValue *v) { g_value_set_object (v, GTK_ENTRY_COMPLETION (o)->model); });
  gtkqt_prop (klass, g_param_spec_int ("text-column", nullptr, nullptr, -1, G_MAXINT, -1, RW),
              [] (GObject *o, const GValue *v) { gtk_entry_completion_set_text_column (GTK_ENTRY_COMPLETION (o), g_value_get_int (v)); },
              [] (GObject *o, GValue *v) { g_value_set_int (v, GTK_ENTRY_COMPLETION (o)->text_column); });
  gtkqt_prop (klass, g_param_spec_int ("minimum-key-length", nullptr, nullptr, 0, G_MAXINT, 1, RW),
              [] (GObject *o, const GValue *v) { GTK_ENTRY_COMPLETION (o)->minimum_key_length = g_value_get_int (v); },
              [] (GObject *o, GValue *v) { g_value_set_int (v, GTK_ENTRY_COMPLETION (o)->minimum_key_length); });
  gtkqt_prop (klass, g_param_spec_boolean ("inline-completion", nullptr, nullptr, FALSE, RW),
              [] (GObject *o, const GValue *v) { GTK_ENTRY_COMPLETION (o)->inline_completion = g_value_get_boolean (v); },
              [] (GObject *o, GValue *v) { g_value_set_boolean (v, GTK_ENTRY_COMPLETION (o)->inline_completion); });
  gtkqt_prop (klass, g_param_spec_boolean ("popup-completion", nullptr, nullptr, TRUE, RW),
              [] (GObject *o, const GValue *v) { GTK_ENTRY_COMPLETION (o)->popup_completion = g_value_get_boolean (v); },
              [] (GObject *o, GValue *v) { g_value_set_boolean (v, GTK_ENTRY_COMPLETION (o)->popup_completion); });
}

extern "C" {

GtkEntryCompletion *gtk_entry_completion_new (void) { return (GtkEntryCompletion *) g_object_new (GTK_TYPE_ENTRY_COMPLETION, nullptr); }
GtkWidget *gtk_entry_completion_get_entry (GtkEntryCompletion *c) { return c->entry; }

void gtk_entry_completion_set_model (GtkEntryCompletion *c, GtkTreeModel *model)
{
  if (model) g_object_ref (model);
  if (c->model) g_object_unref (c->model);
  c->model = model;
  /* GTK picks the first string column once a model is there */
  if (model && c->text_column < 0)
    for (gint i = 0; i < gtk_tree_model_get_n_columns (model); i++)
      if (gtk_tree_model_get_column_type (model, i) == G_TYPE_STRING) { c->text_column = i; break; }
}
GtkTreeModel *gtk_entry_completion_get_model (GtkEntryCompletion *c) { return c->model; }

void gtk_entry_completion_set_match_func (GtkEntryCompletion *c, GtkEntryCompletionMatchFunc func, gpointer data, GDestroyNotify notify)
{
  if (c->match_notify) c->match_notify (c->match_data);
  c->match_func = func;
  c->match_data = data;
  c->match_notify = notify;
}
void gtk_entry_completion_set_minimum_key_length (GtkEntryCompletion *c, gint length) { c->minimum_key_length = MAX (0, length); }
gint gtk_entry_completion_get_minimum_key_length (GtkEntryCompletion *c) { return c->minimum_key_length; }
void gtk_entry_completion_complete (GtkEntryCompletion *c) { completion_run (c); }

void gtk_entry_completion_insert_prefix (GtkEntryCompletion *c)
{
  gchar *prefix = gtk_entry_completion_compute_prefix (c, c->prefix ? c->prefix : "");
  gboolean handled = FALSE;
  if (prefix) g_signal_emit_by_name (c, "insert-prefix", prefix, &handled);
  g_free (prefix);
}

/* the longest common prefix of the matches for key */
gchar *gtk_entry_completion_compute_prefix (GtkEntryCompletion *c, const char *key)
{
  completion_collect (c, key ? key : "");
  if (c->texts->isEmpty ()) return nullptr;
  QString p = c->texts->first ();
  for (const QString &t : *c->texts) {
    int n = 0;
    while (n < p.size () && n < t.size () && p[n] == t[n]) n++;
    p.truncate (n);
  }
  return g_strdup (p.toUtf8 ().constData ());
}

void gtk_entry_completion_set_text_column (GtkEntryCompletion *c, gint column) { c->text_column = column; }
gint gtk_entry_completion_get_text_column (GtkEntryCompletion *c) { return c->text_column; }
void gtk_entry_completion_set_inline_completion (GtkEntryCompletion *c, gboolean v) { c->inline_completion = v; }
gboolean gtk_entry_completion_get_inline_completion (GtkEntryCompletion *c) { return c->inline_completion; }
void gtk_entry_completion_set_inline_selection (GtkEntryCompletion *c, gboolean v) { c->inline_selection = v; }
gboolean gtk_entry_completion_get_inline_selection (GtkEntryCompletion *c) { return c->inline_selection; }
void gtk_entry_completion_set_popup_completion (GtkEntryCompletion *c, gboolean v) { c->popup_completion = v; }
gboolean gtk_entry_completion_get_popup_completion (GtkEntryCompletion *c) { return c->popup_completion; }
void gtk_entry_completion_set_popup_set_width (GtkEntryCompletion *c, gboolean v) { c->popup_set_width = v; }
gboolean gtk_entry_completion_get_popup_set_width (GtkEntryCompletion *c) { return c->popup_set_width; }
void gtk_entry_completion_set_popup_single_match (GtkEntryCompletion *c, gboolean v) { c->popup_single_match = v; }
gboolean gtk_entry_completion_get_popup_single_match (GtkEntryCompletion *c) { return c->popup_single_match; }
const gchar *gtk_entry_completion_get_completion_prefix (GtkEntryCompletion *c) { return c->prefix; }

static void entry_completion_unset (gpointer data)
{
  GtkEntryCompletion *c = (GtkEntryCompletion *) data;
  completion_detach (c);
  g_object_unref (c);
}

void gtk_entry_set_completion (GtkEntry *entry, GtkEntryCompletion *completion)
{
  if (!GTK_IS_ENTRY (entry)) return;
  /* drops (and detaches) the previous one */
  g_object_set_data_full (G_OBJECT (entry), "gtkqt-completion", nullptr, nullptr);
  if (!completion) return;
  g_object_ref (completion);
  completion_detach (completion);
  completion->entry = GTK_WIDGET (entry);
  g_object_add_weak_pointer (G_OBJECT (entry), (gpointer *) &completion->entry);
  g_object_set_data_full (G_OBJECT (entry), "gtkqt-completion", completion, entry_completion_unset);
  /* typing (not set_text from the program) runs the completion */
  QLineEdit *line = gtkqt_q<QLineEdit> (GTK_WIDGET (entry));
  if (line && !line->property ("_gtkqt_completion").toBool ()) {
    line->setProperty ("_gtkqt_completion", true);
    QObject::connect (line, &QLineEdit::textEdited, line, [entry] (const QString &) {
      auto *c = (GtkEntryCompletion *) g_object_get_data (G_OBJECT (entry), "gtkqt-completion");
      if (!c) return;
      GtkQtEmitGuard guard;
      completion_run (c);
    });
  }
}

GtkEntryCompletion *gtk_entry_get_completion (GtkEntry *entry)
{
  return GTK_IS_ENTRY (entry) ? (GtkEntryCompletion *) g_object_get_data (G_OBJECT (entry), "gtkqt-completion") : nullptr;
}

} /* extern "C" */

/* ======================================================================
 * More of the GTK 2 tree API: selection ranges and select functions, the
 * view's coordinates, rows' drop positions
 * ====================================================================== */
extern "C" {

/* ---------- GtkTreeSelection ---------- */
void gtk_tree_selection_set_select_function (GtkTreeSelection *s, GtkTreeSelectionFunc func, gpointer data, GDestroyNotify destroy)
{
  if (s->func_destroy) s->func_destroy (s->func_data);
  s->func = func;
  s->func_data = data;
  s->func_destroy = destroy;
}
gboolean gtk_tree_selection_path_is_selected (GtkTreeSelection *s, GtkTreePath *path)
{
  GtkQtItemModel *a = sel_adapter (s);
  return a && path && selected_rows (s).contains (a->node_at (path));
}
void gtk_tree_selection_unselect_path (GtkTreeSelection *s, GtkTreePath *path)
{
  if (GtkQtItemModel *a = sel_adapter (s)) select_row (s, a->node_at (path), QItemSelectionModel::Deselect);
}
static void select_range (GtkTreeSelection *s, GtkTreePath *a, GtkTreePath *b, QItemSelectionModel::SelectionFlag f)
{
  QItemSelectionModel *sm = sel_model (s);
  QModelIndex ia = view_index (s->view, a), ib = view_index (s->view, b);
  if (!sm || !ia.isValid () || !ib.isValid ()) return;
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (s->view));
  /* rows in display order, across levels */
  QItemSelection sel;
  bool in = false;
  for (QModelIndex i = tv->indexAt (QPoint (0, 0)).isValid () ? tv->model ()->index (0, 0) : QModelIndex (); i.isValid (); i = tv->indexBelow (i)) {
    bool edge = i == ia || i == ib;
    if (edge || in) sel.select (i, i);
    if (edge && (ia != ib)) in = !in;
    if (edge && !in) break;
  }
  sm->select (sel, f | QItemSelectionModel::Rows);
}
void gtk_tree_selection_select_range (GtkTreeSelection *s, GtkTreePath *a, GtkTreePath *b) { select_range (s, a, b, QItemSelectionModel::Select); }
void gtk_tree_selection_unselect_range (GtkTreeSelection *s, GtkTreePath *a, GtkTreePath *b) { select_range (s, a, b, QItemSelectionModel::Deselect); }

/* ---------- GtkTreeView: columns ---------- */
GList *gtk_tree_view_get_columns (GtkTreeView *view)
{
  GList *l = nullptr;
  if (ViewData *d = vd (GTK_WIDGET (view))) for (GtkTreeViewColumn *c : d->columns) l = g_list_append (l, c);
  return l;
}

void gtk_tree_view_move_column_after (GtkTreeView *view, GtkTreeViewColumn *column, GtkTreeViewColumn *base)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d || !d->columns.contains (column) || column == base) return;
  d->columns.removeOne (column);
  d->columns.insert (base ? d->columns.indexOf (base) + 1 : 0, column);
  view_apply_columns (GTK_WIDGET (view));
  g_signal_emit_by_name (view, "columns-changed");
}

void gtk_tree_view_set_column_drag_function (GtkTreeView *view, GtkTreeViewColumnDropFunc func, gpointer data, GDestroyNotify destroy)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d) return;
  if (d->column_drop_destroy) d->column_drop_destroy (d->column_drop_data);
  d->column_drop = func;
  d->column_drop_data = data;
  d->column_drop_destroy = destroy;
}

void gtk_tree_view_set_expander_column (GtkTreeView *view, GtkTreeViewColumn *column) { if (ViewData *d = vd (GTK_WIDGET (view))) d->expander_column = column; }
GtkTreeViewColumn *gtk_tree_view_get_expander_column (GtkTreeView *view) { ViewData *d = vd (GTK_WIDGET (view)); return d ? d->expander_column : nullptr; }

/* ---------- rows ---------- */
void gtk_tree_view_collapse_row (GtkTreeView *view, GtkTreePath *path)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  QModelIndex i = view_index (view, path);
  if (tv && i.isValid ()) tv->collapse (i);
}
gboolean gtk_tree_view_row_expanded (GtkTreeView *view, GtkTreePath *path)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  QModelIndex i = view_index (view, path);
  return tv && i.isValid () && tv->isExpanded (i);
}
void gtk_tree_view_expand_to_path (GtkTreeView *view, GtkTreePath *path)
{
  auto *tv = gtkqt_q<QTreeView> (GTK_WIDGET (view));
  for (QModelIndex i = view_index (view, path); tv && i.isValid (); i = i.parent ()) tv->expand (i);
}

/* ---------- coordinates: widget (with the header), bin window (the
 * viewport, where row events come from) and tree (the whole list) ---------- */
static QTreeView *qtv (GtkTreeView *view) { return gtkqt_q<QTreeView> (GTK_WIDGET (view)); }

gboolean gtk_tree_view_get_path_at_pos (GtkTreeView *view, gint x, gint y, GtkTreePath **path, GtkTreeViewColumn **column,
                                        gint *cell_x, gint *cell_y)
{
  QTreeView *tv = qtv (view);
  ViewData *d = vd (GTK_WIDGET (view));
  if (path) *path = nullptr;
  if (column) *column = nullptr;
  QModelIndex i = tv && d && d->adapter ? tv->indexAt (QPoint (x, y)) : QModelIndex ();
  if (!i.isValid ()) return FALSE;
  QRect r = tv->visualRect (i);
  if (path) *path = d->adapter->path_of (d->adapter->node (i));
  if (column) *column = i.column () < d->columns.size () ? d->columns[i.column ()] : nullptr;
  if (cell_x) *cell_x = x - r.x ();
  if (cell_y) *cell_y = y - r.y ();
  return TRUE;
}

static QRect cell_rect (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column)
{
  QTreeView *tv = qtv (view);
  ViewData *d = vd (GTK_WIDGET (view));
  if (!tv || !d) return QRect ();
  int col = column ? d->columns.indexOf (column) : -1;
  QRect r;
  if (path) {
    QModelIndex i = view_index (view, path, qMax (col, 0));
    if (i.isValid ()) r = tv->visualRect (i);
  }
  if (col >= 0) { r.setX (tv->header ()->sectionViewportPosition (col)); r.setWidth (tv->header ()->sectionSize (col)); }
  else { r.setX (0); r.setWidth (0); }
  if (!path) { r.setY (0); r.setHeight (0); }
  return r;
}

void gtk_tree_view_get_cell_area (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, GdkRectangle *rect)
{
  QRect r = cell_rect (view, path, column);
  if (rect) *rect = { r.x (), r.y (), r.width (), r.height () };
}
void gtk_tree_view_get_background_area (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, GdkRectangle *rect)
{
  gtk_tree_view_get_cell_area (view, path, column, rect);
}

void gtk_tree_view_get_visible_rect (GtkTreeView *view, GdkRectangle *rect)
{
  QTreeView *tv = qtv (view);
  if (!rect) return;
  if (!tv) { *rect = {}; return; }
  *rect = { tv->horizontalScrollBar ()->value (), tv->verticalScrollBar ()->value (), tv->viewport ()->width (), tv->viewport ()->height () };
}

gboolean gtk_tree_view_get_visible_range (GtkTreeView *view, GtkTreePath **start, GtkTreePath **end)
{
  QTreeView *tv = qtv (view);
  if (start) *start = nullptr;
  if (end) *end = nullptr;
  if (!tv) return FALSE;
  QModelIndex a = tv->indexAt (QPoint (1, 1)), b = tv->indexAt (QPoint (1, tv->viewport ()->height () - 2));
  if (!a.isValid ()) return FALSE;
  if (!b.isValid ()) {
    /* the list ends above the bottom: its last visible row */
    b = a;
    for (QModelIndex n = tv->indexBelow (b); n.isValid (); n = tv->indexBelow (n)) b = n;
  }
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d || !d->adapter) return FALSE;
  if (start) *start = d->adapter->path_of (d->adapter->node (a));
  if (end) *end = d->adapter->path_of (d->adapter->node (b));
  return TRUE;
}

void gtk_tree_view_convert_bin_window_to_tree_coords (GtkTreeView *view, gint bx, gint by, gint *tx, gint *ty)
{
  QTreeView *tv = qtv (view);
  if (tx) *tx = bx + (tv ? tv->horizontalScrollBar ()->value () : 0);
  if (ty) *ty = by + (tv ? tv->verticalScrollBar ()->value () : 0);
}
void gtk_tree_view_convert_tree_to_bin_window_coords (GtkTreeView *view, gint tx, gint ty, gint *bx, gint *by)
{
  QTreeView *tv = qtv (view);
  if (bx) *bx = tx - (tv ? tv->horizontalScrollBar ()->value () : 0);
  if (by) *by = ty - (tv ? tv->verticalScrollBar ()->value () : 0);
}
void gtk_tree_view_convert_widget_to_bin_window_coords (GtkTreeView *view, gint wx, gint wy, gint *bx, gint *by)
{
  QTreeView *tv = qtv (view);
  QPoint o = tv ? tv->viewport ()->pos () : QPoint ();
  if (bx) *bx = wx - o.x ();
  if (by) *by = wy - o.y ();
}
void gtk_tree_view_convert_bin_window_to_widget_coords (GtkTreeView *view, gint bx, gint by, gint *wx, gint *wy)
{
  QTreeView *tv = qtv (view);
  QPoint o = tv ? tv->viewport ()->pos () : QPoint ();
  if (wx) *wx = bx + o.x ();
  if (wy) *wy = by + o.y ();
}
/* GTK 2's "widget" coordinates here are the bin window's */
void gtk_tree_view_widget_to_tree_coords (GtkTreeView *view, gint wx, gint wy, gint *tx, gint *ty)
{
  gtk_tree_view_convert_bin_window_to_tree_coords (view, wx, wy, tx, ty);
}
void gtk_tree_view_tree_to_widget_coords (GtkTreeView *view, gint tx, gint ty, gint *wx, gint *wy)
{
  gtk_tree_view_convert_tree_to_bin_window_coords (view, tx, ty, wx, wy);
}

void gtk_tree_view_scroll_to_point (GtkTreeView *view, gint tx, gint ty)
{
  QTreeView *tv = qtv (view);
  if (!tv) return;
  if (tx != -1) tv->horizontalScrollBar ()->setValue (tx);
  if (ty != -1) tv->verticalScrollBar ()->setValue (ty);
}

GdkWindow *gtk_tree_view_get_bin_window (GtkTreeView *view) { ViewData *d = vd (GTK_WIDGET (view)); return d ? d->bin_window : nullptr; }

GtkAdjustment *gtk_tree_view_get_hadjustment (GtkTreeView *view)
{
  QTreeView *tv = qtv (view);
  return tv ? gtkqt_scrollbar_adjustment (G_OBJECT (view), "gtkqt-hadj", tv->horizontalScrollBar ()) : nullptr;
}
GtkAdjustment *gtk_tree_view_get_vadjustment (GtkTreeView *view)
{
  QTreeView *tv = qtv (view);
  return tv ? gtkqt_scrollbar_adjustment (G_OBJECT (view), "gtkqt-vadj", tv->verticalScrollBar ()) : nullptr;
}

/* ---------- looks ---------- */
void gtk_tree_view_set_grid_lines (GtkTreeView *view, GtkTreeViewGridLines grid_lines)
{
  if (ViewData *d = vd (GTK_WIDGET (view))) d->grid_lines = grid_lines;
  if (QTreeView *tv = qtv (view)) tv->viewport ()->update ();
}
GtkTreeViewGridLines gtk_tree_view_get_grid_lines (GtkTreeView *view) { ViewData *d = vd (GTK_WIDGET (view)); return d ? d->grid_lines : GTK_TREE_VIEW_GRID_LINES_NONE; }
gboolean gtk_tree_view_get_headers_visible (GtkTreeView *view) { QTreeView *tv = qtv (view); return tv && !tv->isHeaderHidden (); }
gboolean gtk_tree_view_get_rules_hint (GtkTreeView *view) { QTreeView *tv = qtv (view); return tv && tv->alternatingRowColors (); }
gboolean gtk_tree_view_get_reorderable (GtkTreeView *) { return FALSE; }
void gtk_tree_view_set_fixed_height_mode (GtkTreeView *view, gboolean enable) { if (QTreeView *tv = qtv (view)) tv->setUniformRowHeights (enable || tv->uniformRowHeights ()); }
void gtk_tree_view_set_enable_tree_lines (GtkTreeView *, gboolean) {}
void gtk_tree_view_set_show_expanders (GtkTreeView *view, gboolean enabled) { if (QTreeView *tv = qtv (view)) tv->setItemsExpandable (enabled); }
void gtk_tree_view_set_level_indentation (GtkTreeView *view, gint indentation)
{
  if (QTreeView *tv = qtv (view)) tv->setIndentation (tv->style ()->pixelMetric (QStyle::PM_TreeViewIndentation) + indentation);
}
void gtk_tree_view_set_hover_selection (GtkTreeView *, gboolean) {}
void gtk_tree_view_set_rubber_banding (GtkTreeView *, gboolean) {}

/* ---------- drag and drop onto rows ---------- */
void gtk_tree_view_enable_model_drag_source (GtkTreeView *view, GdkModifierType mask, const GtkTargetEntry *targets, gint n, GdkDragAction actions)
{
  g_object_set_data (G_OBJECT (view), "gtkqt-tree-drag-source", GINT_TO_POINTER (1));
}
void gtk_tree_view_enable_model_drag_dest (GtkTreeView *view, const GtkTargetEntry *targets, gint n, GdkDragAction actions)
{
  gtk_drag_dest_set (GTK_WIDGET (view), GTK_DEST_DEFAULT_ALL, targets, n, actions);
}
void gtk_tree_view_unset_rows_drag_source (GtkTreeView *view) { g_object_set_data (G_OBJECT (view), "gtkqt-tree-drag-source", nullptr); }
void gtk_tree_view_unset_rows_drag_dest (GtkTreeView *view) { gtk_drag_dest_unset (GTK_WIDGET (view)); }

void gtk_tree_view_set_drag_dest_row (GtkTreeView *view, GtkTreePath *path, GtkTreeViewDropPosition pos)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (!d) return;
  if (d->drag_dest) gtk_tree_path_free (d->drag_dest);
  d->drag_dest = path ? gtk_tree_path_copy (path) : nullptr;
  d->drag_pos = pos;
  if (QTreeView *tv = qtv (view)) tv->viewport ()->update ();
}

void gtk_tree_view_get_drag_dest_row (GtkTreeView *view, GtkTreePath **path, GtkTreeViewDropPosition *pos)
{
  ViewData *d = vd (GTK_WIDGET (view));
  if (path) *path = d && d->drag_dest ? gtk_tree_path_copy (d->drag_dest) : nullptr;
  if (pos) *pos = d ? d->drag_pos : GTK_TREE_VIEW_DROP_BEFORE;
}

/* drag_x, drag_y as drag-motion gives them: the rows' (bin window) coordinates */
gboolean gtk_tree_view_get_dest_row_at_pos (GtkTreeView *view, gint x, gint y, GtkTreePath **path, GtkTreeViewDropPosition *pos)
{
  QTreeView *tv = qtv (view);
  ViewData *d = vd (GTK_WIDGET (view));
  if (path) *path = nullptr;
  QModelIndex i = tv && d && d->model ? tv->indexAt (QPoint (x, y)) : QModelIndex ();
  if (!i.isValid ()) return FALSE;
  QRect r = tv->visualRect (i);
  double f = r.height () > 0 ? double (y - r.y ()) / r.height () : 0;
  GtkTreeViewDropPosition p;
  if (gtk_tree_model_get_flags (d->model) & GTK_TREE_MODEL_LIST_ONLY) p = f < 0.5 ? GTK_TREE_VIEW_DROP_BEFORE : GTK_TREE_VIEW_DROP_AFTER;
  else p = f < 0.25 ? GTK_TREE_VIEW_DROP_BEFORE : f < 0.5 ? GTK_TREE_VIEW_DROP_INTO_OR_BEFORE
           : f < 0.75 ? GTK_TREE_VIEW_DROP_INTO_OR_AFTER : GTK_TREE_VIEW_DROP_AFTER;
  if (path) *path = d->adapter ? d->adapter->path_of (d->adapter->node (i)) : nullptr;
  if (pos) *pos = p;
  return TRUE;
}

} /* extern "C" */

/* "move-cursor": the keyboard's cursor moves (programs emit it too), the
 * cursor row selected as GTK does */
static gboolean tree_view_move_cursor (GtkTreeView *view, gint step, gint count)
{
  QTreeView *tv = qtv (view);
  QAbstractItemModel *m = tv ? tv->model () : nullptr;
  if (!m || m->rowCount () == 0 || count == 0) return FALSE;
  QModelIndex cur = tv->currentIndex (), to;
  auto last = [&] {
    QModelIndex l = m->index (m->rowCount () - 1, 0);
    while (tv->isExpanded (l) && m->rowCount (l) > 0) l = m->index (m->rowCount (l) - 1, 0, l);
    return l;
  };
  switch (step) {
  case GTK_MOVEMENT_BUFFER_ENDS:
    to = count < 0 ? m->index (0, 0) : last ();
    break;
  case GTK_MOVEMENT_PAGES:
  case GTK_MOVEMENT_DISPLAY_LINES:
  case GTK_MOVEMENT_PARAGRAPHS:
  case GTK_MOVEMENT_LOGICAL_POSITIONS: {
    int n = qAbs (count);
    if (step == GTK_MOVEMENT_PAGES) {
      int h = tv->sizeHintForRow (0);
      n *= qMax (1, h > 0 ? tv->viewport ()->height () / h - 1 : 10);
    }
    to = cur.isValid () ? cur.siblingAtColumn (0) : m->index (0, 0);
    for (int i = 0; i < n && cur.isValid (); i++) {
      QModelIndex nx = count > 0 ? tv->indexBelow (to) : tv->indexAbove (to);
      if (!nx.isValid ()) break;
      to = nx;
    }
    break;
  }
  default:
    return FALSE;
  }
  if (!to.isValid ()) return FALSE;
  tv->selectionModel ()->setCurrentIndex (to, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  tv->scrollTo (to);
  return TRUE;
}

