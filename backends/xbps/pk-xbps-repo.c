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


static gboolean
pk_xbps_repo_exist (struct xbps_handle *xhp, const gchar *repo) {

    xbps_array_t repos = NULL;
    gsize len, i;
    const gchar *repo_2 = NULL;

    g_assert (xhp && repo);
    /* Initialize */
    len = i = 0;
    repos = xhp->repositories;
    g_assert (repos);

    len = (repos != NULL ? xbps_array_count (repos) : 0);
    for (; i < len; i++) {
        xbps_array_get_cstring_nocopy (repos, i, &repo_2);
        if (repo_2 != NULL && g_strcmp0 (repo, repo_2) == 0)
            return TRUE;
    }

    return FALSE;
}

gchar *
pk_xbps_repodesc_by_url (PkBackendXbpsPrivate *priv, const gchar *url)
{
    g_autoptr (GKeyFile) key_repolist = NULL;
    gchar **repolist_groups = NULL;
    gsize length, i;
    gchar *repodesc, *repolist_group;

    /* Initialize */
    length = i = 0;
    repodesc = repolist_group = NULL;
    key_repolist = g_key_file_new ();
    g_assert (key_repolist);

    /* Get groups from file */
    repolist_groups = pk_xbps_list_groups (key_repolist, priv->path_repolist, &length);
    for (; i < length; i++) {
        g_autofree gchar *repourl = NULL;

        repolist_group = repolist_groups[i];
        repourl = g_key_file_get_value (key_repolist, repolist_group, "repository", NULL);
        if (repourl != NULL && g_strcmp0 (repourl, url) == 0) {
            repodesc = g_key_file_get_value (key_repolist, repolist_group, "desc", NULL);
            g_assert (repodesc);
            break;
        }
    }

    /* Release resources */
    if (repolist_groups != NULL) {
        g_strfreev (repolist_groups);
        repolist_groups = NULL;
    }

    return repodesc;
}

gchar **
pk_xbps_list_groups (GKeyFile *key_file, gchar *path, gsize *length)
{
    g_autoptr (GError) err = NULL;
    gchar **groups = NULL;
    gboolean ret = FALSE;

    ret = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &err);
    if (!ret && err != NULL)
        syslog (LOG_DAEMON | LOG_ERR, "Error in pk_xbps_list_groups() : '%s' !", err->message);
    else
        groups = g_key_file_get_groups (key_file, length);

    return groups;
}

static int
pk_xbps_load_file (GKeyFile *key_file, gchar *path)
{
    g_autoptr (GError) err = NULL;
    gboolean ret = FALSE;

    ret = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &err);
    if (!ret && err != NULL) {
        syslog (LOG_DAEMON | LOG_ERR, "Error in pk_xbps_load_file() : '%s' !", err->message);
        return 1;
    }

    return 0;
}

static int
pk_xbps_save_file (GKeyFile *key_file, gchar *path)
{
    g_autoptr (GError) err = NULL;
    gboolean ret = FALSE;

    ret = g_key_file_save_to_file (key_file, path, &err);
    if (!ret && err != NULL) {
        syslog (LOG_DAEMON | LOG_ERR, "Error in pk_xbps_save_file() : '%s'!", err->message);
        return 1;
    }

    return 0;
}

static void
pk_backend_get_repo_list_thread (PkBackendJob *job, GVariant *params UNUSED,
                                 gpointer user_data UNUSED)
{
    PkBackend *backend = NULL;
    PkBackendXbpsPrivate *priv = NULL;
    g_autoptr (GKeyFile) key_repolist = NULL;
    gchar **groups = NULL;
    gchar *group = NULL;
    gfloat step, perc;
    gsize i, length;

    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    step = perc = i = length = 0;

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, 0);

    /* Get repositories list */
    key_repolist = g_key_file_new ();
    g_assert (key_repolist);
    groups = pk_xbps_list_groups (key_repolist, priv->path_repolist, &length);
    if (length > 0) {
        /* Calculate step and percentage for progress bar */
        step = 90 / length;

        for (; i < length; i++) {
            g_autofree gchar *repo, *desc, *enabled;
            repo = desc = enabled = NULL;

            group = groups[i];

            /* Get properties */
            repo = g_key_file_get_value (key_repolist, group, "repository", NULL);
            g_assert (repo);
            desc = g_key_file_get_value (key_repolist, group, "desc", NULL);
            g_assert (desc);
            enabled = g_key_file_get_value (key_repolist, group, "enabled", NULL);
            g_assert (enabled);

            if (enabled && g_strcmp0 (enabled, "true") == 0)
                pk_backend_job_repo_detail (job, repo, desc, TRUE);
            else
                pk_backend_job_repo_detail (job, repo, desc, FALSE);

            perc += step;
            pk_backend_job_set_percentage (job, perc);

        }
    }

    /* Release resources */
    if (groups != NULL) {
        g_strfreev (groups);
        groups = NULL;
    }

    pk_backend_job_set_percentage(job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    pk_backend_job_thread_create (job, pk_backend_get_repo_list_thread, NULL, NULL);
}

