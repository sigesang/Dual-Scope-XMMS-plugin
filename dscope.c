/*
  Dual Scope 1.3
 -----------------------
  plugin for XMMS

  by Joakim 'basemetal' Elofsson
*/

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmms/plugin.h>
#include <xmms/configfile.h>

#include "bg-def.xpm"
#include "dscope_mini.xpm"

#define THIS_IS "Dual Scope v1.3"

#define CONFIG_SECTION "Dual Scope"

/* THEMEDIR at maketime */
#define THEME_DEFAULT_STR "default"
#define THEME_DEFAULT_PATH THEMEDIR

/*  */
#define FSEL_ALWAYS_DEFAULT_PATH
/* display */
#define AWIDTH 128
#define AHEIGHT 48
/* window */
#define TOP_BORDER 14
#define BOTTOM_BORDER 6
#define SIDE_BORDER 7
#define WINWIDTH 275
#define WINHEIGHT AHEIGHT+BOTTOM_BORDER+TOP_BORDER

#define TYPE_DOT 0
#define TYPE_LINE 1
#define TYPE_FILLED 2
#define TYPE_FILLED2 3

extern GtkWidget *mainwin; /* xmms mainwin */
extern GList *dock_window_list; /* xmms dockwinlist*/

static GtkWidget *window = NULL;
static GtkWidget *drwarea = NULL;
static GtkWidget *win_about = NULL;
static GtkWidget *win_conf = NULL;

static GtkWidget *btn_snapmainwin = NULL;
static GtkWidget *ckbtn_rcoords = NULL;
static GtkWidget *fsel = NULL;
static GtkWidget *etry_theme = NULL;
static GdkBitmap *mask = NULL;

static GdkPixmap *bg_pixmap = NULL;
static GdkPixmap *pixmap = NULL;
static gboolean dscope_window_motion = FALSE;
static GdkColor graphcolor;

static GdkGC *gc = NULL;

typedef struct {
  int       type;
  char      *skin_xpm;
  gint      pos_x;
  gint      pos_y;
  gboolean rel_main;
} DScopeCfg;

static DScopeCfg Cfg = { 0, NULL, 0, 0, 0 };

extern GList *dock_add_window(GList *, GtkWidget *);
extern gboolean dock_is_moving(GtkWidget *);
extern void dock_move_motion(GtkWidget *,GdkEventMotion *);
extern void dock_move_press(GList *, GtkWidget *, GdkEventButton *, gboolean);
extern void dock_move_release(GtkWidget *);

static void dscope_about ();
static void dscope_init (void);
static void dscope_cleanup (void);
static void dscope_playback_start (void);
static void dscope_playback_stop (void);
static void dscope_render_pcm (gint16 data[2][512]);
static void dscope_config (void);
static void create_fileselection (void);
static void dscope_config_read ();
static GtkWidget* dscope_create_menu(void);
static void dscope_config_write ();

VisPlugin dscope_vp = {
	NULL, NULL, 0,
	THIS_IS,
	2, // pcm channels
	0, // freq channels
	dscope_init, 
	dscope_cleanup,
	dscope_about,
	dscope_config,
	NULL,
	dscope_playback_start,
	dscope_playback_stop,
	dscope_render_pcm,
	NULL
};

VisPlugin *get_vplugin_info (void) {
  return &dscope_vp;
}

static void dscope_destroy_cb (GtkWidget *w, gpointer data) {
  dscope_vp.disable_plugin(&dscope_vp);
}

