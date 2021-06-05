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

struct transaction {
    struct xbps_handle *xhp;
    xbps_dictionary_t d;
    xbps_object_iterator_t iter;
    uint32_t inst_pkgcnt;
    uint32_t up_pkgcnt;
    uint32_t cf_pkgcnt;
    uint32_t rm_pkgcnt;
    uint32_t dl_pkgcnt;
    uint32_t hold_pkgcnt;
};

void
pk_xbps_set_transaction_err (PkBackendJob *job, gsize rv, const gchar *f_name)
{
    /* We only handle the eventual xbps auto update error.
     * For the others errors, errno description is enough
     */
    if (rv == EBUSY) {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "%s\n%s",
                                   strerror (rv),
                                   "Could be required a xbps update via 'xbps-install -u xbps' command.");
        syslog (LOG_DAEMON | LOG_ERR, "PackageKit: %s error : %s\n%s",
                f_name, strerror (rv),
                "Could be required a xbps update via 'xbps-install -u xbps' command.");
    }
    else {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "%s", strerror (rv));
        syslog (LOG_DAEMON | LOG_ERR, "PackageKit: %s error : %s",
                f_name, strerror (rv));
    }
}


gsize
pk_xbps_map_pkgname (struct xbps_handle *xhp)
{
    xbps_object_iterator_t iter;
    xbps_object_t obj;
    gsize rv = 0;

    g_assert (xhp);
    iter = xbps_dictionary_iterator (xhp->pkgdb);
    g_assert (iter);

    while ((obj = xbps_object_iterator_next (iter))) {
        xbps_dictionary_t pkgd = NULL;
        const gchar *pkgver = NULL;
        const gchar *pkgname = NULL;
        gchar name[XBPS_NAME_SIZE];

        pkgd = xbps_dictionary_get_keysym (xhp->pkgdb, obj);

        if (xbps_dictionary_get_cstring_nocopy (pkgd, "pkgver", &pkgver)) {
            if (!xbps_dictionary_get_cstring_nocopy (pkgd, "pkgname", &pkgname)) {
                if (xbps_pkg_name (name, sizeof (name), pkgver)) {
                    if (!xbps_dictionary_set_cstring_nocopy (pkgd, "pkgname", name)) {
                        syslog (LOG_DAEMON | LOG_ERR, "PackageKit: pk_xbps_maps_pkgname() error : "
                                "Unable mapping pkgname property into database!");
                        rv = 1;
                        break;
                    }
                }
            }
        }
    }
    xbps_object_iterator_release (iter);

    return rv;
}


static bool
all_pkgs_on_hold (struct transaction *trans)
{
    xbps_object_t obj;
    bool all_on_hold = true;

    while ((obj = xbps_object_iterator_next (trans->iter)) != NULL) {
        if (xbps_transaction_pkg_type (obj) != XBPS_TRANS_HOLD) {
            all_on_hold = false;
            break;
        }
    }
    xbps_object_iterator_reset (trans->iter);
    return all_on_hold;
}