static void
pk_backend_repo_enable_thread (PkBackendJob *job, GVariant *params,
                               gpointer user_data UNUSED)
{
    g_autoptr (GError) err = NULL;
    PkBackend *backend = NULL;
    PkBackendXbpsPrivate *priv = NULL;
    g_autoptr (GKeyFile) key_repolist, key_repoconf;
    gchar **repolist_groups = NULL;
    gsize length, rv, i;
    gchar *repolist_group, *rid;
    gboolean enabled, ret, found;

    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    key_repolist = key_repoconf = NULL;
    repolist_group = rid = NULL;
    enabled = ret = found = FALSE;

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    pk_backend_job_set_percentage(job, 0);

    /* Get parameters */
    g_variant_get (params, "(&sb)", &rid, &enabled);

    /* Loading key_repoconf file */
    key_repoconf = g_key_file_new ();
    g_assert (key_repoconf);
    rv = pk_xbps_load_file (key_repoconf, priv->path_repoconf);

    /* Get repositories list */
    key_repolist = g_key_file_new ();
    g_assert (key_repolist);
    repolist_groups = pk_xbps_list_groups (key_repolist, priv->path_repolist, &length);

    if (rv == 0 && length > 0) {
        for (i = 0; i < length; i++) {
            g_autofree gchar *repo = NULL;

            /* Get group */
            repolist_group = repolist_groups[i];

            /* Get repository */
            repo = g_key_file_get_value (key_repolist, repolist_group, "repository", NULL);
            g_assert (repo);
            if (g_strcmp0 (repo, rid) == 0) {
                found = TRUE;

                if (enabled) {
                    /* Set 'true' in repos.list */
                    g_key_file_set_value (key_repolist, repolist_group, "enabled", "true");

                    /* Add repository url to the xbps confguration file */
                    g_key_file_set_value (key_repoconf, repolist_group, "repository", repo);

                    /* Add the repository string "repo" if it is not already present */
                    if (!pk_xbps_repo_exist (priv->xhp, repo)) {
                        if (!xbps_array_add_cstring (priv->xhp->repositories, repo)) {
                            syslog (LOG_DAEMON | LOG_ERR,
                                    "Error in pk_backend_repo_enable_thread() : %s",
                                    "Unable add repository string!");
                            rv = 1;
                        }
                    }
                }
                else {
                    /* Set 'false' in repos.list */
                    g_key_file_set_value (key_repolist, repolist_group, "enabled", "false");

                    /* Remove the group from the xbps confguration file */
                    ret = g_key_file_remove_group (key_repoconf, repolist_group, &err);
                    if (!ret && err != NULL) {
                        syslog (LOG_DAEMON | LOG_ERR,
                                "Error in pk_backend_repo_enable_thread() : '%s'!",
                                err->message);
                        rv = 1;
                    }

                    /* Remove repo string from repositories array in memory */
                    if (rv == 0 && !xbps_remove_string_from_array (priv->xhp->repositories, rid)) {
                        syslog (LOG_DAEMON | LOG_ERR,
                                "Error in pk_backend_repo_enable_thread() : %s",
                                "Unable remove repository string!");
                    }
                }

                /* Save the changes in repolist file */
                if (rv == 0) {
                    if ((rv = pk_xbps_save_file(key_repolist, priv->path_repolist)) == 0) {
                        /* Save the changes in repoconf file */
                        rv = pk_xbps_save_file(key_repoconf, priv->path_repoconf);
                    }
                }

                break;
            }
        }
    }

    if (rv != 0)
        pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Internal error occurred!");
    else if (rv == 0 && !found) {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_AVAILABLE, "Repository url is not valid!");
        syslog (LOG_DAEMON | LOG_ERR, "Error in pk_backend_repo_enable_thread() : %s",
                "Repository is not valid!");
    }

    /* Release resources */
    if (repolist_groups != NULL) {
        g_strfreev (repolist_groups);
        repolist_groups = NULL;
    }

    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_repo_enable (PkBackend *backend UNUSED, PkBackendJob *job,
                        const gchar *rid UNUSED, gboolean enabled UNUSED)
{
    pk_backend_job_thread_create (job, pk_backend_repo_enable_thread, NULL, NULL);
}

static void
pk_backend_refresh_cache_thread (PkBackendJob *job, GVariant *params UNUSED,
                                 gpointer user_data UNUSED)
{
    int rv;
    PkBackend *backend = pk_backend_job_get_backend (job);
    PkBackendXbpsPrivate *priv = pk_backend_get_user_data (backend);

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
    pk_backend_job_set_percentage (job, 0);

    if ((rv = xbps_rpool_sync (priv->xhp, NULL)) != 0)
        syslog (LOG_DAEMON | LOG_ERR, "Failed to sync xbps cache %s", strerror(rv));

    pk_backend_job_set_percentage (job, 100);
    pk_backend_job_finished (job);
}

void
pk_backend_refresh_cache (PkBackend *backend UNUSED, PkBackendJob *job,
                          gboolean force UNUSED)
{
    pk_backend_job_thread_create (job, pk_backend_refresh_cache_thread, NULL, NULL);
}