static void dscope_set_theme() {
  GdkColor color;
  GdkGC *gc2 = NULL;
  GdkImage *bg_img = NULL;
  guint32 pixel;
  GdkVisual *visual;
  GdkColormap *cmap;

  if (Cfg.skin_xpm != NULL && strcmp(Cfg.skin_xpm, "default") != 0)
    bg_pixmap = gdk_pixmap_create_from_xpm(window->window, &mask, NULL, Cfg.skin_xpm);
  if (bg_pixmap == NULL)
    bg_pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, bg_def_xpm);  

  bg_img = gdk_image_get(bg_pixmap, WINWIDTH + 1, 0, 1, 1);
  pixel = gdk_image_get_pixel(bg_img, 0, 0);
  cmap = gdk_window_get_colormap(window->window);
  visual = gdk_colormap_get_visual (cmap);

  graphcolor.red = ((pixel&visual->red_mask)>>visual->red_shift)<<(16-visual->red_prec);
  graphcolor.green = ((pixel & visual->green_mask)>>visual->green_shift)<<(16-visual->green_prec);
  graphcolor.blue = ((pixel & visual->blue_mask)>>visual->blue_shift)<<(16-visual->blue_prec);

  gdk_colormap_alloc_color(cmap, &graphcolor, FALSE, TRUE);
  gdk_gc_set_foreground(gc, &graphcolor);
  
  gc2 = gdk_gc_new(mask);
  color.pixel = 1;
  gdk_gc_set_foreground(gc2, &color);
  gdk_draw_rectangle(mask, gc2, TRUE, SIDE_BORDER, TOP_BORDER, AWIDTH, AHEIGHT);
  gdk_draw_rectangle(mask, gc2, TRUE, WINWIDTH-SIDE_BORDER-AWIDTH, 
		     TOP_BORDER, AWIDTH, AHEIGHT);
  color.pixel = 0;
  gdk_gc_set_foreground(gc2, &color);
  gdk_draw_line(mask, gc2, WINWIDTH, 0 ,WINWIDTH , WINHEIGHT-1);
  gtk_widget_shape_combine_mask(window, mask, 0, 0);
  gdk_gc_destroy(gc2);

  gdk_draw_pixmap(pixmap, gc , bg_pixmap,
		  0, 0, 0, 0, WINWIDTH, WINHEIGHT);
  gdk_window_clear(drwarea->window);
}

static gint dscope_move_x;
static gint dscope_move_y;

static gint dscope_mousebtnrel_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_BUTTON_RELEASE) {
    if (dock_is_moving(window)) {
      dock_move_release(window);
    }
    if (event->button == 1) {
      if ((event->x > (WINWIDTH - TOP_BORDER)) &&
	 (event->y < TOP_BORDER)) { // topright corner
	dscope_vp.disable_plugin(&dscope_vp);
      }
      if (dscope_window_motion) {
	gdk_pointer_ungrab(GDK_CURRENT_TIME);
	dscope_window_motion = FALSE;
      }
    }
  }
  
  return TRUE;
}

static gint dscope_mousemove_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  if (dscope_window_motion) {
    GdkModifierType modmask;
    gint mouse_x, mouse_y;

    gdk_window_get_pointer(NULL, &mouse_x, &mouse_y, &modmask);
    gdk_window_move(window->window,  mouse_x - dscope_move_x, mouse_y - dscope_move_y);
  }

  if (dock_is_moving(window)) {
    dock_move_motion(window, event);
  }

  return TRUE;
}

static gint dscope_mousebtn_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{

  dscope_move_x = event->x;
  dscope_move_y = event->y;

  if (event->type == GDK_BUTTON_PRESS) {
    if ((event->button == 1) &&
	(event->x < (WINWIDTH - TOP_BORDER)) &&
	(event->y <= TOP_BORDER)) { /*topright corner*/
      gdk_window_raise(window->window);
      gdk_pointer_grab(window->window, FALSE, GDK_BUTTON_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
		       GDK_NONE, GDK_NONE, GDK_CURRENT_TIME);
      dscope_window_motion = TRUE;
      dock_move_press(dock_window_list, window, event, FALSE);
    }
    
    if (event->button == 3) {
      gtk_menu_popup ((GtkMenu *)data, NULL, NULL, NULL, NULL, 
                            event->button, event->time);
    }
  }

  return TRUE;
}

static void dscope_set_icon (GtkWidget *win)
{
  static GdkPixmap *icon;
  static GdkBitmap *mask;
  Atom icon_atom;
  glong data[2];
  
  if (!icon)
    icon = gdk_pixmap_create_from_xpm_d (win->window, &mask, 
					 &win->style->bg[GTK_STATE_NORMAL], 
					 dscope_mini_xpm);
  data[0] = GDK_WINDOW_XWINDOW(icon);
  data[1] = GDK_WINDOW_XWINDOW(mask);
  
  icon_atom = gdk_atom_intern ("KWM_WIN_ICON", FALSE);
  gdk_property_change (win->window, icon_atom, icon_atom, 32,
		       GDK_PROP_MODE_REPLACE, (guchar *)data, 2);
}

