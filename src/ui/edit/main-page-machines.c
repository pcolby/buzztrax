/* $Id: main-page-machines.c,v 1.5 2004-08-27 15:32:46 ensonic Exp $
 * class for the editor main machines page
 */

#define BT_EDIT
#define BT_MAIN_PAGE_MACHINES_C

#include "bt-edit.h"

enum {
  MAIN_PAGE_MACHINES_APP=1,
};


struct _BtMainPageMachinesPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  
  /* the application */
  BtEditApplication *app;

  /* canvas for machine view */
  GnomeCanvas *canvas;
};

//-- event handler

static void on_song_changed(const BtEditApplication *app, gpointer user_data) {
  BtMainPageMachines *self=BT_MAIN_PAGE_MACHINES(user_data);
  BtSong *song;

  GST_INFO("song has changed : app=%p, window=%p",song,user_data);
  // get song from app
  song=BT_SONG(bt_g_object_get_object_property(G_OBJECT(self->private->app),"song"));
  // update page
}

//-- helper methods

// @todo this needs parameters
static void bt_main_page_machines_draw_machine(const BtMainPageMachines *self) {
  GnomeCanvasItem *item;

  item = gnome_canvas_item_new(gnome_canvas_root(self->private->canvas),
                           GNOME_TYPE_CANVAS_RECT,
                           "x1", 1.0,
                           "y1", 1.0,
                           "x2", 26.0,
                           "y2", 16.0,
                           "fill_color", "gray",
                           "outline_color", "black",
                           "width_pixels", 1,
                           NULL);
  item = gnome_canvas_item_new(gnome_canvas_root(self->private->canvas),
                           GNOME_TYPE_CANVAS_TEXT,
                           "x", 12.0,
                           "y", 5.0,
                           "text", "sine1",
                           "fill_color", "black",
                           NULL);
}

static gboolean bt_main_page_machines_init_ui(const BtMainPageMachines *self, const BtEditApplication *app) {
  GtkWidget *toolbar;
  GtkWidget *icon,*button,*image;

  // add toolbar
  toolbar=gtk_toolbar_new();
  gtk_widget_set_name(toolbar,_("machine view tool bar"));
  gtk_box_pack_start(GTK_BOX(self),GTK_WIDGET(toolbar),FALSE,FALSE,0);
  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar),GTK_TOOLBAR_BOTH);
  
  icon=gtk_image_new_from_stock(GTK_STOCK_ZOOM_FIT, gtk_toolbar_get_icon_size(GTK_TOOLBAR(toolbar)));
  button=gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                GTK_TOOLBAR_CHILD_BUTTON,
                                NULL,
                                _("Zoom Fit"),
                                NULL,NULL,
                                icon,NULL,NULL);
  gtk_label_set_use_underline(GTK_LABEL(((GtkToolbarChild*)(g_list_last(GTK_TOOLBAR(toolbar)->children)->data))->label),TRUE);
  gtk_widget_set_name(button,_("Zoom Fit"));
  //g_signal_connect(G_OBJECT(button),"clicked",G_CALLBACK(on_toolbar_new_clicked),(gpointer)self);

  icon=gtk_image_new_from_stock(GTK_STOCK_ZOOM_IN, gtk_toolbar_get_icon_size(GTK_TOOLBAR(toolbar)));
  button=gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                GTK_TOOLBAR_CHILD_BUTTON,
                                NULL,
                                _("Zoom In"),
                                NULL,NULL,
                                icon,NULL,NULL);
  gtk_label_set_use_underline(GTK_LABEL(((GtkToolbarChild*)(g_list_last(GTK_TOOLBAR(toolbar)->children)->data))->label),TRUE);
  gtk_widget_set_name(button,_("Zoom In"));
  
  icon=gtk_image_new_from_stock(GTK_STOCK_ZOOM_OUT, gtk_toolbar_get_icon_size(GTK_TOOLBAR(toolbar)));
  button=gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                GTK_TOOLBAR_CHILD_BUTTON,
                                NULL,
                                _("Zoom Out"),
                                NULL,NULL,
                                icon,NULL,NULL);
  gtk_label_set_use_underline(GTK_LABEL(((GtkToolbarChild*)(g_list_last(GTK_TOOLBAR(toolbar)->children)->data))->label),TRUE);
  gtk_widget_set_name(button,_("Zoom Out"));
  
  // add canvas
  gtk_widget_push_visual(gdk_imlib_get_visual());
  // @todo try gtk_widget_push_colormap(gdk_colormap_get_system());
  //gtk_widget_push_colormap((GdkColormap *)gdk_imlib_get_colormap());
  self->private->canvas = gnome_canvas_new_aa();
  gnome_canvas_set_pixels_per_unit(self->private->canvas,10);
  gnome_canvas_set_scroll_region(self->private->canvas,0.0,0.0,100.0,100.0);
  //gtk_widget_pop_colormap();
  gtk_widget_pop_visual();
  gtk_box_pack_start(GTK_BOX(self),GTK_WIDGET(self->private->canvas),TRUE,TRUE,0);
  // add an example item
  bt_main_page_machines_draw_machine(self);

  // register event handlers
  g_signal_connect(G_OBJECT(app), "song-changed", (GCallback)on_song_changed, (gpointer)self);
  return(TRUE);
}

