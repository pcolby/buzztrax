/* Buzztard
 * Copyright (C) 2006 Buzztard team <buzztard-devel@lists.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "m-bt-edit.h"

//-- globals

//-- fixtures

static void
test_setup (void)
{
  bt_edit_setup ();
}

static void
test_teardown (void)
{
  bt_edit_teardown ();
}

//-- tests

// create app and then unconditionally destroy window
static void
test_create_dialog (BT_TEST_ARGS)
{
  BT_TEST_START;
  BtEditApplication *app;
  GError *err = NULL;
  BtMainWindow *main_window;
  BtSong *song;
  BtMachine *machine;
  GtkWidget *dialog;

  app = bt_edit_application_new ();
  GST_INFO ("back in test app=%p, app->ref_ct=%d", app,
      G_OBJECT_REF_COUNT (app));
  fail_unless (app != NULL, NULL);

  // create a new song
  bt_edit_application_new_song (app);

  // get window and close it
  g_object_get (app, "main-window", &main_window, "song", &song, NULL);
  fail_unless (main_window != NULL, NULL);
  fail_unless (song != NULL, NULL);

  // create a source machine
  machine =
      BT_MACHINE (bt_source_machine_new (song, "synth",
          "buzztard-test-mono-source", 0, &err));
  fail_unless (machine != NULL, NULL);
  fail_unless (err == NULL, NULL);

  // create, show and destroy dialog
  dialog = GTK_WIDGET (bt_machine_rename_dialog_new (machine));
  fail_unless (dialog != NULL, NULL);
  gtk_widget_show_all (dialog);
  // leave out that line! (modal dialog)
  //gtk_dialog_run(GTK_DIALOG(dialog));

  // make screenshot
  check_make_widget_screenshot (GTK_WIDGET (dialog), NULL);

  gtk_widget_destroy (dialog);

  // close window
  gtk_widget_destroy (GTK_WIDGET (main_window));
  while (gtk_events_pending ())
    gtk_main_iteration ();

  // free application
  GST_INFO ("app->ref_ct=%d", G_OBJECT_REF_COUNT (app));
  g_object_unref (machine);
  g_object_unref (song);
  g_object_checked_unref (app);
  BT_TEST_END;
}

TCase *
bt_machine_rename_dialog_example_case (void)
{
  TCase *tc = tcase_create ("BtMachineRenameDialogExamples");

  tcase_add_test (tc, test_create_dialog);
  // we *must* use a checked fixture, as only this runs in the same context
  tcase_add_checked_fixture (tc, test_setup, test_teardown);
  return (tc);
}