int
exec_transaction(struct xbps_handle *xhp, bool drun)
{
    xbps_array_t array;
    struct transaction *trans;
    uint64_t fsize = 0, isize = 0;
    char freesize[8], instsize[8];
    int rv = 0;


    trans = calloc (1, sizeof(*trans));
    if (trans == NULL)
        return ENOMEM;

    if ((rv = xbps_transaction_prepare (xhp)) != 0) {
        if (rv == ENODEV) {
            array = xbps_dictionary_get (xhp->transd, "missing_deps");
            if (xbps_array_count (array)) {
                /* missing dependencies */
                syslog (LOG_DAEMON | LOG_ERR,
                        "Transaction aborted due to unresolved dependencies.\n");
            }
        }
        else if (rv == ENOEXEC) {
            array = xbps_dictionary_get (xhp->transd, "missing_shlibs");
            if (xbps_array_count (array)) {
                /* missing shlibs */
                syslog (LOG_DAEMON | LOG_ERR,
                        "Transaction aborted due to unresolved shlibs.\n");
            }
        }
        else if (rv == EAGAIN) {
            /* conflicts */
            syslog (LOG_DAEMON | LOG_ERR,
                    "Transaction aborted due to conflicting packages.\n");
        }
        else if (rv == ENOSPC) {
            /* not enough free space */
            xbps_dictionary_get_uint64 (xhp->transd,
                "total-installed-size", &isize);
            if (xbps_humanize_number (instsize, (int64_t)isize) == -1) {
                syslog (LOG_DAEMON | LOG_ERR, "humanize_number2 returns "
                    "%s\n", strerror (errno));
                rv = -1;
                goto out;
            }
            xbps_dictionary_get_uint64 (xhp->transd,
                "disk-free-size", &fsize);
            if (xbps_humanize_number (freesize, (int64_t)fsize) == -1) {
                syslog (LOG_DAEMON | LOG_ERR, "humanize_number2 returns "
                    "%s\n", strerror (errno));
                rv = -1;
                goto out;
            }
            syslog (LOG_DAEMON | LOG_ERR, "Transaction aborted due to insufficient disk "
                "space (need %s, got %s free).\n", instsize, freesize);
        }
        else {
            xbps_dbg_printf (xhp, "Empty transaction dictionary: %s\n",
                strerror (errno));
        }
        goto out;
    }

    trans->xhp = xhp;
    trans->d = xhp->transd;
    trans->iter = xbps_array_iter_from_dict(xhp->transd, "packages");
    assert (trans->iter);

    /*
     * dry-run mode, show what would be done but don't run anything.
     */
    if (drun)
        goto out;

    /*
     * No need to do anything if all packages are on hold.
     */
    if (all_pkgs_on_hold (trans))
        goto out;

    /*
     * It's time to run the transaction!
     */
    if ((rv = xbps_transaction_commit (xhp)) == 0) {
        syslog (LOG_DAEMON | LOG_INFO, "\n%u downloaded, %u installed, %u updated, "
            "%u configured, %u removed, %u on hold.\n",
                trans->dl_pkgcnt, trans->inst_pkgcnt,
                trans->up_pkgcnt,
                trans->cf_pkgcnt + trans->inst_pkgcnt + trans->up_pkgcnt,
                trans->rm_pkgcnt,
                trans->hold_pkgcnt);
    }
    else
        syslog (LOG_DAEMON | LOG_ERR, "Transaction failed! see above for errors.\n");

out:

    if (trans->iter)
        xbps_object_iterator_release (trans->iter);
    if (trans)
        free (trans);

    return rv;
}

