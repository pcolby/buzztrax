/* $Id: plainfile-settings.c,v 1.1 2004-09-27 16:05:33 ensonic Exp $
 * plain file based implementation sub class for buzztard settings handling
 */

#define BT_CORE
#define BT_PLAINFILE_SETTINGS_C

#include <libbtcore/core.h>

enum {
  PLAINFILE_SETTINGS_XXX=1
};

struct _BtPlainfileSettingsPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  
  /* key=value list, keys are defined in BtSettings
  GHashTable *settings;
   */
};

static BtSettingsClass *parent_class=NULL;

//-- constructor methods

/**
 * bt_plainfile_settings_new:
 *
 * Create a new instance.
 *
 * Returns: the new instance or NULL in case of an error
 */
BtPlainfileSettings *bt_plainfile_settings_new(void) {
  BtPlainfileSettings *self;
  self=BT_PLAINFILE_SETTINGS(g_object_new(BT_TYPE_PLAINFILE_SETTINGS,NULL));
  
  //bt_settings_new(BT_SETTINGS(self));
  return(self);  
}

//-- methods

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_plainfile_settings_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtPlainfileSettings *self = BT_PLAINFILE_SETTINGS(object);
  return_if_disposed();
  switch (property_id) {
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_plainfile_settings_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtPlainfileSettings *self = BT_PLAINFILE_SETTINGS(object);
  return_if_disposed();
  switch (property_id) {
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_plainfile_settings_dispose(GObject *object) {
  BtPlainfileSettings *self = BT_PLAINFILE_SETTINGS(object);

	return_if_disposed();
  self->private->dispose_has_run = TRUE;
  
  GST_DEBUG("!!!! self=%p",self);
  if(G_OBJECT_CLASS(parent_class)->dispose) {
    (G_OBJECT_CLASS(parent_class)->dispose)(object);
  }
}

static void bt_plainfile_settings_finalize(GObject *object) {
  BtPlainfileSettings *self = BT_PLAINFILE_SETTINGS(object);

  GST_DEBUG("!!!! self=%p",self);

  g_free(self->private);
  if(G_OBJECT_CLASS(parent_class)->finalize) {
    (G_OBJECT_CLASS(parent_class)->finalize)(object);
  }
}

static void bt_plainfile_settings_init(GTypeInstance *instance, gpointer g_class) {
  BtPlainfileSettings *self = BT_PLAINFILE_SETTINGS(instance);
  self->private = g_new0(BtPlainfileSettingsPrivate,1);
  self->private->dispose_has_run = FALSE;
}

static void bt_plainfile_settings_class_init(BtPlainfileSettingsClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  parent_class=g_type_class_ref(BT_TYPE_SETTINGS);

  gobject_class->set_property = bt_plainfile_settings_set_property;
  gobject_class->get_property = bt_plainfile_settings_get_property;
  gobject_class->dispose      = bt_plainfile_settings_dispose;
  gobject_class->finalize     = bt_plainfile_settings_finalize;
}

GType bt_plainfile_settings_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtPlainfileSettingsClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_plainfile_settings_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtPlainfileSettings),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_plainfile_settings_init, // instance_init
    };
		type = g_type_register_static(BT_TYPE_SETTINGS,"BtPlainfileSettings",&info,0);
  }
  return type;
}

