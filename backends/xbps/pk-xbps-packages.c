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

int
repo_import_key_cb (struct xbps_repo *repo, void *arg UNUSED, bool *done UNUSED)
{
    int rv;

    if ((rv = xbps_repo_key_import(repo)) != 0)
        syslog (LOG_DAEMON | LOG_ERR,
                "Failed to import pubkey from %s: %s\n", repo->uri, strerror(rv));

    return rv;
}

static gchar **
pk_xbps_get_pkg_files (xbps_dictionary_t filesd)
{
    xbps_array_t array, allkeys;
    xbps_object_t obj;
    xbps_dictionary_keysym_t ksym;
    gchar **files = NULL;
    const char *keyname, *file, *target;
    gchar *complete_file = NULL;
    int lenfiles = 0;

    array = allkeys = NULL;
    keyname = file = target = NULL;

    if (xbps_object_type(filesd) != XBPS_TYPE_DICTIONARY)
        return NULL;

    allkeys = xbps_dictionary_all_keys(filesd);
    for (unsigned int i = 0; i < xbps_array_count(allkeys); i++) {
        ksym = xbps_array_get(allkeys, i);
        keyname = xbps_dictionary_keysym_cstring_nocopy(ksym);
        if ((strcmp(keyname, "files") &&
            (strcmp(keyname, "conf_files") &&
            (strcmp(keyname, "links")))))
            continue;

        array = xbps_dictionary_get(filesd, keyname);
        if (array == NULL || xbps_array_count(array) == 0)
            continue;

        for (unsigned int x = 0; x < xbps_array_count(array); x++) {
            obj = xbps_array_get(array, x);
            if (xbps_object_type(obj) != XBPS_TYPE_DICTIONARY)
                continue;
            xbps_dictionary_get_cstring_nocopy(obj, "file", &file);
            complete_file = g_strjoin (NULL, file, NULL);
            if (xbps_dictionary_get_cstring_nocopy(obj, "target", &target)) {
                /* If file has a target, append it */
                g_free (complete_file);
                complete_file = g_strjoin (" -> ", file, target, NULL);
            }
            g_assert (complete_file);

            /* Building the array of the results */
            lenfiles++;
            files = g_realloc(files, lenfiles * sizeof (gchar *));
            g_assert (files);
            files[lenfiles - 1] = complete_file;
            g_assert (files[lenfiles - 1]);
        }
    }
    xbps_object_release (allkeys);

    /* Append NULL if files exists */
    if (files != NULL) {
        lenfiles++;
        files = g_realloc(files, lenfiles * sizeof (gchar *));
        g_assert (files);
        files[lenfiles - 1] = NULL;
    }

    return files;
}

void
pk_xbps_show_data (PkBackendXbpsPrivate *priv, PkBackendJob *job,
                          xbps_dictionary_t data, PkBitfield filter)
{
    g_autofree gchar *packageid = NULL;
    const gchar *desc, *installed;
    g_autofree gchar *comp_desc = NULL;

    g_assert (priv);
    g_assert (job);

    /* Initialize */
    desc = installed = NULL;

    /* Get data from dictionary */
    xbps_dictionary_get_cstring_nocopy (data, "short_desc", &desc);
    xbps_dictionary_get_cstring_nocopy (data, "state", &installed);

    /* Remove eventual escape characters from description */
    comp_desc = g_strcompress (desc);

    /* Building package id */
    packageid = pk_xbps_compose_pkgid (priv, data, FALSE);

    if (!pk_bitfield_contain (filter, PK_FILTER_ENUM_NOT_INSTALLED) &&
        installed != NULL && g_strcmp0 (installed, "installed") == 0) {
        pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED, packageid, comp_desc);
    }
    else {
        pk_backend_job_package (job, PK_INFO_ENUM_AVAILABLE, packageid, comp_desc);
    }
}

gchar *
pk_xbps_compose_pkgid (PkBackendXbpsPrivate *priv, xbps_dictionary_t data,
                       gboolean update)
{
    gchar *packageid = NULL;
    const gchar *pkgver, *version, *arch, *desc, *installed, *repourl;
    g_autofree gchar *repodesc = NULL;
    gchar pkgname[XBPS_NAME_SIZE];


    g_assert (priv);
    g_assert (data);
    /* Initialize */
    pkgver = version = arch = desc = installed = NULL;

    /* Get data from dictionary */
    xbps_dictionary_get_cstring_nocopy (data, "pkgver", &pkgver);
    xbps_dictionary_get_cstring_nocopy (data, "architecture", &arch);
    xbps_dictionary_get_cstring_nocopy (data, "state", &installed);
    xbps_dictionary_get_cstring_nocopy (data, "repository", &repourl);

    /*
     * In update case or 'state' is NULL, we show the repository's
     * description
     */
    if (update || installed == NULL || g_strcmp0 (installed, "") == 0 ||
        g_strcmp0 (installed, "not-installed") == 0 ) {
        g_assert (repourl);
        repodesc = pk_xbps_repodesc_by_url (priv, repourl);
        g_assert (repodesc);
        installed = repodesc;
    }

    /* Get version */
    version = xbps_pkg_version (pkgver);
    /* Get pkgname */
    xbps_pkg_name (pkgname, sizeof (pkgname), pkgver);

    /* Building package id */
    packageid = pk_package_id_build (pkgname, version, arch, installed);

    return packageid;
}

