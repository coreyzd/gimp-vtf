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

/*
 * Preparation: work out which VTFs will be written and with which settings
 */

static gchar *
vtf_sanitize_name (const gchar *name)
{
  gchar *result = g_ascii_strdown (name, -1);
  gchar *p;

  for (p = result; *p; p++)
    if (strchr ("\\/:*?\"<>|", *p) || (guchar) *p < 0x20)
      *p = '_';

  return result;
}

static gint
vtf_log2 (guint value)
{
  gint n = 0;

  while (value > 1)
    {
      value >>= 1;
      n++;
    }

  return n;
}

static gboolean
vtf_check_size (const gchar  *dimension,
                gint          value,
                GError      **error)
{
  guint next;

  if (vtf_is_power_of_two (value))
    return TRUE;

  next = vtf_next_power_of_two (value);

  /* TRANSLATORS: %s is "width" or "height" */
  g_set_error (error, VTF_ERROR, 0,
               _("Image %s (%ipx) is not a power of two. "
                 "Nearest valid sizes are %u and %u."),
               dimension, value, next >> 1, next);

  return FALSE;
}

static gint
vtf_count_children (GimpItem *item)
{
  GimpItem **children = gimp_item_get_children (item);
  gint       n        = 0;

  if (children)
    while (children[n])
      n++;

  g_free (children);

  return n;
}

gboolean
vtf_export_prepare (VtfExport  *exp,
                    GimpImage  *image,
                    GFile      *file,
                    GError    **error)
{
  GimpLayer **layers;
  GFile      *parent;
  gchar      *basename;
  gchar      *base;
  gchar      *dot;
  gboolean    cropped = FALSE;
  gint        num_layers = 0;
  guint       i;

  memset (exp, 0, sizeof (VtfExport));

  exp->image  = image;
  exp->file   = g_object_ref (file);
  exp->width  = gimp_image_get_width (image);
  exp->height = gimp_image_get_height (image);
  exp->groups = g_array_new (FALSE, TRUE, sizeof (VtfGroup));

  if (! vtf_check_size (_("width"),  exp->width,  error) ||
      ! vtf_check_size (_("height"), exp->height, error))
    return FALSE;

  exp->exponent_u = vtf_log2 (exp->width);
  exp->exponent_v = vtf_log2 (exp->height);

  /* Every top-level layer group with children becomes a VTF */
  layers = gimp_image_get_layers (image);

  for (i = 0; layers && layers[i]; i++)
    {
      GimpItem *item = GIMP_ITEM (layers[i]);

      if (gimp_item_is_group (item))
        {
          gint n = vtf_count_children (item);

          if (n > 0)
            {
              VtfGroup grp = { 0 };

              grp.item         = item;
              grp.tattoo       = gimp_item_get_tattoo (item);
              grp.name         = gimp_item_get_name (item);
              grp.num_children = n;

              g_array_append_val (exp->groups, grp);
            }
        }
      else
        {
          num_layers++;
        }
    }

  if (exp->groups->len > 0)
    {
      exp->has_groups = TRUE;
    }
  else
    {
      VtfGroup grp = { 0 };

      if (num_layers == 0)
        {
          g_free (layers);
          g_set_error (error, VTF_ERROR, 0, _("No data to export."));
          return FALSE;
        }

      grp.name         = gimp_image_get_name (image);
      grp.num_children = num_layers;

      g_array_append_val (exp->groups, grp);
    }

  g_free (layers);

  /* Destination files */
  parent   = g_file_get_parent (file);
  basename = g_file_get_basename (file);
  base     = g_strdup (basename);

  dot = strrchr (base, '.');
  if (dot && dot != base)
    *dot = '\0';

  if (exp->has_groups)
    {
      /* If the user is exporting to one of the secondary VTFs (e.g.
       * "brick_normal.vtf"), work out the base name so that everything
       * still lines up. */
      for (i = 1; i < exp->groups->len && ! cropped; i++)
        {
          VtfGroup *grp    = &g_array_index (exp->groups, VtfGroup, i);
          gchar    *suffix = g_strconcat ("_", grp->name, NULL);
          gsize     blen   = strlen (base);
          gsize     slen   = strlen (suffix);

          if (slen < blen &&
              g_ascii_strcasecmp (base + blen - slen, suffix) == 0)
            {
              base[blen - slen]  = '\0';
              exp->initial_group = i;
              cropped            = TRUE;
            }

          g_free (suffix);
        }
    }

  for (i = 0; i < exp->groups->len; i++)
    {
      VtfGroup *grp = &g_array_index (exp->groups, VtfGroup, i);

      grp->is_main = (i == 0);

      if (grp->is_main && ! cropped)
        {
          /* exactly the file GIMP asked for */
          grp->file = g_object_ref (file);
        }
      else
        {
          gchar *name;

          if (grp->is_main)
            {
              name = g_strconcat (base, ".vtf", NULL);
            }
          else
            {
              gchar *suffix = vtf_sanitize_name (grp->name);

              name = g_strconcat (base, "_", suffix, ".vtf", NULL);
              g_free (suffix);
            }

          grp->file = parent ? g_file_get_child (parent, name)
                             : g_file_new_for_path (name);
          g_free (name);
        }

      grp->display_name = g_file_get_basename (grp->file);

      /* Settings: stored in the image, else a GIMP 2.8 settings file,
       * else defaults */
      if (! vtf_options_load (&grp->opt, image, grp->item) &&
          ! vtf_options_load_legacy (&grp->opt, image,
                                     exp->has_groups ? (gint32) grp->tattoo : -1))
        {
          vtf_options_init_default (&grp->opt);
        }

      if (grp->opt.alpha_layer_tattoo &&
          ! gimp_image_get_layer_by_tattoo (image, grp->opt.alpha_layer_tattoo))
        {
          gimp_message (_("Could not find the layer that was used as the "
                          "alpha channel. It has been unset."));
          grp->opt.alpha_layer_tattoo = 0;
        }
    }

  g_free (base);
  g_free (basename);
  g_clear_object (&parent);

  return TRUE;
}

