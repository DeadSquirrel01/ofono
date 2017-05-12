/*
 *  oFono - Open Source Telephony - RIL-based devices
 *
 *  Copyright (C) 2015-2017 Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#include "ril_plugin.h"
#include "ril_network.h"
#include "ril_radio.h"
#include "ril_sim_card.h"
#include "ril_sim_settings.h"
#include "ril_cell_info.h"
#include "ril_data.h"
#include "ril_util.h"
#include "ril_log.h"

#include "ofono.h"

#define MAX_PDP_CONTEXTS        (2)
#define ONLINE_TIMEOUT_SECS     (15) /* 20 sec is hardcoded in ofono core */

enum ril_modem_power_state {
	POWERED_OFF,
	POWERED_ON,
	POWERING_OFF
};

enum ril_modem_online_state {
	OFFLINE,
	GOING_ONLINE,
	ONLINE,
	GOING_OFFLINE
};

struct ril_modem_online_request {
	ofono_modem_online_cb_t cb;
	struct ril_modem_data *md;
	void *data;
	guint timeout_id;
};

struct ril_modem_data {
	struct ril_modem modem;
	GRilIoQueue *q;
	char *log_prefix;
	char *imeisv;
	char *imei;
	char *ecclist_file;
	gboolean pre_sim_done;
	gboolean allow_data;
	gulong sim_imsi_event_id;

	guint online_check_id;
	enum ril_modem_power_state power_state;
	gulong radio_state_event_id;

	ril_modem_cb_t removed_cb;
	void *removed_cb_data;

	ril_modem_online_cb_t online_cb;
	void *online_cb_data;

	struct ril_modem_online_request set_online;
	struct ril_modem_online_request set_offline;
};

#define RADIO_POWER_TAG(md) (md)

#define DBG_(md,fmt,args...) DBG("%s" fmt, (md)->log_prefix, ##args)

static struct ril_modem_data *ril_modem_data_from_ofono(struct ofono_modem *o)
{
	struct ril_modem_data *md = ofono_modem_get_data(o);
	GASSERT(md->modem.ofono == o);
	return md;
}

static struct ril_modem_data *ril_modem_data_from_modem(struct ril_modem *m)
{
	return m ? G_CAST(m, struct ril_modem_data, modem) : NULL;
}

static void *ril_modem_get_atom_data(struct ril_modem *modem,
						enum ofono_atom_type type)
{
	if (modem && modem->ofono) {
		struct ofono_atom *atom =
			__ofono_modem_find_atom(modem->ofono, type);

		if (atom) {
			return __ofono_atom_get_data(atom);
		}
	}

	return NULL;
}

struct ofono_sim *ril_modem_ofono_sim(struct ril_modem *modem)
{
	return ril_modem_get_atom_data(modem, OFONO_ATOM_TYPE_SIM);
}

struct ofono_gprs *ril_modem_ofono_gprs(struct ril_modem *modem)
{
	return ril_modem_get_atom_data(modem, OFONO_ATOM_TYPE_GPRS);
}

struct ofono_netreg *ril_modem_ofono_netreg(struct ril_modem *modem)
{
	return ril_modem_get_atom_data(modem, OFONO_ATOM_TYPE_NETREG);
}

static inline struct ofono_radio_settings *ril_modem_radio_settings(
						struct ril_modem *modem)
{
	return ril_modem_get_atom_data(modem, OFONO_ATOM_TYPE_RADIO_SETTINGS);
}

void ril_modem_delete(struct ril_modem *md)
{
	if (md && md->ofono) {
		ofono_modem_remove(md->ofono);
	}
}

void ril_modem_set_removed_cb(struct ril_modem *modem, ril_modem_cb_t cb,
								void *data)
{
	struct ril_modem_data *md = ril_modem_data_from_modem(modem);

	md->removed_cb = cb;
	md->removed_cb_data = data;
}

void ril_modem_set_online_cb(struct ril_modem *modem, ril_modem_online_cb_t cb,
								void *data)
{
	struct ril_modem_data *md = ril_modem_data_from_modem(modem);

	md->online_cb = cb;
	md->online_cb_data = data;
}

