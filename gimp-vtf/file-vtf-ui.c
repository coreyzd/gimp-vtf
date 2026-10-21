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

enum
{
  FORMAT_COLUMN_ALPHA,
  FORMAT_COLUMN_NAME,
  FORMAT_COLUMN_COUNT
};

typedef struct
{
  VtfExport *exp;
  VtfGroup  *grp;
  gint       lod_max;

  GtkWidget *page;
  GtkWidget *icon;
  GtkWidget *export_check;

  GtkWidget *layer_use_box;
  GtkWidget *layer_use;

  GtkWidget *simple_box;
  GtkWidget *compress;
  GtkWidget *with_alpha;
  GtkWidget *advanced;
  GtkWidget *version;

  GtkWidget *formats_frame;
  GtkWidget *formats_view;

  GtkWidget *mipmaps;
  GtkWidget *no_lod;
  GtkWidget *clamp;
  GtkWidget *bump;

  GtkWidget *lod_box;
  GtkWidget *lod_scale;

  GtkWidget *alpha_label;
  GtkWidget *alpha_enable;
  GtkWidget *alpha_combo;
} VtfTab;

static const gchar *vtf_versions[] = { "7.2", "7.3", "7.4", "7.5", "7.6" };

G_STATIC_ASSERT (G_N_ELEMENTS (vtf_versions) == VTF_VERSION_MAX - VTF_VERSION_MIN + 1);


static gint
tab_format (VtfTab *tab)
{
  return vtf_options_get_format (&tab->grp->opt,
                                 gimp_image_get_base_type (tab->exp->image));
}

static void
update_sensitivity (VtfTab *tab)
{
  VtfOptions *opt       = &tab->grp->opt;
  gboolean    has_alpha = vtf_format_has_alpha (tab_format (tab));

  gtk_widget_set_sensitive (tab->page, opt->enabled);
  gtk_widget_set_sensitive (tab->icon, opt->enabled);

  gtk_widget_set_sensitive (tab->layer_use_box, tab->grp->num_children > 1);

  gtk_widget_set_sensitive (tab->simple_box, ! opt->advanced);
  gtk_widget_set_visible (tab->formats_frame, opt->advanced);

  gtk_widget_set_sensitive (tab->no_lod, opt->mipmaps);
  gtk_widget_set_sensitive (tab->lod_box,
                            opt->version >= 3 && opt->mipmaps &&
                            ! opt->no_lod && tab->lod_max > 0);

  gtk_widget_set_sensitive (tab->alpha_label,  has_alpha);
  gtk_widget_set_sensitive (tab->alpha_enable, has_alpha);
  gtk_widget_set_sensitive (tab->alpha_combo,
                            has_alpha &&
                            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tab->alpha_enable)));
}

static void
select_format_row (VtfTab *tab,
                   gint    index)
{
  GtkTreePath *path;

  if (index < 0)
    return;

  path = gtk_tree_path_new_from_indices (index, -1);
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (tab->formats_view), path, NULL, FALSE);
  gtk_tree_path_free (path);
}


/*
 * Callbacks
 */

static void
export_toggled (GtkToggleButton *button,
                VtfTab          *tab)
{
  tab->grp->opt.enabled = gtk_toggle_button_get_active (button);
  update_sensitivity (tab);
}

static void
layer_use_changed (GtkComboBox *combo,
                   VtfTab      *tab)
{
  gint value;

  if (gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (combo), &value))
    tab->grp->opt.layer_use = value;
}

static void
compress_toggled (GtkToggleButton *button,
                  VtfTab          *tab)
{
  tab->grp->opt.compress = gtk_toggle_button_get_active (button);
  update_sensitivity (tab);
}

static void
with_alpha_toggled (GtkToggleButton *button,
                    VtfTab          *tab)
{
  tab->grp->opt.with_alpha = gtk_toggle_button_get_active (button);
  update_sensitivity (tab);
}

static void
advanced_toggled (GtkToggleButton *button,
                  VtfTab          *tab)
{
  VtfOptions *opt = &tab->grp->opt;

  if (gtk_toggle_button_get_active (button) && ! opt->advanced)
    {
      /* start from what the simple settings would have given */
      opt->format   = tab_format (tab);
      opt->advanced = TRUE;
      select_format_row (tab, opt->format);
      gtk_widget_grab_focus (tab->formats_view);
    }
  else
    {
      opt->advanced = gtk_toggle_button_get_active (button);
    }

  update_sensitivity (tab);
}