static void
pk_backend_resolve_thread (PkBackendJob *job, GVariant *params UNUSED, gpointer user_data UNUSED)
{
    PkBackend *backend = NULL;
    PkBackendXbpsPrivate *priv = NULL;
    gsize i;
    PkBitfield filters;
    gfloat len, step, perc;
    gchar **search = NULL;
    xbps_dictionary_t pkgd = NULL;
    gchar *pkg_search = NULL;


    g_assert (job);
    /* Initialize */
    i = len = step = perc = 0;
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);


    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    /* Get parameters */
    g_variant_get (params, "(t^a&s)", &filters, &search);

    len = g_strv_length (search);
    if (len > 0) {
        /* Calculate step and percentage for progress bar */
        step = 80 / len;

        /* Refresh data from db */
        if (priv->xhp->pkgdb != NULL) {
            g_free (priv->xhp->pkgdb);
            priv->xhp->pkgdb = NULL;
        }
        priv->xhp->pkgdb = xbps_dictionary_internalize_from_file (priv->xhp->pkgdb_plist);

        for (; i < len; i++) {
            /* Get package to search */
            pkg_search = search[i];

            /* If we are come from "get-update-detail" function, always get repository
             * package
            */
            if (filters == PK_FILTER_ENUM_FREE) {
                /* Get remote package */
                pkgd = xbps_rpool_get_pkg (priv->xhp , pkg_search);
                if (pkgd == NULL)
                    pkgd = xbps_rpool_get_virtualpkg (priv->xhp, pkg_search);

            }
            else {

                /* Get local package */
                pkgd = xbps_pkgdb_get_pkg (priv->xhp, pkg_search);
                if (pkgd == NULL) {
                    /* Get remote package */
                    pkgd = xbps_rpool_get_pkg (priv->xhp , pkg_search);
                    if (pkgd == NULL)
                        pkgd = xbps_rpool_get_virtualpkg (priv->xhp, pkg_search);

                }
            }

            if (pkgd != NULL)
                pk_xbps_show_data (priv, job, pkgd, filters);

            perc += step;
            pk_backend_job_set_percentage (job, perc);
        }

    }

    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
    pk_backend_job_thread_create (job, pk_backend_resolve_thread, NULL, NULL);
}

