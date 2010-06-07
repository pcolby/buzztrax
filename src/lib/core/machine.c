/* $Id$
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
 * SECTION:btmachine
 * @short_description: base class for signal processing machines
 *
 * The machine class cares about inserting additional low-level elemnts to do
 * signal conversion etc.. Further it provides general facillities like
 * input/output level monitoring.
 *
 * A machine can have several #GstElements:
 * <variablelist>
 *  <varlistentry>
 *    <term>adder:</term>
 *    <listitem><simpara>mixes all incoming signals</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>input volume:</term>
 *    <listitem><simpara>gain for incoming signals</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>input pre/post-gain level:</term>
 *    <listitem><simpara>level meter for incoming signal</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>machine:</term>
 *    <listitem><simpara>the real machine</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>output volume:</term>
 *    <listitem><simpara>gain for outgoing signal</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>output pre/post-gain level:</term>
 *    <listitem><simpara>level meter for outgoing signal</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>spreader:</term>
 *    <listitem><simpara>distibutes signal to outgoing connections</simpara></listitem>
 *  </varlistentry>
 * </variablelist>
 * The adder and spreader elements are activated depending on element type..
 * The volume controls and level meters are activated as requested via the API.
 * It is recommended to only activate them, when needed. The instances are cached
 * after deactivation (so that they can be easily reactivated) and destroyed with
 * the #BtMachine object.
 *
 * Furthermore the machine handles a list of #BtPattern instances. These contain
 * event patterns that form a #BtSequence.
 */
/* @todo: we need BtParameterGroup object with an implementation for the
 * global and one for the voice parameters. Then the machine would have a
 * self->priv->global_params and self->priv->voice_params
 * bt_machine_init_global_params()
 * -> bt_parameter_group_new(self->priv->machines[PART_MACHINE],"global",num_params,GParamSpec **properties)
 * bt_machine_init_voice_params()
 * -> bt_parameter_group_new(voice_child,"voice 0",num_params,GParamSpec **properties)
 *
 * Do we want one ParameterGroup per voice or just one for all voices?
 * bt_machine_set_voice_param_value() and bt_machine_voice_controller_change_value() are voice specific
 *
 * @todo: API cleanup
 * - need coherent api to create machine parts
 *   - export the enum ?
 *
 * @todo: cache the GstControlSource objects?
 * - we look them up a lot, its a linear search in a list, locking and ref/unref
 * - one for each param and again each voice
 *
 * @todo: we could determine the pad-names for parts and use gst_element_{link,unlink}_pads
 * - we get a little speedup if the pad-names are known
 * - passing NULL for a pad-name is ok
 */

#define BT_CORE
#define BT_MACHINE_C

#include "core_private.h"
#include <libbuzztard-ic/ic.h>

//-- signal ids

enum {
  PATTERN_ADDED_EVENT,
  PATTERN_REMOVED_EVENT,
  LAST_SIGNAL
};

//-- property ids

enum {
  MACHINE_CONSTRUCTION_ERROR=1,
  MACHINE_PROPERTIES,
  MACHINE_SONG,
  MACHINE_ID,
  MACHINE_PLUGIN_NAME,
  MACHINE_VOICES,
  MACHINE_GLOBAL_PARAMS,
  MACHINE_VOICE_PARAMS,
  MACHINE_MACHINE,
  MACHINE_ADDER_CONVERT,
  MACHINE_INPUT_PRE_LEVEL,
  MACHINE_INPUT_GAIN,
  MACHINE_INPUT_POST_LEVEL,
  MACHINE_OUTPUT_PRE_LEVEL,
  MACHINE_OUTPUT_GAIN,
  MACHINE_OUTPUT_POST_LEVEL,
  MACHINE_PATTERNS,
  MACHINE_STATE
};

// adder, capsfiter, level, volume are gap-aware
typedef enum {
  /* utillity elements to allow multiple inputs */
  PART_ADDER=0,
  /* helper to enforce common format for adder inputs */
  PART_CAPS_FILTER,
  /* helper to make adder link to next element */
  PART_ADDER_CONVERT,
  /* the elements to control and analyse the current input signal */
  PART_INPUT_PRE_LEVEL,
  PART_INPUT_GAIN,
  PART_INPUT_POST_LEVEL,
  /* the gstreamer element that produces/processes the signal */
  PART_MACHINE,
  /* the elements to control and analyse the current output signal */
  PART_OUTPUT_PRE_LEVEL,
  PART_OUTPUT_GAIN,
  PART_OUTPUT_POST_LEVEL,
  /* utillity elements to allow multiple outputs */
  PART_SPREADER,
  /* how many elements are used */
  PART_COUNT
} BtMachinePart;

struct _BtMachinePrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  /* used to signal failed instance creation */
  GError **constrution_error;

  /* (ui) properties accociated with this machine */
  GHashTable *properties;

  /* the song the machine belongs to */
  G_POINTER_ALIAS(BtSong *,song);

  /* status in songs pipeline */
  gboolean is_added,is_connected;

  /* the id, we can use to lookup the machine */
  gchar *id;
  /* the name of the gst-plugin the machine is using */
  gchar *plugin_name;

  /* the number of voices the machine provides */
  gulong voices;
  /* the number of dynamic params the machine provides per instance */
  gulong global_params;
  /* the number of dynamic params the machine provides per instance and voice */
  gulong voice_params;

  /* the current state of the machine */
  BtMachineState state;

  /* dynamic parameter control */
  GstController *global_controller;
  GstController **voice_controllers;
  GstInterpolationControlSource **global_control_sources;
  GstInterpolationControlSource **voice_control_sources;
  GParamSpec **global_props,**voice_props;
  guint *global_flags,*voice_flags;
  GValue *global_no_val,*voice_no_val;
  GQuark *global_quarks,*voice_quarks;

  /* event patterns */
  GList *patterns;  // each entry points to BtPattern
  guint private_patterns;

  /* the gstreamer elements that are used */
  GstElement *machines[PART_COUNT];
  GstPad *src_pads[PART_COUNT],*sink_pads[PART_COUNT];

  /* caps filter format */
  gint format; /* 0=int/1=float */
  gint channels;
  gint width;
  gint depth;

  /* realtime control (bt-ic) */
  GHashTable *control_data; // each entry points to BtMachineData

  /* src/sink ghost-pad counters for the machine */
  gint src_pad_counter, sink_pad_counter;
};

typedef struct {
  const BtIcControl *control;
  GstObject *object;
  GParamSpec *pspec;
  gulong handler_id;
} BtControlData;

static GQuark error_domain=0;

static GObjectClass *parent_class=NULL;

static guint signals[LAST_SIGNAL]={0,};

static gchar *src_pn[]={
  "src",  /* adder */
  "src",  /* caps filter */
  "src",  /* audioconvert */
  "src",  /* input pre level */
  "src",  /* input gain */
  "src",  /* input post level */
  "src",  /* machine */
  "src",  /* output pre level */
  "src",  /* output gain */
  "src",  /* output post level */
  NULL /* tee */
};
static gchar *sink_pn[]={
  NULL, /* adder */
  "sink",   /* caps filter */
  "sink",   /* audioconvert */
  "sink",   /* input pre level */
  "sink",   /* input gain */
  "sink",   /* input post level */
  "sink",   /* machine */
  "sink",   /* output pre level */
  "sink",   /* output gain */
  "sink",   /* output post level */
  "sink"    /* tee */
};

// macros

#define GLOBAL_PARAM_NAME(ix) self->priv->global_props[ix]->name
#define GLOBAL_PARAM_TYPE(ix) self->priv->global_props[ix]->value_type
#define VOICE_PARAM_NAME(ix) self->priv->voice_props[ix]->name
#define VOICE_PARAM_TYPE(ix) self->priv->voice_props[ix]->value_type

//-- enums

GType bt_machine_state_get_type(void) {
  static GType type = 0;
  if(G_UNLIKELY(type == 0)) {
    static const GEnumValue values[] = {
      { BT_MACHINE_STATE_NORMAL,"BT_MACHINE_STATE_NORMAL","normal" },
      { BT_MACHINE_STATE_MUTE,  "BT_MACHINE_STATE_MUTE",  "mute" },
      { BT_MACHINE_STATE_SOLO,  "BT_MACHINE_STATE_SOLO",  "solo" },
      { BT_MACHINE_STATE_BYPASS,"BT_MACHINE_STATE_BYPASS","bypass" },
      { 0, NULL, NULL},
    };
    type = g_enum_register_static("BtMachineState", values);
  }
  return type;
}

//-- signal handler

void bt_machine_on_bpm_changed(BtSongInfo * const song_info, const GParamSpec * const arg, gconstpointer const user_data) {
  const BtMachine * const self=BT_MACHINE(user_data);
  gulong bpm;

  g_object_get(song_info,"bpm",&bpm,NULL);
  gstbt_tempo_change_tempo(GSTBT_TEMPO(self->priv->machines[PART_MACHINE]),(glong)bpm,-1,-1);
}

void bt_machine_on_tpb_changed(BtSongInfo * const song_info, const GParamSpec * const arg, gconstpointer const user_data) {
  const BtMachine * const self=BT_MACHINE(user_data);
  gulong tpb;

  g_object_get(song_info,"tpb",&tpb,NULL);
  gstbt_tempo_change_tempo(GSTBT_TEMPO(self->priv->machines[PART_MACHINE]),-1,(glong)tpb,-1);
}

//-- helper methods

/*
 * mute the machine output
 */
static gboolean bt_machine_set_mute(const BtMachine * const self,const BtSetup * const setup) {
  const BtMachinePart part=BT_IS_SINK_MACHINE(self)?PART_INPUT_GAIN:PART_OUTPUT_GAIN;

  //if(self->priv->state==BT_MACHINE_STATE_MUTE) return(TRUE);

  if(self->priv->machines[part]) {
    g_object_set(self->priv->machines[part],"mute",TRUE,NULL);
    return(TRUE);
  }
  GST_WARNING_OBJECT(self,"can't mute element '%s'",self->priv->id);
  return(FALSE);
}

/*
 * unmute the machine output
 */
static gboolean bt_machine_unset_mute(const BtMachine *const self, const BtSetup * const setup) {
  const BtMachinePart part=BT_IS_SINK_MACHINE(self)?PART_INPUT_GAIN:PART_OUTPUT_GAIN;

  //if(self->priv->state!=BT_MACHINE_STATE_MUTE) return(TRUE);

  if(self->priv->machines[part]) {
    g_object_set(self->priv->machines[part],"mute",FALSE,NULL);
    return(TRUE);
  }
  GST_WARNING_OBJECT(self,"can't unmute element '%s'",self->priv->id);
  return(FALSE);
}

/*
 * bt_machine_change_state:
 *
 * Reset old state and go to new state. Do sanity checking of allowed states for
 * given machine.
 *
 * Returns: %TRUE for success
 */
static gboolean bt_machine_change_state(const BtMachine * const self, const BtMachineState new_state) {
  gboolean res=TRUE;
  BtSetup *setup;

  // reject a few nonsense changes
  if((new_state==BT_MACHINE_STATE_BYPASS) && (!BT_IS_PROCESSOR_MACHINE(self))) return(FALSE);
  if((new_state==BT_MACHINE_STATE_SOLO) && (BT_IS_SINK_MACHINE(self))) return(FALSE);
  if(self->priv->state==new_state) return(TRUE);

  g_object_get(self->priv->song,"setup",&setup,NULL);

  GST_INFO("state change for element '%s'",self->priv->id);

  // return to normal state
  switch(self->priv->state) {
    case BT_MACHINE_STATE_MUTE:  { // source, processor, sink
      if(!bt_machine_unset_mute(self,setup)) res=FALSE;
    } break;
    case BT_MACHINE_STATE_SOLO:  { // source
      GList *node,*machines=bt_setup_get_machines_by_type(setup,BT_TYPE_SOURCE_MACHINE);
      BtMachine *machine;
      // set all but this machine to playing again
      for(node=machines;node;node=g_list_next(node)) {
        machine=BT_MACHINE(node->data);
        if(machine!=self) {
          if(!bt_machine_unset_mute(machine,setup)) res=FALSE;
        }
        g_object_unref(machine);
      }
      GST_INFO("unmuted %d elements with result = %d",g_list_length(machines),res);
      g_list_free(machines);
    } break;
    case BT_MACHINE_STATE_BYPASS:  { // processor
      const GstElement * const element=self->priv->machines[PART_MACHINE];
      if(GST_IS_BASE_TRANSFORM(element)) {
        gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(element),FALSE);
      }
      else {
        // @todo: disconnect its source and sink + set this machine to playing
        GST_INFO("element does not support passthrough");
      }
    } break;
    case BT_MACHINE_STATE_NORMAL:
      //g_return_val_if_reached(FALSE);
      break;
    default:
      GST_WARNING_OBJECT(self,"invalid old machine state: %d",self->priv->state);
      break;
  }
  // set to new state
  switch(new_state) {
    case BT_MACHINE_STATE_MUTE:  { // source, processor, sink
      if(!bt_machine_set_mute(self,setup)) res=FALSE;
    } break;
    case BT_MACHINE_STATE_SOLO:  { // source
      GList *node,*machines=bt_setup_get_machines_by_type(setup,BT_TYPE_SOURCE_MACHINE);
      BtMachine *machine;
      // set all but this machine to paused
      for(node=machines;node;node=g_list_next(node)) {
        machine=BT_MACHINE(node->data);
        if(machine!=self) {
          // if a different machine is solo, set it to normal and unmute the current source
          if(machine->priv->state==BT_MACHINE_STATE_SOLO) {
            machine->priv->state=BT_MACHINE_STATE_NORMAL;
            g_object_notify(G_OBJECT(machine),"state");
            if(!bt_machine_unset_mute(self,setup)) res=FALSE;
          }
          if(!bt_machine_set_mute(machine,setup)) res=FALSE;
        }
        g_object_unref(machine);
      }
      GST_INFO("muted %d elements with result = %d",g_list_length(machines),res);
      g_list_free(machines);
    } break;
    case BT_MACHINE_STATE_BYPASS:  { // processor
      const GstElement *element=self->priv->machines[PART_MACHINE];
      if(GST_IS_BASE_TRANSFORM(element)) {
        gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(element),TRUE);
      }
      else {
        // @todo set this machine to paused + connect its source and sink
        GST_INFO("element does not support passthrough");
      }
    } break;
    case BT_MACHINE_STATE_NORMAL:
      //g_return_val_if_reached(FALSE);
      break;
    default:
      GST_WARNING_OBJECT(self,"invalid new machine state: %d",new_state);
      break;
  }
  self->priv->state=new_state;

  g_object_unref(setup);
  return(res);
}

/*
 * bt_machine_link_elements:
 * @self: the machine in which we link
 * @src,@sink: the pads
 *
 * Link two pads.
 *
 * Returns: %TRUE for sucess
 */
static gboolean bt_machine_link_elements(const BtMachine * const self, GstPad *src, GstPad *sink) {
  GstPadLinkReturn plr;
  
  if((plr=gst_pad_link(src,sink))!=GST_PAD_LINK_OK) {
    GST_WARNING_OBJECT(self,"can't link %s:%s with %s:%s: %d",GST_DEBUG_PAD_NAME(src),GST_DEBUG_PAD_NAME(sink),plr);
    return(FALSE);
  }
  return(TRUE);
}

/*
 * bt_machine_insert_element:
 * @self: the machine for which the element should be inserted
 * @peer: the peer pad element
 * @pos: the element at this position should be inserted (activated)
 *
 * Searches surrounding elements of the new element for active peers
 * and connects them. The new elemnt needs to be created before calling this method.
 *
 * Returns: %TRUE for sucess
 */
