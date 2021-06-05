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

static gsize
pk_xbps_iter_size (xbps_object_iterator_t iter)
{
    gsize len = 0;

    if (iter != NULL) {
        while (xbps_object_iterator_next(iter) != NULL)
            len++;

        /* Reset iter */
        xbps_object_iterator_reset (iter);
    }

    return len;
}

static int
dist_upgrade (struct xbps_handle *xhp, bool drun)
{
    int rv = 0;

    rv = xbps_transaction_update_packages (xhp);
    if (rv == ENOENT) {
        syslog (LOG_DAEMON | LOG_WARNING, "No packages currently registered.\n");
        return 0;
    }
    else if (rv == EBUSY) {
        if (drun)
            rv = 0;
        else {
            syslog (LOG_DAEMON | LOG_WARNING,
                    "The 'xbps' package must be updated, please run `xbps-install -u xbps`\n");
            return rv;
        }
    }
    else if (rv == EEXIST)
        return 0;
    else if (rv == ENOTSUP) {
        syslog (LOG_DAEMON | LOG_ERR, "No repositories currently registered!\n");
        return rv;
    }
    else if (rv != 0) {
        syslog (LOG_DAEMON | LOG_ERR, "Unexpected error %s\n", strerror (rv));
        return -1;
    }

    return exec_transaction (xhp, drun);
}

static void
pk_backend_get_updates_thread (PkBackendJob *job, GVariant *params UNUSED,
                               gpointer user_data UNUSED)
{
    PkBackendXbpsPrivate *priv = NULL;
    PkBackend *backend = NULL;
    xbps_dictionary_t obj = NULL;
    xbps_object_iterator_t iter = NULL;
    g_autofree gchar *package_id, *comp_desc;
    const char *desc = NULL;
    gfloat len, perc, step;
    gsize rv = 0;
    PkBitfield transaction_flags;
    gboolean simulate, exec_upgrade;


    g_assert (job);
    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    package_id = comp_desc = NULL;
    len = perc = step = 0;
    simulate = exec_upgrade = FALSE;
    transaction_flags = pk_backend_job_get_transaction_flags (job);

    /* Get simulate parameter from transaction_flags */
    simulate = (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE));

    /* Start job */
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_percentage (job, 0);

    /* Set job status */
    if (transaction_flags == 0 && !simulate) {
        /* Get updates */
        pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    }
    else if (transaction_flags != 0 && simulate) {
        /* Upgrade system (simulate) */
        pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
    }
    else if (transaction_flags != 0 && !simulate) {
        exec_upgrade = TRUE;
        /* Upgrade system */
        pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);
    }

    if ((rv = xbps_rpool_sync (priv->xhp, NULL)) == 0 &&
        (rv = xbps_rpool_foreach (priv->xhp, repo_import_key_cb, NULL)) == 0)
    {

        /* If it comes from 'get-updates' or 'upgrade-system' (simulate) */
        if (!exec_upgrade && (rv = dist_upgrade (priv->xhp, true)) == 0) {

            if (priv->xhp->transd != NULL )
                iter = xbps_array_iter_from_dict (priv->xhp->transd, "packages");

            len = pk_xbps_iter_size (iter);
            if (len > 0) {
                /* Calculate step and percentage */
                step = 90 / len;
                while ((obj = xbps_object_iterator_next (iter)) != NULL) {
                    /* Get description */
                    xbps_dictionary_get_cstring_nocopy (obj, "short_desc", &desc);
                    /* Remove eventual escape characters from description */
                    comp_desc = g_strcompress (desc);
                    /* Building package_id */
                    package_id = pk_xbps_compose_pkgid (priv, obj, TRUE);
                    /* Show data */
                    pk_backend_job_package (job, PK_INFO_ENUM_AVAILABLE, package_id, comp_desc);
                    perc += step;
                    pk_backend_job_set_percentage (job, perc);
                }

                /* Release iter */
                xbps_object_iterator_release (iter);
                iter = NULL;
            }
        }
        /* If it comes from 'upgrade-system' (not simulate) */
        else if (exec_upgrade) {
            pk_backend_job_set_percentage (job, 50);
            if ((rv = xbps_pkgdb_lock (priv->xhp)) == 0) {
                rv = dist_upgrade (priv->xhp, false);
                xbps_pkgdb_unlock (priv->xhp);
            }
            if (rv == 0)
                syslog (LOG_DAEMON | LOG_DEBUG, "PackageKit: Xbps system upgrade executed successfully");
        }

        /* Release transd */
        if (priv->xhp->transd != NULL ) {
            xbps_object_release (priv->xhp->transd);
            priv->xhp->transd = NULL;
        }

    }

    /* Show eventual errors */
    if (rv != 0)
        pk_xbps_set_transaction_err (job, rv, "pk_backend_get_updates_thread()");

    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    pk_backend_job_thread_create (job, pk_backend_get_updates_thread, NULL, NULL);
}

