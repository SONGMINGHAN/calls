/*
 * Copyright (C) 2021 Purism SPC
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
 * Author: Evangelos Ribeiro Tzaras <evangelos.tzaras@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#define G_LOG_DOMAIN "CallsCallsSipMediaManager"

#include "calls-sip-media-pipeline.h"
#include "gst-rfc3551.h"
#include "calls-sip-media-manager.h"

#include <gst/gst.h>

typedef struct _CallsSipMediaManager
{
  GObject parent;

  GList *supported_codecs;
} CallsSipMediaManager;

G_DEFINE_TYPE (CallsSipMediaManager, calls_sip_media_manager, G_TYPE_OBJECT);


static void
calls_sip_media_manager_finalize (GObject *object)
{
  gst_deinit ();

  g_list_free (CALLS_SIP_MEDIA_MANAGER (object)->supported_codecs);

  G_OBJECT_CLASS (calls_sip_media_manager_parent_class)->finalize (object);
}


static void
calls_sip_media_manager_class_init (CallsSipMediaManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = calls_sip_media_manager_finalize;
}


static void
calls_sip_media_manager_init (CallsSipMediaManager *self)
{
  gst_init (NULL, NULL);

  self->supported_codecs = media_codecs_get_candidates ();
}


/* Public functions */

CallsSipMediaManager *
calls_sip_media_manager_default ()
{
  static CallsSipMediaManager *instance = NULL;

  if (instance == NULL) {
    g_debug ("Creating CallsSipMediaManager");
    instance = g_object_new (CALLS_TYPE_SIP_MEDIA_MANAGER, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *) &instance);
  }
  return instance;
}


/* calls_sip_media_manager_static_capabilities:
 *
 * @port: Should eventually come from the ICE stack
 * @use_srtp: Whether to use srtp (not really handled)
 *
 * Returns: (full-control) string describing capabilities
 * to be used in the session description (SDP)
 */
char *
calls_sip_media_manager_static_capabilities (CallsSipMediaManager *self,
                                             guint                 port,
                                             gboolean              use_srtp)
{
  char *payload_type = use_srtp ? "SAVP" : "AVP";
  g_autoptr (GString) media_line = NULL;
  g_autoptr (GString) attribute_lines = NULL;
  GList *node;

  g_return_val_if_fail (CALLS_IS_SIP_MEDIA_MANAGER (self), NULL);

  media_line = g_string_new (NULL);
  attribute_lines = g_string_new (NULL);

  if (self->supported_codecs == NULL) {
    g_warning ("No supported codecs found. Can't build meaningful SDP message");
    g_string_append_printf (media_line, "m=audio 0 RTP/AVP 0");
    goto done;
  }

  /* media lines look f.e like "audio 31337 RTP/AVP 9 8 0" */
  g_string_append_printf (media_line,
                          "m=audio %d RTP/%s", port, payload_type);

  for (node = self->supported_codecs; node != NULL; node = node->next) {
    MediaCodecInfo *codec = node->data;

    g_string_append_printf (media_line, " %u", codec->payload_id);
    g_string_append_printf (attribute_lines,
                            "a=rtpmap:%u %s/%u%s",
                            codec->payload_id,
                            codec->name,
                            codec->clock_rate,
                            "\r\n");
  }

  g_string_append_printf (attribute_lines, "a=rtcp:%d\r\n", port + 1);

 done:
  return g_strdup_printf ("v=0\r\n"
                          "%s\r\n"
                          "%s\r\n",
                          media_line->str,
                          attribute_lines->str);
}


MediaCodecInfo*
get_best_codec (CallsSipMediaManager *self)
{
  return media_codec_by_name ("PCMA");
}