static void dscope_init (void) {
  GdkColor color;
  GtkWidget *menu;

  if (window) return;

  dscope_config_read();

  create_fileselection();
  window = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_window_set_title(GTK_WINDOW(window), THIS_IS);
  gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
  gtk_window_set_wmclass(GTK_WINDOW(window), "XMMS_Player", "DualScope");
  gtk_widget_set_usize(window, WINWIDTH, WINHEIGHT);
  gtk_widget_set_events(window, GDK_BUTTON_MOTION_MASK | 
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
  gtk_widget_realize(window);
  dscope_set_icon(window);
  gdk_window_set_decorations(window->window, 0);

  if (Cfg.pos_x != -1)
    gtk_widget_set_uposition (window, Cfg.pos_x, Cfg.pos_y);

  menu = dscope_create_menu();

  gtk_signal_connect(GTK_OBJECT(window),"destroy",
		     GTK_SIGNAL_FUNC(dscope_destroy_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(window), "destroy",
		     GTK_SIGNAL_FUNC(gtk_widget_destroyed), &window);

  gtk_signal_connect(GTK_OBJECT(window), "button_press_event",
		     GTK_SIGNAL_FUNC(dscope_mousebtn_cb), (gpointer) menu);
  gtk_signal_connect(GTK_OBJECT(window), "button_release_event",
		     GTK_SIGNAL_FUNC(dscope_mousebtnrel_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(window), "motion_notify_event",
		     GTK_SIGNAL_FUNC(dscope_mousemove_cb), NULL);

  gc = gdk_gc_new(window->window);
  gdk_color_black(gdk_colormap_get_system(), &color);
  gdk_gc_set_foreground(gc, &color);

  pixmap = gdk_pixmap_new(window->window, WINWIDTH, WINHEIGHT,
			  gdk_visual_get_best_depth());
  gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, WINWIDTH, WINHEIGHT);

  drwarea = gtk_drawing_area_new();
  gtk_widget_show (drwarea);
  gtk_container_add (GTK_CONTAINER (window), drwarea);
  gtk_drawing_area_size((GtkDrawingArea *) drwarea, WINWIDTH, WINHEIGHT);
  gdk_window_set_back_pixmap(drwarea->window, pixmap, 0);
  gdk_window_clear(drwarea->window);

  dscope_set_theme();

  /*gdk_color_white(gdk_colormap_get_system(), &color); */
  /*gdk_gc_set_foreground(gc, &graphcolor);*/

  if (!g_list_find(dock_window_list, window)) {
    dock_add_window(dock_window_list, window);
  }

  gtk_widget_show(window);
}

static void dscope_cleanup (void) {
  dscope_config_write();

  /* dock_remove_window() seems not to exist in xmms,
     not sure if this is needed */
  g_list_remove(dock_window_list, window); 

  if (win_about) gtk_widget_destroy(win_about);
  if (window)    gtk_widget_destroy(window);
  if (fsel)      gtk_widget_destroy(fsel);
  if (gc)           { gdk_gc_unref(gc); gc = NULL; }
  if (bg_pixmap)    { gdk_pixmap_unref(bg_pixmap); bg_pixmap = NULL; }
  if (pixmap) { gdk_pixmap_unref(pixmap); pixmap = NULL; }
  if (Cfg.skin_xpm) g_free(Cfg.skin_xpm);

	
}

static void dscope_playback_start (void) {
  /* only void code */
}

static void dscope_playback_stop (void) {
  /* */
}

static void dscope_render_pcm (gint16 data[2][512]) {
  int i = 0;
  gint32 ry, ly, try = 0, tly = 0;
  gint16 *rd, *ld;

  if (!window) return;

  GDK_THREADS_ENTER();
  /* cleanup grafs */
  gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		     SIDE_BORDER, TOP_BORDER,
		     SIDE_BORDER, TOP_BORDER,
		     AWIDTH, AHEIGHT);
  gdk_draw_pixmap(pixmap, gc, bg_pixmap,
		     WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER,
		     WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER,
		     AWIDTH, AHEIGHT);
  
  ld = data[0];
  rd = data[1];
  
  for ( i = 0; i < 128; i++ ) {
    ry = *rd; rd++;
    ry += *rd; rd++;
    ry += *rd; rd++;
    ry += *rd; rd++;
    /* ry /= 4*65536/AHEIGHT; */
    ry /= 5462;

    ly = *ld; ld++;
    ly += *ld; ld++;
    ly += *ld; ld++;
    ly += *ld; ld++;
    /*ly /= 4*65536/AHEIGHT; */
    ly /= 5462;

    ry += AHEIGHT/2;
    ly += AHEIGHT/2;

    /* range should be 0-47 here */

    if (Cfg.type == TYPE_FILLED2) { /* new ? */
      ry /= 2;
      ly /= 2;
      gdk_draw_line(pixmap, gc,
		    WINWIDTH - SIDE_BORDER - AWIDTH + i, 
		    TOP_BORDER + AHEIGHT/2-1-ry,
		    WINWIDTH - SIDE_BORDER - AWIDTH + i,
		    TOP_BORDER + AHEIGHT/2+ry);
      gdk_draw_line(pixmap, gc,
		    SIDE_BORDER + i,
		    TOP_BORDER + AHEIGHT/2-1-ly,
		    SIDE_BORDER + i,
		    TOP_BORDER + AHEIGHT/2+ly);
    }
    else if (Cfg.type == TYPE_FILLED) { /* filled */
      if (ry != AHEIGHT/2) 
	gdk_draw_line(pixmap, gc, 
		      WINWIDTH - SIDE_BORDER - AWIDTH + i, 
		      TOP_BORDER + ((ry > 0) ? AHEIGHT/2 : AHEIGHT/2 - 1), 
		      WINWIDTH - SIDE_BORDER - AWIDTH + i, 
		      TOP_BORDER + ry);
      if (ly != AHEIGHT/2) 
	gdk_draw_line(pixmap, gc, 
		      SIDE_BORDER + i, 
		      TOP_BORDER + ((ly > 0) ? AHEIGHT/2 : AHEIGHT/2 - 1), 
		      SIDE_BORDER + i, 
		      TOP_BORDER + ly);
    }
    else if (Cfg.type == TYPE_LINE) { /* line */
      if (i == 0) { 
	gdk_draw_point(pixmap, gc, WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER + ry);
	gdk_draw_point(pixmap, gc, SIDE_BORDER, TOP_BORDER + ly);
	tly = ly;
	try = ry;
      }
      else {
	gdk_draw_line(pixmap, gc, 
		      WINWIDTH - SIDE_BORDER - AWIDTH + i-1, TOP_BORDER + try, 
		      WINWIDTH - SIDE_BORDER - AWIDTH + i, TOP_BORDER + ry);
	gdk_draw_line(pixmap, gc, 
		      SIDE_BORDER + i - 1, TOP_BORDER + tly, 
		      SIDE_BORDER + i - 0, TOP_BORDER + ly);
	tly = ly;
	try = ry;
      }
    }
    else { /* (Cfg.type == TYPE_DOT) * dot */
      gdk_draw_point(pixmap, gc,
		     WINWIDTH - SIDE_BORDER - AWIDTH + i, 
		     TOP_BORDER + ry);
      gdk_draw_point(pixmap, gc, 
		     SIDE_BORDER + i,
		     TOP_BORDER + ly);
    }
  }
  gdk_window_clear(drwarea->window);
  gdk_window_clear(drwarea->window);
  GDK_THREADS_LEAVE();

  return;
}

static void dscope_config_read () {
  ConfigFile *cfg;
  gchar *filename, *themefile;

  Cfg.type = 0;
  Cfg.pos_x = -1;
  Cfg.pos_y = -1;

  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  if ((cfg = xmms_cfg_open_file(filename)) != NULL) {
    xmms_cfg_read_int(cfg, CONFIG_SECTION, "type", &Cfg.type);
    xmms_cfg_read_string(cfg, CONFIG_SECTION, "skin_xpm", &themefile);
    if (themefile) Cfg.skin_xpm = g_strdup(themefile);
    xmms_cfg_read_int(cfg, CONFIG_SECTION, "pos_x", &Cfg.pos_x);
    xmms_cfg_read_int(cfg, CONFIG_SECTION, "pos_y", &Cfg.pos_y);
    xmms_cfg_free(cfg);
  }
  g_free(filename);
}

static void dscope_config_write () {
  ConfigFile *cfg;
  gchar *filename;

  if ((Cfg.pos_x != -1) && (window != NULL)) {
    gdk_window_get_position(window->window, &Cfg.pos_x, &Cfg.pos_y);
  }
    
  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  if ((cfg = xmms_cfg_open_file(filename)) != NULL) {
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "type", Cfg.type);
    xmms_cfg_write_string(cfg, CONFIG_SECTION, "skin_xpm",
			    (Cfg.skin_xpm != NULL)?Cfg.skin_xpm:"default");
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "pos_x", Cfg.pos_x);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "pos_y", Cfg.pos_y);
    xmms_cfg_write_file(cfg, filename);
    xmms_cfg_free(cfg);
  }
  g_free(filename);  
}