static void ril_modem_online_request_ok(struct ril_modem_online_request *req)
{
	if (req->timeout_id) {
		g_source_remove(req->timeout_id);
		req->timeout_id = 0;
	}

	if (req->cb) {
		struct ofono_error error;
		ofono_modem_online_cb_t cb = req->cb;
		void *data = req->data;

		req->cb = NULL;
		req->data = NULL;
		cb(ril_error_ok(&error), data);
	}
}

static void ril_modem_update_online_state(struct ril_modem_data *md)
{
	switch (md->modem.radio->state) {
	case RADIO_STATE_ON:
		DBG("online");
		ril_modem_online_request_ok(&md->set_online);
		break;

	case RADIO_STATE_OFF:
	case RADIO_STATE_UNAVAILABLE:
		DBG("offline");
		ril_modem_online_request_ok(&md->set_offline);
		break;

	default:
		break;
	}

	if (!md->set_offline.timeout_id && !md->set_online.timeout_id &&
					md->power_state == POWERING_OFF) {
		md->power_state = POWERED_OFF;
		if (md->modem.ofono) {
			ofono_modem_set_powered(md->modem.ofono, FALSE);
		}
	}
}

static gboolean ril_modem_online_request_timeout(gpointer data)
{
	struct ril_modem_online_request *req = data;
	struct ofono_error error;
	ofono_modem_online_cb_t cb = req->cb;
	void *cb_data = req->data;

	GASSERT(req->timeout_id);
	GASSERT(cb);

	req->timeout_id = 0;
	req->cb = NULL;
	req->data = NULL;
	cb(ril_error_failure(&error), cb_data);
	ril_modem_update_online_state(req->md);

	return G_SOURCE_REMOVE;
}

static gboolean ril_modem_online_check(gpointer data)
{
	struct ril_modem_data *md = data;

	GASSERT(md->online_check_id);
	md->online_check_id = 0;
	ril_modem_update_online_state(md);
	return FALSE;
}

static void ril_modem_schedule_online_check(struct ril_modem_data *md)
{
	if (!md->online_check_id) {
		md->online_check_id = g_idle_add(ril_modem_online_check, md);
	}
}

static void ril_modem_update_radio_settings(struct ril_modem_data *md)
{
	struct ril_modem *m = &md->modem;
	if (m->radio->state == RADIO_STATE_ON && m->sim_settings->imsi) {
		/* radio-settings.c assumes that IMSI is available */
		if (!ril_modem_radio_settings(m)) {
			DBG_(md, "initializing radio settings interface");
			ofono_radio_settings_create(m->ofono, 0,
						RILMODEM_DRIVER, md);
		}
	} else {
		/* ofono core may remove radio settings atom internally */
		struct ofono_radio_settings *rs = ril_modem_radio_settings(m);
		if (rs) {
			DBG_(md, "removing radio settings interface");
			ofono_radio_settings_remove(rs);
		} else {
			DBG_(md, "radio settings interface is already gone");
		}
	}
}

static void ril_modem_radio_state_cb(struct ril_radio *radio, void *data)
{
	struct ril_modem_data *md = data;

	GASSERT(md->modem.radio == radio);
	ril_modem_update_radio_settings(md);
	ril_modem_update_online_state(md);
}

static void ril_modem_imsi_cb(struct ril_sim_settings *settings, void *data)
{
	struct ril_modem_data *md = data;

	GASSERT(md->modem.sim_settings == settings);
	ril_modem_update_radio_settings(md);
}

static void ril_modem_pre_sim(struct ofono_modem *modem)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(modem);

	DBG("%s", ofono_modem_get_path(modem));
	md->pre_sim_done = TRUE;
	ofono_devinfo_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_sim_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_voicecall_create(modem, 0, RILMODEM_DRIVER, md);
	if (!md->radio_state_event_id) {
		md->radio_state_event_id =
			ril_radio_add_state_changed_handler(md->modem.radio,
					ril_modem_radio_state_cb, md);
	}
}

