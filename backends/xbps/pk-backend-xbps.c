/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2021 Domenico Panella <pandom79@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <pk-backend-xbps.h>

const gchar *
pk_backend_get_description (PkBackend *backend UNUSED)
{
    return "X Binary Package System (XBPS)";
}

const gchar *
pk_backend_get_author (PkBackend *backend UNUSED)
{
    return "Domenico Panella <pandom79@gmail.com>";
}

gchar **
pk_backend_get_mime_types (PkBackend *backend UNUSED)
{
    const gchar *mime_types[] = {
                "application/zstd",
                "application/x-zstd-compressed-tar",
                NULL };
    return g_strdupv ((gchar **) mime_types);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
    return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
                                   PK_FILTER_ENUM_NOT_INSTALLED,
                                   PK_FILTER_ENUM_FREE,
                                   -1);
}

int
pk_backend_xbps_init (PkBackendXbpsPrivate *priv)
{
    int rv = 0;
    struct xbps_handle *xhp;

    g_assert (priv && (priv->xhp == NULL));
    xhp = g_new0 (struct xbps_handle, 1);
    g_assert (xhp);
    priv->xhp = xhp;

    /* Set paths */
    g_assert (strcpy (xhp->sysconfdir, priv->xbps_sysconfdir));
    g_assert (strcpy (xhp->confdir, priv->xbps_confdir));

    /* Uncomment the instruction below to enable xbps debug */
//    xhp->flags |= XBPS_FLAG_DEBUG;

    /* Initialize xbps */
    if ((rv = xbps_init (xhp)) != 0)
        syslog (LOG_DAEMON | LOG_ERR, "Failed to initialize xbps %s", strerror(rv));
    else
        syslog (LOG_DAEMON | LOG_DEBUG, "Xbps init succesfully");

    return rv;
}

void
pk_backend_xbps_end (PkBackendXbpsPrivate *priv)
{
    g_assert (priv);
    g_assert (priv->xhp);
    xbps_end (priv->xhp);
    g_free (priv->xhp);
    priv->xhp = NULL;

    syslog (LOG_DAEMON | LOG_DEBUG, "Xbps end successfully");
}

void
pk_backend_initialize (GKeyFile *conf UNUSED, PkBackend *backend)
{
    PkBackendXbpsPrivate *priv;
    gchar *xbps_sysconfdir, *xbps_confdir, *path_repoconf, *path_repolist;

    xbps_sysconfdir = xbps_confdir = path_repoconf = path_repolist = NULL;

    g_assert (backend);
    /* Set private backend */
    priv = g_new0 (PkBackendXbpsPrivate, 1);
    g_assert (priv);
    pk_backend_set_user_data (backend, priv);

    /* Set xbps system configuration path */
    xbps_sysconfdir = g_build_filename (SYSCONFDIR, "PackageKit", "xbps.d", NULL);
    if (xbps_sysconfdir)
        priv->xbps_sysconfdir = xbps_sysconfdir;
    else {
        syslog (LOG_DAEMON | LOG_ERR, "Unable set xbps_sysconfdir '%s' !", xbps_sysconfdir);
        exit (1);
    }

    /* Set xbps configuration path */
    xbps_confdir = g_build_filename (SYSCONFDIR, "xbps.d", NULL);
    if (xbps_confdir)
        priv->xbps_confdir = xbps_confdir;
    else {
        syslog (LOG_DAEMON | LOG_ERR, "Unable set xbps_confdir '%s' !", xbps_confdir);
        exit (1);
    }

    /* Set repositories configuration file path */
    path_repoconf = g_build_filename (xbps_sysconfdir, "repos.conf", NULL);
    if (path_repoconf)
        priv->path_repoconf = path_repoconf;
    else {
        syslog (LOG_DAEMON | LOG_ERR, "Unable set path_repoconf %s !", path_repoconf);
        exit (1);
    }

    /* Set repositories list file path */
    path_repolist = g_build_filename (SYSCONFDIR, "PackageKit", "repos.list", NULL);
    if (path_repolist)
        priv->path_repolist = path_repolist;
    else {
        syslog (LOG_DAEMON | LOG_ERR, "Unable set path_repolist %s !", path_repolist);
        exit (1);
    }

    if (pk_backend_xbps_init (priv) != 0)
        exit(1);

    syslog (LOG_DAEMON | LOG_DEBUG, "Xbps backend initialized successfully");
}

void
pk_backend_destroy (PkBackend *backend)
{
    PkBackendXbpsPrivate *priv = NULL;

    g_assert (backend);
    priv = pk_backend_get_user_data (backend);

    g_assert (priv);
    g_assert (priv->xbps_sysconfdir);
    g_assert (priv->xbps_confdir);
    g_assert (priv->path_repolist);

    /* Release xbps */
    pk_backend_xbps_end (priv);

    /* Release */
    g_free (priv->path_repolist);
    priv->path_repolist = NULL;

    g_free (priv->xbps_confdir);
    priv->xbps_confdir = NULL;

    g_free (priv->xbps_sysconfdir);
    priv->xbps_sysconfdir = NULL;

    g_free (priv);
    priv = NULL;

    syslog (LOG_DAEMON | LOG_DEBUG, "Xbps backend destroyed successfully");

}
