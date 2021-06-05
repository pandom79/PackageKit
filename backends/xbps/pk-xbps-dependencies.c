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


static xbps_array_t
pk_xbps_get_fulldepend(PkBackendXbpsPrivate *priv, xbps_array_t str_arr)
{
    xbps_array_t dict_arr = NULL;
    xbps_dictionary_t dict_repo, dict_local;
    const gchar *pkgver_pat, *pkgver_repo,*pkgver_local;
    gsize len, i;


    g_assert (priv);
    /* Initialize */
    pkgver_pat = pkgver_repo = pkgver_local = NULL;
    dict_repo = dict_local = NULL;
    dict_arr = xbps_array_create();
    g_assert (dict_arr);
    i = 0;
    len = (str_arr != NULL ? xbps_array_count (str_arr) : 0);

    for (; i < len; i++) {
        /* Get package pattern */
        xbps_array_get_cstring_nocopy (str_arr, i, &pkgver_pat);

        /* Get local package */
        dict_local = xbps_pkgdb_get_pkg (priv->xhp, pkgver_pat);
        if (dict_local != NULL)
            xbps_dictionary_get_cstring_nocopy (dict_local, "pkgver", &pkgver_local);

        if (pkgver_local != NULL && g_strcmp0 (pkgver_local, pkgver_pat) == 0)
            xbps_array_add (dict_arr, dict_local);
        else {
            /* Get remote pkg */
            dict_repo = xbps_rpool_get_pkg (priv->xhp, pkgver_pat);
            if (dict_repo == NULL)
                dict_repo = xbps_rpool_get_virtualpkg (priv->xhp, pkgver_pat);

            if (dict_repo != NULL)
                xbps_dictionary_get_cstring_nocopy (dict_repo, "pkgver", &pkgver_repo);

            if (pkgver_repo != NULL && g_strcmp0 (pkgver_repo, pkgver_pat) == 0)
                xbps_array_add (dict_arr, dict_repo);
        }
    }

    return dict_arr;
}

static void
pk_backend_depends_on_thread (PkBackendJob *job, GVariant *params, gpointer user_data UNUSED)
{
    PkBackendXbpsPrivate *priv = NULL;
    PkBackend *backend = NULL;
    PkBitfield filters;
    g_autofree gchar **package_ids = NULL;
    gchar **split_package_id = NULL;
    const gchar *pkgname, *version, *state, *reverse;
    g_autofree gchar *pkgver = NULL;
    gboolean recursive, installed;
    gsize i, j, rv;
    gfloat lenpkg_ids, lendeps, step, perc;
    g_autofree xbps_array_t rdeps, rdeps_dict;
    xbps_dictionary_t depend = NULL;


    g_assert (job);
    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    reverse = pk_backend_job_get_user_data (job);
    pkgname = state = NULL;
    rdeps = rdeps_dict = NULL;
    i = j = lenpkg_ids = lendeps = step = perc = rv = 0;
    depend = NULL;

    /* Get parameters */
    g_variant_get (params, "(t^a&sb)", &filters, &package_ids, &recursive);

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    lenpkg_ids = g_strv_length (package_ids);
    if (lenpkg_ids > 0) {

        /* Refresh data from db */
        if (priv->xhp->pkgdb != NULL) {
            g_free (priv->xhp->pkgdb);
            priv->xhp->pkgdb = NULL;
        }
        priv->xhp->pkgdb = xbps_dictionary_internalize_from_file (priv->xhp->pkgdb_plist);

        /* Pkgname mapping */
        if ((rv = pk_xbps_map_pkgname (priv->xhp)) == 0) {

            for (; i < lenpkg_ids; i++) {
                /* Reset "installed" */
                installed = FALSE;

                /* Split package id */
                split_package_id = pk_package_id_split (package_ids[i]);
                if (g_strv_length (split_package_id) > 0) {
                    /* Get parameters */
                    pkgname = split_package_id[0];
                    version = split_package_id[1];
                    state = split_package_id[3];

                    /* Building pkgver */
                    pkgver = g_strconcat (pkgname, "-", version, NULL);

                    if (state != NULL && g_strcmp0 (state, "installed") == 0)
                        installed = TRUE;

                    if (reverse == NULL || g_strcmp0 (reverse, "") == 0) {
                        /* Get the complete dependencies tree */
                        if (!installed)
                            rdeps = xbps_rpool_get_pkg_fulldeptree (priv->xhp, pkgname);
                        else
                            rdeps = xbps_pkgdb_get_pkg_fulldeptree (priv->xhp, pkgname);
                    }
                    else {
                        /* Get reverse dependencies */
                        if (!installed)
                            rdeps = xbps_rpool_get_pkg_revdeps (priv->xhp, pkgname);
                        else
                            rdeps = xbps_pkgdb_get_pkg_revdeps (priv->xhp, pkgname);
                    }
                    /*
                     * Convert array of strings (pkgver) to
                     * array of dictionaries
                     */
                    if (rdeps != NULL)
                        rdeps_dict = pk_xbps_get_fulldepend (priv, rdeps);

                    /* It will show the local or remote dependencies according its state */
                    lendeps = (rdeps_dict != NULL ? xbps_array_count (rdeps_dict) :0);
                    step = 95 / lendeps;
                    for (j = 0; j < lendeps; j++) {
                        depend = xbps_array_get (rdeps_dict, j);
                        pk_xbps_show_data (priv, job, depend, filters);
                        perc += step;
                        pk_backend_job_set_percentage (job, perc);

                        /* Release resources */
                        xbps_object_release (depend);
                        depend = NULL;
                    }

                    /* Release resources */
                    lendeps = (rdeps != NULL ? xbps_array_count (rdeps) : 0);
                    for (j = 0; j < lendeps; j++) {
                        depend = xbps_array_get (rdeps, j);
                        xbps_object_release (depend);
                        depend = NULL;
                    }
                }

                /* Release resources */
                if (split_package_id != NULL) {
                    g_strfreev (split_package_id);
                    split_package_id = NULL;
                }
            }
        }
        else {
            pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_CORRUPT, "%s", strerror (rv));
            syslog (LOG_DAEMON | LOG_ERR, "PackageKit: pk_backend_depends_on_thread() error : "
                    "Unable mapping pkgname property into database");
        }
    }

    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void pk_backend_depends_on(PkBackend *backend, PkBackendJob *job, PkBitfield filters,
                           gchar **package_ids, gboolean recursive)
{
    pk_backend_job_thread_create (job, pk_backend_depends_on_thread, NULL, NULL);
}


void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters,
                        gchar **package_ids, gboolean recursive)
{
    /* To show reverse dependencies we can use the depends-on thread
     * passing the reverse string as argument
    */
    const gchar *reverse = "reverse";
    pk_backend_job_set_user_data(job, (gpointer)reverse);
    pk_backend_job_thread_create (job, pk_backend_depends_on_thread, NULL, NULL);
}