static void ril_modem_post_sim(struct ofono_modem *modem)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(modem);
	struct ofono_gprs *gprs;

	DBG("%s", ofono_modem_get_path(modem));
	ofono_sms_create(modem, 0, RILMODEM_DRIVER, md);
	gprs = ofono_gprs_create(modem, 0, RILMODEM_DRIVER, md);
	if (gprs) {
		int i;

		for (i = 0; i < MAX_PDP_CONTEXTS; i++) {
			struct ofono_gprs_context *gc =
				ofono_gprs_context_create(modem, 0,
						RILMODEM_DRIVER, md);
			if (gc == NULL)
				break;

			ofono_gprs_add_context(gprs, gc);
		}
	}

	ofono_phonebook_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_call_forwarding_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_call_barring_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_stk_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_cbs_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_message_waiting_register(ofono_message_waiting_create(modem));
}

static void ril_modem_post_online(struct ofono_modem *modem)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(modem);

	DBG("%s", ofono_modem_get_path(modem));
	ofono_call_volume_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_netreg_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_ussd_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_call_settings_create(modem, 0, RILMODEM_DRIVER, md);
	ofono_netmon_create(modem, 0, RILMODEM_DRIVER, md);
}

static void ril_modem_set_online(struct ofono_modem *modem, ofono_bool_t online,
				ofono_modem_online_cb_t cb, void *data)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(modem);
	struct ril_modem_online_request *req;

	DBG("%s going %sline", ofono_modem_get_path(modem),
						online ? "on" : "off");

	if (md->online_cb) {
		md->online_cb(&md->modem, online, md->online_cb_data);
	}

	if (online) {
		ril_radio_power_on(md->modem.radio, RADIO_POWER_TAG(md));
		req = &md->set_online;
	} else {
		ril_radio_power_off(md->modem.radio, RADIO_POWER_TAG(md));
		req = &md->set_offline;
	}

	req->cb = cb;
	req->data = data;
	if (req->timeout_id) {
		g_source_remove(req->timeout_id);
	}
	req->timeout_id = g_timeout_add_seconds(ONLINE_TIMEOUT_SECS,
					ril_modem_online_request_timeout, req);
	ril_modem_schedule_online_check(md);
}

static int ril_modem_enable(struct ofono_modem *modem)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(modem);

	DBG("%s", ofono_modem_get_path(modem));
	md->power_state = POWERED_ON;
	return 0;
}

static int ril_modem_disable(struct ofono_modem *modem)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(modem);

	DBG("%s", ofono_modem_get_path(modem));
	if (md->set_online.timeout_id || md->set_offline.timeout_id) {
		md->power_state = POWERING_OFF;
		return -EINPROGRESS;
	} else {
		md->power_state = POWERED_OFF;
		return 0;
	}
}

static int ril_modem_probe(struct ofono_modem *modem)
{
	DBG("%s", ofono_modem_get_path(modem));
	return 0;
}

static void ril_modem_remove(struct ofono_modem *ofono)
{
	struct ril_modem_data *md = ril_modem_data_from_ofono(ofono);
	struct ril_modem *modem = &md->modem;

	DBG("%s", ril_modem_get_path(modem));
	if (md->removed_cb) {
		ril_modem_cb_t cb = md->removed_cb;
		void *data = md->removed_cb_data;

		md->removed_cb = NULL;
		md->removed_cb_data = NULL;
		cb(modem, data);
	}

	ofono_modem_set_data(ofono, NULL);

	ril_radio_remove_handler(modem->radio, md->radio_state_event_id);
	ril_radio_power_off(modem->radio, RADIO_POWER_TAG(md));
	ril_radio_unref(modem->radio);

	ril_sim_settings_remove_handler(modem->sim_settings,
					md->sim_imsi_event_id);
	ril_sim_settings_unref(modem->sim_settings);

	if (md->online_check_id) {
		g_source_remove(md->online_check_id);
	}

	if (md->set_online.timeout_id) {
		g_source_remove(md->set_online.timeout_id);
	}

	if (md->set_offline.timeout_id) {
		g_source_remove(md->set_offline.timeout_id);
	}

	ril_network_unref(modem->network);
	ril_sim_card_unref(modem->sim_card);
	ril_cell_info_unref(modem->cell_info);
	ril_data_unref(modem->data);
	grilio_channel_unref(modem->io);
	grilio_queue_cancel_all(md->q, FALSE);
	grilio_queue_unref(md->q);
	g_free(md->ecclist_file);
	g_free(md->log_prefix);
	g_free(md->imeisv);
	g_free(md->imei);
	g_free(md);
}

