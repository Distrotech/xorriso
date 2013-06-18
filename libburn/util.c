
/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <string.h>

/* ts A61008 */
/* #include <a ssert.h> */

#include <stdlib.h>
#include <stdio.h>

/* ts A80914 : This is unneeded. Version info comes from libburn.h.
#include "v ersion.h"
*/

#include "util.h"
#include "libburn.h"


void burn_version(int *major, int *minor, int *micro)
{
/* ts A80408 : switched from configure.ac versioning to libburn.h versioning */
	*major = burn_header_version_major;
	*minor = burn_header_version_minor;
	*micro = burn_header_version_micro;
}


struct cd_mid_record {
	char *manufacturer;
	int m_li;
	int s_li;
	int f_li;
	char *other_brands;
};
typedef struct cd_mid_record cd_mid_record_t;


/* ts A90902 */
/** API
   @param flag  Bitfield for control purposes,
                bit0= append "(aka %s)",other_brands to reply
 */
char *burn_guess_cd_manufacturer(int m_li, int s_li, int f_li,
                                 int m_lo, int s_lo, int f_lo, int flag)
{
	static cd_mid_record_t mid_list[]= {
	{"SKC",                                 96, 40,  0,  ""},
	{"Ritek Corp"                         , 96, 43, 30,  ""},
	{"TDK / Ritek"                        , 97, 10,  0,  "TRAXDATA"},
	{"TDK Corporation"                    , 97, 15,  0,  ""},
	{"Ritek Corp"                         , 97, 15, 10,  "7-plus, Aopen, PONY, Power Source, TDK, TRAXDATA, HiCO, PHILIPS, Primdisc, Victor.JVC, OPTI STORAGE, Samsung"},
	{"Mitsubishi Chemical Corporation"    , 97, 15, 20,  ""},
	{"Nan-Ya Plastics Corporation"        , 97, 15, 30,  "Hatron, MMore, Acer, LITEON"},
	{"Delphi"                             , 97, 15, 50,  ""},
	{"Shenzhen SG&SAST"                   , 97, 16, 20,  ""},
	{"Moser Baer India Limited"           , 97, 17,  0,  "EMTEC, Intenso, YAKUMO, PLATINUM, Silver Circle"},
	{"SKY media Manufacturing SA"         , 97, 17, 10,  ""},
	{"Wing"                               , 97, 18, 10,  ""},
	{"DDT"                                , 97, 18, 20,  ""},
	{"Daxon Technology Inc. / Acer"       , 97, 22, 60,  "Maxmax, Diamond Data, BenQ, gold, SONY"},
	{"Taiyo Yuden Company Limited"        , 97, 24,  0,  "Maxell, FUJIFILM, SONY"},
	{"Sony Corporation"                   , 97, 24, 10,  "LeadData, Imation"},
	{"Computer Support Italcard s.r.l"    , 97, 24, 20,  ""},
	{"Unitech Japan Inc."                 , 97, 24, 30,  ""},
	{"MPO, France"                        , 97, 25,  0,  "TDK"},
	{"Hitachi Maxell Ltd."                , 97, 25, 20,  ""},
	{"Infodisc Technology Co,Ltd."        , 97, 25, 30,  "MEMOREX, SPEEDA, Lead data"},
	{"Xcitec"                             , 97, 25, 60,  ""},
	{"Fornet International Pte Ltd"       , 97, 26,  0,  "COMPUSA, Cdhouse"},
	{"Postech Corporation"                , 97, 26, 10,  "Mr.Platinum"},
	{"SKC Co Ltd."                        , 97, 26, 20,  "Infinite"},
	{"Fuji Photo Film Co,Ltd."            , 97, 26, 40,  ""},
	{"Lead Data Inc."                     , 97, 26, 50,  "SONY, Gigastorage, MIRAGE"},
	{"CMC Magnetics Corporation"          , 97, 26, 60,  "Daxon, Verbatim, Memorex, Bi-Winner, PLEXTOR, YAMAHA, Melody, Office DEPOT, Philips, eMARK, imation, HyperMedia, Samsung, Shintaro, Techworks"},
	{"Ricoh Company Limited"              , 97, 27,  0,  "Sony, Digital Storage, Csita"},
	{"Plasmon Data Systems Ltd"           , 97, 27, 10,  "Ritek, TDK, EMTEC, ALPHAPET, MANIA"},
	{"Princo Corporation"                 , 97, 27, 20,  ""},
	{"Pioneer"                            , 97, 27, 30,  ""},
	{"Eastman Kodak Company"              , 97, 27, 40,  ""},
	{"Mitsui Chemicals Inc."              , 97, 27, 50,  "MAM-A, TDK"},
	{"Ricoh Company Limited"              , 97, 27, 60,  "Ritek"},
	{"Gigastorage Corporation"            , 97, 28, 10,  "MaxMax, Nan-Ya"},
	{"Multi Media Masters&Machinary SA"   , 97, 28, 20,  "King, Mmirex"},
	{"Ritek Corp"                         , 97, 31,  0,  "TDK"},
	{"Grand Advance Technology Sdn. Bhd." , 97, 31, 30,  ""},
	{"TDK Corporation"                    , 97, 32, 00,  ""},
	{"Prodisc Technology Inc."            , 97, 32, 10,  "Smartbuy, Mitsubishi, Digmaster, LG, Media Market"},
	{"Mitsubishi Chemical Corporation"    , 97, 34, 20,  "YAMAHA, Verbatim"},
	{"Mitsui Chemicals Inc."              , 97, 48, 50, ""},
	{"TDK Corporation"                    , 97, 49,  0, ""},
	{"", 0, 0, 0, ""}
	};

	int i, f_li_0;
	char buf[1024];
	char *result = NULL;

	if (m_li == 0 && s_li == 2 && f_li == 0) {
		result = strdup("(no manufacturer code)");
		return result;
	}
	f_li_0 = f_li - (f_li % 10);
	for (i = 0; mid_list[i].manufacturer[0]; i++) {
		if (m_li == mid_list[i].m_li &&
		    s_li == mid_list[i].s_li &&
		    (f_li_0 == mid_list[i].f_li || f_li == mid_list[i].f_li))
	break;
	}
	if (mid_list[i].manufacturer[0] == 0) {
		sprintf(buf, "Unknown CD manufacturer. Please report code '%2.2dm%2.2ds%2.2df/%2.2dm%2.2ds%2.2df', the human readable brand, size, and speed to scdbackup@gmx.net.", m_li, s_li, f_li, m_lo, s_lo, f_lo);
		result = strdup(buf);
		return result;
	}

	/* Compose, allocate and copy result */
	if ((flag & 1) && mid_list[i].other_brands[0]) {
		sprintf(buf, "%s  (aka %s)",
			mid_list[i].manufacturer, mid_list[i].other_brands);
		result = strdup(buf);
	} else
		result = strdup(mid_list[i].manufacturer);
	return result;
}


