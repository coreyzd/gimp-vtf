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
 * Pixel formats
 *
 * The order is the order of the list in the export dialog. Settings and the
 * PDB refer to formats by id, so entries can be reordered freely.
 */

const VtfFormat vtf_formats[] =
{
  { "dxt1",     "DXT1",                    IMAGE_FORMAT_DXT1,              "-",     TRUE  },
  { "dxt1a",    "DXT1 with alpha",         IMAGE_FORMAT_DXT1_ONEBITALPHA,  "1-bit", TRUE  },
  { "dxt3",     "DXT3",                    IMAGE_FORMAT_DXT3,              "8-bit", TRUE  },
  { "dxt5",     "DXT5",                    IMAGE_FORMAT_DXT5,              "8-bit", TRUE  },
  { "ati2n",    "ATI2N / BC5 (normal maps)", IMAGE_FORMAT_ATI2N,           "-",     TRUE  },
  { "ati1n",    "ATI1N / BC4 (one channel)", IMAGE_FORMAT_ATI1N,           "-",     TRUE  },
  { "bc7",      "BC7",                     IMAGE_FORMAT_BC7,               "8-bit", TRUE  },
  { "rgb888",   "RGB888",                  IMAGE_FORMAT_RGB888,            "-",     FALSE },
  { "rgba8888", "RGBA8888",                IMAGE_FORMAT_RGBA8888,          "8-bit", FALSE },
  { "bgr888",   "BGR888",                  IMAGE_FORMAT_BGR888,            "-",     FALSE },
  { "bgra8888", "BGRA8888",                IMAGE_FORMAT_BGRA8888,          "8-bit", FALSE },
  { "bgrx8888", "BGRX8888",                IMAGE_FORMAT_BGRX8888,          "-",     FALSE },
  { "bgra4444", "BGRA4444",                IMAGE_FORMAT_BGRA4444,          "4-bit", FALSE },
  { "bgra5551", "BGRA5551",                IMAGE_FORMAT_BGRA5551,          "1-bit", FALSE },
  { "rgb565",   "RGB565",                  IMAGE_FORMAT_RGB565,            "-",     FALSE },
  { "bgr565",   "BGR565",                  IMAGE_FORMAT_BGR565,            "-",     FALSE },
  { "i8",       "I8",                      IMAGE_FORMAT_I8,                "-",     FALSE },
  { "a8",       "A8",                      IMAGE_FORMAT_A8,                "8-bit", FALSE },
  { "ia88",     "IA88",                    IMAGE_FORMAT_IA88,              "8-bit", FALSE },
  { "uv88",     "UV88",                    IMAGE_FORMAT_UV88,              "-",     FALSE },
  { "uvwq8888", "UVWQ8888",                IMAGE_FORMAT_UVWQ8888,          "8-bit", FALSE },
};

const guint vtf_num_formats = G_N_ELEMENTS (vtf_formats);

gboolean
vtf_format_has_alpha (gint index)
{
  if (index < 0 || index >= (gint) vtf_num_formats)
    return FALSE;

  return vtf_formats[index].alpha_label[0] != '-';
}

gboolean
vtf_format_is_compressed (gint index)
{
  if (index < 0 || index >= (gint) vtf_num_formats)
    return FALSE;

  return vtf_formats[index].compressed;
}

gint
vtf_format_index_from_id (const gchar *id)
{
  guint i;

  if (! id)
    return -1;

  for (i = 0; i < vtf_num_formats; i++)
    if (! g_ascii_strcasecmp (vtf_formats[i].id, id))
      return i;

  return -1;
}

gint
vtf_format_index_from_vtflib (VTFImageFormat format)
{
  guint i;

  for (i = 0; i < vtf_num_formats; i++)
    if (vtf_formats[i].format == format)
      return i;

  return -1;
}

gboolean
vtflib_format_has_alpha (VTFImageFormat format)
{
  const SVTFImageFormatInfo *info = vlImageGetImageFormatInfo (format);

  return info && info->uiAlphaBitsPerPixel > 0;
}

