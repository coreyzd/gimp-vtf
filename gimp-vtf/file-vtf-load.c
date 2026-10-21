/*
 * GIMP VTF
 * Copyright (C) 2010 Tom Edwards
 * GIMP 3 / VTFLib++ / Linux port (C) 2026 Corey Holt
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 */

#include "file-vtf.h"

/* Read the whole file and hand it to VTFLib. Going through GIO rather than
 * vlImageLoad() keeps non-ASCII paths working on Windows. */
static gboolean
vtf_read_file (GFile     *file,
               gboolean   header_only,
               GError   **error)
{
  gchar *contents = NULL;
  gsize  length   = 0;
  gboolean ok;

  if (! g_file_load_contents (file, NULL, &contents, &length, NULL, error))
    return FALSE;

  if (length > G_MAXUINT32)
    {
      g_set_error (error, VTF_ERROR, 0, _("'%s' is too large."),
                   gimp_file_get_utf8_name (file));
      g_free (contents);
      return FALSE;
    }

  ok = vlImageLoadLump (contents, (vlUInt) length, header_only);
  g_free (contents);

  if (! ok)
    g_set_error (error, VTF_ERROR, 0, _("Could not open '%s': %s"),
                 gimp_file_get_utf8_name (file), vlGetLastError ());

  return ok;
}

/* Formats with more than 8 bits per channel are loaded into a 32-bit float
 * image instead of being clipped to 8 bits. */
static gboolean
vtf_format_is_hdr (VTFImageFormat format)
{
  switch (format)
    {
    case IMAGE_FORMAT_RGBA16161616F:
    case IMAGE_FORMAT_RGBA16161616:
    case IMAGE_FORMAT_R32F:
    case IMAGE_FORMAT_RGB323232F:
    case IMAGE_FORMAT_RGBA32323232F:
    case IMAGE_FORMAT_BC6H:
    case IMAGE_FORMAT_BC6H_UNSIGNED:
      return TRUE;
    default:
      return FALSE;
    }
}

/* Decode one mip level of one frame/face/slice into a newly allocated
 * buffer, as 8-bit RGBA or (for HDR formats) 32-bit float RGBA. */
static gpointer
vtf_decode (vlUInt    frame,
            vlUInt    face,
            vlUInt    slice,
            vlUInt    mip,
            vlUInt    width,
            vlUInt    height,
            gboolean  as_float,
            GError  **error)
{
  VTFImageFormat format = vlImageGetFormat ();
  vlByte        *src    = vlImageGetData (frame, face, slice, mip);
  gsize          bpp    = as_float ? 16 : 4;
  vlByte        *dest;
  vlBool         ok;

  if (! src)
    {
      g_set_error (error, VTF_ERROR, 0, _("VTF image data is missing."));
      return NULL;
    }

  dest = g_try_malloc ((gsize) width * height * bpp);
  if (! dest)
    {
      g_set_error (error, VTF_ERROR, 0, _("Out of memory."));
      return NULL;
    }

  if (as_float)
    ok = vlImageConvert (src, dest, width, height, format,
                         IMAGE_FORMAT_RGBA32323232F);
  else
    ok = vlImageConvertToRGBA8888 (src, dest, width, height, format);

  if (! ok)
    {
      g_set_error (error, VTF_ERROR, 0, _("Could not decode VTF image: %s"),
                   vlGetLastError ());
      g_free (dest);
      return NULL;
    }

  return dest;
}

static GimpLayer *
vtf_add_layer (GimpImage     *image,
               const gchar   *name,
               gint           width,
               gint           height,
               gconstpointer  pixels,
               const Babl    *format)
{
  GimpLayer  *layer;
  GeglBuffer *buffer;

  layer = gimp_layer_new (image, name, width, height, GIMP_RGBA_IMAGE, 100,
                          gimp_image_get_default_new_layer_mode (image));
  gimp_image_insert_layer (image, layer, NULL, 0);

  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));
  gegl_buffer_set (buffer, GEGL_RECTANGLE (0, 0, width, height), 0,
                   format, pixels, GEGL_AUTO_ROWSTRIDE);
  g_object_unref (buffer);

  return layer;
}

/* Remember how the file was made, so that exporting it again keeps its
 * settings. */
