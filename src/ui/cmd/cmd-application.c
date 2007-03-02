/* $Id: cmd-application.c,v 1.89 2007-03-02 13:17:14 ensonic Exp $
 *
 * Buzztard
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
/**
 * SECTION:btcmdapplication
 * @short_description: class for a commandline based buzztard tool application
 *
 * This class implements the body of the buzztard commandline tool.
 * It provides application level function like play, convert and encode songs.
 */ 
/* @todo: shouldn't we start a mainloop, then launch
 * bt_cmd_application_play_song() in an idle callback, hookup the state-change
 * and the eos to quit the mainloop and run a timer for the play position prints
 * (if not in quite mode)
 */
#define BT_CMD
#define BT_CMD_APPLICATION_C

#include "bt-cmd.h"
#include <libbtcore/application-private.h>

// this needs to be here because of gtk-doc and unit-tests
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

//-- signal ids

//-- property ids

enum {
  CMD_APP_QUIET=1
};

struct _BtCmdApplicationPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  
  /* do no output on stdout */
  gboolean quiet;
};

static BtApplicationClass *parent_class=NULL;

static gboolean is_playing=FALSE;

//-- helper methods

/*
 * on_song_is_playing_notify:
 *
 * playback status signal callback function
 */
static void on_song_is_playing_notify(const BtSong *song, GParamSpec *arg, gpointer user_data) {  
  g_object_get(G_OBJECT(song),"is-playing",&is_playing,NULL);
  GST_INFO("%s playing - invoked per signal : song=%p, user_data=%p",
    (is_playing?"started":"stopped"),song,user_data);
}

/*
 * bt_cmd_application_play_song:
 *
 * start playback, used by play and record
 */
static gboolean bt_cmd_application_play_song(const BtCmdApplication *self,const BtSong *song) {
  gboolean res=FALSE;
  BtSequence *sequence=NULL;
  gulong msec,sec,min;
  gulong length,pos=0;

  // DEBUG
  //bt_song_write_to_highlevel_dot_file(song);
  // DEBUG

  g_object_get(G_OBJECT(song),"sequence",&sequence,NULL);
  g_object_get(G_OBJECT(sequence),"length",&length,NULL);

  // connection play and stop signals
  g_signal_connect(G_OBJECT(song), "notify::is-playing", G_CALLBACK(on_song_is_playing_notify), (gpointer)self);
  if(bt_song_play(song)) {
    GstClockTime bar_time=bt_sequence_get_bar_time(sequence);
    GST_INFO("playing is starting, is_playing=%d",is_playing);
    while(!is_playing) {
      while(g_main_context_pending(NULL)) g_main_context_iteration(NULL,FALSE);
      g_usleep(100);
    }
    GST_INFO("playing has started, is_playing=%d",is_playing);
    while(is_playing && (pos<length)) {
      bt_song_update_playback_position(song);
      g_object_get(G_OBJECT(song),"play-pos",&pos,NULL);

      if(!self->priv->quiet) {
        // get song->play-pos and print progress
        msec=(gulong)((pos*bar_time)/G_USEC_PER_SEC);
        min=(gulong)(msec/60000);msec-=(min*60000);
        sec=(gulong)(msec/ 1000);msec-=(sec* 1000);
        printf("\r%02lu:%02lu.%03lu",min,sec,msec);fflush(stdout);
      }
      while(g_main_context_pending(NULL)) g_main_context_iteration(NULL,FALSE);
      g_usleep(100);
    }
    GST_INFO("finished playing: is_playing=%d, pos=%ld < length=%d",is_playing,pos,length);
    if(!self->priv->quiet) puts("");
    /*
    GMainLoop *main_loop; // make global
    main_loop=g_main_loop_new(NULL,FALSE);
    g_timeout_add(1000,bt_song_play_pos_changed,song); // ggf. main-loop beenden
    g_main_loop_run(main_loop);
    */
    res=TRUE;
  }
  else {
    GST_ERROR("could not play song");
    goto Error;
  }
  is_playing=FALSE;
Error:
  g_object_unref(sequence);
  return(res);
}

/*
 * bt_cmd_application_prepare_encoding:
 *
 * switch master to record mode
 */