/* ************************* */
/* aboutwindow callbacks     */
static void on_btn_about_close_clicked (GtkButton *button, gpointer user_data) {
  gtk_widget_destroy(win_about);
  win_about = NULL;
}

/* ************************* */
/* configwindow callbacks    */
static void on_rdbtn_type_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.type = (int)user_data;
}

static void on_btn_snapmainwin_clicked (GtkButton *button, gpointer user_data) {
  gint x, y, h, w;
  if(mainwin != NULL) {
    gdk_window_get_position(mainwin->window, &x, &y);
    gdk_window_get_size(mainwin->window, &w, &h);
    if(window)
      gdk_window_move(window->window, x, y+h);
    if(gtk_toggle_button_get_active((GtkToggleButton *) user_data)) {
      Cfg.pos_x=x;
      Cfg.pos_y=y+h;
    }
  }
}

static void on_confbtn_close_clicked (GtkButton *button, gpointer user_data) {
  dscope_config_write();
  gtk_widget_destroy(win_conf);
  win_conf=NULL;
}

static void on_ckbtn_rcoords_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  if(gtk_toggle_button_get_active(togglebutton)) {
    if(window!=NULL)
      gdk_window_get_position(window->window, &Cfg.pos_x, &Cfg.pos_y);
  } else {
    Cfg.pos_x=-1;
  }
}

