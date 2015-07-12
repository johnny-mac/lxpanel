/**
 * Copyright (c) 2012-2015 Piotr Sipika; see the AUTHORS file for more.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * See the COPYRIGHT file for more information.
 */

#include "location.h"
#include "weatherwidget.h"
#include "yahooutil.h"
#include "logutil.h"

#include "plugin.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>

/* Need to maintain count for bookkeeping */
static gint g_instancecnt = 0;

typedef struct
{
  gint               myid_;
  GtkWidget        * weather_;
  config_setting_t * config_;
  LXPanel          * panel_;
} WeatherPluginPrivate;


/**
 * Weather Plugin destructor.
 *
 * @param data Pointer to the plugin data (private).
 */
static void
weather_destructor(gpointer data)
{
  WeatherPluginPrivate * priv = (WeatherPluginPrivate *) data;

  LXW_LOG(LXW_DEBUG,
          "weather_destructor(%d): %d",
          priv->myid_,
          g_instancecnt);

  g_free(priv);

  --g_instancecnt;

  if (g_instancecnt == 0) {
    yahooutil_cleanup();
    logutil_cleanup();
  }
}

/**
 * Weather Plugin constructor
 *
 * @param panel  Pointer to the panel where this instance is being loaded.
 * @param config Pointer to the configuration settings for this plugin.
 *
 * @return Pointer to a new weather widget.
 */
static GtkWidget *
weather_constructor(LXPanel *panel, config_setting_t *config)
{
  WeatherPluginPrivate * priv = g_new0(WeatherPluginPrivate, 1);

  priv->config_ = config;
  priv->panel_  = panel;

  /* There is one more now... */
  ++g_instancecnt;

  priv->myid_ = g_instancecnt;

  if (g_instancecnt == 1) {
    logutil_init("syslog");
      
    logutil_max_loglevel_set(LXW_ERROR);

    yahooutil_init();
  }

  LXW_LOG(LXW_DEBUG, "weather_constructor()");
  
  GtkWidget * weather = gtk_weather_new();

  priv->weather_ = weather;

  GtkWidget * eventbox = gtk_event_box_new();

  lxpanel_plugin_set_data(eventbox, priv, weather_destructor);
  gtk_container_add(GTK_CONTAINER(eventbox), weather);

  gtk_widget_set_has_window(eventbox, FALSE);

  gtk_widget_show_all(eventbox);

  /* use config settings */
  LocationInfo * location = g_new0(LocationInfo, 1);
  const char   * dummystr = NULL;
  int            dummyint = 0;

  if (config_setting_lookup_string(config, "alias", &dummystr)) {
    location->alias_ = g_strndup(dummystr, (dummystr) ? strlen(dummystr) : 0);
  } else if (config_setting_lookup_int(config, "alias", &dummyint)) {
    location->alias_ = g_strdup_printf("%d", dummyint);
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup alias in config.");
  }

  if (config_setting_lookup_string(config, "city", &dummystr)) {
    location->city_ = g_strndup(dummystr, (dummystr) ? strlen(dummystr) : 0);
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup city in config.");
  }

  if (config_setting_lookup_string(config, "state", &dummystr)) {
    location->state_ = g_strndup(dummystr, (dummystr) ? strlen(dummystr) : 0);
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup state in config.");
  }

  if (config_setting_lookup_string(config, "country", &dummystr)) {
    location->country_ = g_strndup(dummystr, (dummystr) ? strlen(dummystr) : 0);
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup country in config.");
  }

  if (config_setting_lookup_string(config, "woeid", &dummystr)) {
    location->woeid_ = g_strndup(dummystr, (dummystr) ? strlen(dummystr) : 0);
  } else if (config_setting_lookup_int(config, "woeid", &dummyint)) {
    location->woeid_ = g_strdup_printf("%d", dummyint);
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup woeid in config.");
  }

  if (config_setting_lookup_string(config, "units", &dummystr)) {
    location->units_ = dummystr[0];
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup units in config.");
  }

  if (config_setting_lookup_int(config, "interval", &dummyint)) {
    location->interval_ = (guint)dummyint;
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup interval in config.");
  }

  dummyint = 0;
  if (config_setting_lookup_int(config, "enabled", &dummyint)) {
    location->enabled_ = (gint)dummyint;
  } else {
    LXW_LOG(LXW_ERROR, "Weather: could not lookup enabled flag in config.");
  }

  if (location->alias_ && location->woeid_) {
    GValue locval = G_VALUE_INIT;

    g_value_init(&locval, G_TYPE_POINTER);

    /* location is copied by the widget */
    g_value_set_pointer(&locval, location);

    g_object_set_property(G_OBJECT(weather), "location", &locval);
  }

  location_free(location);

  return eventbox;
}