static gboolean bt_cmd_application_prepare_encoding(const BtCmdApplication *self,const BtSong *song, const gchar *output_file_name) {
  gboolean ret=FALSE;
  BtSetup *setup;
  BtMachine *machine;
  BtSinkBin *sink_bin;
  BtSinkBinRecordFormat format;
  gchar *lc_file_name,*file_name=NULL;
  
  g_object_get(G_OBJECT(song),"setup",&setup,NULL);
  
  lc_file_name=g_ascii_strdown(output_file_name,-1);
  if(g_str_has_suffix(lc_file_name,".ogg")) {
    format=BT_SINK_BIN_RECORD_FORMAT_OGG_VORBIS;
  }
  else if(g_str_has_suffix(lc_file_name,".mp3")) {
    format=BT_SINK_BIN_RECORD_FORMAT_MP3;
  }
  else if(g_str_has_suffix(lc_file_name,".wav")) {
    format=BT_SINK_BIN_RECORD_FORMAT_WAV;
  }
  else if(g_str_has_suffix(lc_file_name,".flac")) {
    format=BT_SINK_BIN_RECORD_FORMAT_OGG_FLAC;
  }
  else if(g_str_has_suffix(lc_file_name,".raw")) {
    format=BT_SINK_BIN_RECORD_FORMAT_RAW;
  }
  else {
    GST_WARNING("unknown file-format extension, using ogg vorbis");
    format=BT_SINK_BIN_RECORD_FORMAT_OGG_VORBIS;
    file_name=g_strdup_printf("%s.ogg",output_file_name);
  }
  g_free(lc_file_name);
  
  // lookup the audio-sink machine and change mode
  if((machine=bt_setup_get_machine_by_type(setup,BT_TYPE_SINK_MACHINE))) {
    g_object_get(G_OBJECT(machine),"machine",&sink_bin,NULL);

    // @todo eventually have a method for the sink bin to only update once after the changes
    g_object_set(sink_bin,
      "mode",BT_SINK_BIN_MODE_RECORD,
      "record-format",format,
      "record-file-name",(file_name?file_name:output_file_name),
      NULL);
    ret=TRUE;
    
    g_free(file_name);
    gst_object_unref(sink_bin);
    g_object_unref(machine);
  }
  g_object_unref(setup);
  return(ret);
}

//-- constructor methods

/**
 * bt_cmd_application_new:
 * @quiet: do not output on stdout
 *
 * Create a new instance
 *
 * Returns: the new instance or %NULL in case of an error
 */
BtCmdApplication *bt_cmd_application_new(gboolean quiet) {
  BtCmdApplication *self;
  
  if(!(self=BT_CMD_APPLICATION(g_object_new(BT_TYPE_CMD_APPLICATION,"quiet",quiet,NULL)))) {
    goto Error;
  }  
  if(!(bt_application_new(BT_APPLICATION(self)))) {
    goto Error;
  }
  GST_INFO("new cmd app created");
  return(self);
Error:
  g_object_try_unref(self);
  return(NULL);
}

//-- methods

/**
 * bt_cmd_application_play:
 * @self: the application instance to run
 * @input_file_name: the file to play
 *
 * load and play the file of the supplied name
 *
 * Returns: %TRUE for success
 */
gboolean bt_cmd_application_play(const BtCmdApplication *self, const gchar *input_file_name) {
  gboolean res=FALSE;
  BtSong *song=NULL;
  BtSongIO *loader=NULL;

  g_return_val_if_fail(BT_IS_CMD_APPLICATION(self),FALSE);
  g_return_val_if_fail(BT_IS_STRING(input_file_name),FALSE);
  
  GST_INFO("application.play launched");
  
  // prepare song and song-io
  if(!(song=bt_song_new(BT_APPLICATION(self)))) {
    goto Error;
  }
  if(!(loader=bt_song_io_new(input_file_name))) {
    goto Error;
  }
  
  GST_INFO("objects initialized");
  
  if(bt_song_io_load(loader,song)) {   
    if(bt_cmd_application_play_song(self,song)) {
      res=TRUE;
    }
    else {
      GST_ERROR("could not play song \"%s\"",input_file_name);
      goto Error;
    }
  }
  else {
    GST_ERROR("could not load song \"%s\"",input_file_name);
    goto Error;
  }
Error:
  g_object_try_unref(song);
  g_object_try_unref(loader);
  return(res);
}