static void
pk_xbps_transaction_thread (PkBackendJob *job, GVariant *params, gpointer user_data UNUSED)
{
    PkBackend *backend = NULL;
    PkBackendXbpsPrivate *priv = NULL;
    gsize i, rv;
    gfloat len, step , perc;
    PkBitfield transaction_flags;
    gchar **package_ids = NULL;
    gboolean simulate, allow_deps, autoremove;
    PkRoleEnum role;


    /* Initialize */
    backend = pk_backend_job_get_backend (job);
    priv = pk_backend_get_user_data (backend);
    g_assert (backend != NULL && priv != NULL);
    i = len = step = perc = rv = 0;
    role = pk_backend_job_get_role (job);
    priv->job_cancelled = FALSE;

    /* Get packages id by role */
    if (role == PK_ROLE_ENUM_INSTALL_PACKAGES || role == PK_ROLE_ENUM_UPDATE_PACKAGES)
        g_variant_get (params, "(t^a&s)", &transaction_flags, &package_ids);
    else if (role == PK_ROLE_ENUM_REMOVE_PACKAGES)
        g_variant_get (params, "(t^a&sbb)", &transaction_flags, &package_ids, &allow_deps, &autoremove);

    /* Get simulate parameter from transaction_flags */
    simulate = (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE));

    /* Start job */
    pk_backend_job_set_percentage (job, 0);

    if (simulate) {
        /* Set job status */
        pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
        pk_backend_job_set_allow_cancel (job, TRUE);
    }
    else {
        /* Before transaction, we initialize the "items" and "pkgdb_map_names_done" variables */
        priv->xhp->pkgdb_fd = -1;
        priv->xhp->pkgdb_map_names_done = false;
        priv->xhp->hashtab = NULL;
        priv->xhp->items = NULL;
        priv->xhp->itemsidx = 0;
        priv->xhp->itemssz = 0;

        /* Set job status and lock */
        pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);
        pk_backend_job_set_locked (job, TRUE);
        pk_backend_job_set_allow_cancel (job, FALSE);
    }

    len = (package_ids != NULL ? g_strv_length (package_ids) : 0);
    /* Calculating step and percentage */
    if (len > 0)
        step = 95 / len;

    for (; i < len; i++) {

        if (!priv->job_cancelled) {

            g_autofree gchar **split_package_id = NULL;
            gchar *pkgname, *version;
            g_autofree gchar *pkgver = NULL;
            const gchar *desc = NULL;
            g_autofree gchar *compdesc = NULL;
            xbps_dictionary_t pkgd = NULL;

            /* Splitting packageid */
            split_package_id = pk_package_id_split (package_ids[i]);
            /* Get package name and version */
            pkgname = split_package_id[0];
            version = split_package_id[1];
            /* Building pkgver */
            pkgver = g_strconcat (pkgname, "-", version, NULL);

            /* Getting local/remote package according the role to retrieve the package description */
            if (role == PK_ROLE_ENUM_REMOVE_PACKAGES)
                pkgd = xbps_pkgdb_get_pkg (priv->xhp, pkgver);
            else {
                pkgd = xbps_rpool_get_pkg (priv->xhp, pkgver);
                if (pkgd == NULL)
                    pkgd = xbps_rpool_get_virtualpkg (priv->xhp, pkgver);
            }

            if (pkgd != NULL) {
                /* Get description */
                xbps_dictionary_get_cstring_nocopy (pkgd, "short_desc", &desc);
                /* Remove eventual escape characters from desc */
                compdesc = g_strcompress (desc);

                switch (role) {
                    case PK_ROLE_ENUM_INSTALL_PACKAGES:
                         pk_backend_job_package (job, PK_INFO_ENUM_INSTALLING, package_ids[i], compdesc);
                         if (!simulate) {
                            rv = xbps_transaction_install_pkg (priv->xhp, pkgver, TRUE);
                            if (rv == 0)
                                pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
                         }
                         break;
                    case PK_ROLE_ENUM_UPDATE_PACKAGES:
                         pk_backend_job_package (job, PK_INFO_ENUM_UPDATING, package_ids[i], compdesc);
                         if (!simulate) {
                            rv = xbps_transaction_update_pkg (priv->xhp, pkgver, TRUE);
                            if (rv == 0)
                                pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
                         }
                         break;
                    case PK_ROLE_ENUM_REMOVE_PACKAGES:
                         pk_backend_job_package (job, PK_INFO_ENUM_REMOVING, package_ids[i], compdesc);
                         if (!simulate && (rv = pk_xbps_map_pkgname (priv->xhp)) == 0) {
                            rv = xbps_transaction_remove_pkg (priv->xhp, pkgver, FALSE);
                            if (rv == 0)
                                pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
                         }
                         break;
                    default:
                         break;
                }
                if (rv != 0)
                    break;
            }

            perc += step;
            pk_backend_job_set_percentage (job, perc);
        }
        else
            break;
    }

    if (!priv->job_cancelled) {
        /* If it isn't simulate and the job is not cancelled, then make a transaction commit */
        if (!simulate && rv == 0) {
            if ((rv = xbps_pkgdb_lock (priv->xhp)) == 0) {
                rv = exec_transaction (priv->xhp, false);
                xbps_pkgdb_unlock (priv->xhp);
            }
        }
        /* Show eventual errors */
        if (rv != 0)
            pk_xbps_set_transaction_err (job, rv, "pk_xbps_transaction_thread()");

    }
    else {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED,
                                   "The task was stopped successfully");
        syslog (LOG_DAEMON | LOG_ERR, "PackageKit: pk_xbps_transaction_thread() error : %s",
                "The task was stopped successfully");
    }

    /* Unlock job if it's not simulate */
    if (!simulate)
        pk_backend_job_set_locked (job, FALSE);

    /* Release transd */
    if (priv->xhp->transd != NULL ) {
        xbps_object_release (priv->xhp->transd);
        priv->xhp->transd = NULL;
    }

    /* End job */
    if (!priv->job_cancelled) {
        pk_backend_job_set_percentage (job, 100);
        pk_backend_job_finished (job);
    }

}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags,
                             gchar **package_ids)
{
    pk_backend_job_thread_create (job, pk_xbps_transaction_thread, NULL, NULL);
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags,
                gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
    pk_backend_job_thread_create (job, pk_xbps_transaction_thread, NULL, NULL);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags,
                            gchar **package_ids)
{
    pk_backend_job_thread_create (job, pk_xbps_transaction_thread, NULL, NULL);
}


void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
    PkBackendXbpsPrivate *priv = pk_backend_get_user_data (backend);;
    g_assert (priv != NULL);
    priv->job_cancelled = TRUE;
    pk_backend_job_set_status (job, PK_STATUS_ENUM_CANCEL);
}