static void
format_selected (GtkTreeSelection *selection,
                 VtfTab           *tab)
{
  GtkTreeModel *model;
  GtkTreeIter   iter;
  GtkTreePath  *path;
  gint          index;

  if (! tab->grp->opt.advanced ||
      ! gtk_tree_selection_get_selected (selection, &model, &iter))
    return;

  path  = gtk_tree_model_get_path (model, &iter);
  index = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  tab->grp->opt.format = index;

  /* mirror the choice in the simple controls */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->compress),
                                vtf_format_is_compressed (index));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->with_alpha),
                                vtf_format_has_alpha (index));

  update_sensitivity (tab);
}

static void
version_changed (GtkComboBox *combo,
                 VtfTab      *tab)
{
  tab->grp->opt.version = gtk_combo_box_get_active (combo) + VTF_VERSION_MIN;
  update_sensitivity (tab);
}

static void
mipmaps_toggled (GtkToggleButton *button,
                 VtfTab          *tab)
{
  tab->grp->opt.mipmaps = gtk_toggle_button_get_active (button);
  update_sensitivity (tab);
}

static void
no_lod_toggled (GtkToggleButton *button,
                VtfTab          *tab)
{
  tab->grp->opt.no_lod = gtk_toggle_button_get_active (button);
  update_sensitivity (tab);
}

static void
clamp_toggled (GtkToggleButton *button,
               VtfTab          *tab)
{
  tab->grp->opt.clamp = gtk_toggle_button_get_active (button);
}

static void
bump_changed (GtkComboBox *combo,
              VtfTab      *tab)
{
  tab->grp->opt.bump = gtk_combo_box_get_active (combo);
}

static void
lod_changed (GtkRange *range,
             VtfTab   *tab)
{
  gint steps = (gint) gtk_range_get_value (range);

  if (steps <= 0)
    {
      /* full resolution: no need for a LOD resource */
      tab->grp->opt.lod_u = 0;
      tab->grp->opt.lod_v = 0;
    }
  else
    {
      tab->grp->opt.lod_u = MAX (1, tab->exp->exponent_u - steps);
      tab->grp->opt.lod_v = MAX (1, tab->exp->exponent_v - steps);
    }
}

static gchar *
lod_format_value (GtkScale *scale,
                  gdouble   value,
                  VtfTab   *tab)
{
  gint steps = (gint) value;

  return g_strdup_printf ("%d×%d",
                          tab->exp->width  >> steps,
                          tab->exp->height >> steps);
}

static void
alpha_layer_changed (VtfTab *tab)
{
  gint id;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tab->alpha_enable)) &&
      gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (tab->alpha_combo), &id))
    {
      GimpItem *item = gimp_item_get_by_id (id);

      tab->grp->opt.alpha_layer_tattoo = item ? gimp_item_get_tattoo (item) : 0;
    }
  else
    {
      tab->grp->opt.alpha_layer_tattoo = 0;
    }

  update_sensitivity (tab);
}

static void
alpha_enable_toggled (GtkToggleButton *button,
                      VtfTab          *tab)
{
  alpha_layer_changed (tab);
}

static void
alpha_combo_changed (GtkComboBox *combo,
                     VtfTab      *tab)
{
  alpha_layer_changed (tab);
}

static gboolean
alpha_layer_constraint (GimpImage *image,
                        GimpItem  *item,
                        gpointer   data)
{
  VtfTab *tab = data;

  return image == tab->exp->image && item != tab->grp->item;
}

static void
vtf_help (const gchar *help_id,
          gpointer     help_data)
{
  g_app_info_launch_default_for_uri (VTF_HELP_URI, NULL, NULL);
}


/*
 * Construction
 */

static GtkWidget *
bold_label (const gchar *text)
{
  GtkWidget *label = gtk_label_new (NULL);
  gchar     *markup = g_markup_printf_escaped ("<b>%s</b>", text);

  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  g_free (markup);

  return label;
}