static gboolean bt_machine_insert_element(BtMachine *const self, GstPad * const peer, const BtMachinePart pos) {
  gboolean res=FALSE;
  gint i,pre,post;
  BtWire *wire;
  GstElement ** const machines=self->priv->machines;
  GstPad ** const src_pads=self->priv->src_pads;
  GstPad ** const sink_pads=self->priv->sink_pads;

  // look for elements before and after pos
  pre=post=-1;
  for(i=pos-1;i>-1;i--) {
    if(machines[i]) {
      pre=i;break;
    }
  }
  for(i=pos+1;i<PART_COUNT;i++) {
    if(machines[i]) {
      post=i;break;
    }
  }
  GST_INFO("positions: %d ... %d(%s) ... %d",pre,pos,GST_OBJECT_NAME(machines[pos]),post);
  // get pads
  if((pre!=-1) && (post!=-1)) {
    // unlink old connection
    gst_pad_unlink(src_pads[pre],sink_pads[post]);
    // link new connection
    res=bt_machine_link_elements(self,src_pads[pre],sink_pads[pos]);
    res&=bt_machine_link_elements(self,src_pads[pos],sink_pads[post]);
    if(!res) {
      gst_pad_unlink(src_pads[pre],sink_pads[pos]);
      gst_pad_unlink(src_pads[pos],sink_pads[post]);
      GST_WARNING_OBJECT(self,"failed to link part '%s' inbetween '%s' and '%s'",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[pre]),GST_OBJECT_NAME(machines[post]));
      // relink previous connection
      bt_machine_link_elements(self,src_pads[pre],sink_pads[post]);
    }
  }
  else if(pre==-1) {
    // unlink old connection
    gst_pad_unlink(peer,sink_pads[post]);
    // link new connection
    res=bt_machine_link_elements(self,peer,sink_pads[pos]);
    res&=bt_machine_link_elements(self,src_pads[pos],sink_pads[post]);
    if(!res) {
      gst_pad_unlink(peer,sink_pads[pos]);
      gst_pad_unlink(src_pads[pos],sink_pads[post]);
      GST_WARNING_OBJECT(self,"failed to link part '%s' before '%s'",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[post]));
      // try to re-wire
      if((res=bt_machine_link_elements(self,src_pads[pos],sink_pads[post]))) {
        if((wire=(self->dst_wires?(BtWire*)(self->dst_wires->data):NULL))) {
          if(!(res=bt_wire_reconnect(wire))) {
            GST_WARNING_OBJECT(self,"failed to reconnect wire after linking '%s' before '%s'",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[post]));
          }
        }
      }
      else {
        GST_WARNING_OBJECT(self,"failed to link part '%s' before '%s' again",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[post]));
      }
    }
  }
  else if(post==-1) {
    // unlink old connection
    gst_pad_unlink(src_pads[pre],peer);
    // link new connection
    res=bt_machine_link_elements(self,src_pads[pre],sink_pads[pos]);
    res&=bt_machine_link_elements(self,src_pads[pos],peer);
    if(!res) {
      gst_pad_unlink(src_pads[pre],sink_pads[pos]);
      gst_pad_unlink(src_pads[pos],peer);
      GST_WARNING_OBJECT(self,"failed to link part '%s' after '%s'",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[pre]));
      // try to re-wire
      if((res=bt_machine_link_elements(self,src_pads[pre],sink_pads[pos]))) {
        if((wire=(self->src_wires?(BtWire*)(self->src_wires->data):NULL))) {
          if(!(res=bt_wire_reconnect(wire))) {
            GST_WARNING_OBJECT(self,"failed to reconnect wire after linking '%s' after '%s'",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[pre]));
          }
        }
      }
      else {
        GST_WARNING_OBJECT(self,"failed to link part '%s' after '%s' again",GST_OBJECT_NAME(machines[pos]),GST_OBJECT_NAME(machines[pre]));
      }
    }
  }
  else {
    GST_ERROR_OBJECT(self,"failed to link part '%s' in broken machine",GST_OBJECT_NAME(machines[pos]));
  }
  return(res);
}

/*
 * bt_machine_resize_pattern_voices:
 * @self: the machine which has changed its number of voices
 *
 * Iterates over the machines patterns and adjust their voices too.
 */
static void bt_machine_resize_pattern_voices(const BtMachine * const self) {
  GList* node;

  // reallocate self->priv->patterns->priv->data
  for(node=self->priv->patterns;node;node=g_list_next(node)) {
    g_object_set(BT_PATTERN(node->data),"voices",self->priv->voices,NULL);
  }
}

/*
 * bt_machine_resize_voices:
 * @self: the machine which has changed its number of voices
 *
 * Adjust the private data structure after a change in the number of voices.
 */
static void bt_machine_resize_voices(const BtMachine * const self, const gulong voices) {
  GST_INFO("changing machine %s:%p voices from %ld to %ld",self->priv->id,self->priv->machines[PART_MACHINE],voices,self->priv->voices);

  // @todo GSTBT_IS_CHILD_BIN <-> GST_IS_CHILD_PROXY (sink-bin is a CHILD_PROXY but not a CHILD_BIN)
  if((!self->priv->machines[PART_MACHINE]) || (!GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE]))) {
    GST_WARNING_OBJECT(self,"machine %s:%p is NULL or not polyphonic!",self->priv->id,self->priv->machines[PART_MACHINE]);
    return;
  }

  g_object_set(self->priv->machines[PART_MACHINE],"children",self->priv->voices,NULL);

  if(voices>self->priv->voices) {
    gulong j;

    // release params for old voices
    for(j=self->priv->voices;j<voices;j++) {
      g_object_try_unref(self->priv->voice_controllers[j]);
    }
  }

  self->priv->voice_controllers=(GstController **)g_renew(gpointer,self->priv->voice_controllers,self->priv->voices);
  self->priv->voice_control_sources=(GstInterpolationControlSource **)g_renew(gpointer,self->priv->voice_control_sources,self->priv->voices*self->priv->voice_params);
  if(voices<self->priv->voices) {
    guint j;

    for(j=voices;j<self->priv->voices;j++) {
      self->priv->voice_controllers[j]=NULL;
    }
    for(j=voices*self->priv->voice_params;j<self->priv->voices*self->priv->voice_params;j++) {
      self->priv->voice_control_sources[j]=NULL;
    }
  }
}

/*
 * bt_machine_get_property_meta_value:
 * @value: the value that will hold the result
 * @property: the paramspec object to get the meta data from
 * @key: the meta data key
 *
 * Fetches the meta data from the given paramspec object and sets the value.
 * The values needs to be initialized to the correct type.
 *
 * Returns: %TRUE if it could get the value
 */
static gboolean bt_machine_get_property_meta_value(GValue * const value, GParamSpec * const property, const GQuark key) {
  gboolean res=FALSE;
  gconstpointer const has_meta=g_param_spec_get_qdata(property,gstbt_property_meta_quark);

  if(has_meta) {
    gconstpointer const qdata=g_param_spec_get_qdata(property,key);

    // it can be that qdata is NULL if the value is NULL
    //if(!qdata) {
    //  GST_WARNING_OBJECT(self,"no property metadata for '%s'",property->name);
    //  return(FALSE);
    //}

    res=TRUE;
    g_value_init(value,property->value_type);
    switch(bt_g_type_get_base_type(property->value_type)) {
      case G_TYPE_BOOLEAN:
        // @todo: this does not work, for no_value it results in
        // g_value_set_boolean(value,255);
        // which is the same as TRUE
        g_value_set_boolean(value,GPOINTER_TO_INT(qdata));
        break;
      case G_TYPE_INT:
        g_value_set_int(value,GPOINTER_TO_INT(qdata));
        break;
      case G_TYPE_UINT:
        g_value_set_uint(value,GPOINTER_TO_UINT(qdata));
        break;
      case G_TYPE_ENUM:
        g_value_set_enum(value,GPOINTER_TO_INT(qdata));
        break;
      case G_TYPE_STRING:
        /* what is in qdata for this type? for buzz this is a note, so its an int
        if(qdata) {
          g_value_set_string(value,qdata);
        }
        else {
          g_value_set_static_string(value,"");
        }
        */
        g_value_set_static_string(value,"");
        break;
      default:
        if(qdata) {
          GST_WARNING("unsupported GType for param %s",property->name);
          //GST_WARNING("unsupported GType=%d:'%s'",property->value_type,G_VALUE_TYPE_NAME(property->value_type));
          res=FALSE;
        }
    }
  }
  return(res);
}

/*
 * bt_machine_make_internal_element:
 * @self: the machine
 * @part: which internal element to create
 * @factory_name: the element-factories name
 * @element_name: the name of the new #GstElement instance
 *
 * This helper is used by the family of bt_machine_enable_xxx() functions.
 */
static gboolean bt_machine_make_internal_element(const BtMachine * const self,const BtMachinePart part,const gchar * const factory_name,const gchar * const element_name) {
  gboolean res=FALSE;
  const gchar * const parent_name=GST_OBJECT_NAME(self);
  gchar * const name=g_alloca(strlen(parent_name)+2+strlen(element_name));

  // create internal element
  //strcat(name,parent_name);strcat(name,":");strcat(name,element_name);
  g_sprintf(name,"%s:%s",parent_name,element_name);
  if(!(self->priv->machines[part]=gst_element_factory_make(factory_name,name))) {
    GST_WARNING_OBJECT(self,"failed to create %s from factory %s",element_name,factory_name);goto Error;
  }

  // get the pads
  if(src_pn[part])
    self->priv->src_pads[part]=gst_element_get_static_pad(self->priv->machines[part],src_pn[part]);
  if(sink_pn[part])
    self->priv->sink_pads[part]=gst_element_get_static_pad(self->priv->machines[part],sink_pn[part]);

  gst_bin_add(GST_BIN(self),self->priv->machines[part]);
  res=TRUE;
Error:
  return(res);
}

/*
 * bt_machine_add_input_element:
 * @self: the machine
 * @part: which internal element to link
 *
 * This helper is used by the family of bt_machine_enable_input_xxx() functions.
 */
static gboolean bt_machine_add_input_element(BtMachine * const self,const BtMachinePart part) {
  gboolean res=FALSE;
  GstElement ** const machines=self->priv->machines;
  GstPad ** const src_pads=self->priv->src_pads;
  GstPad ** const sink_pads=self->priv->sink_pads;
  GstPad *peer;
  guint i, tix=PART_MACHINE;

  // get next element on the source side
  for(i=part+1;i<=PART_MACHINE;i++) {
    if(machines[i]) {
      tix=i;
      GST_DEBUG("src side target at %d: %s:%s",i,GST_DEBUG_PAD_NAME(sink_pads[tix]));
      break;
    }
  }

  // is the machine connected towards the input side (its sink)?
  if(!(peer=gst_pad_get_peer(sink_pads[tix]))) {
    GST_DEBUG("target '%s:%s' is not yet connected on the input side",GST_DEBUG_PAD_NAME(sink_pads[tix]));
    if(!bt_machine_link_elements(self,src_pads[part],sink_pads[tix])) {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(self),GST_DEBUG_GRAPH_SHOW_ALL, PACKAGE_NAME "-machine");
      GST_ERROR_OBJECT(self,"failed to link the element '%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));goto Error;
    }
    GST_INFO("sucessfully prepended element '%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));
  }
  else {
    GST_DEBUG("target '%s:%s' has peer pad '%s:%s', need to inseert",GST_DEBUG_PAD_NAME(sink_pads[tix]),GST_DEBUG_PAD_NAME(peer));
    if(!bt_machine_insert_element(self,peer,part)) {
      gst_object_unref(peer);
      GST_ERROR_OBJECT(self,"failed to link the element '%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));goto Error;
    }
    gst_object_unref(peer);
    GST_INFO("sucessfully inserted element'%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));
  }
  res=TRUE;
Error:
  return(res);
}

/*
 * bt_machine_add_output_element:
 * @self: the machine
 * @part: which internal element to link
 *
 * This helper is used by the family of bt_machine_enable_output_xxx() functions.
 */
static gboolean bt_machine_add_output_element(BtMachine * const self,const BtMachinePart part) {
  gboolean res=FALSE;
  GstElement ** const machines=self->priv->machines;
  GstPad ** const src_pads=self->priv->src_pads;
  GstPad ** const sink_pads=self->priv->sink_pads;
  GstPad *peer;
  guint i, tix=PART_MACHINE;
  
  // get next element on the sink side
  for(i=part-1;i>=PART_MACHINE;i--) {
    if(machines[i]) {
      tix=i;
      GST_DEBUG_OBJECT(self,"sink side target at %d: %s:%s",i,GST_DEBUG_PAD_NAME(src_pads[tix]));
      break;
    }
  }

  // is the machine unconnected towards the output side (its source)?
  if(!(peer=gst_pad_get_peer(src_pads[tix]))) {
    GST_DEBUG("target '%s:%s' is not yet connected on the output side",GST_DEBUG_PAD_NAME(src_pads[tix]));
    if(!bt_machine_link_elements(self,src_pads[tix],sink_pads[part])) {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(self),GST_DEBUG_GRAPH_SHOW_ALL, PACKAGE_NAME "-machine");
      GST_ERROR_OBJECT(self,"failed to link the element '%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));goto Error;
    }
    GST_INFO("sucessfully appended element '%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));
  }
  else {
    GST_DEBUG("target '%s:%s' has peer pad '%s:%s', need to inseert",GST_DEBUG_PAD_NAME(src_pads[tix]),GST_DEBUG_PAD_NAME(peer));
    if(!bt_machine_insert_element(self,peer,part)) {
      gst_object_unref(peer);
      GST_ERROR_OBJECT(self,"failed to link the element '%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));goto Error;
    }
    gst_object_unref(peer);
    GST_INFO("sucessfully inserted element'%s' for '%s'",GST_OBJECT_NAME(machines[part]),GST_OBJECT_NAME(machines[PART_MACHINE]));
  }
  res=TRUE;
Error:
  return(res);
}

/* bt_machine_enable_part:
 * @part: which internal element to create
 * @factory_name: the element-factories name
 * @element_name: the name of the new #GstElement instance
 *
 * can replace _enable_{in,out}put_{level,gain}
 * this is not good enough for adder, ev. okay for spreader
 */
static gboolean bt_machine_enable_part(BtMachine * const self,const BtMachinePart part,const gchar * const factory_name,const gchar * const element_name) {
  gboolean res=FALSE;
  
  if(self->priv->machines[part])
    return(TRUE);

  if(!bt_machine_make_internal_element(self,part,factory_name,element_name)) goto Error;
  // configure part
  switch(part) {
    case PART_INPUT_PRE_LEVEL:
    case PART_INPUT_POST_LEVEL:
    case PART_OUTPUT_PRE_LEVEL:
    case PART_OUTPUT_POST_LEVEL:
      g_object_set(self->priv->machines[part],
        "interval",(GstClockTime)(0.1*GST_SECOND),"message",TRUE,
        "peak-ttl",(GstClockTime)(0.2*GST_SECOND),"peak-falloff", 50.0,
        NULL);
      break;
    default:
      break;
  }
  if(part<PART_MACHINE) {
    if(!bt_machine_add_input_element(self,part)) goto Error;
  }
  else {
    if(!bt_machine_add_output_element(self,part)) goto Error;
  }
  res=TRUE;
Error:
  return(res);
}


//-- init helpers

static gboolean bt_machine_init_core_machine(BtMachine * const self) {
  gboolean res=FALSE;

  if(!bt_machine_make_internal_element(self,PART_MACHINE,self->priv->plugin_name,self->priv->id)) goto Error;
  GST_INFO("  instantiated machine %p, \"%s\", machine->ref_count=%d",self->priv->machines[PART_MACHINE],self->priv->plugin_name,G_OBJECT_REF_COUNT(self->priv->machines[PART_MACHINE]));

  res=TRUE;
Error:
  return(res);
}

static void bt_machine_init_interfaces(const BtMachine * const self) {
  /* initialize buzz-host-callbacks (structure with callbacks)
   * buzzmachines can then call c function of the host
   * would be good to set this as early as possible
   */
  if(g_object_class_find_property(G_OBJECT_CLASS(BT_MACHINE_GET_CLASS(self->priv->machines[PART_MACHINE])),"host-callbacks")) {
    extern void *bt_buzz_callbacks_get(BtSong *song);

    g_object_set(self->priv->machines[PART_MACHINE],"host-callbacks",bt_buzz_callbacks_get(self->priv->song),NULL);
    GST_INFO("  host-callbacks iface initialized");
  }
  // initialize child-proxy iface properties
  if(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE])) {
    if(!self->priv->voices) {
      GST_WARNING_OBJECT(self,"voices==0");
      //g_object_get(self->priv->machines[PART_MACHINE],"children",&self->priv->voices,NULL);
    }
    else {
      g_object_set(self->priv->machines[PART_MACHINE],"children",self->priv->voices,NULL);
    }
    GST_INFO("  child proxy iface initialized");
  }
  // initialize tempo iface properties
  if(GSTBT_IS_TEMPO(self->priv->machines[PART_MACHINE])) {
    BtSongInfo *song_info;
    gulong bpm,tpb;

    g_object_get((gpointer)(self->priv->song),"song-info",&song_info,NULL);
    // @todo handle stpb later (subtick per beat)
    g_object_get(song_info,"bpm",&bpm,"tpb",&tpb,NULL);
    gstbt_tempo_change_tempo(GSTBT_TEMPO(self->priv->machines[PART_MACHINE]),(glong)bpm,(glong)tpb,-1);

    g_signal_connect(song_info,"notify::bpm",G_CALLBACK(bt_machine_on_bpm_changed),(gpointer)self);
    g_signal_connect(song_info,"notify::tpb",G_CALLBACK(bt_machine_on_tpb_changed),(gpointer)self);
    g_object_unref(song_info);
    GST_INFO("  tempo iface initialized");
  }
  GST_INFO("machine element instantiated and interfaces initialized");
}

/*
 * bt_machine_check_type:
 *
 * Sanity check if the machine is of the right type. It counts the source,
 * sink pads and check with the machine class-type.
 *
 * Returns: %TRUE if type and pads match
 */
static gboolean bt_machine_check_type(const BtMachine * const self) {
  BtMachineClass *klass=BT_MACHINE_GET_CLASS(self);
  GstIterator *it;
  GstPad *pad;
  gulong pad_src_ct=0,pad_sink_ct=0;
  gboolean done;

  if(!klass->check_type) {
    GST_WARNING_OBJECT(self,"no BtMachine::check_type() implemented");
    return(TRUE);
  }

  // get pad counts per type
  it=gst_element_iterate_pads(self->priv->machines[PART_MACHINE]);
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (it, (gpointer)&pad)) {
      case GST_ITERATOR_OK:
        switch(gst_pad_get_direction(pad)) {
          case GST_PAD_SRC: pad_src_ct++;break;
          case GST_PAD_SINK: pad_sink_ct++;break;
          default:
            GST_INFO("unhandled pad type discovered");
            break;
        }
        gst_object_unref(pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free(it);

  // test pad counts and element type
  if(!((klass->check_type)(self,pad_src_ct,pad_sink_ct))) {
    return(FALSE);
  }
  return(TRUE);
}

static void bt_machine_init_global_params(const BtMachine * const self) {
  GParamSpec **properties;
  guint number_of_properties;

  if((properties=g_object_class_list_properties(G_OBJECT_CLASS(GST_ELEMENT_GET_CLASS(self->priv->machines[PART_MACHINE])),&number_of_properties))) {
    GParamSpec *property;
    GParamSpec **child_properties=NULL;
    //GstController *ctrl;
    guint number_of_child_properties=0;
    guint i,j;
    gboolean skip;

    // check if the elemnt implements the GstBtChildBin interface (implies GstChildProxy)
    if(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE])) {
      GstObject *voice_child;

      //g_assert(gst_child_proxy_get_children_count(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE])));
      // get child for voice 0
      if((voice_child=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),0))) {
        child_properties=g_object_class_list_properties(G_OBJECT_CLASS(GST_OBJECT_GET_CLASS(voice_child)),&number_of_child_properties);
        g_object_unref(voice_child);
      }
    }

    // count number of controlable params
    for(i=0;i<number_of_properties;i++) {
      if(properties[i]->flags&GST_PARAM_CONTROLLABLE) {
        // check if this param is also registered as child param, if so skip
        skip=FALSE;
        if(child_properties) {
          for(j=0;j<number_of_child_properties;j++) {
            if(!strcmp(properties[i]->name,child_properties[j]->name)) {
              GST_DEBUG("    skipping global_param [%d] \"%s\"",i,properties[i]->name);
              skip=TRUE;
              properties[i]=NULL;
              break;
            }
          }
        }
        if(!skip) self->priv->global_params++;
      }
    }
    GST_INFO("found %lu global params",self->priv->global_params);
    self->priv->global_props =(GParamSpec ** )g_new0(gpointer,self->priv->global_params);
    self->priv->global_flags =(guint *       )g_new0(guint   ,self->priv->global_params);
    self->priv->global_no_val=(GValue *      )g_new0(GValue  ,self->priv->global_params);
    self->priv->global_quarks=(GQuark *      )g_new0(GQuark  ,self->priv->global_params);
    
    self->priv->global_control_sources=(GstInterpolationControlSource **)g_new0(gpointer,self->priv->global_params);

    for(i=j=0;i<number_of_properties;i++) {
      property=properties[i];
      if(property && property->flags&GST_PARAM_CONTROLLABLE) {
        gchar *qname=g_strdup_printf("BtMachine::%s",property->name);
        
        GST_DEBUG("    adding global_param [%u/%lu] \"%s\"",j,self->priv->global_params,property->name);
        // add global param
        self->priv->global_props[j]=property;
        self->priv->global_quarks[j]=g_quark_from_string(qname);
        g_free(qname);
        
        // treat readable params as normal ones, others as triggers
        if(property->flags&G_PARAM_READABLE) {
          self->priv->global_flags[j]=GSTBT_PROPERTY_META_STATE;
        }

        if(GSTBT_IS_PROPERTY_META(self->priv->machines[PART_MACHINE])) {
          gconstpointer const has_meta=g_param_spec_get_qdata(property,gstbt_property_meta_quark);

          if(has_meta) {
            self->priv->global_flags[j]=GPOINTER_TO_INT(g_param_spec_get_qdata(property,gstbt_property_meta_quark_flags));
            if(!(bt_machine_get_property_meta_value(&self->priv->global_no_val[j],property,gstbt_property_meta_quark_no_val))) {
              GST_WARNING_OBJECT(self,"    can't get no-val property-meta for global_param [%u/%lu] \"%s\"",j,self->priv->global_params,property->name);
            }
          }
        }
        // use the properties default value for triggers as a no_value
        if(!G_IS_VALUE(&self->priv->global_no_val[j]) && !(property->flags&G_PARAM_READABLE)) {
          g_value_init(&self->priv->global_no_val[j], property->value_type);
          g_param_value_set_default(property, &self->priv->global_no_val[j]);
        }
        // bind param to machines global controller (possibly returns ref to existing)
        GST_DEBUG("    added global_param [%u/%lu] \"%s\"",j,self->priv->global_params,property->name);
        j++;
      }
    }
    g_free(properties);
    g_free(child_properties);
  }
}