/* ts A90904 */
struct dvd_mid_record {
	char *mc1;
	int mc1_sig_len;
	char *manufacturer;
};
typedef struct dvd_mid_record dvd_mid_record_t;

/* ts A90904 */
char *burn_guess_manufacturer(int prf,
			 char *media_code1, char *media_code2, int flag)
{
	int i, l = 0, m_li, s_li, f_li, m_lo, s_lo, f_lo;
	char buf[1024];
	char *result = NULL, *cpt;

	/* Important Note: media_code1 and media_code2 are supposed to be
	                   encoded by burn_util_make_printable_word().
	                   Especially: ' ' -> '_' , {"_%/" unprintables -> %XY)
	*/
	static dvd_mid_record_t mid_list[]= {
	{"AML",         8, "UML"},
	{"BeAll",       5, "BeAll Developers, Inc."},
	{"CMC",         3, "CMC Magnetics Corporation"},
	{"DAXON",       5, "Daxon Technology Inc. / Acer"},
	{"Daxon",       5, "Daxon Technology Inc. / Acer"},
	{"FUJI",        4, "Fujifilm Holdings Corporation"},
	{"INFODISC",    8, "New Star Digital Co., Ltd."},
	{"INFOME",      6, "InfoMedia Inc."},
	{"ISMMBD",      6, "Info Source Multi Media Ltd."},
	{"JVC",         3, "JVC Limited"},
	{"KIC01RG",     7, "AMC"},
	{"LD",          8, "Lead Data Inc."},
	{"LGE",         3, "LG Electronics"},
	{"MAM",         8, "Mitsui Advanced Media, Inc. Europe"},
	{"MAXELL",      6, "Hitachi Maxell Ltd."},
	{"MBI",         3, "Moser Baer India Limited"},
	{"MCC",         8, "Mitsubishi Chemical Corporation"},
	{"MCI",         8, "Mitsui Chemicals Inc."},
	{"MEI",         3, "Panasonic Corporation"},
	{"MKM",         3, "Mitsubishi Kagaku Media Co."},
	{"MMC",         8, "Mitsubishi Kagaku Media Co."},
	{"MXL",         8, "Hitachi Maxell Ltd."},
	{"NANYA",       5, "Nan-Ya Plastics Corporation"},
	{"NSD",         8, "NESA International Inc."},
	{"OPTODISC",    8, "Optodisc Technology Corporation"},
	{"OTCBDR",      8, "Optodisc Technology Corporation"},
	{"PHILIP",      8, "Moser Baer India Limited"},
	{"PHILIPS",     8, "Philips"},
	{"PRINCO",      6, "Princo Corporation"},
	{"PRODISC",     7, "Prodisc Technology Inc."},
	{"Prodisc",     7, "Prodisc Technology Inc."},
	{"PVC",         3, "Pioneer"},
	{"RICOHJPN",    8, "Ricoh Company Limited"},
	{"RITEK",       5, "Ritek Corp"},
	{"SONY",        4, "Sony Corporation"},
	{"TDK",         3, "TDK Corporation"},
	{"TT",          8, "TDK Corporation"},
	{"TY",          8, "Taiyo Yuden Company Limited"},
	{"TYG",         3, "Taiyo Yuden Company Limited"},
	{"UME",         3, "UmeDisc Limited"},
	{"UTJR001",     7, "Unifino Inc."},
	{"VERBAT",      5, "Mitsubishi Kagaku Media Co."},
	{"YUDEN",       5, "Taiyo Yuden Company Limited"},
	{"", 0, ""}
	};

	if (media_code2 != NULL &&
	    (prf == -1 || prf == 0x09 || prf == 0x0A)) {
		if (strlen(media_code2) == 9 && media_code1[0] == '9' &&
			media_code1[2] == 'm' && media_code1[5] == 's' &&
			media_code1[8] == 'f' &&
			strchr(media_code1, '%') == NULL) {
			sscanf(media_code1, "%dm%ds%df", &m_li, &s_li, &f_li);
			sscanf(media_code2, "%dm%ds%df", &m_lo, &s_lo, &f_lo);
			if (m_li >= 96 && m_li <= 97 && m_lo > 0) {
				result = burn_guess_cd_manufacturer(
					m_li, s_li, f_li, m_lo, s_lo, f_lo, 0);
				return result;
			}
		}
	}

	/* DVD-R do not keep manufacturer id apart from media id.
	   Some manufacturers use a blank as separator which would now be '_'.
	*/
	cpt = strchr(media_code1, '_');
	if (cpt != NULL && (prf == -1 || prf ==  0x11 || prf == 0x13 ||
						 prf == 0x14 || prf == 0x15))
		l = cpt - media_code1;

	for (i = 0; mid_list[i].mc1[0]; i++) {
		if (strncmp(mid_list[i].mc1, media_code1,
			 mid_list[i].mc1_sig_len) == 0)
	break;
		if (l > 0)
			if (strncmp(mid_list[i].mc1, media_code1, l) == 0)
	break;
	}
	if (mid_list[i].mc1[0] == 0) {
		sprintf(buf, "Unknown DVD/BD manufacturer. Please report code '%s/%s', the human readable brand, size, and speed to scdbackup@gmx.net.", 
			media_code1, media_code2);
		result = strdup(buf);
		return result;
	}
	result = strdup(mid_list[i].manufacturer);
	return result;
}