static void
pk_backend_get_update_detail_thread (PkBackendJob *job, GVariant *params UNUSED,
                               gpointer user_data UNUSED)
{
    PkBackendXbpsPrivate *priv = NULL;
    PkBackend *backend = NULL;
    gchar **pkgids_upgrade = NULL;
    gsize i;
    gfloat lenpkgs_ids, step, perc;
    xbps_dictionary_t dic_upgrade, dic_obsolete;

    g_assert (job);
    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    pkgids_upgrade = pk_backend_job_get_user_data (job);
    lenpkgs_ids = i = step = perc = 0;
    dic_upgrade = dic_obsolete = NULL;

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    lenpkgs_ids = (pkgids_upgrade != NULL ? g_strv_length (pkgids_upgrade) : 0);
    if (lenpkgs_ids > 0) {
        step = 100 / lenpkgs_ids;
        for (i = 0; i < lenpkgs_ids; i++) {
            gchar **split_package_id, **pkgid_obsolete, **vendor_url;
            const gchar *pkgname, *desc, *changelog, *repourl;
            split_package_id = pkgid_obsolete = vendor_url = NULL;
            pkgname = desc = changelog = repourl = NULL;

            /* Splitting package id */
            split_package_id = pk_package_id_split (pkgids_upgrade[i]);
            /* Get pkgname */
            pkgname = split_package_id[0];

            /* Get upgrade package */
            dic_upgrade = xbps_rpool_get_pkg (priv->xhp, pkgname);
            if (dic_upgrade == NULL)
                dic_upgrade = xbps_rpool_get_virtualpkg (priv->xhp, pkgname);

            if (dic_upgrade != NULL) {
                /* Get obsolete package */
                dic_obsolete = xbps_pkgdb_get_pkg (priv->xhp, pkgname);
                if (dic_obsolete != NULL) {
                    /* Setting the obsolete package id */
                    pkgid_obsolete = g_new0 (gchar *, 2);
                    g_assert (pkgid_obsolete);
                    pkgid_obsolete[0] = pk_xbps_compose_pkgid (priv, dic_obsolete, FALSE);
                    pkgid_obsolete[1] = NULL;
                }

                /*Get data from remote package */
                xbps_dictionary_get_cstring_nocopy (dic_upgrade, "repository", &repourl);
                xbps_dictionary_get_cstring_nocopy (dic_upgrade, "short_desc", &desc);
                xbps_dictionary_get_cstring_nocopy (dic_upgrade, "changelog", &changelog);

                /* Setting vendor url */
                vendor_url = g_new0 (gchar *, 2);
                g_assert (vendor_url);
                vendor_url[0] = (gchar *)repourl;
                vendor_url[1] = NULL;

                /* Set the job detail */
                pk_backend_job_update_detail ( job, pkgids_upgrade[i], pkgid_obsolete,
                            NULL, vendor_url, NULL, NULL, PK_RESTART_ENUM_NONE,
                            "Update to a new upstream version", changelog,
                            PK_UPDATE_STATE_ENUM_STABLE, NULL, NULL);

                /* Release resources */
                g_strfreev (split_package_id);
                split_package_id = NULL;
                g_strfreev (pkgid_obsolete);
                pkgid_obsolete = NULL;
                g_strfreev (vendor_url);
                vendor_url = NULL;

                perc += step;
                pk_backend_job_set_percentage (job, perc);
            }
        }
    }

    pk_backend_job_finished (job);
}

void
pk_backend_get_update_detail (PkBackend * backend UNUSED, PkBackendJob *job,
                              gchar **package_ids)
{
    pk_backend_job_set_user_data (job, package_ids);
    pk_backend_job_thread_create (job, pk_backend_get_update_detail_thread, NULL, NULL);
}

void
pk_backend_upgrade_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags,
                           const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
    pk_backend_job_thread_create (job, pk_backend_get_updates_thread, NULL, NULL);
}

