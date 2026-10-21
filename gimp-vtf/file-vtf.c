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

#include <libgimp/gimpui.h>

#define COPYRIGHT    "Tom Edwards, Corey Holt"
#define RELEASE_DATE "2026"


typedef struct _Vtf      Vtf;
typedef struct _VtfClass VtfClass;

struct _Vtf
{
  GimpPlugIn parent_instance;
};

struct _VtfClass
{
  GimpPlugInClass parent_class;
};

#define VTF_TYPE (vtf_get_type ())

GType vtf_get_type (void);

static GList          * vtf_query_procedures (GimpPlugIn            *plug_in);
static GimpProcedure  * vtf_create_procedure (GimpPlugIn            *plug_in,
                                              const gchar           *name);
static gboolean         vtf_set_i18n         (GimpPlugIn            *plug_in,
                                              const gchar           *procedure_name,
                                              gchar                **gettext_domain,
                                              gchar                **catalog_dir);

static GimpValueArray * vtf_load             (GimpProcedure         *procedure,
                                              GimpRunMode            run_mode,
                                              GFile                 *file,
                                              GimpMetadata          *metadata,
                                              GimpMetadataLoadFlags *flags,
                                              GimpProcedureConfig   *config,
                                              gpointer               run_data);
static GimpValueArray * vtf_load_thumb       (GimpProcedure         *procedure,
                                              GFile                 *file,
                                              gint                   size,
                                              GimpProcedureConfig   *config,
                                              gpointer               run_data);
static GimpValueArray * vtf_export           (GimpProcedure         *procedure,
                                              GimpRunMode            run_mode,
                                              GimpImage             *image,
                                              GFile                 *file,
                                              GimpExportOptions     *options,
                                              GimpMetadata          *metadata,
                                              GimpProcedureConfig   *config,
                                              gpointer               run_data);


