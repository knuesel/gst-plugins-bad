/* GStreamer Opus Encoder
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011> Vincent Penquerc'h <vincent.penquerch@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/tag/tag.h>
#include <gst/base/gstbytewriter.h>
#include "gstopusheader.h"

static GstBuffer *
gst_opus_enc_create_id_buffer (gint nchannels, gint sample_rate)
{
  GstBuffer *buffer;
  GstByteWriter bw;

  gst_byte_writer_init (&bw);

  /* See http://wiki.xiph.org/OggOpus */
  gst_byte_writer_put_data (&bw, (const guint8 *) "OpusHead", 8);
  gst_byte_writer_put_uint8 (&bw, 0);   /* version number */
  gst_byte_writer_put_uint8 (&bw, nchannels);
  gst_byte_writer_put_uint16_le (&bw, 0);       /* pre-skip *//* TODO: endianness ? */
  gst_byte_writer_put_uint32_le (&bw, sample_rate);
  gst_byte_writer_put_uint16_le (&bw, 0);       /* output gain *//* TODO: endianness ? */
  gst_byte_writer_put_uint8 (&bw, 0);   /* channel mapping *//* TODO: what is this ? */

  buffer = gst_byte_writer_reset_and_get_buffer (&bw);

  GST_BUFFER_OFFSET (buffer) = 0;
  GST_BUFFER_OFFSET_END (buffer) = 0;

  return buffer;
}

static GstBuffer *
gst_opus_enc_create_metadata_buffer (const GstTagList * tags)
{
  GstTagList *empty_tags = NULL;
  GstBuffer *comments = NULL;

  GST_DEBUG ("tags = %" GST_PTR_FORMAT, tags);

  if (tags == NULL) {
    /* FIXME: better fix chain of callers to not write metadata at all,
     * if there is none */
    empty_tags = gst_tag_list_new ();
    tags = empty_tags;
  }
  comments =
      gst_tag_list_to_vorbiscomment_buffer (tags, (const guint8 *) "OpusTags",
      8, "Encoded with GStreamer Opusenc");

  GST_BUFFER_OFFSET (comments) = 0;
  GST_BUFFER_OFFSET_END (comments) = 0;

  if (empty_tags)
    gst_tag_list_free (empty_tags);

  return comments;
}

/*
 * (really really) FIXME: move into core (dixit tpm)
 */
/**
 * _gst_caps_set_buffer_array:
 * @caps: a #GstCaps
 * @field: field in caps to set
 * @buf: header buffers
 *
 * Adds given buffers to an array of buffers set as the given @field
 * on the given @caps.  List of buffer arguments must be NULL-terminated.
 *
 * Returns: input caps with a streamheader field added, or NULL if some error
 */
static GstCaps *
_gst_caps_set_buffer_array (GstCaps * caps, const gchar * field,
    GstBuffer * buf, ...)
{
  GstStructure *structure = NULL;
  va_list va;
  GValue array = { 0 };
  GValue value = { 0 };

  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);
  g_return_val_if_fail (field != NULL, NULL);

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  va_start (va, buf);
  /* put buffers in a fixed list */
  while (buf) {
    g_assert (gst_buffer_is_writable (buf));

    /* mark buffer */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    buf = gst_buffer_copy (buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
    gst_value_set_buffer (&value, buf);
    gst_buffer_unref (buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);

    buf = va_arg (va, GstBuffer *);
  }

  gst_structure_set_value (structure, field, &array);
  g_value_unset (&array);

  return caps;
}

void
gst_opus_header_create_caps (GstCaps ** caps, GSList ** headers, gint nchannels,
    gint sample_rate, const GstTagList * tags)
{
  GstBuffer *buf1, *buf2;

  g_return_if_fail (caps);
  g_return_if_fail (headers && !*headers);
  g_return_if_fail (nchannels > 0);
  g_return_if_fail (sample_rate >= 0);  /* 0 -> unset */

  /* Opus streams in Ogg begin with two headers; the initial header (with
     most of the codec setup parameters) which is mandated by the Ogg
     bitstream spec.  The second header holds any comment fields. */

  /* create header buffers */
  buf1 = gst_opus_enc_create_id_buffer (nchannels, sample_rate);
  buf2 = gst_opus_enc_create_metadata_buffer (tags);

  /* mark and put on caps */
  *caps = gst_caps_from_string ("audio/x-opus");
  *caps = _gst_caps_set_buffer_array (*caps, "streamheader", buf1, buf2, NULL);

  *headers = g_slist_prepend (*headers, buf2);
  *headers = g_slist_prepend (*headers, buf1);
}