void
vtf_export_clear (VtfExport *exp)
{
  guint i;

  if (exp->groups)
    {
      for (i = 0; i < exp->groups->len; i++)
        {
          VtfGroup *grp = &g_array_index (exp->groups, VtfGroup, i);

          g_free (grp->name);
          g_free (grp->display_name);
          g_clear_object (&grp->file);
        }

      g_array_free (exp->groups, TRUE);
      exp->groups = NULL;
    }

  g_clear_object (&exp->file);
}


/*
 * Export
 */

/* Copy a drawable's pixels into an image-sized RGBA canvas, honouring its
 * offsets. Areas the drawable doesn't cover are left alone (transparent). */
static void
vtf_read_drawable (GimpDrawable *drawable,
                   gint          width,
                   gint          height,
                   guint8       *canvas)
{
  GeglBuffer    *buffer;
  GeglRectangle  image_rect = { 0, 0, width, height };
  GeglRectangle  layer_rect;
  GeglRectangle  isect;

  gimp_drawable_get_offsets (drawable, &layer_rect.x, &layer_rect.y);
  layer_rect.width  = gimp_drawable_get_width (drawable);
  layer_rect.height = gimp_drawable_get_height (drawable);

  if (! gegl_rectangle_intersect (&isect, &image_rect, &layer_rect))
    return;

  buffer = gimp_drawable_get_buffer (drawable);

  gegl_buffer_get (buffer,
                   GEGL_RECTANGLE (isect.x - layer_rect.x,
                                   isect.y - layer_rect.y,
                                   isect.width, isect.height),
                   1.0, babl_format ("R'G'B'A u8"),
                   canvas + ((gsize) isect.y * width + isect.x) * 4,
                   (gint) width * 4, GEGL_ABYSS_NONE);

  g_object_unref (buffer);
}

/* gimp_image_duplicate() doesn't keep tattoos, so items are matched between
 * the image and its working copy by their position in the layer tree. */
static GArray *
vtf_item_path (GimpImage *image,
               GimpItem  *item)
{
  GArray *path = g_array_new (FALSE, FALSE, sizeof (gint));

  while (item)
    {
      gint pos = gimp_image_get_item_position (image, item);

      g_array_prepend_val (path, pos);
      item = gimp_item_get_parent (item);
    }

  return path;
}

static GimpItem *
vtf_item_from_path (GimpImage *image,
                    GArray    *path)
{
  GimpItem **items;
  GimpItem  *item = NULL;
  guint      i;

  items = (GimpItem **) gimp_image_get_layers (image);

  for (i = 0; i < path->len && items; i++)
    {
      gint pos = g_array_index (path, gint, i);
      gint n   = 0;

      while (items[n])
        n++;

      item = (pos >= 0 && pos < n) ? items[pos] : NULL;
      g_free (items);
      items = NULL;

      if (item && i + 1 < path->len)
        items = gimp_item_get_children (item);
    }

  g_free (items);

  return item;
}