/**
 * bt_cmd_application_info:
 * @self: the application instance to run
 * @input_file_name: the file to print information about
 * @output_file_name: the file to put informations from the input_file_name. 
 * If the given file_name is NULL, stdout is used to print the informations.
 *
 * load the file of the supplied name and print information about it to stdout.
 *
 * Returns: %TRUE for success
 */
gboolean bt_cmd_application_info(const BtCmdApplication *self, const gchar *input_file_name, const gchar *output_file_name) {
  gboolean res=FALSE;
  BtSong *song=NULL;
  BtSongIO *loader=NULL;
  FILE *output_file=NULL;

  g_return_val_if_fail(BT_IS_CMD_APPLICATION(self),FALSE);
  g_return_val_if_fail(BT_IS_STRING(input_file_name),FALSE);

  GST_INFO("application.info launched");

  // choose appropriate output
  if (!BT_IS_STRING(output_file_name)) {
    output_file=stdout; 
  } else {
    output_file = fopen(output_file_name,"wb");
  }
  // prepare song and song-io
  if(!(song=bt_song_new(BT_APPLICATION(self)))) {
    goto Error;
  }
  if(!(loader=bt_song_io_new(input_file_name))) {
    goto Error;
  }
  
  GST_INFO("objects initialized");
  
  if(bt_song_io_load(loader,song)) {
    BtSongInfo *song_info;
    BtSequence *sequence;
    BtSetup *setup;
    BtWavetable *wavetable;
    BtMachine *machine;
    gchar *name,*info,*author,*genre,*id,*create_dts,*change_dts;
    gulong beats_per_minute,ticks_per_beat;
    gulong length,tracks,n_patterns=0;
    gboolean loop;
    glong loop_start,loop_end;
    GList *machines,*wires,*patterns,*waves,*node;
    GList *missing_machines,*missing_waves;
    GstBin *bin;
    gulong msec,sec,min;

    //DEBUG
    //bt_song_write_to_highlevel_dot_file(song);
    //DEBUG

    g_object_get(G_OBJECT(song),"song-info",&song_info,"sequence",&sequence,"setup",&setup,"wavetable",&wavetable,NULL);
    // get missing element info
    g_object_get(G_OBJECT(setup),"missing-machines",&missing_machines,NULL);
    g_object_get(G_OBJECT(wavetable),"missing-waves",&missing_waves,NULL);
    
    // print some info about the song
    g_object_get(G_OBJECT(song_info),
      "name",&name,"author",&author,"genre",&genre,"info",&info,
      "bpm",&beats_per_minute,"tpb",&ticks_per_beat,
      "create-dts",&create_dts,"change-dts",&change_dts,
      NULL);
    g_fprintf(output_file,"song.song_info.name: \"%s\"\n",name);g_free(name);
    g_fprintf(output_file,"song.song_info.author: \"%s\"\n",author);g_free(author);
    g_fprintf(output_file,"song.song_info.genre: \"%s\"\n",genre);g_free(genre);
    g_fprintf(output_file,"song.song_info.info: \"%s\"\n",info);g_free(info);
    g_fprintf(output_file,"song.song_info.bpm: %lu\n",beats_per_minute);
    g_fprintf(output_file,"song.song_info.tpb: %lu\n",ticks_per_beat);
    g_fprintf(output_file,"song.song_info.created: \"%s\"\n",create_dts);g_free(create_dts);
    g_fprintf(output_file,"song.song_info.changed: \"%s\"\n",change_dts);g_free(change_dts);
    // print some info about the sequence
    g_object_get(G_OBJECT(sequence),"length",&length,"tracks",&tracks,"loop",&loop,"loop-start",&loop_start,"loop-end",&loop_end,NULL);
    g_fprintf(output_file,"song.sequence.length: %lu\n",length);
    g_fprintf(output_file,"song.sequence.tracks: %lu\n",tracks);
    g_fprintf(output_file,"song.sequence.loop: %s\n",(loop?"yes":"no"));
    g_fprintf(output_file,"song.sequence.loop-start: %ld\n",loop_start);
    g_fprintf(output_file,"song.sequence.loop-end: %ld\n",loop_end);
    // print playing-time
    msec=(gulong)((length*bt_sequence_get_bar_time(sequence))/G_USEC_PER_SEC);
    min=(gulong)(msec/60000);msec-=(min*60000);
    sec=(gulong)(msec/ 1000);msec-=(sec* 1000);
    g_fprintf(output_file,"song.sequence.playing_time: %02lu:%02lu.%03lu\n",min,sec,msec);

    // print some statistics about the song (number of machines, wires, patterns)
    g_object_get(G_OBJECT(setup),"machines",&machines,"wires",&wires,NULL);
    g_fprintf(output_file,"song.setup.number_of_machines: %u\n",g_list_length(machines));
    g_fprintf(output_file,"song.setup.number_of_wires: %u\n",g_list_length(wires));
    for(node=machines;node;node=g_list_next(node)) {
      g_object_get(G_OBJECT(node->data),"patterns",&patterns,NULL);
      n_patterns+=g_list_length(patterns);
      g_list_free(patterns);
    }
    g_fprintf(output_file,"song.setup.number_of_patterns: %lu\n",n_patterns);
    g_fprintf(output_file,"song.setup.number_of_missing_machines: %u\n",g_list_length(missing_machines));
    for(node=missing_machines;node;node=g_list_next(node)) {
      g_fprintf(output_file,"  %s\n",(gchar *)(node->data));
    }
    g_list_free(machines);
    g_list_free(wires);
    g_object_get(G_OBJECT(wavetable),"waves",&waves,NULL);
    g_fprintf(output_file,"song.wavetable.number_of_waves: %u\n",g_list_length(waves));
    g_fprintf(output_file,"song.wavetable.number_of_missing_waves: %u\n",g_list_length(missing_waves));
    for(node=missing_waves;node;node=g_list_next(node)) {
      g_fprintf(output_file,"  %s\n",(gchar *)(node->data));
    }
    g_list_free(waves);
    g_object_get(G_OBJECT(self),"bin",&bin,NULL);
    g_fprintf(output_file,"app.bin.number_of_elements: %u\n",GST_BIN_NUMCHILDREN(bin));
    gst_object_unref(bin);

    // lookup the audio-sink machine and print some info about it
    if((machine=bt_setup_get_machine_by_type(setup,BT_TYPE_SINK_MACHINE))) {
      g_object_get(G_OBJECT(machine),"id",&id,"plugin_name",&name,NULL);
      g_fprintf(output_file,"machine.id: \"%s\"\n",id);g_free(id);
      g_fprintf(output_file,"machine.plugin_name: \"%s\"\n",name);g_free(name);
      g_object_unref(machine);
    }

    // release the references
    g_object_try_unref(song_info);
    g_object_try_unref(sequence);
    g_object_try_unref(setup);
    g_object_try_unref(wavetable);
    res=TRUE;
    GST_INFO("finished succesfully");
  }
  else {
    GST_ERROR("could not load song \"%s\"",input_file_name);
    goto Error;
  }
Error:
  g_object_try_unref(song);
  g_object_try_unref(loader);
  if (BT_IS_STRING(output_file_name)) {
    fclose(output_file);
  }
  return(res);
}