G_DEFINE_TYPE (Vtf, vtf, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (VTF_TYPE)


static void
vtf_class_init (VtfClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = vtf_query_procedures;
  plug_in_class->create_procedure = vtf_create_procedure;
  plug_in_class->set_i18n         = vtf_set_i18n;
}

static void
vtf_init (Vtf *vtf)
{
}

static gboolean
vtf_set_i18n (GimpPlugIn   *plug_in,
              const gchar  *procedure_name,
              gchar       **gettext_domain,
              gchar       **catalog_dir)
{
  /* Message catalogs live in <plug-in dir>/locale/<lang>/LC_MESSAGES/file-vtf.mo.
   * English strings are compiled in, so the catalogs are optional: without
   * the folder, localization is simply switched off. */
  gchar    *dir    = g_path_get_dirname (gimp_get_progname ());
  gchar    *locale = g_build_filename (dir, "locale", NULL);
  gboolean  found  = g_file_test (locale, G_FILE_TEST_IS_DIR);

  g_free (locale);
  g_free (dir);

  if (! found)
    return FALSE;

  *gettext_domain = g_strdup (GETTEXT_PACKAGE);
  *catalog_dir    = g_strdup ("locale");

  return TRUE;
}

static GList *
vtf_query_procedures (GimpPlugIn *plug_in)
{
  GList *list = NULL;

  list = g_list_append (list, g_strdup (THUMB_PROC));
  list = g_list_append (list, g_strdup (LOAD_PROC));
  list = g_list_append (list, g_strdup (EXPORT_PROC));

  return list;
}

static GimpProcedure *
vtf_create_procedure (GimpPlugIn  *plug_in,
                      const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (! strcmp (name, LOAD_PROC))
    {
      procedure = gimp_load_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           vtf_load, NULL, NULL);

      gimp_procedure_set_menu_label (procedure, _("Valve Texture Format"));

      gimp_procedure_set_documentation (procedure,
                                        _("Loads Valve Texture Format files"),
                                        _("Loads Valve Texture Format files. "
                                          "Animation frames, cube map faces "
                                          "and volume slices become layers. "
                                          "Uses VTFLib++."),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      COPYRIGHT, COPYRIGHT, RELEASE_DATE);

      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/x-vtf");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "vtf");
      gimp_file_procedure_set_magics (GIMP_FILE_PROCEDURE (procedure),
                                      "0,string,VTF");

      gimp_load_procedure_set_thumbnail_loader (GIMP_LOAD_PROCEDURE (procedure),
                                                THUMB_PROC);
    }
  else if (! strcmp (name, THUMB_PROC))
    {
      procedure = gimp_thumbnail_procedure_new (plug_in, name,
                                                GIMP_PDB_PROC_TYPE_PLUGIN,
                                                vtf_load_thumb, NULL, NULL);

      gimp_procedure_set_documentation (procedure,
                                        _("Loads thumbnails for Valve Texture "
                                          "Format files"),
                                        _("Loads a small mipmap (if there is "
                                          "one) from a VTF's middle "
                                          "frame/face/slice. Uses VTFLib++."),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      COPYRIGHT, COPYRIGHT, RELEASE_DATE);
    }
  else if (! strcmp (name, EXPORT_PROC))
    {
      procedure = gimp_export_procedure_new (plug_in, name,
                                             GIMP_PDB_PROC_TYPE_PLUGIN,
                                             FALSE, vtf_export, NULL, NULL);

      gimp_procedure_set_image_types (procedure, "*");

      gimp_procedure_set_menu_label (procedure, _("Valve Texture Format"));
      gimp_file_procedure_set_format_name (GIMP_FILE_PROCEDURE (procedure),
                                           "VTF");

      gimp_procedure_set_documentation (procedure,
                                        _("Exports Valve Texture Format files"),
                                        _("Writes Valve Texture Format files "
                                          "with any of the pixel formats "
                                          "supported by VTFLib++. Each "
                                          "top-level layer group is written "
                                          "to its own VTF, named "
                                          "<file>_<group>.vtf (the first "
                                          "group gets <file>.vtf).\n\n"
                                          "When run non-interactively the "
                                          "arguments apply to 'layer-group', "
                                          "or to every top-level layer group "
                                          "if it is unset. With last values, "
                                          "the settings stored in the image "
                                          "by the export dialog are used."),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      COPYRIGHT, COPYRIGHT, RELEASE_DATE);

      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/x-vtf");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "vtf");

      /* Layer groups are handled by the plug-in itself and the original
       * image is never modified, so ask GIMP not to touch it. */
      gimp_export_procedure_set_capabilities (GIMP_EXPORT_PROCEDURE (procedure),
                                              GIMP_EXPORT_CAN_HANDLE_RGB     |
                                              GIMP_EXPORT_CAN_HANDLE_GRAY    |
                                              GIMP_EXPORT_CAN_HANDLE_INDEXED |
                                              GIMP_EXPORT_CAN_HANDLE_ALPHA   |
                                              GIMP_EXPORT_CAN_HANDLE_LAYERS,
                                              NULL, NULL, NULL);

      gimp_procedure_add_choice_argument (procedure, "pixel-format",
                                          _("_Pixel format"),
                                          _("Pixel format to write"),
                                          vtf_format_choice_new (),
                                          "dxt5",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_choice_argument (procedure, "layers-as",
                                          _("Use _layers as"),
                                          _("How the layers of a group are used"),
                                          gimp_choice_new_with_values (
                                            "merge",     VTF_LAYERS_MERGE_VISIBLE, _("Nothing (merge visible)"),   NULL,
                                            "animation", VTF_LAYERS_ANIMATION,     _("Animation frames"),          NULL,
                                            "envmap",    VTF_LAYERS_ENVMAP,        _("Environment map faces"),     NULL,
                                            "volume",    VTF_LAYERS_VOLUME,        _("Volumetric texture slices"), NULL,
                                            NULL),
                                          "merge",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "version",
                                       _("VTF _version"),
                                       _("VTF minor version (7.n)"),
                                       VTF_VERSION_MIN, VTF_VERSION_MAX,
                                       VTF_VERSION_DEFAULT,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_boolean_argument (procedure, "mipmaps",
                                           _("_Mipmaps"),
                                           _("Generate mipmaps"),
                                           TRUE,
                                           G_PARAM_READWRITE);

      gimp_procedure_add_boolean_argument (procedure, "no-lod",
                                           _("No LO_D"),
                                           _("Always load at full resolution"),
                                           FALSE,
                                           G_PARAM_READWRITE);

      gimp_procedure_add_boolean_argument (procedure, "clamp",
                                           _("Clam_p"),
                                           _("Prevent texture repetition"),
                                           FALSE,
                                           G_PARAM_READWRITE);

      gimp_procedure_add_choice_argument (procedure, "bump-type",
                                          _("_Bump map"),
                                          _("Bump map type"),
                                          gimp_choice_new_with_values (
                                            "none",   VTF_BUMP_NONE,   _("Not bump"), NULL,
                                            "bump",   VTF_BUMP_NORMAL, _("Bump"),     NULL,
                                            "ssbump", VTF_BUMP_SSBUMP, _("SSBump"),   NULL,
                                            NULL),
                                          "none",
                                          G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "lod-control-u",
                                       _("LOD control U"),
                                       _("Power of 2 which describes the width "
                                         "of the standard mipmap (0 = unset)"),
                                       0, 16, 0,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_int_argument (procedure, "lod-control-v",
                                       _("LOD control V"),
                                       _("Power of 2 which describes the height "
                                         "of the standard mipmap (0 = unset)"),
                                       0, 16, 0,
                                       G_PARAM_READWRITE);

      gimp_procedure_add_layer_argument (procedure, "alpha-layer",
                                         _("_Alpha layer"),
                                         _("Layer to use as the alpha channel "
                                           "(none to use the layers' own "
                                           "alpha)"),
                                         TRUE,
                                         G_PARAM_READWRITE);

      gimp_procedure_add_group_layer_argument (procedure, "layer-group",
                                               _("Layer _group"),
                                               _("Top-level layer group to "
                                                 "export (none for all "
                                                 "groups)"),
                                               TRUE,
                                               G_PARAM_READWRITE);
    }

  return procedure;
}


/*
 * Run functions
 */

static GimpValueArray *
vtf_load (GimpProcedure         *procedure,
          GimpRunMode            run_mode,
          GFile                 *file,
          GimpMetadata          *metadata,
          GimpMetadataLoadFlags *flags,
          GimpProcedureConfig   *config,
          gpointer               run_data)
{
  GimpValueArray *return_vals;
  GimpImage      *image;
  GError         *error = NULL;

  gegl_init (NULL, NULL);

  if (! vtf_lib_begin (&error))
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR, error);

  image = vtf_load_image (file, &error);

  vtf_lib_end ();

  if (! image)
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR, error);

  *flags = GIMP_METADATA_LOAD_NONE;

  return_vals = gimp_procedure_new_return_values (procedure,
                                                  GIMP_PDB_SUCCESS, NULL);
  GIMP_VALUES_SET_IMAGE (return_vals, 1, image);

  return return_vals;
}

static GimpValueArray *
vtf_load_thumb (GimpProcedure       *procedure,
                GFile               *file,
                gint                 size,
                GimpProcedureConfig *config,
                gpointer             run_data)
{
  GimpValueArray *return_vals;
  GimpImage      *image;
  gint            width      = 0;
  gint            height     = 0;
  gint            num_layers = 0;
  GError         *error      = NULL;

  gegl_init (NULL, NULL);

  if (! vtf_lib_begin (&error))
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR, error);

  image = vtf_load_thumbnail (file, size, &width, &height, &num_layers,
                              &error);

  vtf_lib_end ();

  if (! image)
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR, error);

  return_vals = gimp_procedure_new_return_values (procedure,
                                                  GIMP_PDB_SUCCESS, NULL);

  GIMP_VALUES_SET_IMAGE (return_vals, 1, image);
  GIMP_VALUES_SET_INT   (return_vals, 2, width);
  GIMP_VALUES_SET_INT   (return_vals, 3, height);
  GIMP_VALUES_SET_ENUM  (return_vals, 4, GIMP_RGBA_IMAGE);
  GIMP_VALUES_SET_INT   (return_vals, 5, num_layers);

  return return_vals;
}