static GimpItem *
vtf_find_in_copy (GimpImage *image,
                  GimpItem  *item,
                  GimpImage *copy)
{
  GArray   *path;
  GimpItem *result;

  if (! item)
    return NULL;

  path   = vtf_item_path (image, item);
  result = vtf_item_from_path (copy, path);
  g_array_free (path, TRUE);

  return result;
}

/* Turn a layer of the working copy into something with plain pixels:
 * groups are merged and layer masks applied. */
static GimpLayer *
vtf_flatten_item (GimpLayer *layer)
{
  if (gimp_item_is_group (GIMP_ITEM (layer)))
    return gimp_group_layer_merge (GIMP_GROUP_LAYER (layer));

  if (gimp_layer_get_mask (layer))
    gimp_layer_remove_mask (layer,
                            gimp_layer_get_apply_mask (layer) ?
                            GIMP_MASK_APPLY : GIMP_MASK_DISCARD);

  return layer;
}

static gboolean
vtf_write_file (GFile   *file,
                GError **error)
{
  vlUInt   size    = vlImageGetSize ();
  vlUInt   written = 0;
  gpointer data;
  gboolean ok;

  if (size == 0)
    {
      g_set_error (error, VTF_ERROR, 0, _("VTFLib error: %s"),
                   vlGetLastError ());
      return FALSE;
    }

  /* a little slack, in case the writer pads anything */
  size += 64 * 1024;

  data = g_try_malloc (size);
  if (! data)
    {
      g_set_error (error, VTF_ERROR, 0, _("Out of memory."));
      return FALSE;
    }

  if (! vlImageSaveLump (data, size, &written))
    {
      g_set_error (error, VTF_ERROR, 0, _("VTFLib error: %s"),
                   vlGetLastError ());
      g_free (data);
      return FALSE;
    }

  ok = g_file_replace_contents (file, data, written, NULL, FALSE,
                                G_FILE_CREATE_REPLACE_DESTINATION,
                                NULL, NULL, error);
  g_free (data);

  return ok;
}

