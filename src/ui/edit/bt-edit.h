/* $Id: bt-edit.h,v 1.5 2004-08-11 15:50:04 ensonic Exp $
 */

#ifndef BT_EDIT_H
#define BT_EDIT_H

#include <stdio.h>

#include <libbtcore/core.h>
//-- gtk+
#include <gtk/gtk.h>

#include "main-menu-methods.h"
#include "main-window-methods.h"
#include "edit-application-methods.h"

//-- misc
#ifndef GST_CAT_DEFAULT
  #define GST_CAT_DEFAULT bt_edit_debug
#endif
#if defined(BT_EDIT) && !defined(BT_EDIT_APPLICATION_C)
	GST_DEBUG_CATEGORY_EXTERN(GST_CAT_DEFAULT);
#endif

#endif // BT_EDIT_H