static void
vtf_options_from_config (VtfOptions          *opt,
                         GimpProcedureConfig *config)
{
  gchar    *format_id = NULL;
  gint      format;
  gint      version, lod_u, lod_v;
  gboolean  mipmaps, no_lod, clamp;
  GimpLayer *alpha_layer = NULL;

  g_object_get (config,
                "pixel-format",  &format_id,
                "version",       &version,
                "mipmaps",       &mipmaps,
                "no-lod",        &no_lod,
                "clamp",         &clamp,
                "lod-control-u", &lod_u,
                "lod-control-v", &lod_v,
                "alpha-layer",   &alpha_layer,
                NULL);

  format = vtf_format_index_from_id (format_id);
  g_free (format_id);

  opt->enabled            = TRUE;
  opt->advanced           = TRUE;
  opt->format             = format >= 0 ? format : 0;
  opt->with_alpha         = vtf_format_has_alpha (opt->format);
  opt->compress           = vtf_format_is_compressed (opt->format);
  opt->version            = version;
  opt->mipmaps            = mipmaps;
  opt->no_lod             = no_lod;
  opt->clamp              = clamp;
  opt->lod_u              = lod_u;
  opt->lod_v              = lod_v;
  opt->layer_use          = gimp_procedure_config_get_choice_id (config, "layers-as");
  opt->bump               = gimp_procedure_config_get_choice_id (config, "bump-type");
  opt->alpha_layer_tattoo = alpha_layer ?
                            gimp_item_get_tattoo (GIMP_ITEM (alpha_layer)) : 0;

  g_clear_object (&alpha_layer);
}