//-- constructor methods

/**
 * bt_main_page_machines_new:
 * @app: the application the window belongs to
 *
 * Create a new instance
 *
 * Returns: the new instance or NULL in case of an error
 */
BtMainPageMachines *bt_main_page_machines_new(const BtEditApplication *app) {
  BtMainPageMachines *self;

  if(!(self=BT_MAIN_PAGE_MACHINES(g_object_new(BT_TYPE_MAIN_PAGE_MACHINES,"app",app,NULL)))) {
    goto Error;
  }
  // generate UI
  if(!bt_main_page_machines_init_ui(self,app)) {
    goto Error;
  }
  return(self);
Error:
  if(self) g_object_unref(self);
  return(NULL);
}

//-- methods

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_main_page_machines_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtMainPageMachines *self = BT_MAIN_PAGE_MACHINES(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_PAGE_MACHINES_APP: {
      g_value_set_object(value, G_OBJECT(self->private->app));
    } break;
    default: {
 			g_assert(FALSE);
      break;
    }
  }
}

/* sets the given properties for this object */
static void bt_main_page_machines_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtMainPageMachines *self = BT_MAIN_PAGE_MACHINES(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_PAGE_MACHINES_APP: {
      self->private->app = g_object_ref(G_OBJECT(g_value_get_object(value)));
      //GST_DEBUG("set the app for MAIN_PAGE_MACHINES: %p",self->private->app);
    } break;
    default: {
			g_assert(FALSE);
      break;
    }
  }
}

static void bt_main_page_machines_dispose(GObject *object) {
  BtMainPageMachines *self = BT_MAIN_PAGE_MACHINES(object);
	return_if_disposed();
  self->private->dispose_has_run = TRUE;
}

static void bt_main_page_machines_finalize(GObject *object) {
  BtMainPageMachines *self = BT_MAIN_PAGE_MACHINES(object);
  
  g_object_unref(G_OBJECT(self->private->app));
  g_free(self->private);
}

static void bt_main_page_machines_init(GTypeInstance *instance, gpointer g_class) {
  BtMainPageMachines *self = BT_MAIN_PAGE_MACHINES(instance);
  self->private = g_new0(BtMainPageMachinesPrivate,1);
  self->private->dispose_has_run = FALSE;
}

static void bt_main_page_machines_class_init(BtMainPageMachinesClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GParamSpec *g_param_spec;
  
  gobject_class->set_property = bt_main_page_machines_set_property;
  gobject_class->get_property = bt_main_page_machines_get_property;
  gobject_class->dispose      = bt_main_page_machines_dispose;
  gobject_class->finalize     = bt_main_page_machines_finalize;

  g_object_class_install_property(gobject_class,MAIN_PAGE_MACHINES_APP,
                                  g_param_spec_object("app",
                                     "app contruct prop",
                                     "Set application object, the window belongs to",
                                     BT_TYPE_EDIT_APPLICATION, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));
}

GType bt_main_page_machines_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtMainPageMachinesClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_main_page_machines_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtMainPageMachines),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_main_page_machines_init, // instance_init
    };
		type = g_type_register_static(GTK_TYPE_VBOX,"BtMainPageMachines",&info,0);
  }
  return type;
}