GimpChoice *
vtf_format_choice_new (void)
{
  GimpChoice *choice = gimp_choice_new ();
  guint       i;

  for (i = 0; i < vtf_num_formats; i++)
    gimp_choice_add (choice, vtf_formats[i].id, i, vtf_formats[i].label, NULL);

  return choice;
}


/*
 * Options
 */

void
vtf_options_init_default (VtfOptions *opt)
{
  memset (opt, 0, sizeof (VtfOptions));

  opt->enabled    = TRUE;
  opt->version    = VTF_VERSION_DEFAULT;
  opt->advanced   = FALSE;
  opt->with_alpha = FALSE;
  opt->compress   = TRUE;
  opt->format     = vtf_format_index_from_id ("dxt1");
  opt->mipmaps    = TRUE;
  opt->bump       = VTF_BUMP_NONE;
  opt->layer_use  = VTF_LAYERS_MERGE_VISIBLE;
}

/* Resolve the simple/advanced selection to an index into vtf_formats */
gint
vtf_options_get_format (const VtfOptions  *opt,
                        GimpImageBaseType  base_type)
{
  const gchar *id;

  if (opt->advanced && opt->format >= 0 && opt->format < (gint) vtf_num_formats)
    return opt->format;

  if (opt->compress)
    {
      id = opt->with_alpha ? "dxt5" : "dxt1";
    }
  else if (base_type == GIMP_GRAY)
    {
      id = opt->with_alpha ? "ia88" : "i8";
    }
  else
    {
      id = opt->with_alpha ? "rgba8888" : "rgb888";
    }

  return vtf_format_index_from_id (id);
}

#define KF_GROUP "vtf-export"

static gchar *
vtf_options_serialize (const VtfOptions *opt)
{
  GKeyFile *kf = g_key_file_new ();
  gchar    *data;

  g_key_file_set_integer (kf, KF_GROUP, "format-version", 1);
  g_key_file_set_boolean (kf, KF_GROUP, "enabled",     opt->enabled);
  g_key_file_set_integer (kf, KF_GROUP, "version",     opt->version);
  g_key_file_set_boolean (kf, KF_GROUP, "advanced",    opt->advanced);
  g_key_file_set_boolean (kf, KF_GROUP, "with-alpha",  opt->with_alpha);
  g_key_file_set_boolean (kf, KF_GROUP, "compress",    opt->compress);
  if (opt->format >= 0 && opt->format < (gint) vtf_num_formats)
    g_key_file_set_string (kf, KF_GROUP, "pixel-format",
                           vtf_formats[opt->format].id);
  g_key_file_set_boolean (kf, KF_GROUP, "clamp",       opt->clamp);
  g_key_file_set_boolean (kf, KF_GROUP, "no-lod",      opt->no_lod);
  g_key_file_set_boolean (kf, KF_GROUP, "mipmaps",     opt->mipmaps);
  g_key_file_set_integer (kf, KF_GROUP, "bump-type",   opt->bump);
  g_key_file_set_integer (kf, KF_GROUP, "layers-as",   opt->layer_use);
  g_key_file_set_uint64  (kf, KF_GROUP, "alpha-layer-tattoo", opt->alpha_layer_tattoo);
  g_key_file_set_uint64  (kf, KF_GROUP, "flags",       opt->general_flags);
  g_key_file_set_integer (kf, KF_GROUP, "lod-control-u", opt->lod_u);
  g_key_file_set_integer (kf, KF_GROUP, "lod-control-v", opt->lod_v);

  data = g_key_file_to_data (kf, NULL, NULL);
  g_key_file_free (kf);

  return data;
}