static void bt_machine_init_voice_params(const BtMachine * const self) {
  GParamSpec **properties;
  guint number_of_properties;

  // check if the elemnt implements the GstChildProxy interface
  if(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE])) {
    GstObject *voice_child;

    // register voice params
    // get child for voice 0
    if((voice_child=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),0))) {
      if((properties=g_object_class_list_properties(G_OBJECT_CLASS(GST_OBJECT_GET_CLASS(voice_child)),&number_of_properties))) {
        GParamSpec *property;
        guint i,j;

        // count number of controlable params
        for(i=0;i<number_of_properties;i++) {
          if(properties[i]->flags&GST_PARAM_CONTROLLABLE) self->priv->voice_params++;
        }
        GST_INFO("found %lu voice params",self->priv->voice_params);
        self->priv->voice_props =(GParamSpec ** )g_new0(gpointer,self->priv->voice_params);
        self->priv->voice_flags =(guint *       )g_new0(guint   ,self->priv->voice_params);
        self->priv->voice_no_val=(GValue *      )g_new0(GValue  ,self->priv->voice_params);
        self->priv->voice_quarks=(GQuark *      )g_new0(GQuark  ,self->priv->voice_params);

        for(i=j=0;i<number_of_properties;i++) {
          property=properties[i];
          if(property->flags&GST_PARAM_CONTROLLABLE) {
            gchar *qname=g_strdup_printf("BtMachine::%s",property->name);

            GST_DEBUG("    adding voice_param %p, [%u/%lu] \"%s\"",property, j,self->priv->voice_params,property->name);
            // add voice param
            self->priv->voice_props[j]=property;
            self->priv->voice_quarks[j]=g_quark_from_string(qname);
            g_free(qname);
            
            // treat readable params as normal ones, others as triggers
            if(property->flags&G_PARAM_READABLE) {
              self->priv->voice_flags[j]=GSTBT_PROPERTY_META_STATE;
            }
    
            if(GSTBT_IS_PROPERTY_META(voice_child)) {
              gconstpointer const has_meta=g_param_spec_get_qdata(property,gstbt_property_meta_quark);

              if(has_meta) {
                self->priv->voice_flags[j]=GPOINTER_TO_INT(g_param_spec_get_qdata(property,gstbt_property_meta_quark_flags));
                if(!(bt_machine_get_property_meta_value(&self->priv->voice_no_val[j],property,gstbt_property_meta_quark_no_val))) {
                  GST_WARNING_OBJECT(self,"    can't get no-val property-meta for voice_param [%u/%lu] \"%s\"",j,self->priv->voice_params,property->name);
                }
              }
            }
            // use the properties default value for triggers as a no_value
            if(!G_IS_VALUE(&self->priv->voice_no_val[j]) && !(property->flags&G_PARAM_READABLE)) {
              g_value_init(&self->priv->voice_no_val[j], property->value_type);
              g_param_value_set_default(property, &self->priv->voice_no_val[j]);
            }
            GST_DEBUG("    added voice_param [%u/%lu] \"%s\"",j,self->priv->voice_params,property->name);
            j++;
          }
        }
      }
      g_free(properties);

      // bind params to machines voice controller
      bt_machine_resize_voices(self,0);
      g_object_unref(voice_child);
    }
    else {
      GST_WARNING_OBJECT(self,"  can't get first voice child!");
    }
  }
  else {
    GST_INFO("  instance is monophonic!");
    self->priv->voices=0;
  }
}

//-- methods

/**
 * bt_machine_enable_input_pre_level:
 * @self: the machine to enable the pre-gain input-level analyser in
 *
 * Creates the pre-gain input-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean bt_machine_enable_input_pre_level(BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SOURCE_MACHINE(self),FALSE);

  return(bt_machine_enable_part(self,PART_INPUT_PRE_LEVEL,"level","input_pre_level"));
}

/**
 * bt_machine_enable_input_post_level:
 * @self: the machine to enable the post-gain input-level analyser in
 *
 * Creates the post-gain input-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean bt_machine_enable_input_post_level(BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SOURCE_MACHINE(self),FALSE);

  return(bt_machine_enable_part(self,PART_INPUT_POST_LEVEL,"level","input_post_level"));
}

/**
 * bt_machine_enable_output_pre_level:
 * @self: the machine to enable the pre-gain output-level analyser in
 *
 * Creates the pre-gain output-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean bt_machine_enable_output_pre_level(BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SINK_MACHINE(self),FALSE);
  
  return(bt_machine_enable_part(self,PART_OUTPUT_PRE_LEVEL,"level","output_pre_level"));
}

/**
 * bt_machine_enable_output_post_level:
 * @self: the machine to enable the post-gain output-level analyser in
 *
 * Creates the post-gain output-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean bt_machine_enable_output_post_level(BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SINK_MACHINE(self),FALSE);
  
  return(bt_machine_enable_part(self,PART_OUTPUT_POST_LEVEL,"level","output_post_level"));
}

/**
 * bt_machine_enable_input_gain:
 * @self: the machine to enable the input-gain element in
 *
 * Creates the input-gain element of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean bt_machine_enable_input_gain(BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SOURCE_MACHINE(self),FALSE);
  return(bt_machine_enable_part(self,PART_INPUT_GAIN,"volume","input_gain"));
}

/**
 * bt_machine_enable_output_gain:
 * @self: the machine to enable the output-gain element in
 *
 * Creates the output-gain element of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean bt_machine_enable_output_gain(BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SINK_MACHINE(self),FALSE);
  return(bt_machine_enable_part(self,PART_OUTPUT_GAIN,"volume","output_gain"));
}

/**
 * bt_machine_activate_adder:
 * @self: the machine to activate the adder in
 *
 * Machines use an adder to allow multiple incoming wires.
 * This method is used by the #BtWire class to activate the adder when needed.
 *
 * Returns: %TRUE for success
 */
gboolean bt_machine_activate_adder(BtMachine * const self) {
  gboolean res=FALSE;

  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SOURCE_MACHINE(self),FALSE);
  
  GstElement ** const machines=self->priv->machines;

  if(!machines[PART_ADDER]) {
    gboolean skip_convert=FALSE;
    GstPad ** const src_pads=self->priv->src_pads;
    GstPad ** const sink_pads=self->priv->sink_pads;
    guint i, tix=PART_MACHINE;

    // get first element on the source side
    for(i=PART_INPUT_PRE_LEVEL;i<=PART_MACHINE;i++) {
      if(machines[i]) {
        tix=i;
        GST_DEBUG_OBJECT(self,"src side target at %d: %s:%s",i,GST_DEBUG_PAD_NAME(sink_pads[tix]));
        break;
      }
    }

    // create the adder
    if(!(bt_machine_make_internal_element(self,PART_ADDER,"adder","adder"))) goto Error;
    /* live adder mixes by timestamps and does a timeout if an input is late */
    //if(!(bt_machine_make_internal_element(self,PART_ADDER,"liveadder","adder"))) goto Error;

    // try without capsfilter (>= 0.10.24)
    if(!g_object_class_find_property(G_OBJECT_CLASS(BT_MACHINE_GET_CLASS(machines[PART_ADDER])),"caps")) {
      if(!(bt_machine_make_internal_element(self,PART_CAPS_FILTER,"capsfilter","capsfilter"))) goto Error;
      g_object_set(machines[PART_CAPS_FILTER],"caps",bt_default_caps,NULL);
    }
    else {
      g_object_set(machines[PART_ADDER],"caps",bt_default_caps,NULL);
    }

    if(!BT_IS_SINK_MACHINE(self)) {
      // try without converters in effects
#if GST_CHECK_VERSION(0,10,25)
      skip_convert=gst_caps_can_intersect(bt_default_caps, gst_pad_get_pad_template_caps(sink_pads[PART_MACHINE]));
#else
      GstCaps *c=gst_caps_intersect(bt_default_caps, gst_pad_get_pad_template_caps(sink_pads[PART_MACHINE]));
      skip_convert=!(c && gst_caps_is_empty(c));
      gst_caps_unref(c);
#endif
    }
    if(skip_convert) {
      GST_DEBUG_OBJECT(self,"  about to link adder -> dst_elem");
      if(!machines[PART_CAPS_FILTER]) {
        if(!bt_machine_link_elements(self,src_pads[PART_ADDER], sink_pads[tix])) {
          GST_ERROR_OBJECT(self,"failed to link the internal adder of machine");
          goto Error;
        }
      }
      else {
        res=bt_machine_link_elements(self,src_pads[PART_ADDER], sink_pads[PART_CAPS_FILTER]);
        res&=bt_machine_link_elements(self,src_pads[PART_CAPS_FILTER], sink_pads[tix]);
        if(!res) {
          gst_pad_unlink(src_pads[PART_ADDER], sink_pads[PART_CAPS_FILTER]);
          gst_pad_unlink(src_pads[PART_CAPS_FILTER], sink_pads[tix]);
          GST_ERROR_OBJECT(self,"failed to link the internal adder of machine");
          goto Error;
        }
      }
    }
    else {
      GST_WARNING_OBJECT(self,"adding converter");
      if(!(bt_machine_make_internal_element(self,PART_ADDER_CONVERT,"audioconvert","audioconvert"))) goto Error;
      if(!BT_IS_SINK_MACHINE(self)) {
        // only do this for the final mix, if at all
        g_object_set(machines[PART_ADDER_CONVERT],"dithering",0,"noise-shaping",0,NULL);
      }
      GST_DEBUG_OBJECT(self,"  about to link adder -> convert -> dst_elem");
      if(!machines[PART_CAPS_FILTER]) {
        res=bt_machine_link_elements(self,src_pads[PART_ADDER], sink_pads[PART_ADDER_CONVERT]);
        res&=bt_machine_link_elements(self,src_pads[PART_ADDER_CONVERT], sink_pads[tix]);
        if(!res) {
          gst_pad_unlink(src_pads[PART_ADDER], sink_pads[PART_ADDER_CONVERT]);
          gst_pad_unlink(src_pads[PART_ADDER_CONVERT], sink_pads[tix]);
          GST_ERROR_OBJECT(self,"failed to link the internal adder of machine");
          goto Error;
        }
      }
      else {
        res=bt_machine_link_elements(self,src_pads[PART_ADDER], sink_pads[PART_CAPS_FILTER]);
        res&=bt_machine_link_elements(self,src_pads[PART_CAPS_FILTER], sink_pads[PART_ADDER_CONVERT]);
        res&=bt_machine_link_elements(self,src_pads[PART_ADDER_CONVERT], sink_pads[tix]);
        if(!res) {
          gst_pad_unlink(src_pads[PART_ADDER], sink_pads[PART_CAPS_FILTER]);
          gst_pad_unlink(src_pads[PART_CAPS_FILTER], sink_pads[PART_ADDER_CONVERT]);
          gst_pad_unlink(src_pads[PART_ADDER_CONVERT], sink_pads[tix]);
          GST_ERROR_OBJECT(self,"failed to link the internal adder of machine");
          goto Error;
        }
      }
    }
    GST_DEBUG_OBJECT(self,"  adder activated");
  }
  res=TRUE;
Error:
  bt_machine_dbg_print_parts(self);
  bt_song_write_to_lowlevel_dot_file(self->priv->song);
  return(res);
}

/**
 * bt_machine_has_active_adder:
 * @self: the machine to check
 *
 * Checks if the machine currently uses an adder.
 * This method is used by the #BtWire class to activate the adder when needed.
 *
 * Returns: %TRUE for success
 */
gboolean bt_machine_has_active_adder(const BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);

  return(self->priv->machines[PART_ADDER]!=NULL);
}

/**
 * bt_machine_activate_spreader:
 * @self: the machine to activate the spreader in
 *
 * Machines use a spreader to allow multiple outgoing wires.
 * This method is used by the #BtWire class to activate the spreader when needed.
 *
 * Returns: %TRUE for success
 */
gboolean bt_machine_activate_spreader(BtMachine * const self) {
  gboolean res=FALSE;

  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(!BT_IS_SINK_MACHINE(self),FALSE);
  
  GstElement ** const machines=self->priv->machines;

  if(!machines[PART_SPREADER]) {
    GstPad ** const src_pads=self->priv->src_pads;
    GstPad ** const sink_pads=self->priv->sink_pads;
    guint i, tix=PART_MACHINE;
    
    // get next element on the sink side
    for(i=PART_OUTPUT_POST_LEVEL;i>=PART_MACHINE;i--) {
      if(machines[i]) {
        tix=i;
        GST_DEBUG_OBJECT(self,"sink side target at %d: %s:%s",i,GST_DEBUG_PAD_NAME(src_pads[tix]));
        break;
      }
    }

    // create the spreader (tee)
    if(!(bt_machine_make_internal_element(self,PART_SPREADER,"tee","tee"))) goto Error;
    if(!bt_machine_link_elements(self,src_pads[tix], sink_pads[PART_SPREADER])) {
      GST_ERROR_OBJECT(self,"failed to link the internal spreader of machine");
      goto Error;
    }
    GST_DEBUG_OBJECT(self,"  spreader activated");
  }
  res=TRUE;
Error:
  bt_machine_dbg_print_parts(self);
  bt_song_write_to_lowlevel_dot_file(self->priv->song);
  return(res);
}

/**
 * bt_machine_has_active_spreader:
 * @self: the machine to check
 *
 * Checks if the machine currently uses an spreader.
 * This method is used by the #BtWire class to activate the spreader when needed.
 *
 * Returns: %TRUE for success
 */
gboolean bt_machine_has_active_spreader(const BtMachine * const self) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);

  return(self->priv->machines[PART_SPREADER]!=NULL);
}

// pattern handling