static GimpValueArray *
vtf_export (GimpProcedure        *procedure,
            GimpRunMode           run_mode,
            GimpImage            *image,
            GFile                *file,
            GimpExportOptions    *options,
            GimpMetadata         *metadata,
            GimpProcedureConfig  *config,
            gpointer              run_data)
{
  VtfExport          exp    = { 0 };
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GError            *error  = NULL;
  guint              i;

  gegl_init (NULL, NULL);

  if (run_mode == GIMP_RUN_INTERACTIVE)
    gimp_ui_init (PLUG_IN_BINARY);

  if (! vtf_lib_begin (&error))
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR, error);

  if (! vtf_export_prepare (&exp, image, file, &error))
    {
      status = GIMP_PDB_EXECUTION_ERROR;
      goto out;
    }

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      if (! vtf_export_dialog (&exp))
        {
          status = GIMP_PDB_CANCEL;
          goto out;
        }

      for (i = 0; i < exp.groups->len; i++)
        {
          VtfGroup *grp = &g_array_index (exp.groups, VtfGroup, i);

          vtf_options_store (&grp->opt, image, grp->item);
        }
      break;

    case GIMP_RUN_NONINTERACTIVE:
      {
        GimpGroupLayer *target = NULL;

        g_object_get (config, "layer-group", &target, NULL);

        for (i = 0; i < exp.groups->len; i++)
          {
            VtfGroup *grp = &g_array_index (exp.groups, VtfGroup, i);
            guint32   flags = grp->opt.general_flags;

            vtf_options_from_config (&grp->opt, config);
            grp->opt.general_flags = flags;

            if (target && grp->item != GIMP_ITEM (target))
              grp->opt.enabled = FALSE;
          }

        if (target)
          {
            gboolean found = FALSE;

            for (i = 0; i < exp.groups->len; i++)
              found |= g_array_index (exp.groups, VtfGroup, i).opt.enabled;

            if (! found)
              {
                g_set_error (&error, VTF_ERROR, 0,
                             _("'layer-group' is not a top-level layer group "
                               "of this image."));
                status = GIMP_PDB_CALLING_ERROR;
              }
          }

        g_clear_object (&target);

        if (status != GIMP_PDB_SUCCESS)
          goto out;
      }
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /* use the settings stored in the image */
      break;
    }

  if (! vtf_export_run (&exp, run_mode == GIMP_RUN_INTERACTIVE, &error))
    status = GIMP_PDB_EXECUTION_ERROR;

 out:
  vtf_export_clear (&exp);
  vtf_lib_end ();

  return gimp_procedure_new_return_values (procedure, status, error);
}


/*
 * Helpers
 */

static vlUInt vtf_image_handle = 0;

gboolean
vtf_lib_begin (GError **error)
{
  if (vlInitialize ()                    &&
      vlCreateImage (&vtf_image_handle)  &&
      vlBindImage (vtf_image_handle))
    return TRUE;

  g_set_error (error, VTF_ERROR, 0, _("Could not initialize VTFLib: %s"),
               vlGetLastError ());
  return FALSE;
}

void
vtf_lib_end (void)
{
  if (vtf_image_handle)
    vlDeleteImage (vtf_image_handle);
  vtf_image_handle = 0;

  vlShutdown ();
}

G_DEFINE_QUARK (gimp-vtf-error-quark, vtf_error)

gboolean
vtf_is_power_of_two (guint value)
{
  return value > 0 && (value & (value - 1)) == 0;
}

guint
vtf_next_power_of_two (guint value)
{
  guint result = 1;

  while (result < value && result < (1u << 31))
    result <<= 1;

  return result;
}