static GtkWidget *
tab_label_new (VtfTab *tab)
{
  VtfGroup  *grp = tab->grp;
  GtkWidget *box;
  GtkWidget *frame;
  GtkWidget *label;
  GdkPixbuf *thumb;
  gchar     *markup;

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_tooltip_text (box, grp->display_name);

  /* FIXME: can't be focused with the keyboard in this location */
  tab->export_check = gtk_check_button_new ();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->export_check),
                                grp->opt.enabled);
  gtk_box_pack_start (GTK_BOX (box), tab->export_check, FALSE, FALSE, 0);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (box), frame, FALSE, FALSE, 0);

  tab->icon = gtk_image_new ();
  thumb = gimp_drawable_get_thumbnail (GIMP_DRAWABLE (grp->item), 32, 32,
                                       GIMP_PIXBUF_SMALL_CHECKS);
  if (thumb)
    {
      gtk_image_set_from_pixbuf (GTK_IMAGE (tab->icon), thumb);
      g_object_unref (thumb);
    }
  gtk_container_add (GTK_CONTAINER (frame), tab->icon);

  if (grp->is_main)
    markup = g_markup_printf_escaped ("<u>%s</u>", grp->name);
  else
    markup = g_markup_escape_text (grp->name, -1);

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 2);
  g_free (markup);

  gtk_widget_show_all (box);

  return box;
}