/* ts A90905 */
/* Make *text a single printable word */
/* IMPORTANT: text must be freeable memory !
   @param flag bit0=escape '/' too
               bit1=(overrides bit0) do not escape " _/"
*/
int burn_util_make_printable_word(char **text, int flag)
{
	int i, esc_add = 0, ret;
	char *wpt, *rpt, *new_text = NULL;

	if (flag & 2)
		flag &= ~1;

	for (i = 0; (*text)[i]; i++) {
		rpt = (*text) + i;
		if (*rpt < 32 || *rpt > 126 || *rpt == 96 ||
		    ((*rpt == '_' || *rpt == '%') && (!(flag & 2))) ||
		    (*rpt == '/' && (flag & 1)))
			esc_add += 2;
	}
	if (esc_add) {
		new_text = calloc(strlen(*text) + esc_add + 1, 1);
		if (new_text == NULL) {
			ret = -1;
			goto ex;
		}
		wpt = new_text;
		for (i = 0; (*text)[i]; i++) {
			rpt = (*text) + i;
			if (*rpt < 32 || *rpt > 126 || *rpt == 96 ||
			    ((*rpt == '_' || *rpt == '%') && (!(flag & 2))) ||
			    (*rpt == '/' && (flag & 1))) {
				sprintf(wpt, "%%%2.2X", 
				    (unsigned int) *((unsigned char *) rpt));
				wpt+= 3;
			} else
				*(wpt++) = *rpt;
		}
		*wpt = 0;
		free(*text);
		*text = new_text;
	}
	if (!(flag & 2))
		for (i = 0; (*text)[i]; i++)
			if ((*text)[i] == ' ')
				(*text)[i] = '_';
	ret = 1;
ex:
	return ret;
}


/* ts B11216 */
/** Read a line from fp and strip LF or CRLF */
char *burn_sfile_fgets(char *line, int maxl, FILE *fp)
{
	int l;
	char *ret;

	ret = fgets(line, maxl, fp);
	if (ret == NULL)
		return NULL;
	l = strlen(line);
	if (l > 0)
		if (line[l - 1] == '\r')
			line[--l] = 0;
	if (l > 0)
		if (line[l - 1] == '\n')
			line[--l] = 0;
	if(l > 0)
		if(line[l - 1] == '\r')
			line[--l] = 0;
	return ret;
}


char *burn_printify(char *msg)
{
	char *cpt;

	for (cpt = msg; *cpt != 0; cpt++)
		if (*cpt < 32 || *cpt > 126)
			*cpt = '#';
	return msg;
}