static void on_etry_theme_changed (GtkEditable *editable, gpointer user_data) {
  g_free(Cfg.skin_xpm);
  Cfg.skin_xpm = g_strdup(gtk_entry_get_text((GtkEntry *) editable));
  if(window) dscope_set_theme();
}

static void on_btn_theme_clicked (GtkButton *button, gpointer user_data) {
  if(fsel == NULL)
    create_fileselection();
  gtk_widget_show(fsel);
}

/* ************************* */
/* fileselect callbacks     */
static void on_btn_fsel_cancel_clicked (GtkButton *button, gpointer user_data) {
  gtk_widget_destroy(fsel);
  fsel=NULL;
}

static void on_btn_fsel_ok_clicked (GtkButton *button, gpointer user_data) {
  gchar *fname;
  fname=gtk_file_selection_get_filename((GtkFileSelection *) fsel);
  gtk_entry_set_text((GtkEntry *) etry_theme, fname);
  gtk_widget_destroy(fsel);
  fsel=NULL;
}

/* ****                                            */
/* creates aboutwindow if not present and shows it */

#define ABOUT_MARGIN 10
#define ABOUT_WIDTH 300
#define ABOUT_HEIGHT 150

static void dscope_about (void) {
  GtkWidget *vb_main;
  GtkWidget *frm;
  GtkWidget *lbl_author;
  GtkWidget *btn_about_close;

  if (win_about) return;

  win_about = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_widget_realize(win_about);
  gtk_window_set_title (GTK_WINDOW (win_about), "About");
  gtk_signal_connect(GTK_OBJECT(win_about), "destroy", 
		     GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		     &win_about);

  vb_main = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (win_about), vb_main);
  gtk_widget_show (vb_main);

  frm = gtk_frame_new(THIS_IS);
  gtk_box_pack_start (GTK_BOX (vb_main), frm, TRUE, TRUE, 0);
  gtk_widget_set_usize (frm, ABOUT_WIDTH - ABOUT_WIDTH * 2, ABOUT_HEIGHT - ABOUT_MARGIN * 2);
  gtk_container_set_border_width (GTK_CONTAINER (frm), ABOUT_MARGIN);
  gtk_widget_show (frm);

  lbl_author = gtk_label_new ("plugin for XMMS\n"
			      "made by Joakim Elofsson\n"
			      "joakim.elofsson@home.se\n"
			      "   http://hem.passagen.se/joakime/index.html   ");
  gtk_container_add (GTK_CONTAINER (frm), lbl_author);
  gtk_widget_show (lbl_author);

  btn_about_close = gtk_button_new_with_label ("Close");
  gtk_box_pack_start (GTK_BOX (vb_main), btn_about_close, FALSE, FALSE, 0);
  gtk_widget_show (btn_about_close);

  gtk_signal_connect (GTK_OBJECT (btn_about_close), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_about_close_clicked),
                      GTK_OBJECT(win_about));

  gtk_widget_show (win_about);

}
 