static GtkWidget *
tab_page_new (VtfTab       *tab,
              GtkListStore *formats_store)
{
  VtfExport  *exp = tab->exp;
  VtfGroup   *grp = tab->grp;
  VtfOptions *opt = &grp->opt;
  GtkWidget  *vbox;
  GtkWidget  *hbox;
  GtkWidget  *label;
  GtkWidget  *scrolled;
  GtkCellRenderer   *renderer;
  GtkTreeViewColumn *column;
  guint       i;

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), exp->has_groups ? 6 : 0);
  tab->page = vbox;

  /* Destination file */
  if (exp->has_groups)
    {
      gchar *markup = g_markup_printf_escaped ("<i>%s</i>", grp->display_name);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label), markup);
      gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
      g_free (markup);
    }

  /* ---- Format ---- */
  gtk_box_pack_start (GTK_BOX (vbox), bold_label (_("Format:")), FALSE, FALSE, 0);

  /* Layer use */
  tab->layer_use_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), tab->layer_use_box, FALSE, FALSE, 0);

  label = gtk_label_new_with_mnemonic (_("Use _layers as:"));
  gtk_box_pack_start (GTK_BOX (tab->layer_use_box), label, FALSE, FALSE, 0);

  tab->layer_use = gimp_int_combo_box_new (_("Nothing (merge visible)"),   VTF_LAYERS_MERGE_VISIBLE,
                                           _("Animation frames"),          VTF_LAYERS_ANIMATION,
                                           _("Environment map faces"),     VTF_LAYERS_ENVMAP,
                                           _("Volumetric texture slices"), VTF_LAYERS_VOLUME,
                                           NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (tab->layer_use), opt->layer_use);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), tab->layer_use);
  gtk_box_pack_start (GTK_BOX (tab->layer_use_box), tab->layer_use, TRUE, TRUE, 0);

  /* Simple format selection, advanced toggle, version */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  tab->simple_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (hbox), tab->simple_box, FALSE, FALSE, 0);

  tab->compress = gtk_check_button_new_with_mnemonic (_("Comp_ress"));
  gtk_widget_set_tooltip_markup (tab->compress,
                                 _("Apply lossy DXT compression to the texture."
                                   "<small><i>\n\n"
                                   "DXT is an algorithm that can be decoded "
                                   "very quickly. It saves memory but degrades "
                                   "colour accuracy, so don't use it for "
                                   "images with fine gradients.</i></small>"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->compress), opt->compress);
  gtk_box_pack_start (GTK_BOX (tab->simple_box), tab->compress, FALSE, FALSE, 0);

  tab->with_alpha = gtk_check_button_new_with_mnemonic (_("_Alpha"));
  gtk_widget_set_tooltip_markup (tab->with_alpha,
                                 _("Save the texture with an alpha channel."
                                   "<small><i>\n\n"
                                   "Alpha is an invisible \"fourth colour\" "
                                   "that can serve many different uses."
                                   "</i></small>"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->with_alpha), opt->with_alpha);
  gtk_box_pack_start (GTK_BOX (tab->simple_box), tab->with_alpha, FALSE, FALSE, 0);

  tab->advanced = gtk_toggle_button_new_with_mnemonic (_("Ad_vanced"));
  gtk_widget_set_tooltip_text (tab->advanced, _("Choose a pixel format from a list"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->advanced), opt->advanced);
  gtk_box_pack_start (GTK_BOX (hbox), tab->advanced, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox),
                      gtk_separator_new (GTK_ORIENTATION_VERTICAL),
                      FALSE, FALSE, 0);

  tab->version = gtk_combo_box_text_new ();
  gtk_widget_set_tooltip_markup (tab->version,
                                 _("VTF version to use. Older games cannot "
                                   "read newer formats.<small><i>\n\n"
                                   "7.2: Half-Life 2, 2004\n"
                                   "7.3 + 7.4: Orange Box, 2007\n"
                                   "7.5: Alien Swarm, 2010\n"
                                   "7.6: Strata Source</i></small>"));
  for (i = 0; i < G_N_ELEMENTS (vtf_versions); i++)
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tab->version), vtf_versions[i]);
  gtk_combo_box_set_active (GTK_COMBO_BOX (tab->version),
                            CLAMP (opt->version, VTF_VERSION_MIN, VTF_VERSION_MAX) - VTF_VERSION_MIN);
  gtk_box_pack_end (GTK_BOX (hbox), tab->version, FALSE, FALSE, 0);

  /* Advanced format list */
  tab->formats_frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (tab->formats_frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (vbox), tab->formats_frame, FALSE, FALSE, 0);

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scrolled), 200);
  gtk_container_add (GTK_CONTAINER (tab->formats_frame), scrolled);

  tab->formats_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (formats_store));
  gtk_container_add (GTK_CONTAINER (scrolled), tab->formats_view);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "xalign", 0.5, NULL);
  column = gtk_tree_view_column_new_with_attributes (_("Alpha"), renderer,
                                                     "text", FORMAT_COLUMN_ALPHA,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tab->formats_view), column);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "xpad", 4, NULL);
  column = gtk_tree_view_column_new_with_attributes (_("Pixel format"), renderer,
                                                     "text", FORMAT_COLUMN_NAME,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tab->formats_view), column);

  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (tab->formats_view)),
                               GTK_SELECTION_BROWSE);

  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_separator_new (GTK_ORIENTATION_HORIZONTAL),
                      FALSE, FALSE, 0);

  /* ---- Settings ---- */
  gtk_box_pack_start (GTK_BOX (vbox), bold_label (_("Settings:")), FALSE, FALSE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  tab->mipmaps = gtk_check_button_new_with_mnemonic (_("_Mipmaps"));
  gtk_widget_set_tooltip_markup (tab->mipmaps,
                                 _("Generate and embed low-resolution copies "
                                   "of the texture.<small><i>\n\n"
                                   "Mips are used on distant surfaces to "
                                   "improve performance and reduce resizing "
                                   "artifacts. Large mips can also be "
                                   "discarded to conserve texture memory. It "
                                   "is recommended to disable mips on UI "
                                   "textures.</i></small>"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->mipmaps), opt->mipmaps);
  gtk_box_pack_start (GTK_BOX (hbox), tab->mipmaps, FALSE, FALSE, 0);

  tab->no_lod = gtk_check_button_new_with_mnemonic (_("No LO_D"));
  gtk_widget_set_tooltip_markup (tab->no_lod,
                                 _("Ignore the player's texture detail "
                                   "setting.<small><i>\n\n"
                                   "Forces Source to load and render <b>at "
                                   "least</b> the standard mipmap regardless "
                                   "of the user's texture detail setting."
                                   "</i></small>"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->no_lod), opt->no_lod);
  gtk_box_pack_start (GTK_BOX (hbox), tab->no_lod, FALSE, FALSE, 0);

  tab->clamp = gtk_check_button_new_with_mnemonic (_("Clam_p"));
  gtk_widget_set_tooltip_markup (tab->clamp,
                                 _("Do not repeat the texture across a "
                                   "surface.<small><i>\n\n"
                                   "Prevents colour bleed by smearing the "
                                   "texture's edge pixels outward."
                                   "</i></small>"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->clamp), opt->clamp);
  gtk_box_pack_start (GTK_BOX (hbox), tab->clamp, FALSE, FALSE, 0);

  tab->bump = gtk_combo_box_text_new ();
  gtk_widget_set_tooltip_markup (tab->bump,
                                 _("Mark this texture as being a bump map."
                                   "<small><i>\n\n"
                                   "Use ATI2N or an uncompressed format for "
                                   "the best quality. GIMP's Filters → Generic "
                                   "→ Normal Map can generate one."
                                   "</i></small>"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tab->bump), _("Not bump"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tab->bump), _("Bump"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (tab->bump), _("SSBump"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (tab->bump), opt->bump);
  gtk_box_pack_end (GTK_BOX (hbox), tab->bump, FALSE, FALSE, 0);

  /* LOD control */
  tab->lod_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_tooltip_markup (tab->lod_box,
                                 _("Resolution at High texture detail."
                                   "<small><i>\n\n"
                                   "Lowering this setting allows a texture to "
                                   "reserve its largest mips for Very High "
                                   "texture detail (mat_picmip -1) and "
                                   "beyond. Requires VTF 7.3 or later."
                                   "</i></small>"));
  gtk_box_pack_start (GTK_BOX (vbox), tab->lod_box, FALSE, FALSE, 0);

  label = gtk_label_new (_("Standard mipmap:"));
  gtk_box_pack_start (GTK_BOX (tab->lod_box), label, FALSE, FALSE, 0);

  /* limit to four steps for UI sanity */
  tab->lod_max = MIN (3, MIN (exp->exponent_u, exp->exponent_v));

  tab->lod_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                             0, MAX (1, tab->lod_max), 1);
  gtk_scale_set_value_pos (GTK_SCALE (tab->lod_scale), GTK_POS_LEFT);
  gtk_scale_set_digits (GTK_SCALE (tab->lod_scale), 0);
  gtk_range_set_inverted (GTK_RANGE (tab->lod_scale), TRUE);
  gtk_range_set_increments (GTK_RANGE (tab->lod_scale), 1, 1);
  gtk_range_set_round_digits (GTK_RANGE (tab->lod_scale), 0);
  if (opt->lod_u > 0)
    gtk_range_set_value (GTK_RANGE (tab->lod_scale),
                         CLAMP (exp->exponent_u - opt->lod_u, 0, tab->lod_max));
  gtk_box_pack_start (GTK_BOX (tab->lod_box), tab->lod_scale, TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_separator_new (GTK_ORIENTATION_HORIZONTAL),
                      FALSE, FALSE, 0);

  /* ---- Use layer as alpha channel ---- */
  tab->alpha_label = bold_label (_("Use layer as alpha channel:"));
  gtk_box_pack_start (GTK_BOX (vbox), tab->alpha_label, FALSE, FALSE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_tooltip_markup (hbox,
                                 _("Override the final alpha channel with the "
                                   "contents of this layer.<small>\n\n<i>"
                                   "GIMP assumes that alpha is transparency "
                                   "and will happily destroy pixels it "
                                   "considers invisible. Separating the alpha "
                                   "channel into another layer bypasses this "
                                   "issue and makes editing easier."
                                   "</i></small>"));
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  tab->alpha_enable = gtk_check_button_new ();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->alpha_enable),
                                opt->alpha_layer_tattoo != 0);
  gtk_box_pack_start (GTK_BOX (hbox), tab->alpha_enable, FALSE, FALSE, 0);

  tab->alpha_combo = gimp_layer_combo_box_new (alpha_layer_constraint, tab, NULL);
  if (opt->alpha_layer_tattoo)
    {
      GimpLayer *layer = gimp_image_get_layer_by_tattoo (exp->image,
                                                         opt->alpha_layer_tattoo);
      if (layer)
        gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (tab->alpha_combo),
                                       gimp_item_get_id (GIMP_ITEM (layer)));
    }
  gtk_box_pack_start (GTK_BOX (hbox), tab->alpha_combo, TRUE, TRUE, 0);

  gtk_widget_show_all (vbox);

  return vbox;
}

