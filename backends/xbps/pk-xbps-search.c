/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2020 Domenico Panella <pandom79@gmail.com>
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
#include <ctype.h>


static char *
strcasestr (const char *s, const char *find)
{
    char c, sc;
    size_t len;

    g_assert(s != NULL);
    g_assert(find != NULL);

    if ((c = *find++) != 0) {
        c = tolower((unsigned char)c);
        len = strlen(find);
        do {
            do {
                if ((sc = *s++) == 0)
                    return (NULL);
            } while ((char)tolower((unsigned char)sc) != c);
        } while (strncasecmp(s, find, len) != 0);
        s--;
    }
    return __UNCONST(s);
}

static int
pk_xbps_search_array_cb (struct xbps_handle *xhp UNUSED,
                         xbps_object_t obj,
                         const char *key UNUSED,
                         void *arg,
                         bool *done UNUSED)
{
    struct search_data *sd = arg;
    const char *pkgver, *version, *desc, *arch;
    bool vpkgfound, found;


    g_assert (obj);
    /* Initialize */
    pkgver = version = desc = arch = NULL;
    vpkgfound = found = false;

    if (!xbps_dictionary_get_cstring_nocopy (obj, "pkgver", &pkgver))
        return 0;

    xbps_dictionary_get_cstring_nocopy (obj, "short_desc", &desc);
    if (xbps_match_virtual_pkg_in_dict (obj, sd->pat))
        vpkgfound = true;
    else {
        if ((strcasestr (pkgver, sd->pat)) || (strcasestr (desc, sd->pat)) ||
            (xbps_pkgpattern_match (pkgver, sd->pat)) || vpkgfound)
            found = true;
    }
    if (vpkgfound || found)
        xbps_array_add (sd->results, obj);

    return 0;
}

static int
pk_xbps_search_repo_cb (struct xbps_repo *repo, void *arg, bool *done UNUSED)
{
    xbps_array_t allkeys;
    struct search_data *sd = arg;
    int rv;

    if (repo->idx == NULL)
        return 0;

    sd->repourl = repo->uri;
    allkeys = xbps_dictionary_all_keys (repo->idx);
    rv = xbps_array_foreach_cb (repo->xhp, allkeys, repo->idx, pk_xbps_search_array_cb, sd);
    xbps_object_release (allkeys);

    return rv;
}

struct search_data *
pk_xbps_search_pkgpat (PkBackendXbpsPrivate *priv, gchar *pkgpattern, gboolean repomode)
{
    struct search_data *sd = NULL;
    gsize rv = 0;


    /* Initialize */
    sd = g_new0 (struct search_data, 1);
    g_assert (sd);
    sd->pat = pkgpattern;
    sd->results = xbps_array_create ();
    g_assert (sd->results);

    /* Search packages */
    if (repomode) {
        rv = xbps_rpool_foreach (priv->xhp, pk_xbps_search_repo_cb, sd);
        if (rv != 0 && rv != ENOTSUP)
            syslog (LOG_DAEMON | LOG_ERR, "Failed to initialize rpool: %s\n", strerror (rv));
    }
    else {
        rv = xbps_pkgdb_foreach_cb (priv->xhp, pk_xbps_search_array_cb, sd);
        if (rv != 0)
            syslog (LOG_DAEMON | LOG_ERR, "Failed to initialize pkgdb: %s\n", strerror (rv));
    }

    if (rv != 0) {
        /* Release resources */
        if (sd->results != NULL) {
            xbps_object_release (sd->results);
            sd->results = NULL;
        }
        if (sd != NULL) {
            g_free (sd);
            sd = NULL;
        }
    }

    return sd;
}

static void
pk_backend_search_names_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
    g_autofree gchar **search = NULL;
    PkBitfield filters;

    /* Get params */
    g_variant_get (params, "(t^a&s)", &filters, &search);
    g_assert (search);
    /* Set search pattern of package to "user_data" */
    user_data = *search;

    pk_backend_xbps_packages_thread (job, params, user_data);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters,
                         gchar **values)
{
    pk_backend_job_thread_create (job, pk_backend_search_names_thread, NULL, NULL);
}