struct ril_modem *ril_modem_create(GRilIoChannel *io, const char *log_prefix,
		const struct ril_slot_info *slot, struct ril_radio *radio,
		struct ril_network *network, struct ril_sim_card *card,
		struct ril_data *data, struct ril_sim_settings *settings,
		struct ril_cell_info *cell_info)
{
	/* Skip the slash from the path, it looks like "/ril_0" */
	struct ofono_modem *ofono = ofono_modem_create(slot->path + 1,
							RILMODEM_DRIVER);
	if (ofono) {
		int err;
		struct ril_modem_data *md = g_new0(struct ril_modem_data, 1);
		struct ril_modem *modem = &md->modem;

		/*
		 * ril_plugin.c must wait until IMEI becomes known before
		 * creating the modem
		 */
		GASSERT(slot->imei);

		/* Copy config */
		modem->config = *slot->config;
		modem->imei = md->imei = g_strdup(slot->imei);
		modem->imeisv = md->imeisv = g_strdup(slot->imeisv);
		modem->log_prefix = log_prefix;
		modem->ecclist_file =
		md->ecclist_file = g_strdup(slot->ecclist_file);
		md->log_prefix = (log_prefix && log_prefix[0]) ?
			g_strconcat(log_prefix, " ", NULL) : g_strdup("");

		modem->ofono = ofono;
		modem->radio = ril_radio_ref(radio);
		modem->network = ril_network_ref(network);
		modem->sim_card = ril_sim_card_ref(card);
		modem->sim_settings = ril_sim_settings_ref(settings);
		modem->cell_info = ril_cell_info_ref(cell_info);
		modem->data = ril_data_ref(data);
		modem->io = grilio_channel_ref(io);
		md->q = grilio_queue_new(io);

		/*
		 * modem->sim_settings->imsi follows IMSI known to the ofono
		 * core, unlike ril_sim_info->imsi which may point to the
		 * cached IMSI even before the PIN code is entered.
		 */
		md->sim_imsi_event_id =
			ril_sim_settings_add_imsi_changed_handler(settings,
						ril_modem_imsi_cb, md);

		md->set_online.md = md;
		md->set_offline.md = md;
		ofono_modem_set_data(ofono, md);
		err = ofono_modem_register(ofono);
		if (!err) {
			ril_radio_power_cycle(modem->radio);
			GASSERT(io->connected);

			/*
			 * ofono_modem_reset sets Powered to TRUE without
			 * issuing PropertyChange signal.
			 */
			ofono_modem_set_powered(modem->ofono, FALSE);
			ofono_modem_set_powered(modem->ofono, TRUE);
			md->power_state = POWERED_ON;

			/*
			 * With some RIL implementations, querying available
			 * band modes causes some magic Android properties to
			 * appear. Otherwise this request is pretty harmless
			 * and useless.
			 */
			grilio_queue_send_request(md->q, NULL,
				RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE);

			ril_modem_update_radio_settings(md);
			return modem;
		} else {
			ofono_error("Error %d registering %s",
				    err, RILMODEM_DRIVER);

			/*
			 * If ofono_modem_register() failed, then
			 * ofono_modem_remove() won't invoke
			 * ril_modem_remove() callback.
			 */
			ril_modem_remove(ofono);
		}

		ofono_modem_remove(ofono);
	}

	return NULL;
}

const struct ofono_modem_driver ril_modem_driver = {
	.name           = RILMODEM_DRIVER,
	.probe          = ril_modem_probe,
	.remove         = ril_modem_remove,
	.enable         = ril_modem_enable,
	.disable        = ril_modem_disable,
	.pre_sim        = ril_modem_pre_sim,
	.post_sim       = ril_modem_post_sim,
	.post_online    = ril_modem_post_online,
	.set_online     = ril_modem_set_online
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