static void
tab_connect (VtfTab *tab)
{
  g_signal_connect (tab->export_check, "toggled",  G_CALLBACK (export_toggled),       tab);
  g_signal_connect (tab->layer_use,    "changed",  G_CALLBACK (layer_use_changed),    tab);
  g_signal_connect (tab->compress,     "toggled",  G_CALLBACK (compress_toggled),     tab);
  g_signal_connect (tab->with_alpha,   "toggled",  G_CALLBACK (with_alpha_toggled),   tab);
  g_signal_connect (tab->advanced,     "toggled",  G_CALLBACK (advanced_toggled),     tab);
  g_signal_connect (tab->version,      "changed",  G_CALLBACK (version_changed),      tab);
  g_signal_connect (tab->mipmaps,      "toggled",  G_CALLBACK (mipmaps_toggled),      tab);
  g_signal_connect (tab->no_lod,       "toggled",  G_CALLBACK (no_lod_toggled),       tab);
  g_signal_connect (tab->clamp,        "toggled",  G_CALLBACK (clamp_toggled),        tab);
  g_signal_connect (tab->bump,         "changed",  G_CALLBACK (bump_changed),         tab);
  g_signal_connect (tab->lod_scale,    "value-changed", G_CALLBACK (lod_changed),     tab);
  g_signal_connect (tab->lod_scale,    "format-value",  G_CALLBACK (lod_format_value), tab);
  g_signal_connect (tab->alpha_enable, "toggled",  G_CALLBACK (alpha_enable_toggled), tab);
  g_signal_connect (tab->alpha_combo,  "changed",  G_CALLBACK (alpha_combo_changed),  tab);
  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (tab->formats_view)),
                    "changed", G_CALLBACK (format_selected), tab);
}