static gboolean
vtf_options_deserialize (VtfOptions  *opt,
                         const gchar *data,
                         gsize        len)
{
  GKeyFile *kf = g_key_file_new ();
  gchar    *format_id;

  if (! g_key_file_load_from_data (kf, data, len, G_KEY_FILE_NONE, NULL) ||
      ! g_key_file_has_group (kf, KF_GROUP))
    {
      g_key_file_free (kf);
      return FALSE;
    }

  vtf_options_init_default (opt);

#define GET_BOOL(key, field) \
  if (g_key_file_has_key (kf, KF_GROUP, key, NULL)) \
    field = g_key_file_get_boolean (kf, KF_GROUP, key, NULL)
#define GET_INT(key, field) \
  if (g_key_file_has_key (kf, KF_GROUP, key, NULL)) \
    field = g_key_file_get_integer (kf, KF_GROUP, key, NULL)
#define GET_UINT(key, field) \
  if (g_key_file_has_key (kf, KF_GROUP, key, NULL)) \
    field = g_key_file_get_uint64 (kf, KF_GROUP, key, NULL)

  GET_BOOL ("enabled",    opt->enabled);
  GET_INT  ("version",    opt->version);
  GET_BOOL ("advanced",   opt->advanced);
  GET_BOOL ("with-alpha", opt->with_alpha);
  GET_BOOL ("compress",   opt->compress);
  GET_BOOL ("clamp",      opt->clamp);
  GET_BOOL ("no-lod",     opt->no_lod);
  GET_BOOL ("mipmaps",    opt->mipmaps);
  GET_INT  ("bump-type",  opt->bump);
  GET_INT  ("layers-as",  opt->layer_use);
  GET_UINT ("alpha-layer-tattoo", opt->alpha_layer_tattoo);
  GET_UINT ("flags",      opt->general_flags);
  GET_INT  ("lod-control-u", opt->lod_u);
  GET_INT  ("lod-control-v", opt->lod_v);

#undef GET_BOOL
#undef GET_INT
#undef GET_UINT

  format_id = g_key_file_get_string (kf, KF_GROUP, "pixel-format", NULL);
  if (format_id && vtf_format_index_from_id (format_id) >= 0)
    opt->format = vtf_format_index_from_id (format_id);
  g_free (format_id);

  g_key_file_free (kf);

  /* sanitize */
  opt->version   = CLAMP (opt->version, VTF_VERSION_MIN, VTF_VERSION_MAX);
  opt->bump      = CLAMP (opt->bump, VTF_BUMP_NONE, VTF_BUMP_SSBUMP);
  opt->layer_use = CLAMP (opt->layer_use, VTF_LAYERS_MERGE_VISIBLE, VTF_LAYERS_VOLUME);
  opt->lod_u     = CLAMP (opt->lod_u, 0, 16);
  opt->lod_v     = CLAMP (opt->lod_v, 0, 16);

  return TRUE;
}

gboolean
vtf_options_load (VtfOptions *opt,
                  GimpImage  *image,
                  GimpItem   *group)
{
  GimpParasite  *parasite;
  gconstpointer  data;
  guint32        len = 0;
  gboolean       ok  = FALSE;

  if (group)
    parasite = gimp_item_get_parasite (group, VTF_PARASITE_NAME);
  else
    parasite = gimp_image_get_parasite (image, VTF_PARASITE_NAME);

  if (! parasite)
    return FALSE;

  data = gimp_parasite_get_data (parasite, &len);
  if (data && len > 0)
    ok = vtf_options_deserialize (opt, data, len);

  gimp_parasite_free (parasite);

  return ok;
}

void
vtf_options_store (const VtfOptions *opt,
                   GimpImage        *image,
                   GimpItem         *group)
{
  GimpParasite *parasite;
  gchar        *data = vtf_options_serialize (opt);

  /* Persistent (saved in the XCF), but not undoable: storing export
   * settings should not add to the undo history. */
  parasite = gimp_parasite_new (VTF_PARASITE_NAME, GIMP_PARASITE_PERSISTENT,
                                strlen (data) + 1, data);

  if (group)
    gimp_item_attach_parasite (group, parasite);
  else
    gimp_image_attach_parasite (image, parasite);

  gimp_parasite_free (parasite);
  g_free (data);
}