static void dscope_config (void) {
  GtkWidget *vb;
  GtkWidget *nb;
  GtkWidget *frm_type;
  GtkWidget *vbox2;
  GSList *type_group = NULL;
  GtkWidget *rdbtn_dots;
  GtkWidget *rdbtn_line;
  GtkWidget *rdbtn_filled;
  GtkWidget *rdbtn_filled2;
  
  GtkWidget *nblbl_type;
  GtkWidget *vb_misc;
  GtkWidget *frm_win;
  GtkWidget *vbox3;

  GtkWidget *frm_theme;
  GtkWidget *hb_theme;
  GtkWidget *btn_theme;
  GtkWidget *nblbl_misc;
  GtkWidget *btn_close;

  if (win_conf) return;

  if (Cfg.skin_xpm == NULL) /* if config never read */
    dscope_config_read ();  

  win_conf = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_wmclass(GTK_WINDOW(win_conf), "XMMS_Player", "DualScope");
  
  gtk_object_set_data (GTK_OBJECT (win_conf), "win_conf", win_conf);
  gtk_window_set_title (GTK_WINDOW (win_conf), "Config - " THIS_IS);

  vb = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb);
  gtk_container_add (GTK_CONTAINER (win_conf), vb);

  nb = gtk_notebook_new ();
  gtk_widget_show (nb);
  gtk_box_pack_start (GTK_BOX (vb), nb, TRUE, TRUE, 0);

  frm_type = gtk_frame_new ("Type");
  gtk_widget_show (frm_type);
  gtk_container_add (GTK_CONTAINER (nb), frm_type);

  vbox2 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox2);
  gtk_container_add (GTK_CONTAINER (frm_type), vbox2);

  rdbtn_dots = gtk_radio_button_new_with_label (type_group, "dots");
  type_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_dots));
  gtk_widget_show (rdbtn_dots);
  gtk_box_pack_start (GTK_BOX (vbox2), rdbtn_dots, FALSE, FALSE, 0);

  rdbtn_line = gtk_radio_button_new_with_label (type_group, "line");
  type_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_line));
  gtk_widget_show (rdbtn_line);
  gtk_box_pack_start (GTK_BOX (vbox2), rdbtn_line, FALSE, FALSE, 0);

  rdbtn_filled = gtk_radio_button_new_with_label (type_group, "filled");
  type_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_filled));
  gtk_widget_show (rdbtn_filled);
  gtk_box_pack_start (GTK_BOX (vbox2), rdbtn_filled, FALSE, FALSE, 0);

  rdbtn_filled2 = gtk_radio_button_new_with_label (type_group, "filled2");
  type_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_filled2));
  gtk_widget_show (rdbtn_filled2);
  gtk_box_pack_start (GTK_BOX (vbox2), rdbtn_filled2, FALSE, FALSE, 0);

  if ( Cfg.type == 3 )
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_filled2, TRUE);
  else if ( Cfg.type == 2 )
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_filled, TRUE);
  else if (Cfg.type == 1)
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_line, TRUE);
  else
    gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_dots, TRUE);

  nblbl_type = gtk_label_new ("Type");
  gtk_widget_show (nblbl_type);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb), 
	gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb), 0), nblbl_type);
  
  vb_misc = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_misc);
  gtk_container_add (GTK_CONTAINER (nb), vb_misc);

  frm_win = gtk_frame_new ("Window");
  gtk_widget_show (frm_win);
  gtk_box_pack_start (GTK_BOX (vb_misc), frm_win, FALSE, FALSE, 0);

  vbox3 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox3);
  gtk_container_add (GTK_CONTAINER (frm_win), vbox3);

  btn_snapmainwin = gtk_button_new_with_label ("Snap below XMMS mainwindow");
  gtk_widget_show (btn_snapmainwin);
  gtk_box_pack_start (GTK_BOX (vbox3), btn_snapmainwin, FALSE, FALSE, 0);

  ckbtn_rcoords = gtk_check_button_new_with_label ("Remember possiton");
  gtk_toggle_button_set_active((GtkToggleButton *) ckbtn_rcoords, (Cfg.pos_x!=-1)?TRUE:FALSE);
  gtk_widget_show (ckbtn_rcoords);
  gtk_box_pack_start (GTK_BOX (vbox3), ckbtn_rcoords, FALSE, FALSE, 0);

  frm_theme = gtk_frame_new ("Theme");
  gtk_widget_show (frm_theme);
  gtk_box_pack_start (GTK_BOX (vb_misc), frm_theme, TRUE, TRUE, 0);

  hb_theme = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hb_theme);
  gtk_container_add (GTK_CONTAINER (frm_theme), hb_theme);

  etry_theme = gtk_entry_new ();
  gtk_widget_show (etry_theme);
  gtk_box_pack_start (GTK_BOX (hb_theme), etry_theme, TRUE, TRUE, 0);
  gtk_entry_set_editable (GTK_ENTRY (etry_theme), TRUE);
  gtk_entry_set_text((GtkEntry *) etry_theme, Cfg.skin_xpm);

  btn_theme = gtk_button_new_with_label ("Choose Theme");
  gtk_widget_show (btn_theme);
  gtk_box_pack_start (GTK_BOX (hb_theme), btn_theme, FALSE, FALSE, 0);

  nblbl_misc = gtk_label_new ("Misc");
  gtk_widget_show (nblbl_misc);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb), gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb), 1), nblbl_misc);

  btn_close = gtk_button_new_with_label ("Close");
  gtk_widget_show (btn_close);
  gtk_box_pack_start (GTK_BOX (vb), btn_close, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (rdbtn_dots), "clicked",
                      GTK_SIGNAL_FUNC (on_rdbtn_type_toggled), (gpointer) TYPE_DOT);
  gtk_signal_connect (GTK_OBJECT (rdbtn_line), "clicked",
                      GTK_SIGNAL_FUNC (on_rdbtn_type_toggled), (gpointer) TYPE_LINE);
  gtk_signal_connect (GTK_OBJECT (rdbtn_filled), "clicked",
                      GTK_SIGNAL_FUNC (on_rdbtn_type_toggled), (gpointer) TYPE_FILLED);
  gtk_signal_connect (GTK_OBJECT (rdbtn_filled2), "clicked",
                      GTK_SIGNAL_FUNC (on_rdbtn_type_toggled), (gpointer) TYPE_FILLED2);
  gtk_signal_connect (GTK_OBJECT (btn_close), "clicked",
                      GTK_SIGNAL_FUNC (on_confbtn_close_clicked), NULL);
  gtk_signal_connect (GTK_OBJECT (ckbtn_rcoords), "toggled",
                      GTK_SIGNAL_FUNC (on_ckbtn_rcoords_toggled), NULL);
  gtk_signal_connect (GTK_OBJECT (btn_snapmainwin), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_snapmainwin_clicked), (gpointer) ckbtn_rcoords);
  gtk_signal_connect (GTK_OBJECT (btn_theme), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_theme_clicked), NULL);
  gtk_signal_connect (GTK_OBJECT (etry_theme), "changed",
                      GTK_SIGNAL_FUNC (on_etry_theme_changed), NULL);
  gtk_widget_show(win_conf);
}

