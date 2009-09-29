/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdio.h>
#include <gio/gio.h>
#include <glib.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-control.h>

#include "egg-debug.h"

#include "pk-control-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	gboolean	 ret;
	guint		 seconds;
	gchar		**transaction_list;
} PkControlHelper;

/**
 * pk_control_get_properties_cb:
 **/
static void
pk_control_get_properties_cb (PkControl *control, GAsyncResult *res, PkControlHelper *helper)
{
	/* get the result */
	helper->ret = pk_control_get_properties_finish (control, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_control_get_properties:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the properties the daemon supports.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the properties were set correctly
 **/
gboolean
pk_control_get_properties (PkControl *control, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	PkControlHelper *helper;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper = g_new0 (PkControlHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_control_get_properties_async (control, cancellable, (GAsyncReadyCallback) pk_control_get_properties_cb, helper);
	g_main_loop_run (helper->loop);

	ret = helper->ret;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return ret;
}

/**
 * pk_control_get_transaction_list_cb:
 **/
static void
pk_control_get_transaction_list_cb (PkControl *control, GAsyncResult *res, PkControlHelper *helper)
{
	/* get the result */
	helper->transaction_list = pk_control_get_transaction_list_finish (control, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_control_get_transaction_list:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the transaction list in progress.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: The list of transaction id's, or %NULL, free with g_strfreev()
 **/
gchar **
pk_control_get_transaction_list (PkControl *control, GCancellable *cancellable, GError **error)
{
	gchar **transaction_list;
	PkControlHelper *helper;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper = g_new0 (PkControlHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_control_get_transaction_list_async (control, cancellable, (GAsyncReadyCallback) pk_control_get_transaction_list_cb, helper);
	g_main_loop_run (helper->loop);

	transaction_list = helper->transaction_list;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return transaction_list;
}

