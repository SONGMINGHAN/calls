/*
 * Copyright (C) 2018 Purism SPC
 *
 * This file is part of Calls.
 *
 * Calls is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Calls is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Calls.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Bob Ham <bob.ham@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "calls-ringer.h"
#include "calls-enumerate.h"
#include "config.h"

#include <gsound.h>
#include <glib/gi18n.h>
#include <glib-object.h>


struct _CallsRinger
{
  GtkApplicationWindow parent_instance;

  CallsProvider *provider;
  GSoundContext *ctx;
  unsigned       ring_count;
  GCancellable  *playing;
};

G_DEFINE_TYPE (CallsRinger, calls_ringer, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_PROVIDER,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


static void
ringer_error (CallsRinger *self,
              const gchar *prefix,
              GError      *error)
{
  g_warning ("%s: %s", prefix, error->message);
  g_error_free (error);

  g_clear_object (&self->ctx);
}


static gboolean
create_ctx (CallsRinger *self)
{
  GError *error = NULL;
  gboolean ok;

  self->ctx = gsound_context_new (NULL, &error);
  if (!self->ctx)
    {
      ringer_error (self, "Error creating GSound context", error);
      return FALSE;
    }

  ok = gsound_context_set_attributes
    (self->ctx,
     &error,
     GSOUND_ATTR_APPLICATION_ICON_NAME, APP_ID,
     NULL);
  if (!ok)
    {
      ringer_error (self, "Error setting GSound attributes", error);
      return FALSE;
    }

  g_debug ("Created ringtone context");

  return TRUE;
}


static void play (CallsRinger *self);


static void
play_cb (GSoundContext *ctx,
         GAsyncResult  *res,
         CallsRinger   *self)
{
  gboolean ok;
  GError *error = NULL;

  ok = gsound_context_play_full_finish (ctx, res, &error);
  if (!ok)
    {
      g_clear_object (&self->playing);

      if (error->domain == G_IO_ERROR
          && error->code == G_IO_ERROR_CANCELLED)
        {
          g_debug ("Ringtone cancelled");
        }
      else
        {
          ringer_error (self, "Error playing ringtone", error);
        }

      return;
    }

  g_assert (self->ring_count > 0);
  play (self);
}


static void
play (CallsRinger *self)
{
  g_assert (self->ctx     != NULL);
  g_assert (self->playing != NULL);

  g_debug ("Playing ringtone");
  gsound_context_play_full (self->ctx,
                            self->playing,
                            (GAsyncReadyCallback)play_cb,
                            self,
                            GSOUND_ATTR_MEDIA_ROLE, "event",
                            GSOUND_ATTR_EVENT_ID, "phone-incoming-call",
                            GSOUND_ATTR_EVENT_DESCRIPTION, _("Incoming call"),
                            NULL);
}


static void
start (CallsRinger *self)
{
  g_assert (self->playing == NULL);

  if (!self->ctx)
    {
      gboolean ok;

      ok = create_ctx (self);
      if (!ok)
        {
          return;
        }
    }

  g_debug ("Starting ringtone");
  self->playing = g_cancellable_new ();
  play (self);
}


static void
stop (CallsRinger *self)
{
  g_debug ("Stopping ringtone");

  g_assert (self->ctx != NULL);

  g_cancellable_cancel (self->playing);
}


static void
update_ring (CallsRinger *self)
{
  if (!self->playing)
    {
      if (self->ring_count > 0)
        {
          g_debug ("Starting ringer");
          start (self);
        }
    }
  else
    {
      if (self->ring_count == 0)
        {
          g_debug ("Stopping ringer");
          stop (self);
        }
    }
}


static inline gboolean
is_ring_state (CallsCallState state)
{
  switch (state)
    {
    case CALLS_CALL_STATE_INCOMING:
    case CALLS_CALL_STATE_WAITING:
      return TRUE;
    default:
      return FALSE;
    }
}


static void
state_changed_cb (CallsRinger   *self,
                  CallsCallState new_state,
                  CallsCallState old_state)
{
  gboolean old_is_ring;

  g_return_if_fail (old_state != new_state);

  old_is_ring = is_ring_state (old_state);
  if (old_is_ring == is_ring_state (new_state))
    {
      // No change in ring state
      return;
    }

  if (old_is_ring)
    {
      --self->ring_count;
    }
  else
    {
      ++self->ring_count;
    }

  update_ring (self);
}


static void
update_count (CallsRinger    *self,
              CallsCall      *call,
              short           delta)
{
  if (is_ring_state (calls_call_get_state (call)))
    {
      self->ring_count += delta;
    }

  update_ring (self);
}


static void
call_added_cb (CallsRinger *self, CallsCall *call)
{
  update_count (self, call, +1);
}


static void
call_removed_cb (CallsRinger *self, CallsCall *call, const gchar *reason)
{
  update_count (self, call, -1);
}


static void
calls_ringer_init (CallsRinger *self)
{
}


static void
set_property (GObject      *object,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  CallsRinger *self = CALLS_RINGER (object);

  switch (property_id) {
  case PROP_PROVIDER:
    g_set_object (&self->provider, CALLS_PROVIDER (g_value_get_object (value)));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
set_up_provider (CallsRinger *self)
{
  CallsEnumerateParams *params;

  params = calls_enumerate_params_new (self);

  calls_enumerate_params_add
    (params, CALLS_TYPE_ORIGIN, "call-added", G_CALLBACK (call_added_cb));
  calls_enumerate_params_add
    (params, CALLS_TYPE_ORIGIN, "call-removed", G_CALLBACK (call_removed_cb));

  calls_enumerate_params_add
    (params, CALLS_TYPE_CALL, "state-changed", G_CALLBACK (state_changed_cb));

  calls_enumerate (self->provider, params);

  g_object_unref (params);
}

static void
constructed (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  CallsRinger *self = CALLS_RINGER (object);

  create_ctx (self);
  set_up_provider (self);

  parent_class->constructed (object);
}


static void
dispose (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  CallsRinger *self = CALLS_RINGER (object);

  g_clear_object (&self->provider);

  parent_class->dispose (object);
}


static void
calls_ringer_class_init (CallsRingerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = set_property;
  object_class->constructed = constructed;
  object_class->dispose = dispose;

  props[PROP_PROVIDER] =
    g_param_spec_object ("provider",
                         _("Provider"),
                         _("An object implementing low-level call-making functionality"),
                         CALLS_TYPE_PROVIDER,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

CallsRinger *
calls_ringer_new (CallsProvider *provider)
{
  return g_object_new (CALLS_TYPE_RINGER,
                       "provider", provider,
                       NULL);
}
