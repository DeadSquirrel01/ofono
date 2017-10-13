/*
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2017 Jolla Ltd.
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

#include "drivers/ril/ril_util.h"

#include "ofono.h"
#include "common.h"

void test_parse_tech(void)
{
	int tech = 0;

	g_assert(ril_parse_tech(NULL, NULL) == -1);
	g_assert(ril_parse_tech(NULL, &tech) == -1);
	g_assert(tech == -1);
	g_assert(ril_parse_tech("-1", &tech) == -1);
	g_assert(tech == -1);
	g_assert(ril_parse_tech("0", &tech) == -1);
	g_assert(tech == -1);
	g_assert(ril_parse_tech("1", &tech) == ACCESS_TECHNOLOGY_GSM);
	g_assert(tech == RADIO_TECH_GPRS);
	g_assert(ril_parse_tech("16", &tech) == ACCESS_TECHNOLOGY_GSM);
	g_assert(tech == RADIO_TECH_GSM);
	g_assert(ril_parse_tech("2", &tech) == ACCESS_TECHNOLOGY_GSM_EGPRS);
	g_assert(tech == RADIO_TECH_EDGE);
	g_assert(ril_parse_tech("3", &tech) == ACCESS_TECHNOLOGY_UTRAN);
	g_assert(tech == RADIO_TECH_UMTS);
	g_assert(ril_parse_tech("9", &tech) == ACCESS_TECHNOLOGY_UTRAN_HSDPA);
	g_assert(tech == RADIO_TECH_HSDPA);
	g_assert(ril_parse_tech("10", &tech) == ACCESS_TECHNOLOGY_UTRAN_HSUPA);
	g_assert(tech == RADIO_TECH_HSUPA);
	g_assert(ril_parse_tech("11", &tech) ==
					ACCESS_TECHNOLOGY_UTRAN_HSDPA_HSUPA);
	g_assert(tech == RADIO_TECH_HSPA);
	g_assert(ril_parse_tech("15", &tech) ==
					ACCESS_TECHNOLOGY_UTRAN_HSDPA_HSUPA);
	g_assert(tech == RADIO_TECH_HSPAP);
	g_assert(ril_parse_tech("14", &tech) == ACCESS_TECHNOLOGY_EUTRAN);
	g_assert(tech == RADIO_TECH_LTE);
}

void test_parse_mcc_mnc(void)
{
	struct ofono_network_operator op;

	memset(&op, 0, sizeof(op));
	g_assert(!ril_parse_mcc_mnc(NULL, &op));
	g_assert(!ril_parse_mcc_mnc("", &op));
	g_assert(!ril_parse_mcc_mnc("24x", &op));
	g_assert(!ril_parse_mcc_mnc("244", &op));
	g_assert(!ril_parse_mcc_mnc("244x", &op));
	g_assert(ril_parse_mcc_mnc("24412", &op));
	g_assert(!strcmp(op.mcc, "244"));
	g_assert(!strcmp(op.mnc, "12"));
	g_assert(!op.tech);
	g_assert(ril_parse_mcc_mnc("25001+", &op));
	g_assert(!strcmp(op.mcc, "250"));
	g_assert(!strcmp(op.mnc, "01"));
	g_assert(!op.tech);
	g_assert(ril_parse_mcc_mnc("25503+14", &op));
	g_assert(!strcmp(op.mcc, "255"));
	g_assert(!strcmp(op.mnc, "03"));
	g_assert(op.tech == ACCESS_TECHNOLOGY_EUTRAN);
	/* Not sure if this is right but that's now it currently works: */
	op.tech = 0;
	g_assert(ril_parse_mcc_mnc("3101500", &op));
	g_assert(!strcmp(op.mcc, "310"));
	g_assert(!strcmp(op.mnc, "150"));
	g_assert(!op.tech);
}

void test_parse_int(void)
{
	int value;

	g_assert(!ril_parse_int(NULL, 0, NULL));
	g_assert(!ril_parse_int("", 0, NULL));
	g_assert(!ril_parse_int("garbage", 0, NULL));
	g_assert(!ril_parse_int("0 trailing garbage", 0, NULL));
	g_assert(ril_parse_int("0", 0, NULL));
	g_assert(ril_parse_int("0", 0, &value));
	g_assert(value == 0);
	g_assert(!ril_parse_int("0x10000000000000000", 0, &value));
	g_assert(!ril_parse_int("-2147483649", 0, &value));
	g_assert(!ril_parse_int("4294967295", 0, &value));
	g_assert(ril_parse_int(" 0x7fffffff ", 0, &value));
	g_assert(value == 0x7fffffff);
	g_assert(ril_parse_int(" 7fffffff ", 16, &value));
	g_assert(value == 0x7fffffff);
	g_assert(!ril_parse_int("0xffffffff", 0, &value));
}

#define TEST_(name) "/ril_util/" name

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);

	__ofono_log_init("test-ril_util",
		g_test_verbose() ? "*" : NULL,
		FALSE, FALSE);

	g_test_add_func(TEST_("parse_tech"), test_parse_tech);
	g_test_add_func(TEST_("parse_mcc_mnc"), test_parse_mcc_mnc);
	g_test_add_func(TEST_("parse_int"), test_parse_int);

	return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