/**
 * bt_machine_add_pattern:
 * @self: the machine to add the pattern to
 * @pattern: the new pattern instance
 *
 * Add the supplied pattern to the machine. This is automatically done by
 * bt_pattern_new().
 */
void bt_machine_add_pattern(const BtMachine * const self, const BtPattern * const pattern) {
  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(BT_IS_PATTERN(pattern));

  if(!g_list_find(self->priv->patterns,pattern)) {
    gboolean is_internal;

    self->priv->patterns=g_list_append(self->priv->patterns,g_object_ref((gpointer)pattern));

    // check if its a internal pattern and if it is update count of those
    g_object_get((gpointer)pattern,"is-internal",&is_internal,NULL);
    if(is_internal) {
      self->priv->private_patterns++;
      GST_DEBUG("adding internal pattern, nr=%u",self->priv->private_patterns);
    }
    else {
      g_signal_emit((gpointer)self,signals[PATTERN_ADDED_EVENT], 0, pattern);
      bt_song_set_unsaved(self->priv->song,TRUE);
    }
  }
  else {
    GST_WARNING_OBJECT(self,"trying to add pattern again");
  }
}

/**
 * bt_machine_remove_pattern:
 * @self: the machine to remove the pattern from
 * @pattern: the existing pattern instance
 *
 * Remove the given pattern from the machine.
 */
void bt_machine_remove_pattern(const BtMachine * const self, const BtPattern * const pattern) {
  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(BT_IS_PATTERN(pattern));

  if(g_list_find(self->priv->patterns,pattern)) {
    self->priv->patterns=g_list_remove(self->priv->patterns,pattern);
    g_signal_emit((gpointer)self,signals[PATTERN_REMOVED_EVENT], 0, pattern);
    GST_DEBUG("removing pattern: ref_count=%d",G_OBJECT_REF_COUNT(pattern));
    g_object_unref((gpointer)pattern);
    bt_song_set_unsaved(self->priv->song,TRUE);
  }
  else {
    GST_WARNING_OBJECT(self,"trying to remove pattern that is not in machine");
  }
}

/**
 * bt_machine_get_pattern_by_id:
 * @self: the machine to search for the pattern
 * @id: the identifier of the pattern
 *
 * Search the machine for a pattern by the supplied id.
 * The pattern must have been added previously to this setup with #bt_machine_add_pattern().
 * Unref the pattern, when done with it.
 *
 * Returns: #BtPattern instance or %NULL if not found
 */
BtPattern *bt_machine_get_pattern_by_id(const BtMachine * const self,const gchar * const id) {
  gboolean found=FALSE;
  BtPattern *pattern;
  gchar *pattern_id;
  GList* node;

  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(BT_IS_STRING(id),NULL);

  //GST_DEBUG("pattern-list.length=%d",g_list_length(self->priv->patterns));

  for(node=self->priv->patterns;node;node=g_list_next(node)) {
    pattern=BT_PATTERN(node->data);
    g_object_get(pattern,"id",&pattern_id,NULL);
    if(!strcmp(pattern_id,id)) found=TRUE;
    g_free(pattern_id);
    if(found) return(g_object_ref(pattern));
  }
  GST_DEBUG("no pattern found for id \"%s\"",id);
  return(NULL);
}

/**
 * bt_machine_get_pattern_by_index:
 * @self: the machine to search for the pattern
 * @index: the index of the pattern in the machines pattern list
 *
 * Fetches the pattern from the given position of the machines pattern list.
 * The pattern must have been added previously to this setup with #bt_machine_add_pattern().
 * Unref the pattern, when done with it.
 *
 * Returns: #BtPattern instance or %NULL if not found
 */
BtPattern *bt_machine_get_pattern_by_index(const BtMachine * const self, const gulong index) {
  BtPattern *pattern;
  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);

  if((pattern=g_list_nth_data(self->priv->patterns,(guint)index))) {
    pattern=g_object_ref(pattern);
  }
  return(pattern);
}

/**
 * bt_machine_get_unique_pattern_name:
 * @self: the machine for which the name should be unique
 *
 * The function generates a unique pattern name for this machine by eventually
 * adding a number postfix. This method should be used when adding new patterns.
 *
 * Returns: the newly allocated unique name
 */
gchar *bt_machine_get_unique_pattern_name(const BtMachine * const self) {
  BtPattern *pattern=NULL;
  gchar *id,*ptr;
  guint8 i=0;

  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);

  id=g_strdup_printf("%s 00",self->priv->id);
  ptr=&id[strlen(self->priv->id)+1];
  do {
    (void)g_sprintf(ptr,"%02u",i++);
    g_object_try_unref(pattern);
  } while((pattern=bt_machine_get_pattern_by_id(self,id)) && (i<100));
  g_object_try_unref(pattern);
  g_free(id);
  i--;

  return(g_strdup_printf("%02u",i));
}

/**
 * bt_machine_has_patterns:
 * @self: the machine for which to check the patterns
 *
 * Check if the machine has #BtPattern entries appart from the standart private
 * ones.
 *
 * Returns: %TRUE if it has patterns
 */
gboolean bt_machine_has_patterns(const BtMachine * const self) {
  return(g_list_length(self->priv->patterns)>self->priv->private_patterns);
}

// global and voice param handling

/**
 * bt_machine_is_polyphonic:
 * @self: the machine to check
 *
 * Tells if the machine can produce (multiple) voices. Monophonic machines have
 * their (one) voice params as part of the global params.
 *
 * Returns: %TRUE for polyphic machines, %FALSE for monophonic ones
 */
gboolean bt_machine_is_polyphonic(const BtMachine * const self) {
  gboolean res;
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);

  res=GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE]);
  GST_INFO(" is machine \"%s\" polyphonic ? %d",self->priv->id,res);
  return(res);
}

/**
 * bt_machine_is_global_param_trigger:
 * @self: the machine to check params from
 * @index: the offset in the list of global params
 *
 * Tests if the global param is a trigger param
 * (like a key-note or a drum trigger).
 *
 * Returns: %TRUE if it is a trigger
 */
gboolean bt_machine_is_global_param_trigger(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(index<self->priv->global_params,FALSE);

  if(!(self->priv->global_flags[index]&GSTBT_PROPERTY_META_STATE)) return(TRUE);
  return(FALSE);
}

/**
 * bt_machine_is_voice_param_trigger:
 * @self: the machine to check params from
 * @index: the offset in the list of voice params
 *
 * Tests if the voice param is a trigger param
 * (like a key-note or a drum trigger).
 *
 * Returns: %TRUE if it is a trigger
 */
gboolean bt_machine_is_voice_param_trigger(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(index<self->priv->voice_params,FALSE);

  if(!(self->priv->voice_flags[index]&GSTBT_PROPERTY_META_STATE)) return(TRUE);
  return(FALSE);
}

/**
 * bt_machine_is_global_param_no_value:
 * @self: the machine to check params from
 * @index: the offset in the list of global params
 * @value: the value to compare against the no-value
 *
 * Tests if the given value is the no-value of the global param
 *
 * Returns: %TRUE if it is the no-value
 */
gboolean bt_machine_is_global_param_no_value(const BtMachine * const self, const gulong index, GValue * const value) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(index<self->priv->global_params,FALSE);
  g_return_val_if_fail(G_IS_VALUE(value),FALSE);

  if(!G_IS_VALUE(&self->priv->global_no_val[index])) return(FALSE);

  if(gst_value_compare(&self->priv->global_no_val[index],value)==GST_VALUE_EQUAL) return(TRUE);
  return(FALSE);
}

/**
 * bt_machine_is_voice_param_no_value:
 * @self: the machine to check params from
 * @index: the offset in the list of voice params
 * @value: the value to compare against the no-value
 *
 * Tests if the given value is the no-value of the voice param
 *
 * Returns: %TRUE if it is the no-value
 */
gboolean bt_machine_is_voice_param_no_value(const BtMachine * const self, const gulong index, GValue * const value) {
  g_return_val_if_fail(BT_IS_MACHINE(self),FALSE);
  g_return_val_if_fail(index<self->priv->voice_params,FALSE);
  g_return_val_if_fail(G_IS_VALUE(value),FALSE);

  if(!G_IS_VALUE(&self->priv->voice_no_val[index])) return(FALSE);

  if(gst_value_compare(&self->priv->voice_no_val[index],value)==GST_VALUE_EQUAL) {
    return(TRUE);
  }
  return(FALSE);
}

/**
 * bt_machine_get_global_wave_param_index:
 * @self: the machine to lookup the param from
 *
 * Searches for the wave-table index parameter (if any). This parameter should
 * refer to a wavetable index that should be used to play a note.
 *
 * Returns: the index of the wave-table parameter or -1 if none.
 */
glong bt_machine_get_global_wave_param_index(const BtMachine * const self) {
  glong i;
  g_return_val_if_fail(BT_IS_MACHINE(self),-1);
  
  for(i=0;i<self->priv->global_params;i++) {
    if(self->priv->global_flags[i]&GSTBT_PROPERTY_META_WAVE) return(i);
  }
  return(-1);
}

/**
 * bt_machine_get_voice_wave_param_index:
 * @self: the machine to lookup the param from
 *
 * Searches for the wave-table index parameter (if any). This parameter should
 * refer to a wavetable index that should be used to play a note.
 *
 * Returns: the index of the wave-table parameter or -1 if none.
 */
glong bt_machine_get_voice_wave_param_index(const BtMachine * const self) {
  glong i;
  g_return_val_if_fail(BT_IS_MACHINE(self),-1);
  
  for(i=0;i<self->priv->voice_params;i++) {
    if(self->priv->voice_flags[i]&GSTBT_PROPERTY_META_WAVE) return(i);
  }
  
  return(-1);
}

/*
 * bt_machine_has_global_param_default_set:
 * @self: the machine to check params from
 * @index: the offset in the list of global params
 *
 * Tests if the global param uses the default at timestamp=0. Parameters have a
 * default if there is no control-point at that timestamp. When interactively
 * changing the parameter, the default needs to be updated by calling
 * bt_machine_global_controller_change_value().
 *
 * Returns: %TRUE if it has a default there
 */
static gboolean bt_machine_has_global_param_default_set(const BtMachine * const self, const gulong index) {
  GObject *param_parent=(GObject*)(self->priv->machines[PART_MACHINE]);
  return GPOINTER_TO_INT(g_object_get_qdata(param_parent,self->priv->global_quarks[index]));
}

/*
 * bt_machine_has_voice_param_default_set:
 * @self: the machine to check params from
 * @voice: the voice number
 * @index: the offset in the list of global params
 *
 * Tests if the voice param uses the default at timestamp=0. Parameters have a
 * default if there is no control-point at that timestamp. When interactively
 * changing the parameter, the default needs to be updated by calling
 * bt_machine_global_controller_change_value().
 *
 * Returns: %TRUE if it has a default there
 */
static gboolean bt_machine_has_voice_param_default_set(const BtMachine * const self, const gulong voice, const gulong index) {
  GObject *param_parent=(GObject*)(gst_child_proxy_get_child_by_index((GstChildProxy*)(self->priv->machines[PART_MACHINE]),voice));
  gboolean result=GPOINTER_TO_INT(g_object_get_qdata(param_parent,self->priv->voice_quarks[index]));
  g_object_unref(param_parent);
  return(result);
}

/**
 * bt_machine_set_global_param_default:
 * @self: the machine
 * @index: the offset in the list of global params
 *
 * Set a default value that should be used before the first control-point.
 */
void bt_machine_set_global_param_default(const BtMachine * const self,const gulong index) {
  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(index<self->priv->global_params);
  
  if(bt_machine_has_global_param_default_set(self,index)) {
    GST_WARNING_OBJECT(self,"updating global param %d at ts=0",index);
    bt_machine_global_controller_change_value(self,index,G_GUINT64_CONSTANT(0),NULL);
  }
}

/**
 * bt_machine_set_voice_param_default:
 * @self: the machine
 * @voice: the voice number
 * @index: the offset in the list of global params
 *
 * Set a default value that should be used before the first control-point.
 */
void bt_machine_set_voice_param_default(const BtMachine * const self,const gulong voice, const gulong index) {
  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(index<self->priv->voice_params);

  if(bt_machine_has_voice_param_default_set(self,voice,index)) {
    GST_WARNING_OBJECT(self,"updating voice %ld param %d at ts=0",voice,index);
    bt_machine_voice_controller_change_value(self,voice,index,G_GUINT64_CONSTANT(0),NULL);
  }
}

/**
 * bt_machine_set_param_defaults:
 * @self: the machine
 *
 * Sets default values that should be used before the first control-point.
 * Should be called, if all parameters are changed (like after switching
 * presets).
 */
void bt_machine_set_param_defaults(const BtMachine *const self) {
  GstElement *machine=self->priv->machines[PART_MACHINE];
  GstObject *voice;
  GstController *ctrl;
  gulong i,j;
  
  if((ctrl=gst_object_get_controller(G_OBJECT(machine)))) {
    for(i=0;i<self->priv->global_params;i++) {
      bt_machine_set_global_param_default(self,i);
    }
  }
  for(j=0;j<self->priv->voices;j++) {
    voice=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(machine),j);
    if((ctrl=gst_object_get_controller(G_OBJECT(voice)))) {
      for(i=0;i<self->priv->voice_params;i++) {
        bt_machine_set_voice_param_default(self,j,i);
      }
    }
    gst_object_unref(voice);
  }
}


/**
 * bt_machine_get_global_param_index:
 * @self: the machine to search for the global param
 * @name: the name of the global param
 * @error: the location of an error instance to fill with a message, if an error occures
 *
 * Searches the list of registered param of a machine for a global param of
 * the given name and returns the index if found.
 *
 * Returns: the index or sets error if it is not found and returns -1.
 */
glong bt_machine_get_global_param_index(const BtMachine *const self, const gchar * const name, GError **error) {
  glong ret=-1,i;
  gboolean found=FALSE;

  g_return_val_if_fail(BT_IS_MACHINE(self),-1);
  g_return_val_if_fail(BT_IS_STRING(name),-1);
  g_return_val_if_fail(error == NULL || *error == NULL, -1);

  for(i=0;i<self->priv->global_params;i++) {
    if(!strcmp(GLOBAL_PARAM_NAME(i),name)) {
      ret=i;
      found=TRUE;
      break;
    }
  }
  if(!found) {
    GST_WARNING_OBJECT(self,"global param for name %s not found", name);
    if(error) {
      g_set_error (error, error_domain, /* errorcode= */0,
                  "global param for name %s not found", name);
    }
  }
  //g_assert((found || (error && *error)));
  g_assert(((found && (ret>=0)) || ((ret==-1) && ((error && *error) || !error))));
  return(ret);
}

/**
 * bt_machine_get_voice_param_index:
 * @self: the machine to search for the voice param
 * @name: the name of the voice param
 * @error: the location of an error instance to fill with a message, if an error occures
 *
 * Searches the list of registered param of a machine for a voice param of
 * the given name and returns the index if found.
 *
 * Returns: the index or sets error if it is not found and returns -1.
 */
glong bt_machine_get_voice_param_index(const BtMachine * const self, const gchar * const name, GError **error) {
  gulong ret=-1,i;
  gboolean found=FALSE;

  g_return_val_if_fail(BT_IS_MACHINE(self),-1);
  g_return_val_if_fail(BT_IS_STRING(name),-1);
  g_return_val_if_fail(error == NULL || *error == NULL, -1);

  for(i=0;i<self->priv->voice_params;i++) {
    if(!strcmp(VOICE_PARAM_NAME(i),name)) {
      ret=i;
      found=TRUE;
      break;
    }
  }
  if(!found) {
    GST_WARNING_OBJECT(self,"voice param for name %s not found", name);
    if(error) {
      g_set_error (error, error_domain, /* errorcode= */0,
                  "voice param for name %s not found", name);
    }
  }
  g_assert(((found && (ret>=0)) || ((ret==-1) && ((error && *error) || !error))));
  return(ret);
}

/**
 * bt_machine_get_global_param_spec:
 * @self: the machine to search for the global param
 * @index: the offset in the list of global params
 *
 * Retrieves the parameter specification for the global param
 *
 * Returns: the #GParamSpec for the requested global param
 */
GParamSpec *bt_machine_get_global_param_spec(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(index<self->priv->global_params,NULL);

  return(self->priv->global_props[index]);
}

/**
 * bt_machine_get_voice_param_spec:
 * @self: the machine to search for the voice param
 * @index: the offset in the list of voice params
 *
 * Retrieves the parameter specification for the voice param
 *
 * Returns: the #GParamSpec for the requested voice param
 */
GParamSpec *bt_machine_get_voice_param_spec(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(index<self->priv->voice_params,NULL);

  return(self->priv->voice_props[index]);

#if 0
  GstObject *voice_child;
  GParamSpec *pspec=NULL;

  GST_INFO("    voice-param %d '%s'",index,VOICE_PARAM_NAME(index));

  if((voice_child=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),0))) {
    pspec=g_object_class_find_property(
      G_OBJECT_CLASS(GST_OBJECT_GET_CLASS(voice_child)),
      VOICE_PARAM_NAME(index));
    g_object_unref(voice_child);
  }
  return(pspec);
#endif
}

/**
 * bt_machine_set_global_param_value:
 * @self: the machine to set the global param value
 * @index: the offset in the list of global params
 * @event: the new value
 *
 * Sets a the specified global param to the give data value.
 */
void bt_machine_set_global_param_value(const BtMachine * const self, const gulong index, GValue * const event) {
  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(G_IS_VALUE(event));
  g_return_if_fail(index<self->priv->global_params);

  GST_DEBUG("set value for %s.%s",self->priv->id,GLOBAL_PARAM_NAME(index));
  g_object_set_property(G_OBJECT(self->priv->machines[PART_MACHINE]),GLOBAL_PARAM_NAME(index),event);
}

