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

#ifndef FILE_VTF_H
#define FILE_VTF_H

#include <string.h>

#include <libgimp/gimp.h>
#include <glib/gi18n-lib.h>

#include "VTFLib.h"

#define LOAD_PROC        "file-vtf-load"
#define THUMB_PROC       "file-vtf-load-thumb"
#define EXPORT_PROC      "file-vtf-export"
#define PLUG_IN_BINARY   "file-vtf"
#define PLUG_IN_ROLE     "gimp-file-vtf"

#define VTF_PARASITE_NAME   "gimp-vtf-export-options"
#define VTF_HELP_URI        "https://developer.valvesoftware.com/wiki/VTF"

/*
 * Pixel formats offered by the exporter
 */

typedef struct
{
  const gchar    *id;          /* stable identifier, used by the PDB and in saved settings */
  const gchar    *label;
  VTFImageFormat  format;
  const gchar    *alpha_label; /* "-" if the format has no alpha channel */
  gboolean        compressed;
} VtfFormat;

extern const VtfFormat vtf_formats[];
extern const guint     vtf_num_formats;

gboolean     vtf_format_has_alpha        (gint            index);
gboolean     vtf_format_is_compressed    (gint            index);
gint         vtf_format_index_from_id    (const gchar    *id);
gint         vtf_format_index_from_vtflib(VTFImageFormat  format);
gboolean     vtflib_format_has_alpha     (VTFImageFormat  format);
GimpChoice * vtf_format_choice_new       (void);

/*
 * Per-layer-group export options
 */

typedef enum
{
  VTF_LAYERS_MERGE_VISIBLE = 0,
  VTF_LAYERS_ANIMATION,
  VTF_LAYERS_ENVMAP,
  VTF_LAYERS_VOLUME
} VtfLayerUse;

typedef enum
{
  VTF_BUMP_NONE = 0,
  VTF_BUMP_NORMAL,
  VTF_BUMP_SSBUMP
} VtfBumpType;

#define VTF_VERSION_MIN     2
#define VTF_VERSION_MAX     6
#define VTF_VERSION_DEFAULT 4

typedef struct
{
  gboolean    enabled;        /* export this layer group? */

  gint        version;        /* 7.n */
  gboolean    advanced;       /* pick the pixel format from the full list */

  /* simple format selection */
  gboolean    with_alpha;
  gboolean    compress;

  /* advanced format selection (index into vtf_formats) */
  gint        format;

  gboolean    clamp;
  gboolean    no_lod;
  gboolean    mipmaps;
  VtfBumpType bump;
  VtfLayerUse layer_use;
  guint       alpha_layer_tattoo;

  guint32     general_flags;  /* flags carried over from an imported VTF */

  /* LOD control resource: log2 of the clamped resolution, 0 = unset */
  gint        lod_u;
  gint        lod_v;
} VtfOptions;

void     vtf_options_init_default (VtfOptions        *opt);
gint     vtf_options_get_format   (const VtfOptions  *opt,
                                   GimpImageBaseType  base_type);

/* Settings are kept as parasites on the layer group (or on the image when
 * there are no layer groups), so they are saved inside the XCF. */
gboolean vtf_options_load         (VtfOptions        *opt,
                                   GimpImage         *image,
                                   GimpItem          *group);
void     vtf_options_store        (const VtfOptions  *opt,
                                   GimpImage         *image,
                                   GimpItem          *group);

/* Settings written by gimp-vtf 1.x for GIMP 2.8, in "<image>.xcf.vtf-settings" */
gboolean vtf_options_load_legacy  (VtfOptions        *opt,
                                   GimpImage         *image,
                                   gint32             key);

/*
 * Export state: one entry per top-level layer group
 */

typedef struct
{
  GimpItem   *item;         /* NULL when the image has no layer groups */
  guint       tattoo;
  gchar      *name;
  GFile      *file;         /* destination */
  gchar      *display_name; /* destination file name, for the UI */
  gboolean    is_main;      /* written to the file name GIMP asked for */
  gint        num_children;
  VtfOptions  opt;
} VtfGroup;

typedef struct
{
  GimpImage  *image;
  GFile      *file;
  gint        width;
  gint        height;
  gint        exponent_u;   /* log2(width)  */
  gint        exponent_v;   /* log2(height) */
  gboolean    has_groups;   /* FALSE: whole image exported as one VTF */
  GArray     *groups;       /* of VtfGroup */
  guint       initial_group;
} VtfExport;

/* file-vtf-load.c */
GimpImage * vtf_load_image     (GFile     *file,
                                GError   **error);
GimpImage * vtf_load_thumbnail (GFile     *file,
                                gint       size,
                                gint      *width,
                                gint      *height,
                                gint      *num_layers,
                                GError   **error);

/* file-vtf-save.c */
gboolean    vtf_export_prepare (VtfExport *exp,
                                GimpImage *image,
                                GFile     *file,
                                GError   **error);
gboolean    vtf_export_run     (VtfExport *exp,
                                gboolean   interactive,
                                GError   **error);
void        vtf_export_clear   (VtfExport *exp);

/* file-vtf-ui.c */
gboolean    vtf_export_dialog  (VtfExport *exp);

/* file-vtf.c */
gboolean    vtf_lib_begin      (GError   **error);
void        vtf_lib_end        (void);
GQuark      vtf_error_quark    (void);
#define VTF_ERROR (vtf_error_quark ())

gboolean    vtf_is_power_of_two (guint value);
guint       vtf_next_power_of_two (guint value);

#endif /* FILE_VTF_H */