gboolean
vtf_export_dialog (VtfExport *exp)
{
  GtkWidget    *dialog;
  GtkWidget    *notebook;
  GtkListStore *formats_store;
  VtfTab       *tabs;
  gchar        *image_name;
  gchar        *title;
  gchar        *dot;
  gboolean      run;
  guint         i;

  {
    /* "brick.xcf" or "brick.vtf" → "brick" */
    GFile *image_file = gimp_image_get_file (exp->image);

    if (image_file)
      {
        image_name = g_file_get_basename (image_file);
        g_object_unref (image_file);
      }
    else
      {
        image_name = gimp_image_get_name (exp->image);
      }
  }
  dot = strrchr (image_name, '.');
  if (dot && dot != image_name)
    *dot = '\0';

  title = g_strdup_printf (_("Export %s to VTF"), image_name);
  g_free (image_name);

  dialog = gimp_dialog_new (title, PLUG_IN_ROLE, NULL, 0,
                            vtf_help, "file-vtf",
                            _("_Cancel"), GTK_RESPONSE_CANCEL,
                            _("_Export"), GTK_RESPONSE_OK,
                            NULL);
  g_free (title);

  gimp_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                            GTK_RESPONSE_OK,
                                            GTK_RESPONSE_CANCEL,
                                            -1);
  gimp_window_set_transient (GTK_WINDOW (dialog));
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  formats_store = gtk_list_store_new (FORMAT_COLUMN_COUNT,
                                      G_TYPE_STRING, G_TYPE_STRING);
  for (i = 0; i < vtf_num_formats; i++)
    gtk_list_store_insert_with_values (formats_store, NULL, -1,
                                       FORMAT_COLUMN_ALPHA, vtf_formats[i].alpha_label,
                                       FORMAT_COLUMN_NAME,  vtf_formats[i].label,
                                       -1);

  notebook = gtk_notebook_new ();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_LEFT);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), exp->has_groups);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), exp->has_groups);
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (notebook), 12);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      notebook, TRUE, TRUE, 0);
  gtk_widget_show (notebook);

  tabs = g_new0 (VtfTab, exp->groups->len);

  for (i = 0; i < exp->groups->len; i++)
    {
      VtfTab    *tab = &tabs[i];
      GtkWidget *page;
      GtkWidget *tab_label;

      tab->exp = exp;
      tab->grp = &g_array_index (exp->groups, VtfGroup, i);

      page = tab_page_new (tab, formats_store);

      if (exp->has_groups)
        {
          tab_label = tab_label_new (tab);
        }
      else
        {
          tab_label = gtk_label_new (NULL);
          tab->export_check = gtk_check_button_new ();
          tab->icon = gtk_image_new ();
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tab->export_check), TRUE);
          g_object_ref_sink (tab->export_check);
          g_object_ref_sink (tab->icon);
        }

      gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, tab_label);

      if (tab->grp->opt.advanced)
        select_format_row (tab, tab->grp->opt.format);

      tab_connect (tab);
      update_sensitivity (tab);
    }

  g_object_unref (formats_store);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), exp->initial_group);

  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  if (! exp->has_groups)
    {
      g_object_unref (tabs[0].export_check);
      g_object_unref (tabs[0].icon);
    }

  g_free (tabs);

  return run;
}