void create_fileselection (void) {
  GtkWidget *btn_fsel_cancel;
  GtkWidget *btn_fsel_ok;
  gchar *themefile = NULL;

  fsel = gtk_file_selection_new ("Välj fil");
  gtk_object_set_data (GTK_OBJECT (fsel), "fsel", fsel);
  gtk_container_set_border_width (GTK_CONTAINER (fsel), 5);

  btn_fsel_ok = GTK_FILE_SELECTION (fsel)->ok_button;
  gtk_object_set_data (GTK_OBJECT (fsel), "btn_fsel_ok", btn_fsel_ok);
  gtk_widget_show (btn_fsel_ok);
  GTK_WIDGET_SET_FLAGS (btn_fsel_ok, GTK_CAN_DEFAULT);

  btn_fsel_cancel = GTK_FILE_SELECTION (fsel)->cancel_button;
  gtk_object_set_data (GTK_OBJECT (fsel), 
		       "btn_fsel_cancel", btn_fsel_cancel);
  gtk_widget_show (btn_fsel_cancel);
  GTK_WIDGET_SET_FLAGS (btn_fsel_cancel, GTK_CAN_DEFAULT);
    
#ifndef FSEL_ALWAYS_DEFAULT_PATH
  themefile = Cfg.skin_xpm;
  if (!themefile && (strcmp(Cfg.skin_xpm, THEME_DEFAULT_STR) == 0))
    themefile = (THEME_DEFAULT_PATH);
#else
  themefile = (THEME_DEFAULT_PATH);
#endif

  gtk_file_selection_set_filename((GtkFileSelection *) fsel, themefile);

  gtk_signal_connect (GTK_OBJECT (btn_fsel_cancel), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_fsel_cancel_clicked),
		      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_fsel_ok), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_fsel_ok_clicked),
		      NULL);
}