/**
 * Weather Plugin callback to save configuration
 *
 * @param widget   Pointer to this widget.
 * @param location Pointer to the current location.
 */
void weather_save_configuration(GtkWidget * widget, LocationInfo * location)
{
  GtkWidget * parent = gtk_widget_get_parent(widget);
  WeatherPluginPrivate * priv = NULL;

  if (parent) {
    priv = (WeatherPluginPrivate *) lxpanel_plugin_get_data(parent);
  }

  if (priv == NULL) {
    LXW_LOG(LXW_ERROR,
            "Weather: weather_save_configuration() for invalid widget");
    return;
  }

  LXW_LOG(LXW_DEBUG, "weather_save_configuration(%d)", priv->myid_);

  if (location) {
    /* save configuration */
    config_group_set_string(priv->config_, "alias",   location->alias_);
    config_group_set_string(priv->config_, "city",    location->city_);
    config_group_set_string(priv->config_, "state",   location->state_);
    config_group_set_string(priv->config_, "country", location->country_);
    config_group_set_string(priv->config_, "woeid",   location->woeid_);

    char units[2] = {0};
    if (snprintf(units, 2, "%c", location->units_) > 0) {
      config_group_set_string(priv->config_, "units", units);
    }

    config_group_set_int(priv->config_, "interval", (int) location->interval_);
    config_group_set_int(priv->config_, "enabled", (int) location->enabled_);
  }

}

/**
 * Sets the temperature text for the label next to the icon.
 *
 * @param widget Pointer to the weather widget.
 * @param label  Pointer to the label widget.
 * @param text   String to put on the label.
 *
 */
void weather_set_label_text(GtkWidget   * widget,
                            GtkWidget   * label,
                            const gchar * text)
{
  GtkWidget * parent = gtk_widget_get_parent(widget);

  WeatherPluginPrivate * priv = NULL;

  if (parent)  {
      priv = (WeatherPluginPrivate *) lxpanel_plugin_get_data(parent);
  }

  if (priv == NULL) {
    LXW_LOG(LXW_ERROR, "Weather: weather_set_label_text() for invalid widget");
    return;
  }

  lxpanel_draw_label_text(priv->panel_, label, text, TRUE, 1, TRUE);
}

/**
 * Weather Plugin configuration change callback.
 *
 * @param panel  Pointer to the panel instance.
 * @param widget Pointer to this widget.
 */
static void
weather_configuration_changed(LXPanel * panel, GtkWidget * widget)
{
  LXW_LOG(LXW_DEBUG, "weather_configuration_changed()");

  if (panel && widget) {
    LXW_LOG(LXW_DEBUG, 
            "   orientation: %s, width: %d, height: %d, icon size: %d\n", 
            (panel_get_orientation(panel) == GTK_ORIENTATION_HORIZONTAL)?"HORIZONTAL":
            (panel_get_orientation(panel) == GTK_ORIENTATION_VERTICAL)?"VERTICAL":"NONE",
            panel_get_width(panel), panel_get_height(panel),
            panel_get_icon_size(panel));
  }
}

/**
 * Weather Plugin configuration dialog callback.
 *
 * @param panel  Pointer to the panel instance.
 * @param widget Pointer to the Plugin widget instance.
 * @param pParent Pointer to the GtkWindow parent.
 *
 * @return Instance of the widget.
 */
static GtkWidget *
weather_configure(LXPanel *panel G_GNUC_UNUSED, GtkWidget *widget)
{
  LXW_LOG(LXW_DEBUG, "weather_configure()");

  WeatherPluginPrivate * priv =
    (WeatherPluginPrivate *) lxpanel_plugin_get_data(widget);

  GtkWidget * dialog =
    gtk_weather_create_preferences_dialog(GTK_WIDGET(priv->weather_));

  return dialog;
}

FM_DEFINE_MODULE(lxpanel_gtk, weather)

/**
 * Definition of the weather plugin module
 */
LXPanelPluginInit fm_module_init_lxpanel_gtk =
{
  .name        = N_("Weather Plugin"),
  .description = N_("Show weather conditions for a location."),

  // API functions
  .new_instance = weather_constructor,
  .config       = weather_configure,
  .reconfigure  = weather_configuration_changed
};