/**
 * bt_machine_set_voice_param_value:
 * @self: the machine to set the voice param value
 * @voice: the voice to change
 * @index: the offset in the list of voice params
 * @event: the new value
 *
 * Sets a the specified voice param to the give data value.
 */
void bt_machine_set_voice_param_value(const BtMachine * const self, const gulong voice, const gulong index, GValue * const event) {
  GstObject *voice_child;

  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(G_IS_VALUE(event));
  g_return_if_fail(voice<self->priv->voices);
  g_return_if_fail(index<self->priv->voice_params);

  if((voice_child=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),voice))) {
    g_object_set_property(G_OBJECT(voice_child),VOICE_PARAM_NAME(index),event);
    g_object_unref(voice_child);
  }
}

/**
 * bt_machine_get_global_param_name:
 * @self: the machine to get the param name from
 * @index: the offset in the list of global params
 *
 * Gets the global param name. Do not modify returned content.
 *
 * Returns: the requested name
 */
const gchar *bt_machine_get_global_param_name(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(index<self->priv->global_params,NULL);

  return(GLOBAL_PARAM_NAME(index));
}

/**
 * bt_machine_get_voice_param_name:
 * @self: the machine to get the param name from
 * @index: the offset in the list of voice params
 *
 * Gets the voice param name. Do not modify returned content.
 *
 * Returns: the requested name
 */
const gchar *bt_machine_get_voice_param_name(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(index<self->priv->voice_params,NULL);

  return(VOICE_PARAM_NAME(index));
}

static void bt_machine_get_param_details(const BtMachine * const self, GParamSpec *property, GValue **min_val, GValue **max_val) {
  gboolean done=FALSE;

  if(min_val || max_val) {
    GType base_type=bt_g_type_get_base_type(property->value_type);

    if(min_val) *min_val=g_new0(GValue,1);
    if(max_val) *max_val=g_new0(GValue,1);
    if(GSTBT_IS_PROPERTY_META(self->priv->machines[PART_MACHINE])) {
      if(min_val) done=bt_machine_get_property_meta_value(*min_val,property,gstbt_property_meta_quark_min_val);
      if(max_val) {
        if(!bt_machine_get_property_meta_value(*max_val,property,gstbt_property_meta_quark_max_val)) {
          // if this failed max val has not been set
          if(done) g_value_unset(*min_val);
          done=FALSE;
        }
      }
    }
    if(!done) {
      if(min_val) g_value_init(*min_val,property->value_type);
      if(max_val) g_value_init(*max_val,property->value_type);     
      switch(base_type) {
        case G_TYPE_BOOLEAN:
          if(min_val) g_value_set_boolean(*min_val,0);
          if(max_val) g_value_set_boolean(*max_val,0);
        break;
        case G_TYPE_INT: {
          const GParamSpecInt *int_property=G_PARAM_SPEC_INT(property);
          if(min_val) g_value_set_int(*min_val,int_property->minimum);
          if(max_val) g_value_set_int(*max_val,int_property->maximum);
        } break;
        case G_TYPE_UINT: {
          const GParamSpecUInt *uint_property=G_PARAM_SPEC_UINT(property);
          if(min_val) g_value_set_uint(*min_val,uint_property->minimum);
          if(max_val) g_value_set_uint(*max_val,uint_property->maximum);
        } break;
        case G_TYPE_LONG: {
          const GParamSpecLong *long_property=G_PARAM_SPEC_LONG(property);
          if(min_val) g_value_set_long(*min_val,long_property->minimum);
          if(max_val) g_value_set_long(*max_val,long_property->maximum);
        } break;
        case G_TYPE_ULONG: {
          const GParamSpecULong *ulong_property=G_PARAM_SPEC_ULONG(property);
          if(min_val) g_value_set_ulong(*min_val,ulong_property->minimum);
          if(max_val) g_value_set_ulong(*max_val,ulong_property->maximum);
        } break;
        case G_TYPE_FLOAT: {
          const GParamSpecFloat *float_property=G_PARAM_SPEC_FLOAT(property);
          if(min_val) g_value_set_float(*min_val,float_property->minimum);
          if(max_val) g_value_set_float(*max_val,float_property->maximum);
        } break;
        case G_TYPE_DOUBLE: {
          const GParamSpecDouble *double_property=G_PARAM_SPEC_DOUBLE(property);
          if(min_val) g_value_set_double(*min_val,double_property->minimum);
          if(max_val) g_value_set_double(*max_val,double_property->maximum);
        } break;
        case G_TYPE_ENUM: {
          const GParamSpecEnum *enum_property=G_PARAM_SPEC_ENUM(property);
          const GEnumClass *enum_class=enum_property->enum_class;
          if(min_val) g_value_set_enum(*min_val,enum_class->minimum);
          if(max_val) g_value_set_enum(*max_val,enum_class->maximum);
        } break;
        case G_TYPE_STRING:
          // nothing to do for this
          break;
        default:
          GST_ERROR_OBJECT(self,"unsupported GType=%lu:'%s' ('%s')",(gulong)property->value_type,g_type_name(property->value_type),g_type_name(base_type));
      }
    }
  }
}

/**
 * bt_machine_get_global_param_details:
 * @self: the machine to search for the global param details
 * @index: the offset in the list of global params
 * @pspec: place for the param spec
 * @min_val: place to hold new GValue with minimum
 * @max_val: place to hold new GValue with maximum 
 *
 * Retrieves the details of a global param. Any detail can be %NULL if its not
 * wanted.
 */
void bt_machine_get_global_param_details(const BtMachine * const self, const gulong index, GParamSpec **pspec, GValue **min_val, GValue **max_val) {
  GParamSpec *property=bt_machine_get_global_param_spec(self,index);
  
  if(pspec) {
    *pspec=property;
  }
  bt_machine_get_param_details(self, property, min_val, max_val);
}

/**
 * bt_machine_get_voice_param_details:
 * @self: the machine to search for the voice param details
 * @index: the offset in the list of voice params
 * @pspec: place for the param spec
 * @min_val: place to hold new GValue with minimum
 * @max_val: place to hold new GValue with maximum 
 *
 * Retrieves the details of a voice param. Any detail can be %NULL if its not
 * wanted.
 */
void bt_machine_get_voice_param_details(const BtMachine * const self, const gulong index, GParamSpec **pspec, GValue **min_val, GValue **max_val) {
  GParamSpec *property=bt_machine_get_voice_param_spec(self,index);

  if(pspec) {
    *pspec=property;
  }
  bt_machine_get_param_details(self, property, min_val, max_val);
}

/**
 * bt_machine_get_global_param_type:
 * @self: the machine to search for the global param type
 * @index: the offset in the list of global params
 *
 * Retrieves the GType of a global param
 *
 * Returns: the requested GType
 */
GType bt_machine_get_global_param_type(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),G_TYPE_INVALID);
  g_return_val_if_fail(index<self->priv->global_params,G_TYPE_INVALID);

  return(GLOBAL_PARAM_TYPE(index));
}

/**
 * bt_machine_get_voice_param_type:
 * @self: the machine to search for the voice param type
 * @index: the offset in the list of voice params
 *
 * Retrieves the GType of a voice param
 *
 * Returns: the requested GType
 */
GType bt_machine_get_voice_param_type(const BtMachine * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_MACHINE(self),G_TYPE_INVALID);
  g_return_val_if_fail(index<self->priv->voice_params,G_TYPE_INVALID);

  return(VOICE_PARAM_TYPE(index));
}

/**
 * bt_machine_describe_global_param_value:
 * @self: the machine to get a param description from
 * @index: the offset in the list of global params
 * @event: the value to describe
 *
 * Described a param value in human readable form. The type of the given @value
 * must match the type of the paramspec of the param referenced by @index.
 *
 * Returns: the description as newly allocated string
 */
gchar *bt_machine_describe_global_param_value(const BtMachine * const self, const gulong index, GValue * const event) {
  gchar *str=NULL;

  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(index<self->priv->global_params,NULL);
  g_return_val_if_fail(G_IS_VALUE(event),NULL);

  if(GSTBT_IS_PROPERTY_META(self->priv->machines[PART_MACHINE])) {
    str=gstbt_property_meta_describe_property(GSTBT_PROPERTY_META(self->priv->machines[PART_MACHINE]),index,event);
  }
  return(str);
}

/**
 * bt_machine_describe_voice_param_value:
 * @self: the machine to get a param description from
 * @index: the offset in the list of voice params
 * @event: the value to describe
 *
 * Described a param value in human readable form. The type of the given @value
 * must match the type of the paramspec of the param referenced by @index.
 *
 * Returns: the description as newly allocated string
 */
gchar *bt_machine_describe_voice_param_value(const BtMachine * const self, const gulong index, GValue * const event) {
  gchar *str=NULL;

  GST_INFO("%p voice value %lu %p",self,index,event);

  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(index<self->priv->voice_params,NULL);
  g_return_val_if_fail(G_IS_VALUE(event),NULL);

  if(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE])) {
    GstObject *voice_child;

    // get child for voice 0
    if((voice_child=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),0))) {
      if(GSTBT_IS_PROPERTY_META(voice_child)) {
        str=gstbt_property_meta_describe_property(GSTBT_PROPERTY_META(voice_child),index,event);
      }
      //else {
        //GST_WARNING_OBJECT(self,"%s is not PROPERTY_META",self->priv->id);
      //}
      g_object_unref(voice_child);
    }
    //else {
      //GST_WARNING_OBJECT(self,"%s has no voice child",self->priv->id);
    //}
  }
  //else {
    //GST_WARNING_OBJECT(self,"%s is not CHILD_BIN",self->priv->id);
  //}
  return(str);
}

//-- controller handling

static gboolean controller_need_activate(GstInterpolationControlSource *cs) {
  if(cs && gst_interpolation_control_source_get_count(cs)) {
    return(FALSE);
  }
  return(TRUE);
}

static gboolean controller_rem_value(GstInterpolationControlSource *cs, const GstClockTime timestamp, const gboolean has_default) {
  if(cs) {
    gint count;
    
    gst_interpolation_control_source_unset(cs,timestamp);

    // check if the property is not having control points anymore
    count=gst_interpolation_control_source_get_count(cs);
    if(has_default) // remove also if there is a default only left
      count--;
    // @bug: http://bugzilla.gnome.org/show_bug.cgi?id=538201 -> fixed in 0.10.21
    return(count==0);
  }
  return(FALSE);
}

/**
 * bt_machine_global_controller_change_value:
 * @self: the machine to change the param for
 * @param: the global parameter index
 * @timestamp: the time stamp of the change
 * @value: the new value or %NULL to unset a previous one
 *
 * Depending on whether the given value is NULL, sets or unsets the controller
 * value for the specified param and at the given time.
 * If @timestamp is 0 and @value is %NULL it set a default value for the start
 * of the controller sequence, taken from the current value of the parameter.
 */
void bt_machine_global_controller_change_value(const BtMachine * const self, const gulong param, const GstClockTime timestamp, GValue *value) {
  GObject *param_parent;
  GValue def_value={0,};
  GstInterpolationControlSource *cs;
  gchar *param_name;

  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(param<self->priv->global_params);

  param_parent=G_OBJECT(self->priv->machines[PART_MACHINE]);
  param_name=GLOBAL_PARAM_NAME(param);
  cs=self->priv->global_control_sources[param];
  
  if(G_UNLIKELY(!timestamp)) {
    if(!value) {
      // we set it later
      value=&def_value;
      // need to remember that we set a default, so that we can update it
      // (bt_machine_has_global_param_default_set)
      g_object_set_qdata(param_parent,self->priv->global_quarks[param],GINT_TO_POINTER(TRUE));
      GST_INFO("set global default for %s param %lu:%s",self->priv->id,param,param_name);
    }
    else {
      // we set a real value for ts=0, no need to update the default
      g_object_set_qdata(param_parent,self->priv->global_quarks[param],GINT_TO_POINTER(FALSE));
    }
  }

  if(value) {
    gboolean add=controller_need_activate(cs);
    gboolean is_trigger=bt_machine_is_global_param_trigger(self,param);
    
    if(G_UNLIKELY(value==&def_value)) {
      // only set default value if this is not the first controlpoint
      if(!add) {
        if (!is_trigger) {
          g_value_init(&def_value,GLOBAL_PARAM_TYPE(param));
          g_object_get_property(param_parent,param_name,&def_value);
          GST_LOG("set global controller: %"GST_TIME_FORMAT" param %s:%s",GST_TIME_ARGS(G_GUINT64_CONSTANT(0)),g_type_name(GLOBAL_PARAM_TYPE(param)),param_name);
          gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&def_value);
          g_value_unset(&def_value);
        }
        else {
          gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&self->priv->global_no_val[param]);
        }
      }
    }
    else {
      if(G_UNLIKELY(add)) {
        GstController *ctrl;

        if((ctrl=gst_object_control_properties(param_parent, param_name, NULL))) {
          cs=gst_interpolation_control_source_new();
          gst_controller_set_control_source(ctrl,param_name,GST_CONTROL_SOURCE(cs));
          // set interpolation mode depending on param type
          gst_interpolation_control_source_set_interpolation_mode(cs,is_trigger?GST_INTERPOLATE_TRIGGER:GST_INTERPOLATE_NONE);
          self->priv->global_control_sources[param]=cs;
        }

        // @todo: is this needed, we're in add=TRUE after all
        g_object_try_unref(self->priv->global_controller);
        self->priv->global_controller=ctrl;
    
        if(timestamp) {
          // also set default value, as first control point is not a time=0
          GST_LOG("set global controller: %"GST_TIME_FORMAT" param %s:%s",GST_TIME_ARGS(G_GUINT64_CONSTANT(0)),g_type_name(GLOBAL_PARAM_TYPE(param)),param_name);
          if (!is_trigger) {
            g_value_init(&def_value,GLOBAL_PARAM_TYPE(param));
            g_object_get_property(param_parent,param_name,&def_value);
            gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&def_value);
            g_value_unset(&def_value);
          }
          else {
            gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&self->priv->global_no_val[param]);
          }
        }
      }
      GST_LOG("set global controller: %"GST_TIME_FORMAT" param %s:%s",GST_TIME_ARGS(timestamp),g_type_name(GLOBAL_PARAM_TYPE(param)),param_name);
      gst_interpolation_control_source_set(cs,timestamp,value);
    }
  }
  else {
    gboolean has_default=bt_machine_has_global_param_default_set(self,param);

    GST_LOG("unset global controller: %"GST_TIME_FORMAT" param %s:%s",GST_TIME_ARGS(timestamp),g_type_name(GLOBAL_PARAM_TYPE(param)),param_name);
    if(controller_rem_value(cs,timestamp,has_default)) {
      gst_controller_set_control_source(self->priv->global_controller,param_name,NULL);
      g_object_unref(cs);
      self->priv->global_control_sources[param]=NULL;
      gst_object_uncontrol_properties(param_parent, param_name, NULL);
    }
  }
}

/**
 * bt_machine_voice_controller_change_value:
 * @self: the machine to change the param for
 * @voice: the voice number
 * @param: the voice parameter index
 * @timestamp: the time stamp of the change
 * @value: the new value or %NULL to unset a previous one
 *
 * Depending on whether the given value is NULL, sets or unsets the controller
 * value for the specified param and at the given time.
 * If @timestamp is 0 and @value is %NULL it set a default value for the start
 * of the controller sequence, taken from the current value of the parameter.
 */