/*
 * Legacy settings files from gimp-vtf 1.2 (GIMP 2.8)
 *
 * "<image>.xcf.vtf-settings" is a sequence of records:
 *   gint32 key (layer group tattoo, or -1 when there were no groups)
 *   guint32 size
 *   VtfSaveOptions_t (size bytes, 56 in practice)
 */

typedef struct
{
  gint32  enabled;
  guint8  version;
  guint8  pad0[3];
  gint32  advanced;
  gint32  with_alpha;
  gint32  compress;
  guint8  pixel_format;
  guint8  pad1[3];
  gint32  clamp;
  gint32  no_lod;
  gint32  with_mips;
  gint32  bump_type;
  gint32  layer_use;
  gint32  alpha_layer_tattoo;
  guint32 general_flags;
  gint8   lod_u;
  gint8   lod_v;
  guint8  pad2[2];
} LegacyOptions;

G_STATIC_ASSERT (sizeof (LegacyOptions) == 56);

/* gimp-vtf 1.2's format list, by index */
static const gchar *legacy_formats[] =
{
  "dxt1", "dxt1a", "dxt3", "dxt5", "rgb888", "rgba8888",
  "bgra4444", "rgb565", "i8", "a8", "ia88"
};

gboolean
vtf_options_load_legacy (VtfOptions *opt,
                         GimpImage  *image,
                         gint32      key)
{
  GFile   *xcf;
  gchar   *contents = NULL;
  gsize    length   = 0;
  gsize    pos      = 0;
  gchar   *path, *settings_path;
  gboolean found    = FALSE;

  xcf = gimp_image_get_xcf_file (image);
  if (! xcf)
    return FALSE;

  path = g_file_get_path (xcf);
  g_object_unref (xcf);
  if (! path)
    return FALSE;

  settings_path = g_strconcat (path, ".vtf-settings", NULL);
  g_free (path);

  if (! g_file_get_contents (settings_path, &contents, &length, NULL))
    {
      g_free (settings_path);
      return FALSE;
    }
  g_free (settings_path);

  while (pos + 8 <= length)
    {
      gint32  record_key;
      guint32 size;

      memcpy (&record_key, contents + pos, 4);
      memcpy (&size,       contents + pos + 4, 4);
      pos += 8;

      if (size == 0 || size > length - pos)
        break;

      if (record_key == key)
        {
          LegacyOptions lo = { 0 };

          memcpy (&lo, contents + pos, MIN (size, sizeof (lo)));

          vtf_options_init_default (opt);
          opt->enabled            = lo.enabled != 0;
          opt->version            = CLAMP (lo.version, VTF_VERSION_MIN, VTF_VERSION_MAX);
          opt->advanced           = lo.advanced != 0;
          opt->with_alpha         = lo.with_alpha != 0;
          opt->compress           = lo.compress != 0;
          if (lo.pixel_format < G_N_ELEMENTS (legacy_formats))
            opt->format = vtf_format_index_from_id (legacy_formats[lo.pixel_format]);
          opt->clamp              = lo.clamp != 0;
          opt->no_lod             = lo.no_lod != 0;
          opt->mipmaps            = lo.with_mips != 0;
          opt->bump               = CLAMP (lo.bump_type, VTF_BUMP_NONE, VTF_BUMP_SSBUMP);
          opt->layer_use          = CLAMP (lo.layer_use, VTF_LAYERS_MERGE_VISIBLE, VTF_LAYERS_VOLUME);
          opt->alpha_layer_tattoo = lo.alpha_layer_tattoo > 0 ? lo.alpha_layer_tattoo : 0;
          opt->general_flags      = lo.general_flags;
          opt->lod_u              = CLAMP (lo.lod_u, 0, 16);
          opt->lod_v              = CLAMP (lo.lod_v, 0, 16);

          found = TRUE;
          break;
        }

      pos += size;
    }

  g_free (contents);

  return found;
}