static gboolean
vtf_export_group (VtfExport  *exp,
                  VtfGroup   *grp,
                  GError    **error)
{
  const VtfOptions  *opt = &grp->opt;
  SVTFCreateOptions  create;
  GimpImage         *work;
  GimpItem          *group = NULL;
  GimpLayer         *alpha_layer = NULL;
  guint8            *alpha_mask  = NULL;
  GPtrArray         *frames;
  vlByte           **pixels      = NULL;
  gsize              canvas_size = (gsize) exp->width * exp->height * 4;
  gint               fmt;
  guint              n_frames, i;
  vlUInt             count[3]    = { 1, 1, 1 }; /* frames, faces, slices */
  gboolean           ok          = FALSE;

  fmt = vtf_options_get_format (opt, gimp_image_get_base_type (exp->image));
  if (fmt < 0)
    {
      g_set_error (error, VTF_ERROR, 0, _("Invalid pixel format."));
      return FALSE;
    }

  /* All the merging happens on a throwaway copy of the image */
  work = gimp_image_duplicate (exp->image);
  gimp_image_undo_disable (work);

  if (exp->has_groups)
    {
      GimpItem *orig = GIMP_ITEM (gimp_image_get_layer_by_tattoo (exp->image,
                                                                  grp->tattoo));

      group = vtf_find_in_copy (exp->image, orig, work);

      if (! group || ! gimp_item_is_group (group))
        {
          g_set_error (error, VTF_ERROR, 0,
                       _("Could not find layer. Did you delete something?"));
          goto out;
        }
    }

  /* Alpha channel from another layer */
  if (opt->alpha_layer_tattoo && vtf_format_has_alpha (fmt))
    {
      GimpItem *orig = GIMP_ITEM (gimp_image_get_layer_by_tattoo (exp->image,
                                                                  opt->alpha_layer_tattoo));

      alpha_layer = GIMP_LAYER (vtf_find_in_copy (exp->image, orig, work));

      if (! alpha_layer || GIMP_ITEM (alpha_layer) == group)
        {
          g_set_error (error, VTF_ERROR, 0, _("Invalid alpha layer."));
          goto out;
        }
      else
        {
          GimpLayer *flat = vtf_flatten_item (alpha_layer);
          guint8    *rgba = g_try_malloc0 (canvas_size);
          gsize      p;

          if (! rgba)
            {
              g_set_error (error, VTF_ERROR, 0, _("Out of memory."));
              goto out;
            }

          vtf_read_drawable (GIMP_DRAWABLE (flat), exp->width, exp->height,
                             rgba);

          /* Luminance, with transparent areas counting as black */
          alpha_mask = g_malloc ((gsize) exp->width * exp->height);
          for (p = 0; p < (gsize) exp->width * exp->height; p++)
            {
              const guint8 *px = rgba + p * 4;
              guint lum = (2126 * px[0] + 7152 * px[1] + 722 * px[2]) / 10000;

              alpha_mask[p] = (guint8) ((lum * px[3] + 127) / 255);
            }

          g_free (rgba);

          /* keep it out of the colour data */
          alpha_layer = flat;
          gimp_item_set_visible (GIMP_ITEM (alpha_layer), FALSE);
        }
    }

  /* Collect the layers that become frames/faces/slices */
  frames = g_ptr_array_new ();

  if (opt->layer_use == VTF_LAYERS_MERGE_VISIBLE)
    {
      GimpLayer *merged;

      if (group)
        merged = gimp_group_layer_merge (GIMP_GROUP_LAYER (group));
      else
        merged = gimp_image_merge_visible_layers (work, GIMP_CLIP_TO_IMAGE);

      if (merged)
        g_ptr_array_add (frames, merged);
    }
  else
    {
      GimpItem **children;

      if (group)
        children = gimp_item_get_children (group);
      else
        children = (GimpItem **) gimp_image_get_layers (work);

      /* GIMP lists the top layer first; the bottom layer is frame 0 */
      for (i = 0; children && children[i]; i++)
        ;
      while (i-- > 0)
        {
          GimpItem *child = children[i];

          if (child == GIMP_ITEM (alpha_layer))
            continue;

          /* with no layer groups, the (empty) groups at the top level are
           * not frames */
          if (! group && gimp_item_is_group (child))
            continue;

          g_ptr_array_add (frames, vtf_flatten_item (GIMP_LAYER (child)));
        }

      g_free (children);
    }

  n_frames = frames->len;

  if (n_frames == 0)
    {
      g_set_error (error, VTF_ERROR, 0, _("No data to export."));
      g_ptr_array_free (frames, TRUE);
      goto out;
    }

  switch (opt->layer_use)
    {
    case VTF_LAYERS_MERGE_VISIBLE:
      break;
    case VTF_LAYERS_ANIMATION:
      count[0] = n_frames;
      break;
    case VTF_LAYERS_ENVMAP:
      if (n_frames != 6 && n_frames != 7)
        {
          g_set_error (error, VTF_ERROR, 0,
                       _("An environment map needs 6 layers (one per cube "
                         "face), but there are %u."), n_frames);
          g_ptr_array_free (frames, TRUE);
          goto out;
        }
      count[1] = n_frames;
      break;
    case VTF_LAYERS_VOLUME:
      count[2] = n_frames;
      break;
    }

  /* Pixels */
  pixels = g_new0 (vlByte *, n_frames);

  for (i = 0; i < n_frames; i++)
    {
      pixels[i] = g_try_malloc0 (canvas_size);
      if (! pixels[i])
        {
          g_set_error (error, VTF_ERROR, 0, _("Out of memory."));
          g_ptr_array_free (frames, TRUE);
          goto out;
        }

      vtf_read_drawable (GIMP_DRAWABLE (g_ptr_array_index (frames, i)),
                         exp->width, exp->height, pixels[i]);

      if (alpha_mask)
        {
          gsize p;

          for (p = 0; p < (gsize) exp->width * exp->height; p++)
            pixels[i][p * 4 + 3] = (pixels[i][p * 4 + 3] * alpha_mask[p] + 127) / 255;
        }
    }

  g_ptr_array_free (frames, TRUE);

  /* Hand off to VTFLib++ */
  vlImageCreateDefaultCreateStructure (&create);

  create.ImageFormat  = vtf_formats[fmt].format;
  create.uiVersion[1] = opt->version;
  create.uiFlags      = opt->general_flags; /* keep flags of an imported VTF */
  create.bMipmaps     = opt->mipmaps;
  create.bResize      = vlFalse;

  /* VTFLib++ can't generate mipmaps for volume textures */
  if (opt->layer_use == VTF_LAYERS_VOLUME)
    create.bMipmaps = vlFalse;

  /* flags that are derived from the image itself */
  create.uiFlags &= ~(TEXTUREFLAGS_ONEBITALPHA | TEXTUREFLAGS_EIGHTBITALPHA |
                      TEXTUREFLAGS_ENVMAP | TEXTUREFLAGS_NOMIP);

  if (opt->clamp)
    create.uiFlags |= TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT;
  else
    create.uiFlags &= ~(TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT);

  if (opt->no_lod)
    create.uiFlags |= TEXTUREFLAGS_NOLOD;
  else
    create.uiFlags &= ~TEXTUREFLAGS_NOLOD;

  switch (opt->bump)
    {
    case VTF_BUMP_NORMAL:
      create.uiFlags   |= TEXTUREFLAGS_NORMAL;
      create.uiFlags   &= ~TEXTUREFLAGS_SSBUMP;
      create.bNormalMap = vlTrue;
      break;
    case VTF_BUMP_SSBUMP:
      create.uiFlags |= TEXTUREFLAGS_SSBUMP;
      create.uiFlags &= ~TEXTUREFLAGS_NORMAL;
      break;
    case VTF_BUMP_NONE:
      create.uiFlags &= ~(TEXTUREFLAGS_NORMAL | TEXTUREFLAGS_SSBUMP);
      break;
    }

  if (! vlImageCreateMultiple (exp->width, exp->height,
                               count[0], count[1], count[2],
                               pixels, &create))
    {
      g_set_error (error, VTF_ERROR, 0, _("VTFLib error: %s"),
                   vlGetLastError ());
      goto out;
    }

  if (vlImageGetSupportsResources () &&
      opt->mipmaps && opt->lod_u > 0 && opt->lod_v > 0)
    {
      vlByte lod[4] = { (vlByte) opt->lod_u, (vlByte) opt->lod_v, 0, 0 };

      vlImageSetResourceData (VTF_RSRC_TEXTURE_LOD_SETTINGS, sizeof (lod), lod);
    }

  ok = vtf_write_file (grp->file, error);

 out:
  if (pixels)
    {
      for (i = 0; i < n_frames; i++)
        g_free (pixels[i]);
      g_free (pixels);
    }

  g_free (alpha_mask);
  gimp_image_delete (work);

  return ok;
}