/**
 * bt_cmd_application_convert:
 * @self: the application instance to run
 * @input_file_name: the file to read in
 * @output_file_name: the file to generate
 *
 * Load the file of the supplied name and convert it into a buzztard song file.
 * The type of the input file is automatically determined.
 *
 * Returns: %TRUE for success
 */
gboolean bt_cmd_application_convert(const BtCmdApplication *self, const gchar *input_file_name, const gchar *output_file_name) {
  gboolean res=FALSE;
  BtSong *song=NULL;
  BtSongIO *loader=NULL,*saver=NULL;
  
  g_return_val_if_fail(BT_IS_CMD_APPLICATION(self),FALSE);
  g_return_val_if_fail(BT_IS_STRING(input_file_name),FALSE);
  g_return_val_if_fail(BT_IS_STRING(output_file_name),FALSE);

  // prepare song and song-io
  if(!(song=bt_song_new(BT_APPLICATION(self)))) {
    goto Error;
  }
  if(!(loader=bt_song_io_new(input_file_name))) {
    goto Error;
  }
  if(!(saver=bt_song_io_new(output_file_name))) {
    goto Error;
  }
  
  GST_INFO("objects initialized");
  
  if(bt_song_io_load(loader,song)) {
    if(bt_song_io_save(saver,song)) {
      res=TRUE;
    }
    else {
      GST_ERROR("could not save song \"%s\"",output_file_name);
    }
  }
  else {
    GST_ERROR("could not load song \"%s\"",input_file_name);
  }
Error:
  g_object_try_unref(song);
  g_object_try_unref(loader);
  g_object_try_unref(saver);
  return(res);  
}