static void
pk_backend_get_details_thread (PkBackendJob *job, GVariant *params UNUSED, gpointer user_data UNUSED)
{
    PkBackend *backend = NULL;
    PkBackendXbpsPrivate *priv = NULL;
    const gchar *pkgname, *version, *desc, *license, *homepage;
    g_autofree gchar *pkgver, *comp_desc;
    gchar **packageids = NULL;
    gchar **split_package_id = NULL;
    gboolean installed = FALSE;
    gsize installed_size, i;
    gfloat lenpkgs, perc, step;
    xbps_dictionary_t pkgd = NULL;


    g_assert (job);
    /* Initialize */
    packageids = pk_backend_job_get_user_data (job);
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    pkgname = version = desc = license =
    homepage = pkgver = comp_desc = NULL;
    installed_size = lenpkgs = step = perc = i = 0;

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    lenpkgs = g_strv_length (packageids);
    if (lenpkgs > 0) {

        /* Refresh data from db */
        if (priv->xhp->pkgdb != NULL) {
            g_free (priv->xhp->pkgdb);
            priv->xhp->pkgdb = NULL;
        }
        priv->xhp->pkgdb = xbps_dictionary_internalize_from_file (priv->xhp->pkgdb_plist);

        /* Calculate step and percentage for progress bar */
        step = 95 / lenpkgs;
        for (; i < lenpkgs; i++) {
            /* Reset installed variable */
            installed = FALSE;

            /* Split package id */
            split_package_id = pk_package_id_split (packageids[i]);
            if (g_strv_length (split_package_id) > 0) {
                /* Get parameters */
                pkgname = split_package_id[0];
                version = split_package_id[1];
                if (split_package_id[3] != NULL &&
                    g_strcmp0 (split_package_id[3], "installed") == 0)
                    installed = TRUE;

                /* Building pkgver */
                pkgver = g_strconcat (pkgname, "-", version, NULL);
                if (!installed) {
                    /* Get remote package */
                    pkgd = xbps_rpool_get_pkg (priv->xhp, pkgver);
                    if (pkgd == NULL)
                        pkgd = xbps_rpool_get_virtualpkg (priv->xhp, pkgver);
                }
                else {
                    /* Get local package */
                    pkgd = xbps_pkgdb_get_pkg (priv->xhp, pkgver);
                }

                /* Get properties from dictionary */
                xbps_dictionary_get_cstring_nocopy (pkgd, "short_desc", &desc);
                /* Remove eventual escape characters from description */
                comp_desc = g_strcompress (desc);
                xbps_dictionary_get_cstring_nocopy (pkgd, "license", &license);
                xbps_dictionary_get_uint64 (pkgd, "installed_size", &installed_size);
                xbps_dictionary_get_cstring_nocopy (pkgd, "homepage", &homepage);

                pk_backend_job_details (job, packageids[i], NULL, license, PK_GROUP_ENUM_UNKNOWN,
                                        comp_desc, homepage, installed_size);
                perc += step;
                pk_backend_job_set_percentage (job, perc);
            }

            /* Release resources */
            if (split_package_id != NULL) {
                g_strfreev (split_package_id);
                split_package_id = NULL;
            }
        }
    }

    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_get_details (PkBackend *backend UNUSED, PkBackendJob *job, gchar **package_ids)
{
    pk_backend_job_set_user_data (job, package_ids);
    pk_backend_job_thread_create (job, pk_backend_get_details_thread, NULL, NULL);
}

void
pk_backend_xbps_packages_thread (PkBackendJob *job, GVariant *params UNUSED, gpointer user_data)
{

    PkBackendXbpsPrivate *priv = NULL;
    PkBackend *backend = NULL;
    g_autofree struct search_data *pkgs_data = NULL;
    g_autofree xbps_array_t repo_results, results;
    const gchar *pkg_search_pat = "";
    gsize i;
    gfloat len, step, perc;
    xbps_dictionary_t repo_pkgd, local_pkgd, pkgd;
    const char *pkgver_repo, *pkgver_local;
    char pkgname[XBPS_NAME_SIZE];
    gboolean add_remote;


    g_assert (job);
    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    repo_pkgd = local_pkgd = pkgd = NULL;
    repo_results = NULL;
    i = len = step = perc = 0;
    pkgver_repo = pkgver_local = NULL;
    results = xbps_array_create();
    g_assert (results);

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    /*
     * If we are come from "search name function",
     * user_data will contain the search pattern of packages
    */
    if (user_data != NULL)
        pkg_search_pat = (gchar *)user_data;

    /* Refresh data from db */
    if (priv->xhp->pkgdb != NULL) {
        g_free (priv->xhp->pkgdb);
        priv->xhp->pkgdb = NULL;
    }
    priv->xhp->pkgdb = xbps_dictionary_internalize_from_file (priv->xhp->pkgdb_plist);

    /* Search data */
    pkgs_data = pk_xbps_search_pkgpat (priv, (gchar *)pkg_search_pat, TRUE);
    if (pkgs_data != NULL) {
        repo_results = pkgs_data->results;
        len = (repo_results != NULL ? xbps_array_count (repo_results) : 0);
        /* Calculate step and percentage for progress bar */
        step = 48 / len;

        for (; i < len; i++) {
            /* Reset add_remote variable */
            add_remote = FALSE;

            /* Get remote package */
            repo_pkgd = xbps_array_get (repo_results, i);
            /* Get pkgver and pkgname from remote package */
            xbps_dictionary_get_cstring_nocopy (repo_pkgd, "pkgver", &pkgver_repo);
            xbps_pkg_name (pkgname, sizeof (pkgname), pkgver_repo);

            /* Get local pkg */
            local_pkgd = xbps_pkgdb_get_pkg (priv->xhp, pkgname);

            if (local_pkgd != NULL) {
                /* Get pkgver from local package */
                xbps_dictionary_get_cstring_nocopy (local_pkgd, "pkgver", &pkgver_local);
                /* Compare version */
                if (g_strcmp0 (pkgver_repo, pkgver_local) != 0)
                    add_remote = TRUE;

                /* Add local package */
                xbps_array_add (results, local_pkgd);
            }
            else
                add_remote = TRUE;

            if (add_remote) {
                /* Get remote package detail to retrieve the repository url */
                repo_pkgd = xbps_rpool_get_pkg (priv->xhp, pkgver_repo);
                if (repo_pkgd == NULL)
                    repo_pkgd = xbps_rpool_get_virtualpkg (priv->xhp, pkgver_repo);

                /* Add this package at the final list */
                if (repo_pkgd != NULL) {
                    /* Remove the eventual installed state to show repository description
                     * in pk_xbps_show_data
                    */
                    xbps_dictionary_remove (repo_pkgd, "state");
                    xbps_array_add (results, repo_pkgd);
                }
            }

            perc += step;
            pk_backend_job_set_percentage (job, perc);
        }
    }

    /* Show data */
    len = (results != NULL ? xbps_array_count (results) : 0);
    /* Calculate step and percentage for progress bar */
    step = 48 / len;
    for (i = 0; i < len; i++) {
        pkgd = xbps_array_get (results, i);
        pk_xbps_show_data (priv, job, pkgd, PK_FILTER_ENUM_UNKNOWN);
        perc += step;
        pk_backend_job_set_percentage (job, perc);

        /* Release pkgd */
        xbps_object_release (pkgd);
        pkgd = NULL;
    }

    /* Release resources */
    len = (repo_results != NULL ? xbps_array_count (repo_results) : 0);
    for (int i = 0; i < len; i++) {
        repo_pkgd = xbps_array_get (repo_results, i);
        xbps_object_release (repo_pkgd);
        repo_pkgd = NULL;
    }

    /* End job */
    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    pk_backend_job_thread_create (job, pk_backend_xbps_packages_thread, NULL, NULL);
}

static void
pk_backend_get_files_thread (PkBackendJob *job, GVariant *params UNUSED,
                                gpointer user_data UNUSED)
{
    PkBackendXbpsPrivate *priv = NULL;
    PkBackend *backend = NULL;
    gchar **search_ids = NULL;
    gsize i, k, numfiles;
    gfloat len, step , perc;


    g_assert (job);
    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    search_ids = pk_backend_job_get_user_data (job);
    i = len = step = perc = k = numfiles = 0;

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    len = (search_ids != NULL ? g_strv_length (search_ids) : 0);
    if (len > 0) {

        /* Refresh data from db */
        if (priv->xhp->pkgdb != NULL) {
            g_free (priv->xhp->pkgdb);
            priv->xhp->pkgdb = NULL;
        }
        priv->xhp->pkgdb = xbps_dictionary_internalize_from_file (priv->xhp->pkgdb_plist);

        for (; i < len; i++) {
            gchar **split_package_id = NULL;
            gboolean installed = FALSE;
            xbps_dictionary_t dic = NULL;
            gchar **files = NULL;
            gchar *pkgname, *version;
            g_autofree gchar *pkgver = NULL;

            pkgname = version = NULL;

            /* Splitting packageid */
            split_package_id = pk_package_id_split (search_ids[i]);
            pkgname = split_package_id[0];
            version = split_package_id[1];
            /* Building pkgver */
            pkgver = g_strconcat (pkgname, "-", version, NULL);

            /* Check if the package is installed */
            if (split_package_id[3] != NULL &&
                g_strcmp0 (split_package_id[3], "installed") == 0)
                installed = TRUE;

            if (installed)
                /* Local */
                dic = xbps_pkgdb_get_pkg_files (priv->xhp, pkgver);
            else
                /* Remote */
                dic = xbps_rpool_get_pkg_plist (priv->xhp, pkgver, "/files.plist");

            g_assert (dic);

            /* Get files */
            files = pk_xbps_get_pkg_files (dic);

            /* Computing step for progress bar */
            numfiles = g_strv_length(files);
            step = (100 / (gfloat)numfiles) / len;
            for (k = 0; k < numfiles; k++) {
                perc += step;
                pk_backend_job_set_percentage (job, perc);
                g_usleep (1);
            }
            /* Show data */
            pk_backend_job_files (job, search_ids[i], files);

            /* Release resources */
            if (split_package_id != NULL) {
                g_strfreev (split_package_id);
                split_package_id = NULL;
            }

            if (files != NULL) {
                g_strfreev (files);
                files = NULL;
            }

            xbps_object_release (dic);
            dic = NULL;
        }
    }

    /* End job */
    pk_backend_job_finished (job);
}

void
pk_backend_get_files (PkBackend *backend UNUSED, PkBackendJob *job, gchar **package_ids)
{
    pk_backend_job_set_user_data (job, package_ids);
    pk_backend_job_thread_create (job, pk_backend_get_files_thread, NULL, NULL);
}