void bt_machine_voice_controller_change_value(const BtMachine * const self, const gulong voice, const gulong param, const GstClockTime timestamp, GValue *value) {
  GObject *param_parent;
  GValue def_value={0,};
  GstInterpolationControlSource *cs;
  gchar *param_name;

  g_return_if_fail(BT_IS_MACHINE(self));
  g_return_if_fail(param<self->priv->voice_params);
  g_return_if_fail(voice<self->priv->voices);
  g_return_if_fail(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE]));

  param_parent=(GObject*)(gst_child_proxy_get_child_by_index((GstChildProxy*)(self->priv->machines[PART_MACHINE]),voice));
  param_name=VOICE_PARAM_NAME(param);
  cs=self->priv->voice_control_sources[voice*self->priv->voice_params+param];

  if(G_UNLIKELY(!timestamp)) {
    if(!value) {
      // we set it later
      value=&def_value;
      // need to remember that we set a default, so that we can update it
      // (bt_machine_has_voice_param_default_set)
      g_object_set_qdata(param_parent,self->priv->voice_quarks[param],GINT_TO_POINTER(TRUE));
      //GST_INFO("set voice default for %s:%lu param %lu:%s",self->priv->id,voice,param,param_name);
    }
    else {
      // we set a real value for ts=0, no need to update the default
      g_object_set_qdata(param_parent,self->priv->voice_quarks[param],GINT_TO_POINTER(FALSE));
    }
  }

  if(value) {
    gboolean add=controller_need_activate(cs);
    gboolean is_trigger=bt_machine_is_voice_param_trigger(self,param);

    if(G_UNLIKELY(value==&def_value)) {
      // only set default value if this is not the first controlpoint
      if(!add) {
        if (!is_trigger) {
          g_value_init(&def_value,VOICE_PARAM_TYPE(param));
          g_object_get_property(param_parent,param_name,&def_value);
          GST_LOG("set voice[%lu] controller: %"GST_TIME_FORMAT" param %s:%s",voice,GST_TIME_ARGS(G_GUINT64_CONSTANT(0)),g_type_name(VOICE_PARAM_TYPE(param)),param_name);
          gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&def_value);
          g_value_unset(&def_value);
        }
        else {
          gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&self->priv->voice_no_val[param]);
        }
      }
    }
    else {
      if(G_UNLIKELY(add)) {
        GstController *ctrl;
        
        if((ctrl=gst_object_control_properties(param_parent, param_name, NULL))) {
          cs=gst_interpolation_control_source_new();
          gst_controller_set_control_source(ctrl,param_name,GST_CONTROL_SOURCE(cs));
          // set interpolation mode depending on param type
          gst_interpolation_control_source_set_interpolation_mode(cs,is_trigger?GST_INTERPOLATE_TRIGGER:GST_INTERPOLATE_NONE);
          self->priv->voice_control_sources[voice*self->priv->voice_params+param]=cs;
        }

        // @todo: is this needed, we're in add=TRUE after all
        g_object_try_unref(self->priv->voice_controllers[voice]);
        self->priv->voice_controllers[voice]=ctrl;

        if(timestamp) {
          // also set default value, as first control point is not a time=0
          GST_LOG("set voice[%lu] controller: %"GST_TIME_FORMAT" param %s:%s",voice,GST_TIME_ARGS(G_GUINT64_CONSTANT(0)),g_type_name(VOICE_PARAM_TYPE(param)),param_name);
          if (!is_trigger) {
            g_value_init(&def_value,VOICE_PARAM_TYPE(param));
            g_object_get_property(param_parent,param_name,&def_value);
            gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&def_value);
            g_value_unset(&def_value);
          }
          else {
            gst_interpolation_control_source_set(cs,G_GUINT64_CONSTANT(0),&self->priv->voice_no_val[param]);
          }
            
        }
      }
      GST_LOG("set voice[%lu] controller: %"GST_TIME_FORMAT" param %s:%s",voice,GST_TIME_ARGS(timestamp),g_type_name(VOICE_PARAM_TYPE(param)),param_name);
      gst_interpolation_control_source_set(cs,timestamp,value);
    }
  }
  else {
    gboolean has_default=bt_machine_has_voice_param_default_set(self,voice,param);

    GST_LOG("unset voice[%lu] controller: %"GST_TIME_FORMAT" param %s:%s",voice,GST_TIME_ARGS(timestamp),g_type_name(VOICE_PARAM_TYPE(param)),param_name);
    if(controller_rem_value(cs,timestamp,has_default)) {
      gst_controller_set_control_source(self->priv->voice_controllers[voice],param_name,NULL);
      g_object_unref(cs);
      self->priv->voice_control_sources[voice*self->priv->voice_params+param]=NULL;
      gst_object_uncontrol_properties(param_parent, param_name, NULL);
    }
  }
  g_object_unref(param_parent);
}

//-- interaction control

static void free_control_data(BtControlData *data) {
  BtIcDevice *device;

  // stop the device
  g_object_get((gpointer)(data->control),"device",&device,NULL);
  btic_device_stop(device);
  g_object_unref(device);

  // disconnect the handler
  g_signal_handler_disconnect((gpointer)data->control,data->handler_id);
  g_object_unref((gpointer)(data->control));

  g_free(data);
}

static void on_boolean_control_notify(const BtIcControl *control,GParamSpec *arg,gpointer user_data) {
  BtControlData *data=(BtControlData *)(user_data);
  gboolean value;

  g_object_get((gpointer)(data->control),"value",&value,NULL);
  g_object_set(data->object,data->pspec->name,value,NULL);
}

static void on_uint_control_notify(const BtIcControl *control,GParamSpec *arg,gpointer user_data) {
  BtControlData *data=(BtControlData *)(user_data);
  GParamSpecUInt *pspec=(GParamSpecUInt *)data->pspec;
  glong svalue,min,max;
  guint dvalue;

  g_object_get((gpointer)(data->control),"value",&svalue,"min",&min,"max",&max,NULL);
  dvalue=pspec->minimum+(guint)((svalue-min)*((gdouble)(pspec->maximum-pspec->minimum)/(gdouble)(max-min)));
  dvalue=CLAMP(dvalue,pspec->minimum,pspec->maximum);
  g_object_set(data->object,data->pspec->name,dvalue,NULL);
}

static void on_double_control_notify(const BtIcControl *control,GParamSpec *arg,gpointer user_data) {
  BtControlData *data=(BtControlData *)(user_data);
  GParamSpecDouble *pspec=(GParamSpecDouble *)data->pspec;
  glong svalue,min,max;
  gdouble dvalue;

  g_object_get((gpointer)(data->control),"value",&svalue,"min",&min,"max",&max,NULL);
  dvalue=pspec->minimum+((svalue-min)*((pspec->maximum-pspec->minimum)/(gdouble)(max-min)));
  dvalue=CLAMP(dvalue,pspec->minimum,pspec->maximum);
  //GST_INFO("setting %s value %lf",data->pspec->name,dvalue);
  g_object_set(data->object,data->pspec->name,dvalue,NULL);
}

/**
 * bt_machine_bind_parameter_control:
 * @self: machine
 * @object: child object (global or voice child)
 * @property_name: name of the parameter
 * @control: interaction control object
 *
 * Connect the interaction control object to the give parameter. Changes of the
 * control-value are mapped into a change of the parameter.
 */
void bt_machine_bind_parameter_control(const BtMachine * const self, GstObject *object, const gchar *property_name, BtIcControl *control) {
  BtControlData *data;
  GParamSpec *pspec;
  BtIcDevice *device;
  gboolean new_data=FALSE;

  pspec=g_object_class_find_property(G_OBJECT_GET_CLASS(object),property_name);

  data=(BtControlData *)g_hash_table_lookup(self->priv->control_data,(gpointer)pspec);
  if(!data) {
    new_data=TRUE;
    data=g_new(BtControlData,1);
    data->object=object;
    data->pspec=pspec;
  }
  else {
    // stop the old device
    g_object_get((gpointer)(data->control),"device",&device,NULL);
    btic_device_stop(device);
    g_object_unref(device);
    // disconnect old signal handler
    g_signal_handler_disconnect((gpointer)data->control,data->handler_id);
    g_object_unref((gpointer)(data->control));
  }
  data->control=g_object_ref(control);
  // start the new device
  g_object_get((gpointer)(data->control),"device",&device,NULL);
  btic_device_start(device);
  g_object_unref(device);
  /* @todo: controls need flags to indicate whether they are absolute or relative
   * we conect a different handler for relative ones that add/sub values to current value
   */
  // connect signal handler
  switch(bt_g_type_get_base_type(pspec->value_type)) {
    case G_TYPE_BOOLEAN:
      data->handler_id=g_signal_connect(control,"notify::value",G_CALLBACK(on_boolean_control_notify),(gpointer)data);
      break;
    case G_TYPE_UINT:
      data->handler_id=g_signal_connect(control,"notify::value",G_CALLBACK(on_uint_control_notify),(gpointer)data);
      break;
    case G_TYPE_DOUBLE:
      data->handler_id=g_signal_connect(control,"notify::value",G_CALLBACK(on_double_control_notify),(gpointer)data);
      break;
    default:
      GST_WARNING_OBJECT(self,"unhandled type \"%s\"",G_PARAM_SPEC_TYPE_NAME(pspec));
      break;
  }

  if(new_data) {
    g_hash_table_insert(self->priv->control_data,(gpointer)pspec,(gpointer)data);
  }
}

/**
 * bt_machine_unbind_parameter_control:
 * @self: machine
 * @object: child object (global or voice child)
 * @property_name: name of the parameter
 *
 * Disconnect the interaction control object from the give parameter.
 */
void bt_machine_unbind_parameter_control(const BtMachine * const self, GstObject *object, const char *property_name) {
  GParamSpec *pspec;

  pspec=g_object_class_find_property(G_OBJECT_GET_CLASS(object),property_name);
  g_hash_table_remove(self->priv->control_data,(gpointer)pspec);
}

/**
 * bt_machine_unbind_parameter_controls:
 * @self: machine
 *
 * Disconnect all interaction controls.
 */
void bt_machine_unbind_parameter_controls(const BtMachine * const self) {
  g_hash_table_remove_all(self->priv->control_data);
}

//-- settings

static void
bt_g_object_randomize_parameter(GObject *self, GParamSpec *property) {
  gdouble rnd = ((gdouble) rand ()) / (RAND_MAX + 1.0);

  GST_DEBUG ("set random value for property: %s (type is %s)", property->name,
      G_PARAM_SPEC_TYPE_NAME (property));

  switch (bt_g_type_get_base_type(property->value_type)) {
    case G_TYPE_BOOLEAN:{
      g_object_set (self, property->name, (gboolean) (2.0 * rnd), NULL);
    } break;
    case G_TYPE_INT:{
      const GParamSpecInt *int_property = G_PARAM_SPEC_INT (property);

      g_object_set (self, property->name,
          (gint) (int_property->minimum + ((int_property->maximum -
                  int_property->minimum) * rnd)), NULL);
    } break;
    case G_TYPE_UINT:{
      const GParamSpecUInt *uint_property = G_PARAM_SPEC_UINT (property);

      g_object_set (self, property->name,
          (guint) (uint_property->minimum + ((uint_property->maximum -
                   uint_property->minimum) * rnd)), NULL);
    } break;
    case G_TYPE_LONG:{
      const GParamSpecLong *long_property = G_PARAM_SPEC_LONG (property);

      g_object_set (self, property->name,
          (glong) (long_property->minimum + ((long_property->maximum -
                  long_property->minimum) * rnd)), NULL);
    } break;
    case G_TYPE_ULONG:{
      const GParamSpecULong *ulong_property = G_PARAM_SPEC_ULONG (property);

      g_object_set (self, property->name,
          (gulong) (ulong_property->minimum + ((ulong_property->maximum -
                   ulong_property->minimum) * rnd)), NULL);
    } break;
    case G_TYPE_FLOAT:{
      const GParamSpecFloat *float_property =
          G_PARAM_SPEC_FLOAT (property);

      g_object_set (self, property->name,
          (gfloat) (float_property->minimum + ((float_property->maximum -
                    float_property->minimum) * rnd)), NULL);
    } break;
    case G_TYPE_DOUBLE:{
      const GParamSpecDouble *double_property =
          G_PARAM_SPEC_DOUBLE (property);

      g_object_set (self, property->name,
          (gdouble) (double_property->minimum + ((double_property->maximum -
                     double_property->minimum) * rnd)), NULL);
    } break;
    case G_TYPE_ENUM:{
      const GParamSpecEnum *enum_property = G_PARAM_SPEC_ENUM (property);
      const GEnumClass *enum_class = enum_property->enum_class;
      gint value = (enum_class->minimum + ((enum_class->maximum -
                    enum_class->minimum) * rnd));

      // handle sparse enums (lets go the next smaller valid value for now)
      while(!g_enum_get_value((GEnumClass *)enum_class, value) && (value>=enum_class->minimum)) {
        value--;
      }      
      g_object_set (self, property->name, value, NULL);
    } break;
    default:
      GST_WARNING ("incomplete implementation for GParamSpec type '%s'",
          G_PARAM_SPEC_TYPE_NAME (property));
  }
}

/**
 * bt_machine_randomize_parameters:
 * @self: machine
 *
 * Randomizes machine parameters.
 */
void bt_machine_randomize_parameters(const BtMachine * const self) {
  GObject *machine=G_OBJECT(self->priv->machines[PART_MACHINE]),*voice;
  gulong i,j;

  for(i=0;i<self->priv->global_params;i++) {
    bt_g_object_randomize_parameter(machine,self->priv->global_props[i]);
  }
  for(j=0;j<self->priv->voices;j++) {
    voice=G_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(machine),j));
    for(i=0;i<self->priv->voice_params;i++) {
      bt_g_object_randomize_parameter(voice,self->priv->voice_props[i]);
    }
  }
  bt_machine_set_param_defaults(self);
}

/**
 * bt_machine_reset_parameters:
 * @self: machine
 *
 * Resets machine parameters back to defaults.
 */
void bt_machine_reset_parameters(const BtMachine * const self) {
  GObject *machine=G_OBJECT(self->priv->machines[PART_MACHINE]),*voice;
  GValue gvalue={0,};
  gulong i,j;

  for(i=0;i<self->priv->global_params;i++) {
    g_value_init(&gvalue, GLOBAL_PARAM_TYPE(i));
    g_param_value_set_default(self->priv->global_props[i], &gvalue);
    g_object_set_property (machine, GLOBAL_PARAM_NAME(i), &gvalue);
    g_value_unset(&gvalue);
  }
  for(j=0;j<self->priv->voices;j++) {
    voice=G_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(machine),j));
    for(i=0;i<self->priv->voice_params;i++) {
      g_value_init(&gvalue, VOICE_PARAM_TYPE(i));
      g_param_value_set_default(self->priv->voice_props[i], &gvalue);
      g_object_set_property(voice, VOICE_PARAM_NAME(i), &gvalue);
      g_value_unset(&gvalue);
    }
  }
}

//-- linking

/**
 * bt_machine_get_wire_by_dst_machine:
 * @self: the machine that is at src of a wire
 * @dst: the machine that is at the dst end of the wire
 *
 * Searches for a wire in the wires originating from this machine that uses the
 * given #BtMachine instances as a target. Unref the wire, when done with it.
 *
 * Returns: the #BtWire or NULL
 *
 * Since: 0.6
 */
BtWire *bt_machine_get_wire_by_dst_machine(const BtMachine * const self, const BtMachine * const dst) {
  gboolean found=FALSE;
  BtMachine * const machine;
  const GList *node;

  g_return_val_if_fail(BT_IS_MACHINE(self),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(dst),NULL);
  
  // either src or dst has no wires
  if(!self->src_wires || !dst->dst_wires) return(NULL);
  
  // check if self links to dst
  // ideally we would search the shorter of the lists
  for(node=self->src_wires;node;node=g_list_next(node)) {
    BtWire * const wire=BT_WIRE(node->data);
    g_object_get(wire,"dst",&machine,NULL);
    if(machine==dst) found=TRUE;
    g_object_unref(machine);
    if(found) return(g_object_ref(wire));
  }
  GST_DEBUG("no wire found for machines %p:%s %p:%s",self,GST_OBJECT_NAME(self),dst,GST_OBJECT_NAME(dst));
  return(NULL);
}


//-- debug helper

// used in bt_song_write_to_highlevel_dot_file
GList *bt_machine_get_element_list(const BtMachine * const self) {
  GList *list=NULL;
  gulong i;

  for(i=0;i<PART_COUNT;i++) {
    if(self->priv->machines[i]) {
      list=g_list_append(list,self->priv->machines[i]);
    }
  }

  return(list);
}

void bt_machine_dbg_print_parts(const BtMachine * const self) {
  /* [A AC I<L IG I>L M O<L OG O>L S] */
  GST_INFO("%s [%s %s %s %s %s %s %s %s %s %s]",
    self->priv->id,
    self->priv->machines[PART_ADDER]?"A":"a",
    self->priv->machines[PART_ADDER_CONVERT]?"AC":"ac",
    self->priv->machines[PART_INPUT_PRE_LEVEL]?"I<L":"i<l",
    self->priv->machines[PART_INPUT_GAIN]?"IG":"ig",
    self->priv->machines[PART_INPUT_POST_LEVEL]?"I>L":"i>l",
    self->priv->machines[PART_MACHINE]?"M":"m",
    self->priv->machines[PART_OUTPUT_PRE_LEVEL]?"O<L":"o<l",
    self->priv->machines[PART_OUTPUT_GAIN]?"OG":"og",
    self->priv->machines[PART_OUTPUT_POST_LEVEL]?"O>L":"o>l",
    self->priv->machines[PART_SPREADER]?"S":"s"
  );
}

#if 0
void bt_machine_dbg_dump_global_controller_queue(const BtMachine * const self) {
  gulong i;
  FILE *file;
  gchar *name,*str;
  GList *list,*node;
  GstTimedValue *tv;

  if(!self->priv->global_controller)
    return;
  
  for(i=0;i<self->priv->global_params;i++) {
    name=g_strdup_printf("%s"G_DIR_SEPARATOR_S"buzztard-%s_g%02lu.dat",g_get_tmp_dir(),self->priv->id,i);
    if((file=fopen(name,"wb"))) {
      fprintf(file,"# global param \"%s\" for machine \"%s\"\n",GLOBAL_PARAM_NAME(i),self->priv->id);
      GstControlSource *cs;

      list=NULL;
      if((cs=gst_controller_get_control_source(self->priv->global_controller,GLOBAL_PARAM_NAME(i)))) {
        list=gst_interpolation_control_source_get_all(GST_INTERPOLATION_CONTROL_SOURCE(cs));
        g_object_unref(cs);
      }
      if(list) {
        for(node=list;node;node=g_list_next(node)) {
          tv=(GstTimedValue *)node->data;
          str=g_strdup_value_contents(&tv->value);
          fprintf(file,"%"GST_TIME_FORMAT" %"G_GUINT64_FORMAT" %s\n",GST_TIME_ARGS(tv->timestamp),tv->timestamp,str);
          g_free(str);
        }
        g_list_free(list);
      }
      fclose(file);
    }
    g_free(name);
  }
}