/**
 * bt_cmd_application_encode:
 * @self: the application instance to run
 * @input_file_name: the file to read in
 * @output_file_name: the file to generate
 *
 * Load the file of the supplied name and encode it as an audio file.
 * The type of the output file is automatically determined from the filename
 * extension.
 *
 * Returns: %TRUE for success
 */
gboolean bt_cmd_application_encode(const BtCmdApplication *self, const gchar *input_file_name, const gchar *output_file_name) {
  gboolean res=FALSE;
  BtSong *song=NULL;
  BtSongIO *loader=NULL;

  g_return_val_if_fail(BT_IS_CMD_APPLICATION(self),FALSE);
  g_return_val_if_fail(BT_IS_STRING(input_file_name),FALSE);
  g_return_val_if_fail(BT_IS_STRING(output_file_name),FALSE);

  GST_INFO("application.play launched");
  
  // prepare song and song-io
  if(!(song=bt_song_new(BT_APPLICATION(self)))) {
    goto Error;
  }
  if(!(loader=bt_song_io_new(input_file_name))) {
    goto Error;
  }
  
  GST_INFO("objects initialized");
  
  if(bt_song_io_load(loader,song)) {
    if(bt_cmd_application_prepare_encoding(self,song,output_file_name)) {
      if(bt_cmd_application_play_song(self,song)) {
        res=TRUE;
      }
      else {
        GST_ERROR("could not play song \"%s\"",input_file_name);
        goto Error;
      }
    }
    else {
      GST_ERROR("could switch to record mode");
      goto Error;
    }
  }
  else {
    GST_ERROR("could not load song \"%s\"",input_file_name);
    goto Error;
  }
Error:
  g_object_try_unref(song);
  g_object_try_unref(loader);
  return(res);
}

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_cmd_application_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtCmdApplication *self = BT_CMD_APPLICATION(object);
  return_if_disposed();
  switch (property_id) {
    case CMD_APP_QUIET: {
      g_value_set_boolean(value, self->priv->quiet);
    } break;
    default: {
       G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_cmd_application_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtCmdApplication *self = BT_CMD_APPLICATION(object);
  return_if_disposed();
  switch (property_id) {
    case CMD_APP_QUIET: {
      self->priv->quiet = g_value_get_boolean(value);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_cmd_application_dispose(GObject *object) {
  BtCmdApplication *self = BT_CMD_APPLICATION(object);

  return_if_disposed();
  self->priv->dispose_has_run = TRUE;
  
  GST_DEBUG("!!!! self=%p",self);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void bt_cmd_application_finalize(GObject *object) {
  BtCmdApplication *self = BT_CMD_APPLICATION(object);

  GST_DEBUG("!!!! self=%p",self);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void bt_cmd_application_init(GTypeInstance *instance, gpointer g_class) {
  BtCmdApplication *self = BT_CMD_APPLICATION(instance);
  
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, BT_TYPE_CMD_APPLICATION, BtCmdApplicationPrivate);
}

static void bt_cmd_application_class_init(BtCmdApplicationClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  parent_class=g_type_class_peek_parent(klass);
  g_type_class_add_private(klass,sizeof(BtCmdApplicationPrivate));
  
  gobject_class->set_property = bt_cmd_application_set_property;
  gobject_class->get_property = bt_cmd_application_get_property;
  gobject_class->dispose      = bt_cmd_application_dispose;
  gobject_class->finalize     = bt_cmd_application_finalize;

  g_object_class_install_property(gobject_class,CMD_APP_QUIET,
                                  g_param_spec_boolean("quiet",
                                     "quiet prop",
                                     "tell wheter the app should do output or not",
                                     FALSE,
                                     G_PARAM_READWRITE));

}

GType bt_cmd_application_get_type(void) {
  static GType type = 0;
  if (G_UNLIKELY(type == 0)) {
    const GTypeInfo info = {
      G_STRUCT_SIZE(BtCmdApplicationClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_cmd_application_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      G_STRUCT_SIZE(BtCmdApplication),
      0,   // n_preallocs
      (GInstanceInitFunc)bt_cmd_application_init, // instance_init
      NULL // value_table
    };
    type = g_type_register_static(BT_TYPE_APPLICATION,"BtCmdApplication",&info,0);
  }
  return type;
}