gboolean
vtf_export_run (VtfExport  *exp,
                gboolean    interactive,
                GError    **error)
{
  guint i, n_enabled = 0, n_done = 0;

  for (i = 0; i < exp->groups->len; i++)
    if (g_array_index (exp->groups, VtfGroup, i).opt.enabled)
      n_enabled++;

  if (n_enabled == 0)
    {
      g_set_error (error, VTF_ERROR, 0, _("No data to export."));
      return FALSE;
    }

  gimp_progress_init (_("Exporting VTF..."));

  for (i = 0; i < exp->groups->len; i++)
    {
      VtfGroup *grp = &g_array_index (exp->groups, VtfGroup, i);
      GError   *local_error = NULL;
      gint      fmt;

      if (! grp->opt.enabled)
        continue;

      gimp_progress_set_text_printf (_("Exporting %s..."), grp->display_name);

      if (! vtf_export_group (exp, grp, &local_error))
        {
          if (exp->has_groups)
            g_set_error (error, VTF_ERROR, 0,
                         _("Error when processing \"%s\": %s"),
                         grp->name, local_error->message);
          else
            g_propagate_error (error, g_steal_pointer (&local_error));

          g_clear_error (&local_error);
          return FALSE;
        }

      fmt = vtf_options_get_format (&grp->opt,
                                    gimp_image_get_base_type (exp->image));

      if (interactive &&
          grp->opt.bump != VTF_BUMP_NONE &&
          vtf_format_is_compressed (fmt) &&
          vtf_formats[fmt].format != IMAGE_FORMAT_ATI2N)
        {
          gimp_message (_("Image exported, but compressing bump maps with "
                          "DXT is not recommended!"));
        }

      n_done++;
      gimp_progress_update ((gdouble) n_done / n_enabled);
    }

  gimp_progress_update (1.0);

  return TRUE;
}