void bt_machine_dbg_dump_voice_controller_queue(const BtMachine * const self) {
  gulong i;
  FILE *file;
  gchar *name,*str;
  GList *list,*node;
  GstTimedValue *tv;

  if(!self->priv->voice_controllers || !self->priv->voice_controllers[0])
    return;
  
  for(i=0;i<self->priv->voice_params;i++) {
    name=g_strdup_printf("%s"G_DIR_SEPARATOR_S"buzztard-%s_v%02lu.dat",g_get_tmp_dir(),self->priv->id,i);
    if((file=fopen(name,"wb"))) {
      fprintf(file,"# voice 0 param \"%s\" for machine \"%s\"\n",VOICE_PARAM_NAME(i),self->priv->id);
      GstControlSource *cs;

      list=NULL;
      if((cs=gst_controller_get_control_source(self->priv->voice_controllers[0],VOICE_PARAM_NAME(i)))) {
        list=gst_interpolation_control_source_get_all(GST_INTERPOLATION_CONTROL_SOURCE(cs));
        g_object_unref(cs);
      }
      if(list) {
        for(node=list;node;node=g_list_next(node)) {
          tv=(GstTimedValue *)node->data;
          str=g_strdup_value_contents(&tv->value);
          fprintf(file,"%"GST_TIME_FORMAT" %"G_GUINT64_FORMAT" %s\n",GST_TIME_ARGS(tv->timestamp),tv->timestamp,str);
          g_free(str);
        }
        g_list_free(list);
      }
      fclose(file);
    }
    g_free(name);
  }
}
#endif

//-- io interface

static xmlNodePtr bt_machine_persistence_save(const BtPersistence * const persistence, const xmlNodePtr const parent_node) {
  const BtMachine * const self = BT_MACHINE(persistence);
  GstObject *machine,*machine_voice;
  xmlNodePtr node=NULL;
  xmlNodePtr child_node;
  gulong i,j;
  GValue value={0,};

  GST_DEBUG("PERSISTENCE::machine");

  if((node=xmlNewChild(parent_node,NULL,XML_CHAR_PTR("machine"),NULL))) {
    xmlNewProp(node,XML_CHAR_PTR("id"),XML_CHAR_PTR(self->priv->id));

    // @todo: also store non-controllable parameters (preferences) <prefsdata name="" value="">
    // @todo: skip parameters which are default values (is that really a good idea?)
    machine=GST_OBJECT(self->priv->machines[PART_MACHINE]);
    for(i=0;i<self->priv->global_params;i++) {
      // skip trigger parameters and parameters that are also used as voice params
      if(bt_machine_is_global_param_trigger(self,i)) continue;
      if(self->priv->voice_params && bt_machine_get_voice_param_index(self,GLOBAL_PARAM_NAME(i),NULL)>-1) continue;

      if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("globaldata"),NULL))) {
        g_value_init(&value,GLOBAL_PARAM_TYPE(i));
        g_object_get_property(G_OBJECT(machine),GLOBAL_PARAM_NAME(i),&value);
        gchar * const str=bt_persistence_get_value(&value);
        xmlNewProp(child_node,XML_CHAR_PTR("name"),XML_CHAR_PTR(GLOBAL_PARAM_NAME(i)));
        xmlNewProp(child_node,XML_CHAR_PTR("value"),XML_CHAR_PTR(str));
        g_free(str);
        g_value_unset(&value);
      }
    }
    for(j=0;j<self->priv->voices;j++) {
      machine_voice=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(machine),j);

      for(i=0;i<self->priv->voice_params;i++) {
        if(bt_machine_is_voice_param_trigger(self,i)) continue;
        if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("voicedata"),NULL))) {
          g_value_init(&value,VOICE_PARAM_TYPE(i));
          g_object_get_property(G_OBJECT(machine_voice),VOICE_PARAM_NAME(i),&value);
          gchar * const str=bt_persistence_get_value(&value);
          xmlNewProp(child_node,XML_CHAR_PTR("voice"),XML_CHAR_PTR(bt_persistence_strfmt_ulong(j)));
          xmlNewProp(child_node,XML_CHAR_PTR("name"),XML_CHAR_PTR(VOICE_PARAM_NAME(i)));
          xmlNewProp(child_node,XML_CHAR_PTR("value"),XML_CHAR_PTR(str));
          g_free(str);
          g_value_unset(&value);
        }
      }
      g_object_unref(machine_voice);
    }
    
    if(g_hash_table_size(self->priv->properties)) {
      if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("properties"),NULL))) {
        if(!bt_persistence_save_hashtable(self->priv->properties,child_node)) goto Error;
      }
      else goto Error;
    }
    if(bt_machine_has_patterns(self)) {
      if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("patterns"),NULL))) {
        bt_persistence_save_list(self->priv->patterns,child_node);
      }
      else goto Error;
    }
    if(g_hash_table_size(self->priv->control_data)) {
      if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("interaction-controllers"),NULL))) {
        GList *list=NULL,*lnode;
        BtControlData *data;
        BtIcDevice *device;
        gchar *device_name,*control_name;
        xmlNodePtr sub_node;

        g_hash_table_foreach(self->priv->control_data,bt_persistence_collect_hashtable_entries,(gpointer)&list);

        for(lnode=list;lnode;lnode=g_list_next(lnode)) {
          data=(BtControlData *)lnode->data;

          g_object_get((gpointer)(data->control),"device",&device,"name",&control_name,NULL);
          g_object_get(device,"name",&device_name,NULL);
          g_object_unref(device);

          sub_node=xmlNewChild(child_node,NULL,XML_CHAR_PTR("interaction-controller"),NULL);
          // we need global or voiceXX here
          if(data->object==(GstObject *)self->priv->machines[PART_MACHINE]) {
            xmlNewProp(sub_node,XML_CHAR_PTR("global"),XML_CHAR_PTR("0"));
          }
          else {
            if(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE])) {
              GstObject *voice_child;
              gulong i;
              gboolean found=FALSE;

              for(i=0;i<self->priv->voices;i++) {
                if((voice_child=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),i))) {
                  if(data->object==voice_child) {
                    xmlNewProp(sub_node,XML_CHAR_PTR("voice"),XML_CHAR_PTR(bt_persistence_strfmt_ulong(i)));
                    found=TRUE;
                  }
                  g_object_unref(voice_child);
                  if(found) break;
                }
              }
            }
          }
          xmlNewProp(sub_node,XML_CHAR_PTR("parameter"),XML_CHAR_PTR(data->pspec->name));
          xmlNewProp(sub_node,XML_CHAR_PTR("device"),XML_CHAR_PTR(device_name));
          xmlNewProp(sub_node,XML_CHAR_PTR("control"),XML_CHAR_PTR(control_name));

          g_free(device_name);
          g_free(control_name);
        }
        g_list_free(list);
      }
      else goto Error;
    }
  }
Error:
  return(node);
}

static BtPersistence *bt_machine_persistence_load(const GType type, const BtPersistence * const persistence, xmlNodePtr node, GError **err, va_list var_args) {
  BtMachine * const self = BT_MACHINE(persistence);
  xmlChar *name,*global_str,*voice_str,*value_str;
  xmlNodePtr child_node;
  GValue value={0,};
  glong param,voice;
  GstObject *machine,*machine_voice;
  GError *error=NULL;

  GST_DEBUG("PERSISTENCE::machine");
  g_assert(node);

  if((machine=GST_OBJECT(self->priv->machines[PART_MACHINE]))) {
    for(node=node->children;node;node=node->next) {
      if(!xmlNodeIsText(node)) {
        // @todo: load prefsdata
        if(!strncmp((gchar *)node->name,"globaldata\0",11)) {
          name=xmlGetProp(node,XML_CHAR_PTR("name"));
          value_str=xmlGetProp(node,XML_CHAR_PTR("value"));
          param=bt_machine_get_global_param_index(self,(gchar *)name,&error);
          if(!error) {
            if(value_str) {
              g_value_init(&value,GLOBAL_PARAM_TYPE(param));
              bt_persistence_set_value(&value,(gchar *)value_str);
              g_object_set_property(G_OBJECT(machine),(gchar *)name,&value);
              g_value_unset(&value);
              bt_machine_set_global_param_default(self,
              bt_machine_get_global_param_index(self,(gchar *)name,NULL));
            }
            GST_INFO("initialized global machine data for param %ld: %s",param, name);
          }
          else {
            GST_WARNING_OBJECT(self,"error while loading global machine data for param %ld: %s",param,error->message);
            g_error_free(error);error=NULL;
          }
          xmlFree(name);xmlFree(value_str);
        }
        else if(!strncmp((gchar *)node->name,"voicedata\0",10)) {
          voice_str=xmlGetProp(node,XML_CHAR_PTR("voice"));
          voice=atol((char *)voice_str);
          name=xmlGetProp(node,XML_CHAR_PTR("name"));
          value_str=xmlGetProp(node,XML_CHAR_PTR("value"));
          param=bt_machine_get_voice_param_index(self,(gchar *)name,&error);
          if(!error) {
            if(value_str) {
              machine_voice=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(machine),voice);
              g_assert(machine_voice);
    
              g_value_init(&value,VOICE_PARAM_TYPE(param));
              bt_persistence_set_value(&value,(gchar *)value_str);
              g_object_set_property(G_OBJECT(machine_voice),(gchar *)name,&value);
              bt_machine_set_voice_param_default(self,voice,
                bt_machine_get_voice_param_index(self,(gchar *)name,NULL));
              g_value_unset(&value);
              g_object_unref(machine_voice);
            }
            GST_INFO("initialized voice machine data for param %ld: %s",param, name);
          }
          else {
            GST_WARNING_OBJECT(self,"error while loading voice machine data for param %ld, voice %ld: %s",param,voice,error->message);
            g_error_free(error);error=NULL;
          }
          xmlFree(name);xmlFree(value_str);xmlFree(voice_str);
        }
        else if(!strncmp((gchar *)node->name,"properties\0",11)) {
          bt_persistence_load_hashtable(self->priv->properties,node);
        }
        else if(!strncmp((gchar *)node->name,"patterns\0",9)) {
          BtPattern *pattern;
  
          for(child_node=node->children;child_node;child_node=child_node->next) {
            if((!xmlNodeIsText(child_node)) && (!strncmp((char *)child_node->name,"pattern\0",8))) {
              GError *err=NULL;
              pattern=BT_PATTERN(bt_persistence_load(BT_TYPE_PATTERN,NULL,child_node,&err,"song",self->priv->song,"machine",self,NULL));
              if(err!=NULL) {
                GST_WARNING_OBJECT(self,"Can't create pattern: %s",err->message);
                g_error_free(err);
              }
              g_object_unref(pattern);
            }
          }
        }
        else if(!strncmp((gchar *)node->name,"interaction-controllers\0",24)) {
          BtIcRegistry *registry;
          BtIcDevice *device;
          BtIcControl *control;
          GList *lnode,*devices,*controls;
          gchar *name;
          xmlChar *device_str,*control_str,*property_name;
          gboolean found;
  
          registry=btic_registry_new();
          g_object_get(registry,"devices",&devices,NULL);
  
          for(child_node=node->children;child_node;child_node=child_node->next) {
            if((!xmlNodeIsText(child_node)) && (!strncmp((char *)child_node->name,"interaction-controller\0",23))) {
              control=NULL;
  
              if((device_str=xmlGetProp(child_node,XML_CHAR_PTR("device")))) {
                found=FALSE;
                for(lnode=devices;lnode;lnode=g_list_next(lnode)) {
                  device=BTIC_DEVICE(lnode->data);
                  g_object_get(device,"name",&name,NULL);
                  if(!strcmp(name,(gchar *)device_str))
                    found=TRUE;
                  g_free(name);
                  if(found) break;
                }
                if(found) {
                  if((control_str=xmlGetProp(child_node,XML_CHAR_PTR("control")))) {
                    found=FALSE;
                    g_object_get(device,"controls",&controls,NULL);
                    for(lnode=controls;lnode;lnode=g_list_next(lnode)) {
                      control=BTIC_CONTROL(lnode->data);
                      g_object_get(control,"name",&name,NULL);
                      if(!strcmp(name,(gchar *)control_str))
                        found=TRUE;
                      g_free(name);
                      if(found) break;
                    }
                    g_list_free(controls);
                    if(found) {
                      if((property_name=xmlGetProp(child_node,XML_CHAR_PTR("parameter")))) {
                        if((global_str=xmlGetProp(child_node,XML_CHAR_PTR("global")))) {
                          bt_machine_bind_parameter_control(self,machine,(gchar*)property_name,control);
                          xmlFree(global_str);
                        }
                        else {
                          if((voice_str=xmlGetProp(child_node,XML_CHAR_PTR("voice")))) {
                            voice=atol((char *)voice_str);
                            machine_voice=gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(machine),voice);
                            bt_machine_bind_parameter_control(self,machine_voice,(gchar*)property_name,control);
                            g_object_unref(machine_voice);
                            xmlFree(voice_str);
                          }
                        }
                        xmlFree(property_name);
                      }
                    }
                    xmlFree(control_str);
                  }
                }
                xmlFree(device_str);
              }
            }
          }
          g_list_free(devices);
        }
      }
    }
  }
  return(BT_PERSISTENCE(persistence));
}

static void bt_machine_persistence_interface_init(gpointer const g_iface, gconstpointer const iface_data) {
  BtPersistenceInterface * const iface = g_iface;

  iface->load = bt_machine_persistence_load;
  iface->save = bt_machine_persistence_save;
}

//-- wrapper

//-- gstelement overrides

static GstPad* bt_machine_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar* _name) {
  BtMachine * const self=BT_MACHINE(element);
  gchar *name;
  GstPad *pad, *target;
  
  // check direction
  if(GST_PAD_TEMPLATE_DIRECTION(templ)==GST_PAD_SRC) {
    target=gst_element_get_request_pad(self->priv->machines[PART_SPREADER],"src%d");
    name=g_strdup_printf("src%d", self->priv->src_pad_counter++);
    GST_INFO_OBJECT(element,"request src pad: %s",name);
  }
  else {
    target=gst_element_get_request_pad(self->priv->machines[PART_ADDER],"sink%d");
    name=g_strdup_printf ("sink%d", self->priv->sink_pad_counter++);
    GST_INFO_OBJECT(element,"request sink pad: %s",name);
  }
  if((pad=gst_ghost_pad_new(name,target))) {
    GST_INFO("%s:%s: %s%s%s",GST_DEBUG_PAD_NAME(target),
      GST_OBJECT(target)->flags&GST_PAD_BLOCKED?"blocked, ":"",
      GST_OBJECT(target)->flags&GST_PAD_FLUSHING?"flushing, ":"",
      GST_OBJECT(target)->flags&GST_PAD_BLOCKING?"blocking, ":"");
    GST_INFO("%s:%s: %s%s%s",GST_DEBUG_PAD_NAME(pad),
      GST_OBJECT(pad)->flags&GST_PAD_BLOCKED?"blocked, ":"",
      GST_OBJECT(pad)->flags&GST_PAD_FLUSHING?"flushing, ":"",
      GST_OBJECT(pad)->flags&GST_PAD_BLOCKING?"blocking, ":"");
    
    if(GST_STATE(element)==GST_STATE_PLAYING) {
      GST_DEBUG_OBJECT(element,"activating pad");
      gst_pad_set_active(pad, TRUE);
    }
    gst_element_add_pad(element, pad);
  }
  else {
    GST_WARNING_OBJECT(element,"failed to create ghostpad %s to target %s:%s",name,GST_DEBUG_PAD_NAME(target));
  }
  gst_object_unref (target);
  g_free(name);

  return(pad);
}

static void	bt_machine_release_pad(GstElement *element, GstPad *pad) {
  BtMachine * const self=BT_MACHINE(element);
  GstPad *target;
  
  if(GST_STATE(element)==GST_STATE_PLAYING) {
    GST_DEBUG_OBJECT(element,"deactivating pad");
    gst_pad_set_active(pad, FALSE);
  }
  
  target=gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
  gst_element_remove_pad(element, pad);

  if(gst_pad_get_direction(pad)==GST_PAD_SRC) {
    GST_INFO_OBJECT(element,"release src pad: %s:%s", GST_DEBUG_PAD_NAME(target));
    gst_element_release_request_pad(self->priv->machines[PART_SPREADER],target);
  }
  else {
    GST_INFO_OBJECT(element,"release sink pad: %s:%s", GST_DEBUG_PAD_NAME(target));
    gst_element_release_request_pad(self->priv->machines[PART_ADDER],target);
  }
  gst_object_unref (target);
}


//-- gobject overrides

static void bt_machine_constructed(GObject *object) {
  BtMachine * const self=BT_MACHINE(object);
  BtPattern *pattern;
  
  GST_INFO("machine constructed ...");

  if(G_OBJECT_CLASS(parent_class)->constructed)
    G_OBJECT_CLASS(parent_class)->constructed(object);

  g_return_if_fail(BT_IS_SONG(self->priv->song));
  g_return_if_fail(BT_IS_STRING(self->priv->id));
  g_return_if_fail(BT_IS_STRING(self->priv->plugin_name));

  GST_INFO("initializing machine");
  
  gst_object_set_name(GST_OBJECT(self),self->priv->id);
  GST_INFO("naming machine : %s",self->priv->id);

  // name the machine and try to instantiate it
  if(!bt_machine_init_core_machine(self)) {
    goto Error;
  }

  // initialize iface properties
  bt_machine_init_interfaces(self);
  // we need to make sure the machine is from the right class
  if(!bt_machine_check_type(self)) {
    goto Error;
  }

  GST_DEBUG("machine-refs: %d",G_OBJECT_REF_COUNT(self));

  // register global params
  bt_machine_init_global_params(self);
  // register voice params
  bt_machine_init_voice_params(self);

  GST_DEBUG("machine-refs: %d",G_OBJECT_REF_COUNT(self));

  // post sanity checks
  GST_INFO("  added machine %p to bin, machine->ref_count=%d",self->priv->machines[PART_MACHINE],G_OBJECT_REF_COUNT(self->priv->machines[PART_MACHINE]));
  g_assert(self->priv->machines[PART_MACHINE]!=NULL);
  if(!(self->priv->global_params+self->priv->voice_params)) {
    GST_WARNING_OBJECT(self,"  machine %s has no params",self->priv->id);
  }

  // prepare common internal patterns for the machine
  pattern=bt_pattern_new_with_event(self->priv->song,self,BT_PATTERN_CMD_BREAK);
  g_object_unref(pattern);
  pattern=bt_pattern_new_with_event(self->priv->song,self,BT_PATTERN_CMD_MUTE);
  g_object_unref(pattern);

  GST_INFO("machine constructed");
  return;
Error:
  GST_WARNING_OBJECT(self,"failed to create machine: %s",self->priv->plugin_name);
  if(self->priv->constrution_error) {
    g_set_error(self->priv->constrution_error, error_domain, /* errorcode= */0,
               "failed to setup the machine.");
  }
}