static void
vtf_store_import_settings (GimpImage *image)
{
  VtfOptions     opt;
  VTFImageFormat format = vlImageGetFormat ();
  gint           index;

  vtf_options_init_default (&opt);

  opt.version = CLAMP ((gint) vlImageGetMinorVersion (),
                       VTF_VERSION_MIN, VTF_VERSION_MAX);

  switch (format)
    {
    case IMAGE_FORMAT_RGBA8888:
      opt.with_alpha = TRUE;
      /* fall through */
    case IMAGE_FORMAT_RGB888:
      opt.compress = FALSE;
      break;

    case IMAGE_FORMAT_DXT5:
      opt.with_alpha = TRUE;
      /* fall through */
    case IMAGE_FORMAT_DXT1:
      opt.compress = TRUE;
      break;

    case IMAGE_FORMAT_IA88:
      opt.with_alpha = TRUE;
      /* fall through */
    case IMAGE_FORMAT_I8:
      opt.compress = FALSE;
      break;

    default:
      index = vtf_format_index_from_vtflib (format);
      if (index >= 0)
        {
          opt.advanced   = TRUE;
          opt.format     = index;
          opt.with_alpha = vtf_format_has_alpha (index);
          opt.compress   = vtf_format_is_compressed (index);
        }
      else
        {
          /* not something we can write (e.g. HDR): pick the closest */
          opt.with_alpha = vtflib_format_has_alpha (format);
          opt.compress   = FALSE;
        }
      break;
    }

  opt.general_flags = vlImageGetFlags ();
  opt.clamp   = (opt.general_flags & TEXTUREFLAGS_CLAMPS) &&
                (opt.general_flags & TEXTUREFLAGS_CLAMPT);
  opt.no_lod  = (opt.general_flags & TEXTUREFLAGS_NOLOD) != 0;
  opt.mipmaps = ! (opt.general_flags & TEXTUREFLAGS_NOMIP);

  if (vlImageGetFrameCount () > 1)
    opt.layer_use = VTF_LAYERS_ANIMATION;
  else if (vlImageGetFaceCount () >= 6)
    opt.layer_use = VTF_LAYERS_ENVMAP;
  else if (vlImageGetDepth () > 1)
    opt.layer_use = VTF_LAYERS_VOLUME;
  else
    opt.layer_use = VTF_LAYERS_MERGE_VISIBLE;

  if (opt.general_flags & TEXTUREFLAGS_NORMAL)
    opt.bump = VTF_BUMP_NORMAL;
  else if (opt.general_flags & TEXTUREFLAGS_SSBUMP)
    opt.bump = VTF_BUMP_SSBUMP;

  if (vlImageGetSupportsResources () &&
      vlImageGetHasResource (VTF_RSRC_TEXTURE_LOD_SETTINGS))
    {
      vlUInt  size = 0;
      vlByte *data = vlImageGetResourceData (VTF_RSRC_TEXTURE_LOD_SETTINGS,
                                             &size);
      if (data && size >= 2)
        {
          opt.lod_u = data[0];
          opt.lod_v = data[1];
        }
    }

  vtf_options_store (&opt, image, NULL);
}