void on_item_close_activate(GtkMenuItem *menuitem, gpointer data)
{
  dscope_vp.disable_plugin(&dscope_vp);
}

void on_item_about_activate(GtkMenuItem *menuitem, gpointer data)
{
  dscope_about();
}

void on_item_conf_activate(GtkMenuItem *menuitem, gpointer data)
{
  dscope_config();
}

GtkWidget* dscope_create_menu(void)
{
  GtkWidget *menu;
  GtkAccelGroup *m_acc;
  
  GtkWidget *sep;
  GtkWidget *item_close;
  GtkWidget *item_about;
  GtkWidget *item_conf;

  menu = gtk_menu_new();
  m_acc = gtk_menu_ensure_uline_accel_group(GTK_MENU(menu));

  item_about = gtk_menu_item_new_with_label("About " THIS_IS);
  gtk_widget_show(item_about);
  gtk_container_add (GTK_CONTAINER(menu), item_about);

  sep = gtk_menu_item_new ();
  gtk_widget_show(sep);
  gtk_container_add (GTK_CONTAINER(menu), sep);
  gtk_widget_set_sensitive(sep, FALSE);

  item_conf = gtk_menu_item_new_with_label("Config");
  gtk_widget_show(item_conf);
  gtk_container_add (GTK_CONTAINER(menu), item_conf);

  item_close = gtk_menu_item_new_with_label("Close");
  gtk_widget_show(item_close);
  gtk_container_add (GTK_CONTAINER(menu), item_close);

  gtk_signal_connect(GTK_OBJECT(item_close), "activate",
		     GTK_SIGNAL_FUNC(on_item_close_activate), NULL);

  gtk_signal_connect(GTK_OBJECT(item_about), "activate",
		     GTK_SIGNAL_FUNC(on_item_about_activate), NULL);

  gtk_signal_connect(GTK_OBJECT(item_conf), "activate",
		     GTK_SIGNAL_FUNC(on_item_conf_activate), NULL);

  return menu;
}