/* returns a property for the given property_id for this object */
static void bt_machine_get_property(GObject * const object, const guint property_id, GValue * const value, GParamSpec * const pspec) {
  const BtMachine * const self = BT_MACHINE(object);

  return_if_disposed();
  switch (property_id) {
    case MACHINE_CONSTRUCTION_ERROR: {
      g_value_set_pointer(value, self->priv->constrution_error);
    } break;
    case MACHINE_PROPERTIES: {
      g_value_set_pointer(value, self->priv->properties);
    } break;
    case MACHINE_SONG: {
      g_value_set_object(value, self->priv->song);
    } break;
    case MACHINE_ID: {
      g_value_set_string(value, self->priv->id);
    } break;
    case MACHINE_PLUGIN_NAME: {
      g_value_set_string(value, self->priv->plugin_name);
    } break;
    case MACHINE_VOICES: {
      g_value_set_ulong(value, self->priv->voices);
    } break;
    case MACHINE_GLOBAL_PARAMS: {
      g_value_set_ulong(value, self->priv->global_params);
    } break;
    case MACHINE_VOICE_PARAMS: {
      g_value_set_ulong(value, self->priv->voice_params);
    } break;
    case MACHINE_MACHINE: {
      g_value_set_object(value, self->priv->machines[PART_MACHINE]);
    } break;
    case MACHINE_ADDER_CONVERT: {
      g_value_set_object(value, self->priv->machines[PART_ADDER_CONVERT]);
    } break;
    case MACHINE_INPUT_PRE_LEVEL: {
      g_value_set_object(value, self->priv->machines[PART_INPUT_PRE_LEVEL]);
    } break;
    case MACHINE_INPUT_GAIN: {
      g_value_set_object(value, self->priv->machines[PART_INPUT_GAIN]);
    } break;
    case MACHINE_INPUT_POST_LEVEL: {
      g_value_set_object(value, self->priv->machines[PART_INPUT_POST_LEVEL]);
    } break;
    case MACHINE_OUTPUT_PRE_LEVEL: {
      g_value_set_object(value, self->priv->machines[PART_OUTPUT_PRE_LEVEL]);
    } break;
    case MACHINE_OUTPUT_GAIN: {
      g_value_set_object(value, self->priv->machines[PART_OUTPUT_GAIN]);
    } break;
    case MACHINE_OUTPUT_POST_LEVEL: {
      g_value_set_object(value, self->priv->machines[PART_OUTPUT_POST_LEVEL]);
    } break;
    case MACHINE_PATTERNS: {
      g_value_set_pointer(value,g_list_copy(self->priv->patterns));
    } break;
    case MACHINE_STATE: {
      g_value_set_enum(value, self->priv->state);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_machine_set_property(GObject * const object, const guint property_id, const GValue * const value, GParamSpec * const pspec) {
  const BtMachine * const self = BT_MACHINE(object);

  return_if_disposed();
  switch (property_id) {
    case MACHINE_CONSTRUCTION_ERROR: {
      self->priv->constrution_error=(GError **)g_value_get_pointer(value);
    } break;
    case MACHINE_SONG: {
      self->priv->song = BT_SONG(g_value_get_object(value));
      g_object_try_weak_ref(self->priv->song);
      //GST_DEBUG("set the song for machine: %p",self->priv->song);
    } break;
    case MACHINE_ID: {
      g_free(self->priv->id);
      self->priv->id = g_value_dup_string(value);
      GST_DEBUG("set the id for machine: %s",self->priv->id);
      if(self->priv->machines[PART_MACHINE]) {
        GstObject *parent=gst_object_get_parent(GST_OBJECT(self->priv->machines[PART_MACHINE]));
        if(!parent) {
          gchar *name=g_alloca(strlen(self->priv->id)+16);

          g_sprintf(name,"%s_%p",self->priv->id,self);
          gst_element_set_name(self->priv->machines[PART_MACHINE],name);
        }
        else {
          gst_object_unref(parent);
        }
      }
      bt_song_set_unsaved(self->priv->song,TRUE);
    } break;
    case MACHINE_PLUGIN_NAME: {
      g_free(self->priv->plugin_name);
      self->priv->plugin_name = g_value_dup_string(value);
      GST_DEBUG("set the plugin_name for machine: %s",self->priv->plugin_name);
    } break;
    case MACHINE_VOICES: {
      const gulong voices=self->priv->voices;
      self->priv->voices = g_value_get_ulong(value);
      if(GSTBT_IS_CHILD_BIN(self->priv->machines[PART_MACHINE])) {
        if(voices!=self->priv->voices) {
          GST_DEBUG("set the voices for machine: %lu",self->priv->voices);
          bt_machine_resize_voices(self,voices);
          bt_machine_resize_pattern_voices(self);
          bt_song_set_unsaved(self->priv->song,TRUE);
        }
      }
    } break;
    case MACHINE_GLOBAL_PARAMS: {
      self->priv->global_params = g_value_get_ulong(value);
    } break;
    case MACHINE_VOICE_PARAMS: {
      self->priv->voice_params = g_value_get_ulong(value);
    } break;
    case MACHINE_STATE: {
      if(bt_machine_change_state(self,g_value_get_enum(value))) {
        GST_DEBUG("set the state for machine: %d",self->priv->state);
        bt_song_set_unsaved(self->priv->song,TRUE);
      }
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_machine_dispose(GObject * const object) {
  const BtMachine * const self = BT_MACHINE(object);
  GObject *param_parent;
  guint i,j;

  return_if_disposed();
  self->priv->dispose_has_run = TRUE;

  GST_DEBUG_OBJECT(self,"!!!! self=%p,%s, song=%p",self,self->priv->id,self->priv->song);

  // shut down interaction control setup
  g_hash_table_destroy(self->priv->control_data);

  // disconnect notify handlers
  if(self->priv->song) {
    BtSongInfo *song_info;
    g_object_get((gpointer)(self->priv->song),"song-info",&song_info,NULL);
    if(song_info) {
      GST_DEBUG("  disconnecting song-info handlers");
      g_signal_handlers_disconnect_matched(song_info,G_SIGNAL_MATCH_FUNC,0,0,NULL,bt_machine_on_bpm_changed,NULL);
      g_signal_handlers_disconnect_matched(song_info,G_SIGNAL_MATCH_FUNC,0,0,NULL,bt_machine_on_tpb_changed,NULL);
      g_object_unref(song_info);
    }
  }
  
  // unref controllers
  GST_DEBUG("  releasing controllers, global.ref_ct=%d, voices=%lu",
    (self->priv->global_controller?G_OBJECT_REF_COUNT(self->priv->global_controller):-1),
    self->priv->voices);
  param_parent=G_OBJECT(self->priv->machines[PART_MACHINE]);
  for(j=0;j<self->priv->global_params;j++) {
    g_object_try_unref(self->priv->global_control_sources[j]);
    //bt_gst_object_deactivate_controller(param_parent, GLOBAL_PARAM_NAME(j));
  }
  //self->priv->global_controller=NULL; // <- this is wrong, controllers have a refcount on the gstelement
  g_object_try_unref(self->priv->global_controller);
  if(self->priv->voice_controllers) {
    for(i=0;i<self->priv->voices;i++) {
      param_parent=G_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE]),i));
      for(j=0;j<self->priv->voice_params;j++) {
        g_object_try_unref(self->priv->voice_control_sources[i*self->priv->voice_params+j]);
        //bt_gst_object_deactivate_controller(param_parent, VOICE_PARAM_NAME(j));
      }
      g_object_unref(param_parent);
      //self->priv->voice_controllers[i]=NULL; // <- this is wrong, controllers have a refcount on the gstelement
      g_object_try_unref(self->priv->voice_controllers[i]);
    }
  }
  
  // unref the pads
  for(i=0;i<PART_COUNT;i++) {
    if(self->priv->src_pads[i])
      gst_object_unref(self->priv->src_pads[i]);
    if(self->priv->sink_pads[i])
      gst_object_unref(self->priv->sink_pads[i]);
  }

  // gstreamer uses floating references, therefore elements are destroyed, when removed from the bin
  GST_DEBUG("  releasing song: %p",self->priv->song);
  g_object_try_weak_unref(self->priv->song);

  GST_DEBUG("  releasing patterns");
  // unref list of patterns
  if(self->priv->patterns) {
    GList* node;
    for(node=self->priv->patterns;node;node=g_list_next(node)) {
      g_object_try_unref(node->data);
      node->data=NULL;
    }
  }

  GST_DEBUG("  chaining up");
  G_OBJECT_CLASS(parent_class)->dispose(object);
  GST_DEBUG("  done");
}

static void bt_machine_finalize(GObject * const object) {
  const BtMachine * const self = BT_MACHINE(object);
  guint i;

  GST_DEBUG_OBJECT(self,"!!!! self=%p",self);

  g_hash_table_destroy(self->priv->properties);
  g_free(self->priv->id);
  g_free(self->priv->plugin_name);

  // unset no_values
  for(i=0;i<self->priv->global_params;i++) {
    if(G_IS_VALUE(&self->priv->global_no_val[i]))
      g_value_unset(&self->priv->global_no_val[i]);
  }
  for(i=0;i<self->priv->voice_params;i++) {
    if(G_IS_VALUE(&self->priv->voice_no_val[i]))
      g_value_unset(&self->priv->voice_no_val[i]);
  }

  g_free(self->priv->voice_quarks);
  g_free(self->priv->global_quarks);
  g_free(self->priv->voice_no_val);
  g_free(self->priv->global_no_val);
  g_free(self->priv->voice_flags);
  g_free(self->priv->global_flags);
  g_free(self->priv->voice_props);
  g_free(self->priv->global_props);
  g_free(self->priv->voice_controllers);
  g_free(self->priv->global_control_sources);
  g_free(self->priv->voice_control_sources);
  // free list of patterns
  if(self->priv->patterns) {
    g_list_free(self->priv->patterns);
    self->priv->patterns=NULL;
  }
  
  if(self->src_wires) {
    g_list_free(self->src_wires);
  }
  if(self->dst_wires) {
    g_list_free(self->dst_wires);
  }

  GST_DEBUG("  chaining up");
  G_OBJECT_CLASS(parent_class)->finalize(object);
  GST_DEBUG("  done");
}

//-- class internals

static void bt_machine_init(GTypeInstance * const instance, gconstpointer g_class) {
  BtMachine * const self = BT_MACHINE(instance);

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, BT_TYPE_MACHINE, BtMachinePrivate);
  // default is no voice, only global params
  //self->priv->voices=1;
  self->priv->properties=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);

  self->priv->control_data=g_hash_table_new_full(NULL,NULL,NULL,(GDestroyNotify)free_control_data);

  GST_DEBUG("!!!! self=%p",self);
}

static void bt_machine_class_init(BtMachineClass * const klass) {
  GObjectClass * const gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass * const gstelement_class = GST_ELEMENT_CLASS(klass);

  error_domain=g_type_qname(BT_TYPE_MACHINE);
  parent_class=g_type_class_peek_parent(klass);
  g_type_class_add_private(klass,sizeof(BtMachinePrivate));

  gobject_class->constructed  = bt_machine_constructed;
  gobject_class->set_property = bt_machine_set_property;
  gobject_class->get_property = bt_machine_get_property;
  gobject_class->dispose      = bt_machine_dispose;
  gobject_class->finalize     = bt_machine_finalize;
  
  gstelement_class->request_new_pad = bt_machine_request_new_pad;
  gstelement_class->release_pad     = bt_machine_release_pad;

  /**
   * BtMachine::pattern-added:
   * @self: the machine object that emitted the signal
   * @pattern: the new pattern
   *
   * A new pattern item has been added to the machine
   */
  signals[PATTERN_ADDED_EVENT] = g_signal_new("pattern-added",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        0,
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__OBJECT,
                                        G_TYPE_NONE, // return type
                                        1, // n_params
                                        BT_TYPE_PATTERN // param data
                                        );

  /**
   * BtMachine::pattern-removed:
   * @self: the machine object that emitted the signal
   * @pattern: the old pattern
   *
   * A pattern item has been removed from the machine
   */
  signals[PATTERN_REMOVED_EVENT] = g_signal_new("pattern-removed",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        0,
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__OBJECT,
                                        G_TYPE_NONE, // return type
                                        1, // n_params
                                        BT_TYPE_PATTERN // param data
                                        );

  g_object_class_install_property(gobject_class,MACHINE_CONSTRUCTION_ERROR,
                                  g_param_spec_pointer("construction-error",
                                     "construction error prop",
                                     "signal failed instance creation",
                                     G_PARAM_CONSTRUCT_ONLY|G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_PROPERTIES,
                                  g_param_spec_pointer("properties",
                                     "properties prop",
                                     "list of machine properties",
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_SONG,
                                  g_param_spec_object("song",
                                     "song contruct prop",
                                     "song object, the machine belongs to",
                                     BT_TYPE_SONG, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY|G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_ID,
                                  g_param_spec_string("id",
                                     "id contruct prop",
                                     "machine identifier",
                                     "unamed machine", /* default value */
                                     G_PARAM_CONSTRUCT|G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_PLUGIN_NAME,
                                  g_param_spec_string("plugin-name",
                                     "plugin-name construct prop",
                                     "the name of the gst plugin for the machine",
                                     "unamed machine", /* default value */
                                     G_PARAM_CONSTRUCT|G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_VOICES,
                                  g_param_spec_ulong("voices",
                                     "voices prop",
                                     "number of voices in the machine",
                                     0,
                                     G_MAXULONG,
                                     0,
                                     G_PARAM_CONSTRUCT|G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_GLOBAL_PARAMS,
                                  g_param_spec_ulong("global-params",
                                     "global-params prop",
                                     "number of params for the machine",
                                     0,
                                     G_MAXULONG,
                                     0,
                                     G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_VOICE_PARAMS,
                                  g_param_spec_ulong("voice-params",
                                     "voice-params prop",
                                     "number of params for each machine voice",
                                     0,
                                     G_MAXULONG,
                                     0,
                                     G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_MACHINE,
                                  g_param_spec_object("machine",
                                     "machine element prop",
                                     "the machine element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_ADDER_CONVERT,
                                  g_param_spec_object("adder-convert",
                                     "adder-convert prop",
                                     "the after mixing format converter element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_INPUT_PRE_LEVEL,
                                  g_param_spec_object("input-pre-level",
                                     "input-pre-level prop",
                                     "the pre-gain input-level element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_INPUT_GAIN,
                                  g_param_spec_object("input-gain",
                                     "input-gain prop",
                                     "the input-gain element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_INPUT_POST_LEVEL,
                                  g_param_spec_object("input-post-level",
                                     "input-post-level prop",
                                     "the post-gain input-level element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_OUTPUT_PRE_LEVEL,
                                  g_param_spec_object("output-pre-level",
                                     "output-pre-level prop",
                                     "the pre-gain output-level element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_OUTPUT_GAIN,
                                  g_param_spec_object("output-gain",
                                     "output-gain prop",
                                     "the output-gain element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_OUTPUT_POST_LEVEL,
                                  g_param_spec_object("output-post-level",
                                     "output-post-level prop",
                                     "the post-gain output-level element, if any",
                                     GST_TYPE_ELEMENT, /* object type */
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_PATTERNS,
                                  g_param_spec_pointer("patterns",
                                     "pattern list prop",
                                     "a copy of the list of patterns",
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class,MACHINE_STATE,
                                  g_param_spec_enum("state",
                                     "state prop",
                                     "the current state of this machine",
                                     BT_TYPE_MACHINE_STATE,  /* enum type */
                                     BT_MACHINE_STATE_NORMAL, /* default value */
                                     G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS));
}

GType bt_machine_get_type(void) {
  static GType type = 0;
  if (G_UNLIKELY(type == 0)) {
    const GTypeInfo info = {
      sizeof(BtMachineClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_machine_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof(BtMachine),
      0,   // n_preallocs
      (GInstanceInitFunc)bt_machine_init, // instance_init
      NULL // value_table
    };
    const GInterfaceInfo persistence_interface_info = {
      (GInterfaceInitFunc) bt_machine_persistence_interface_init,  // interface_init
      NULL, // interface_finalize
      NULL  // interface_data
    };
    type = g_type_register_static(GST_TYPE_BIN,"BtMachine",&info,G_TYPE_FLAG_ABSTRACT);
    g_type_add_interface_static(type, BT_TYPE_PERSISTENCE, &persistence_interface_info);
  }
  return type;
}