GimpImage *
vtf_load_image (GFile   *file,
                GError **error)
{
  GimpImage     *image;
  vlUInt         width, height;
  vlUInt         frames, faces, slices;
  vlUInt         frame, face, slice;
  guint          num_layers, n = 0;
  gboolean       as_float;
  const Babl    *babl_fmt;
  VTFImageFormat format;

  if (! vtf_read_file (file, FALSE, error))
    return NULL;

  width   = vlImageGetWidth ();
  height  = vlImageGetHeight ();
  frames  = MAX (1, vlImageGetFrameCount ());
  faces   = MAX (1, vlImageGetFaceCount ());
  slices  = MAX (1, vlImageGetDepth ());
  format  = vlImageGetFormat ();

  if (width == 0 || height == 0 || ! vlImageGetHasImage ())
    {
      g_set_error (error, VTF_ERROR, 0, _("'%s' contains no image data."),
                   gimp_file_get_utf8_name (file));
      return NULL;
    }

  as_float = vtf_format_is_hdr (format);
  babl_fmt = as_float ? babl_format ("RGBA float") : babl_format ("R'G'B'A u8");

  num_layers = frames * faces * slices;

  if (num_layers == 1)
    gimp_progress_init (_("Loading VTF..."));
  else
    gimp_progress_init_printf (_("Loading %u VTF layers..."), num_layers);

  if (as_float)
    image = gimp_image_new_with_precision (width, height, GIMP_RGB,
                                           GIMP_PRECISION_FLOAT_LINEAR);
  else
    image = gimp_image_new (width, height, GIMP_RGB);

  gimp_image_undo_disable (image);

  for (frame = 0; frame < frames; frame++)
    for (face = 0; face < faces; face++)
      for (slice = 0; slice < slices; slice++)
        {
          gpointer   pixels;
          GimpLayer *layer;
          GString   *name;

          pixels = vtf_decode (frame, face, slice, 0, width, height,
                               as_float, error);
          if (! pixels)
            {
              gimp_image_delete (image);
              return NULL;
            }

          name = g_string_new (NULL);

          if (num_layers == 1)
            {
              /* GIMP's own translation of "Background" */
              g_string_append (name, g_dgettext ("gimp30", "Background"));
            }
          else
            {
              if (frames > 1)
                g_string_append_printf (name, _("Frame #%u"), frame + 1);

              if (faces > 1)
                {
                  if (name->len)
                    g_string_append_c (name, ' ');
                  g_string_append_printf (name, _("Face #%u"), face + 1);
                }

              if (slices > 1)
                {
                  if (name->len)
                    g_string_append_c (name, ' ');
                  g_string_append_printf (name, _("Slice #%u"), slice + 1);
                }
            }

          layer = vtf_add_layer (image, name->str, width, height, pixels,
                                 babl_fmt);

          g_string_free (name, TRUE);
          g_free (pixels);

          if (vtflib_format_has_alpha (format))
            {
              /* Alpha doesn't always represent transparency in game
               * textures, so move it into a layer mask. Even when it is
               * transparency this is useful, as it reveals the colour of
               * invisible pixels (which can bleed onto visible ones when
               * the GPU filters the texture). */
              gimp_layer_add_mask (layer,
                                   gimp_layer_create_mask (layer,
                                                           GIMP_ADD_MASK_ALPHA_TRANSFER));
              gimp_layer_set_edit_mask (layer, FALSE);
            }
          else
            {
              gimp_layer_flatten (layer);
            }

          n++;
          gimp_progress_update ((gdouble) n / (gdouble) num_layers);
        }

  if (format == IMAGE_FORMAT_I8 || format == IMAGE_FORMAT_IA88)
    gimp_image_convert_grayscale (image);

  vtf_store_import_settings (image);

  gimp_image_undo_enable (image);
  gimp_progress_update (1.0);

  return image;
}

GimpImage *
vtf_load_thumbnail (GFile   *file,
                    gint     size,
                    gint    *width,
                    gint    *height,
                    gint    *num_layers,
                    GError **error)
{
  GimpImage *image;
  GimpLayer *layer;
  vlUInt     mip = 0, mips;
  vlUInt     mip_w, mip_h, mip_d;
  guint8    *pixels;
  gsize      i;

  if (! vtf_read_file (file, FALSE, error))
    return NULL;

  *width      = vlImageGetWidth ();
  *height     = vlImageGetHeight ();
  *num_layers = MAX (1, vlImageGetFrameCount ()) *
                MAX (1, vlImageGetFaceCount ())  *
                MAX (1, vlImageGetDepth ());

  mips = MAX (1, vlImageGetMipmapCount ());

  vlImageComputeMipmapDimensions (*width, *height, 1, mip,
                                  &mip_w, &mip_h, &mip_d);

  while ((gint) MAX (mip_w, mip_h) > size && mip + 1 < mips)
    {
      mip++;
      vlImageComputeMipmapDimensions (*width, *height, 1, mip,
                                      &mip_w, &mip_h, &mip_d);
    }

  /* use the middle frame/face/slice */
  pixels = vtf_decode (vlImageGetFrameCount () / 2,
                       vlImageGetFaceCount () / 2,
                       vlImageGetDepth () / 2,
                       mip, mip_w, mip_h, FALSE, error);
  if (! pixels)
    return NULL;

  /* Alpha doesn't always represent transparency in game engine textures,
   * so leave it out of thumbnails. */
  for (i = 0; i < (gsize) mip_w * mip_h; i++)
    pixels[i * 4 + 3] = 255;

  image = gimp_image_new (mip_w, mip_h, GIMP_RGB);
  gimp_image_undo_disable (image);

  layer = vtf_add_layer (image, "VTF thumbnail", mip_w, mip_h, pixels,
                         babl_format ("R'G'B'A u8"));
  gimp_layer_flatten (layer);

  g_free (pixels);

  return image;
}
