/* packet-dhpcv6.c
 * Routines for DHCPv6 packet disassembly
 * Jun-ichiro itojun Hagino <itojun@iijlab.net>
 * IItom Tsutomu MIENO <iitom@utouto.com>
 * SHIRASAKI Yasuhiro <yasuhiro@gnome.gr.jp>
 * Tony Lindstrom <tony.lindstrom@ericsson.com>
 *
 * $Id: packet-dhcpv6.c,v 1.8 2003/08/18 18:20:11 guy Exp $
 *
 * The information used comes from:
 * draft-ietf-dhc-dhcpv6-28.txt
 * draft-ietf-dhc-dhcpv6-opt-prefix-delegation-04.txt
 * draft-ietf-dhc-dhcpv6-opt-dnsconfig-03.txt
 * draft-ietf-dhc-dhcpv6-opt-nisconfig-02.txt
 * draft-ietf-dhc-dhcpv6-opt-timeconfig-02.txt
 * Note that protocol constants are still subject to change, based on IANA
 * assignment decisions.
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <glib.h>
#include <epan/int-64bit.h>
#include <epan/packet.h>
#include <epan/ipv6-utils.h>

static int proto_dhcpv6 = -1;
static int hf_dhcpv6_msgtype = -1;

static guint ett_dhcpv6 = -1;
static guint ett_dhcpv6_option = -1;

#define UDP_PORT_DHCPV6_DOWNSTREAM	546
#define UDP_PORT_DHCPV6_UPSTREAM	547

#define DHCPV6_LEASEDURATION_INFINITY	0xffffffff

#define	SOLICIT			1
#define	ADVERTISE		2
#define	REQUEST			3
#define	CONFIRM			4
#define	RENEW			5
#define	REBIND			6
#define	REPLY			7
#define	RELEASE			8
#define	DECLINE			9
#define	RECONFIGURE		10
#define	INFORMATION_REQUEST	11
#define	RELAY_FORW		12
#define	RELAY_REPL		13

#define	OPTION_CLIENTID		1
#define	OPTION_SERVERID		2
#define	OPTION_IA_NA		3
#define	OPTION_IA_TA		4
#define	OPTION_IAADDR		5
#define	OPTION_ORO		6
#define	OPTION_PREFERENCE	7
#define	OPTION_ELAPSED_TIME	8
#define	OPTION_RELAY_MSG	9
/* #define	OPTION_SERVER_MSG	10 */
#define	OPTION_AUTH		11
#define	OPTION_UNICAST		12
#define	OPTION_STATUS_CODE	13
#define	OPTION_RAPID_COMMIT	14
#define	OPTION_USER_CLASS	15
#define	OPTION_VENDOR_CLASS	16
#define	OPTION_VENDOR_OPTS	17
#define	OPTION_INTERFACE_ID	18
#define	OPTION_RECONF_MSG	19
#define	OPTION_RECONF_ACCEPT	20

#define	OPTION_IA_PD		21
#define	OPTION_IAPREFIX		22
#define OPTION_DNS_RESOLVERS	30
#define OPTION_DOMAIN_LIST      31
#define OPTION_NIS_SERVERS	35
#define OPTION_NISP_SERVERS	36
#define OPTION_NIS_DOMAIN_NAME  37
#define OPTION_NISP_DOMAIN_NAME 38
#define OPTION_NTP_SERVERS	40
#define OPTION_TIME_ZONE	41
/* define OPTION_DNS_SERVERS	50 */

#define	DUID_LLT		1
#define	DUID_EN			2
#define	DUID_LL			3
#define	DUID_LL_OLD		4

static const value_string msgtype_vals[] = {
	{ SOLICIT,	"Solicit" },
	{ ADVERTISE,	"Advertise" },
	{ REQUEST,	"Request" },
	{ CONFIRM,	"Confirm" },
	{ RENEW,	"Renew" },
	{ REBIND,	"Rebind" },
	{ REPLY,	"Reply" },
	{ RELEASE,	"Release" },
	{ DECLINE,	"Decline" },
	{ RECONFIGURE,	"Reconfigure" },
	{ INFORMATION_REQUEST,	"Information-request" },
	{ RELAY_FORW,	"Relay-forw" },
	{ RELAY_REPL,	"Relay-repl" },
	{ 0, NULL }
};

static const value_string opttype_vals[] = {
	{ OPTION_CLIENTID,	"Client Identifier" },
	{ OPTION_SERVERID,	"Server Identifier" },
	{ OPTION_IA_NA,		"Identify Association" },
	{ OPTION_IA_TA,		"Identify Association for Temporary Address" },
	{ OPTION_IAADDR,	"IA Address" },
	{ OPTION_ORO,		"Option Request" },
	{ OPTION_PREFERENCE,	"Preference" },
	{ OPTION_ELAPSED_TIME,	"Elapsed time" },
	{ OPTION_RELAY_MSG,	"Relay Message" },
/*	{ OPTION_SERVER_MSG,	"Server message" }, */
	{ OPTION_AUTH,		"Authentication" },
	{ OPTION_UNICAST,	"Server unicast" },
	{ OPTION_STATUS_CODE,	"Status code" },
	{ OPTION_RAPID_COMMIT,	"Rapid Commit" },
	{ OPTION_USER_CLASS,	"User Class" },
	{ OPTION_VENDOR_CLASS,	"Vendor Class" },
	{ OPTION_VENDOR_OPTS,	"Vendor-specific Information" },
	{ OPTION_INTERFACE_ID,	"Interface-Id" },
	{ OPTION_RECONF_MSG,	"Reconfigure Message" },
	{ OPTION_RECONF_ACCEPT,	"Reconfigure Accept" },
	{ OPTION_IA_PD,		"Identify Association for Prefix Delegation" },
	{ OPTION_IAPREFIX,	"IA Prefix" },
	{ OPTION_DNS_RESOLVERS,	"DNS Resolver" },
	{ OPTION_DOMAIN_LIST,	"Domain Search List" },
	{ OPTION_NIS_SERVERS,	"Network Information Server" },
	{ OPTION_NISP_SERVERS,	"Network Information Server V2" },
	{ OPTION_NIS_DOMAIN_NAME, "Network Information Server Domain Name" },
	{ OPTION_NISP_DOMAIN_NAME,"Network Information Server V2 Domain Name" },
	{ OPTION_NTP_SERVERS,	"Network Time Protocol Server" },
	{ OPTION_TIME_ZONE,	"Time zone" },
/*	{ OPTION_DNS_SERVERS,	"Domain Name Server" }, */
	{ 0,	NULL }
};

static const value_string statuscode_vals[] =
{
	{0, "Success" },
	{1, "UnspecFail" },
	{2, "NoAddrAvail" },
	{3, "NoBinding" },
	{4, "NotOnLink" },
	{5, "UseMulticast" },
	{6, "NoPrefixAvail" },
	{0, NULL }
};

static const value_string duidtype_vals[] =
{
	{ DUID_LLT,	"link-layer address plus time" },
	{ DUID_EN,	"assigned by vendor based on Enterprise number" },
	{ DUID_LL,	"link-layer address" },
	{ DUID_LL_OLD,	"link-layer address (old)" },
	{ 0, NULL }
};

/* Returns the number of bytes consumed by this option. */
static int
dhcpv6_option(tvbuff_t *tvb, proto_tree *bp_tree, int off, int eoff,
    gboolean *at_end)
{
	guint16	opttype;
	guint16	optlen;
	guint16	temp_optlen = 0;
	proto_item *ti;
	proto_tree *subtree;
	int i;
	struct e_in6_addr in6;
	guint16 duidtype;

	/* option type and length must be present */
	if (eoff - off < 4) {
		*at_end = TRUE;
		return 0;
	}

	opttype = tvb_get_ntohs(tvb, off);
	optlen = tvb_get_ntohs(tvb, off + 2);

	/* truncated case */
	if (eoff - off < 4 + optlen) {
		*at_end = TRUE;
		return 0;
	}

	ti = proto_tree_add_text(bp_tree, tvb, off, 4 + optlen,
		"%s", val_to_str(opttype, opttype_vals, "DHCP option %u"));

	subtree = proto_item_add_subtree(ti, ett_dhcpv6_option);
	proto_tree_add_text(subtree, tvb, off, 2, "option type: %d", opttype);
	proto_tree_add_text(subtree, tvb, off + 2, 2, "option length: %d",
		optlen);

	off += 4;
	switch (opttype) {
	case OPTION_CLIENTID:
	case OPTION_SERVERID:
		if (optlen < 2) {
			proto_tree_add_text(subtree, tvb, off, optlen,
				"DUID: malformed option");
			break;
		}
		duidtype = tvb_get_ntohs(tvb, off);
		proto_tree_add_text(subtree, tvb, off, 2,
			"DUID type: %s (%u)",
				    val_to_str(duidtype,
					       duidtype_vals, "Unknown"),
				    duidtype);
		switch (duidtype) {
		case DUID_LLT:
			if (optlen < 8) {
				proto_tree_add_text(subtree, tvb, off,
					optlen, "DUID: malformed option");
				break;
			}
			/* XXX seconds since Jan 1 2000 */
			proto_tree_add_text(subtree, tvb, off + 2, 2,
				"Hardware type: %u",
				tvb_get_ntohs(tvb, off + 2));
			proto_tree_add_text(subtree, tvb, off + 4, 4,
				"Time: %u", tvb_get_ntohl(tvb, off + 4));
			if (optlen > 8) {
				proto_tree_add_text(subtree, tvb, off + 8,
					optlen - 8, "Link-layer address");
			}
			break;
		case DUID_EN:
			if (optlen < 6) {
				proto_tree_add_text(subtree, tvb, off,
					optlen, "DUID: malformed option");
				break;
			}
			proto_tree_add_text(subtree, tvb, off + 2, 4,
					    "enterprise-number");
			if (optlen > 6) {
				proto_tree_add_text(subtree, tvb, off + 6,
					optlen - 6, "identifier");
			}
			break;
		case DUID_LL:
		case DUID_LL_OLD:
			if (optlen < 4) {
				proto_tree_add_text(subtree, tvb, off,
					optlen, "DUID: malformed option");
				break;
			}
			proto_tree_add_text(subtree, tvb, off + 2, 2,
				"Hardware type: %u",
				tvb_get_ntohs(tvb, off + 2));
			if (optlen > 4) {
				proto_tree_add_text(subtree, tvb, off + 4,
					optlen - 4, "Link-layer address");
			}
			break;
		}
		break;
	case OPTION_IA_NA:
	case OPTION_IA_PD:
          if (optlen < 12) {
             if (opttype == OPTION_IA_NA)
                proto_tree_add_text(subtree, tvb, off,
                                    optlen, "IA_NA: malformed option");
             else
                proto_tree_add_text(subtree, tvb, off,
                                    optlen, "IA_PD: malformed option");
             break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 4,
			      "IAID: %u",
			      tvb_get_ntohl(tvb, off));
	  proto_tree_add_text(subtree, tvb, off+4, 4,
			      "T1: %u", tvb_get_ntohl(tvb, off+4));
	  proto_tree_add_text(subtree, tvb, off+8, 4,
			      "T2: %u", tvb_get_ntohl(tvb, off+8));

          temp_optlen = 12;
	  while ((optlen - temp_optlen) > 0) {
	    gboolean at_end_ = FALSE;
	    temp_optlen += dhcpv6_option(tvb, subtree, off+temp_optlen, off + optlen, &at_end_);
	  }
	  break;
	case OPTION_IA_TA:
	  if (optlen < 4) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "IA_TA: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 4,
			      "IAID: %u",
			      tvb_get_ntohl(tvb, off));
          temp_optlen = 4;
	  while ((optlen - temp_optlen) > 0) {
	    gboolean at_end_;
	    temp_optlen += dhcpv6_option(tvb, subtree, off+temp_optlen, off + optlen, &at_end_);
	  }
	  break;
	case OPTION_IAADDR:
        {
           guint32 preferred_lifetime, valid_lifetime;

           if (optlen < 24) {
              proto_tree_add_text(subtree, tvb, off,
                                  optlen, "IAADDR: malformed option");
              break;
           }
           tvb_memcpy(tvb, (guint8 *)&in6, off, sizeof(in6));
           proto_tree_add_text(subtree, tvb, off,
                               sizeof(in6), "IPv6 address: %s",
                               ip6_to_str(&in6));
           
           preferred_lifetime = tvb_get_ntohl(tvb, off + 16);
           valid_lifetime = tvb_get_ntohl(tvb, off + 20);
           
           if (preferred_lifetime == DHCPV6_LEASEDURATION_INFINITY) {
              proto_tree_add_text(subtree, tvb, off + 16, 4,
                                  "Preferred lifetime: infinity");
           } else {
              proto_tree_add_text(subtree, tvb, off + 16, 4,
                                  "Preferred lifetime: %u", preferred_lifetime);
           }
           if (valid_lifetime == DHCPV6_LEASEDURATION_INFINITY) {
              proto_tree_add_text(subtree, tvb, off + 20, 4,
                                  "Valid lifetime: infinity");
           } else {
              proto_tree_add_text(subtree, tvb, off + 20, 4,
                                  "Valid lifetime: %u", valid_lifetime);
           }
           
           temp_optlen = 24;
           while ((optlen - temp_optlen) > 0) {
              gboolean at_end_;
              temp_optlen += dhcpv6_option(tvb, subtree, off+temp_optlen, off + optlen, &at_end_);
           }
        }
        break;
	case OPTION_ORO:
		for (i = 0; i < optlen; i += 2) {
		    guint16 requested_opt_code;
		    requested_opt_code = tvb_get_ntohs(tvb, off + i);
		    proto_tree_add_text(subtree, tvb, off + i,
			    2, "Requested Option code: %s (%d)",
					    val_to_str(requested_opt_code,
						       opttype_vals,
						       "Unknown"),
					    requested_opt_code);
		}
		break;
	case OPTION_PREFERENCE:
	  if (optlen != 1) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "PREFERENCE: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 1,
			      "pref-value: %d",
			      (guint32)tvb_get_guint8(tvb, off));
	  break;
	case OPTION_ELAPSED_TIME:
	  if (optlen != 2) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "ELAPSED-TIME: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 2,
			      "elapsed-time: %d sec",
			      (guint32)tvb_get_ntohs(tvb, off));
	  break;
	case OPTION_RELAY_MSG:
	  if (optlen == 0) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "RELAY-MSG: malformed option");
	    break;
	  } else {
	    gboolean at_end_;
	    dhcpv6_option(tvb, subtree, off, off + optlen, &at_end_);
          } 
	  break;
	case OPTION_AUTH:
	  if (optlen < 15) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "AUTH: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 1,
			      "Protocol: %d",
			      (guint32)tvb_get_guint8(tvb, off));
	  proto_tree_add_text(subtree, tvb, off+1, 1,
			      "Algorithm: %d",
			      (guint32)tvb_get_guint8(tvb, off+1));
	  proto_tree_add_text(subtree, tvb, off+2, 1,
			      "RDM: %d",
			      (guint32)tvb_get_guint8(tvb, off+2));
	  proto_tree_add_text(subtree, tvb, off+3, 8,
			      "Reply Detection");
	  proto_tree_add_text(subtree, tvb, off+11, optlen-11,
			      "Authentication Information");
	  break;
	case OPTION_UNICAST:
	  if (optlen != 16) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "UNICAST: malformed option");
	    break;
	  }
	  tvb_memcpy(tvb, (guint8 *)&in6, off, sizeof(in6));
	  proto_tree_add_text(subtree, tvb, off,
			      sizeof(in6), "IPv6 address: %s",
				ip6_to_str(&in6));
	  break;
	case OPTION_STATUS_CODE:
	    {
		guint16 status_code;
		char *status_message = 0;
		status_code = tvb_get_ntohs(tvb, off);
		proto_tree_add_text(subtree, tvb, off, 2,
				    "Status Code: %s (%d)",
				    val_to_str(status_code, statuscode_vals,
					       "Unknown"),
				    status_code);

		if (optlen - 2 > 0)
		    status_message = g_malloc(optlen - 2 + 1);
		if (status_message != 0){
		    memset(status_message, 0, optlen - 2 + 1);
		    status_message = tvb_memcpy(tvb, status_message, off + 2,
						optlen - 2);
		    proto_tree_add_text(subtree, tvb, off + 2, optlen - 2,
					"Status Message: %s",
					status_message);
		    g_free(status_message);
		}
	    }
	    break;
	case OPTION_VENDOR_CLASS:
	  if (optlen < 4) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "VENDOR_CLASS: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 4,
			      "enterprise-number: %u",
			      tvb_get_ntohl(tvb, off));
	  if (optlen > 4) {
	    proto_tree_add_text(subtree, tvb, off+4, optlen-4,
				"vendor-class-data");
	  }
	  break;
	case OPTION_VENDOR_OPTS:
	  if (optlen < 4) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "VENDOR_OPTS: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, 4,
			      "enterprise-number: %u",
			      tvb_get_ntohl(tvb, off));
	  if (optlen > 4) {
	    proto_tree_add_text(subtree, tvb, off+4, optlen-4,
				"option-data");
	  }
	  break;
	case OPTION_INTERFACE_ID:
	  if (optlen == 0) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "INTERFACE_ID: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, optlen, "Interface-ID");
	  break;
	case OPTION_RECONF_MSG:
	  if (optlen != 1) {
	    proto_tree_add_text(subtree, tvb, off,
				optlen, "RECONF_MSG: malformed option");
	    break;
	  }
	  proto_tree_add_text(subtree, tvb, off, optlen,
			      "Reconfigure-type: %s",
			      val_to_str(tvb_get_guint8(tvb, off),
					 msgtype_vals,
					 "Message Type %u"));
	  break;
/*	case OPTION_DNS_SERVERS:
		if (optlen % 16) {
			proto_tree_add_text(subtree, tvb, off, optlen,
				"DNS servers address: malformed option");
			break;
		}
		for (i = 0; i < optlen; i += 16) {
			tvb_memcpy(tvb, (guint8 *)&in6, off + i, sizeof(in6));
			proto_tree_add_text(subtree, tvb, off + i,
				sizeof(in6), "DNS servers address: %s",
				ip6_to_str(&in6));
		}
		break;
*/
	case OPTION_DNS_RESOLVERS:
		if (optlen % 16) {
			proto_tree_add_text(subtree, tvb, off, optlen,
				"DNS resolvers address: malformed option");
			break;
		}
		for (i = 0; i < optlen; i += 16) {
			tvb_memcpy(tvb, (guint8 *)&in6, off + i, sizeof(in6));
			proto_tree_add_text(subtree, tvb, off + i,
				sizeof(in6), "DNS resolvers address: %s",
				ip6_to_str(&in6));
		}
		break;
	case OPTION_DOMAIN_LIST:
	  if (optlen > 0) {
	    proto_tree_add_text(subtree, tvb, off, optlen, "Search String");
	  }
	  break;
	case OPTION_NIS_SERVERS:
		if (optlen % 16) {
			proto_tree_add_text(subtree, tvb, off, optlen,
				"NIS servers address: malformed option");
			break;
		}
		for (i = 0; i < optlen; i += 16) {
			tvb_memcpy(tvb, (guint8 *)&in6, off + i, sizeof(in6));
			proto_tree_add_text(subtree, tvb, off + i,
				sizeof(in6), "NIS servers address: %s",
				ip6_to_str(&in6));
		}
		break;
	case OPTION_NISP_SERVERS:
		if (optlen % 16) {
			proto_tree_add_text(subtree, tvb, off, optlen,
				"NISP servers address: malformed option");
			break;
		}
		for (i = 0; i < optlen; i += 16) {
			tvb_memcpy(tvb, (guint8 *)&in6, off + i, sizeof(in6));
			proto_tree_add_text(subtree, tvb, off + i,
				sizeof(in6), "NISP servers address: %s",
				ip6_to_str(&in6));
		}
		break;
	case OPTION_NIS_DOMAIN_NAME:
	  if (optlen > 0) {
	    proto_tree_add_text(subtree, tvb, off, optlen, "nis-domain-name");
	  }
	  break;
	case OPTION_NISP_DOMAIN_NAME:
	  if (optlen > 0) {
	    proto_tree_add_text(subtree, tvb, off, optlen, "nisp-domain-name");
	  }
	  break;
	case OPTION_NTP_SERVERS:
		if (optlen % 16) {
			proto_tree_add_text(subtree, tvb, off, optlen,
				"NTP servers address: malformed option");
			break;
		}
		for (i = 0; i < optlen; i += 16) {
			tvb_memcpy(tvb, (guint8 *)&in6, off + i, sizeof(in6));
			proto_tree_add_text(subtree, tvb, off + i,
				sizeof(in6), "NTP servers address: %s",
				ip6_to_str(&in6));
		}
		break;
	case OPTION_TIME_ZONE:
	  if (optlen > 0) {
	    proto_tree_add_text(subtree, tvb, off, optlen, "time-zone");
	  }
	  break;
	case OPTION_IAPREFIX:
	    {
		guint32 preferred_lifetime, valid_lifetime;
		guint8  prefix_length;
		struct e_in6_addr in6;

                if (optlen < 25) {
                   proto_tree_add_text(subtree, tvb, off,
                                       optlen, "IAPREFIX: malformed option");
                   break;
                }

		preferred_lifetime = tvb_get_ntohl(tvb, off);
		valid_lifetime = tvb_get_ntohl(tvb, off + 4);
		prefix_length  = tvb_get_guint8(tvb, off + 8);
		if (preferred_lifetime == DHCPV6_LEASEDURATION_INFINITY) {
			proto_tree_add_text(subtree, tvb, off, 4,
				    "Preferred lifetime: infinity");
		} else {
			proto_tree_add_text(subtree, tvb, off, 4,
				    "Preferred lifetime: %u", preferred_lifetime);
		}
		if (valid_lifetime == DHCPV6_LEASEDURATION_INFINITY) {
			proto_tree_add_text(subtree, tvb, off + 4, 4,
				    "Valid lifetime: infinity");
		} else {
			proto_tree_add_text(subtree, tvb, off + 4, 4,
				    "Valid lifetime: %u", valid_lifetime);
		}
		proto_tree_add_text(subtree, tvb, off + 8, 1,
				    "Prefix length: %d", prefix_length);
		tvb_memcpy(tvb, (guint8 *)&in6, off + 9 , sizeof(in6));
		proto_tree_add_text(subtree, tvb, off + 9,
				    16, "Prefix address: %s",
				    ip6_to_str(&in6));
                
                temp_optlen = 25;
                while ((optlen - temp_optlen) > 0) {
                   gboolean at_end_;
                   temp_optlen += dhcpv6_option(tvb, subtree, off+temp_optlen, off + optlen, &at_end_);
                }
	    }
	    break;
	}

	return 4 + optlen;
}


static void
dissect_dhcpv6(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree,
    gboolean downstream)
{
	proto_tree *bp_tree = NULL;
	proto_item *ti;
	guint8 msgtype, hop_count ;
	guint32 xid;
	int off = 0;
        int eoff;
	struct e_in6_addr in6;
	gboolean at_end;
        gboolean relay_msg_option = FALSE;
        int length;

	eoff = tvb_reported_length(tvb);
	downstream = 0; /* feature reserved */
	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "DHCPv6");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	msgtype = tvb_get_guint8(tvb, off);

	if (tree) {
		ti = proto_tree_add_item(tree, proto_dhcpv6, tvb, 0, -1, FALSE);
		bp_tree = proto_item_add_subtree(ti, ett_dhcpv6);
        }

        while (msgtype == RELAY_FORW || msgtype == RELAY_REPL) {
           
           if (check_col(pinfo->cinfo, COL_INFO)) {
              col_set_str(pinfo->cinfo, COL_INFO,
                          val_to_str(msgtype,
                                     msgtype_vals,
                                     "Message Type %u"));
           }

           proto_tree_add_uint(bp_tree, hf_dhcpv6_msgtype, tvb, off, 1, msgtype);

           hop_count = tvb_get_guint8(tvb, off+1);
           proto_tree_add_text(bp_tree, tvb, off+1, 1, "Hop count: %d", hop_count);

           tvb_memcpy(tvb, (guint8 *)&in6, off+2, sizeof(in6));
           proto_tree_add_text(bp_tree, tvb, off+2, sizeof(in6), 
                               "Link-address: %s",ip6_to_str(&in6));

           tvb_memcpy(tvb, (guint8 *)&in6, off+18, sizeof(in6));
           proto_tree_add_text(bp_tree, tvb, off+18, sizeof(in6), 
                               "Peer-address: %s",ip6_to_str(&in6));

           off += 34;
           relay_msg_option = FALSE;

           while (!relay_msg_option && off < eoff) {
              length = dhcpv6_option(tvb, bp_tree, off, eoff, &at_end);

              if (tvb_get_ntohs(tvb, off) == OPTION_RELAY_MSG) {
                 relay_msg_option = TRUE;
                 off += 4;
              }
              else {
                 if (length > 0)
                    off += length;
                 else {
                    proto_tree_add_text(bp_tree, tvb, off, eoff, "Message: malformed");
                    return;
                 }
              }
           }
           
           msgtype = tvb_get_guint8(tvb, off);
        }
        
	xid = tvb_get_ntohl(tvb, off) & 0x00ffffff;

        if (!off) {
           if (check_col(pinfo->cinfo, COL_INFO)) {
              col_set_str(pinfo->cinfo, COL_INFO,
                          val_to_str(msgtype,
                                     msgtype_vals,
                                     "Message Type %u"));
           }
        }

	if (tree) {
		proto_tree_add_uint(bp_tree, hf_dhcpv6_msgtype, tvb, off, 1,
			msgtype);
		proto_tree_add_text(bp_tree, tvb, off+1, 3, "Transaction-ID: 0x%08x", xid);
#if 0
		tvb_memcpy(tvb, (guint8 *)&in6, 4, sizeof(in6));
		proto_tree_add_text(bp_tree, tvb, 4, sizeof(in6),
			"Server address: %s", ip6_to_str(&in6));
#endif
	}

	off += 4;

	at_end = FALSE;
	while (off < eoff && !at_end)
		off += dhcpv6_option(tvb, bp_tree, off, eoff, &at_end);
}

static void
dissect_dhcpv6_downstream(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	dissect_dhcpv6(tvb, pinfo, tree, TRUE);
}

static void
dissect_dhcpv6_upstream(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	dissect_dhcpv6(tvb, pinfo, tree, FALSE);
}


void
proto_register_dhcpv6(void)
{
  static hf_register_info hf[] = {
    { &hf_dhcpv6_msgtype,
      { "Message type",			"dhcpv6.msgtype",	 FT_UINT8,
         BASE_DEC, 			VALS(msgtype_vals),   0x0,
      	"", HFILL }},
  };
  static gint *ett[] = {
    &ett_dhcpv6,
    &ett_dhcpv6_option,
  };

  proto_dhcpv6 = proto_register_protocol("DHCPv6", "DHCPv6", "dhcpv6");
  proto_register_field_array(proto_dhcpv6, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_dhcpv6(void)
{
  dissector_handle_t dhcpv6_handle;

  dhcpv6_handle = create_dissector_handle(dissect_dhcpv6_downstream,
	proto_dhcpv6);
  dissector_add("udp.port", UDP_PORT_DHCPV6_DOWNSTREAM, dhcpv6_handle);
  dhcpv6_handle = create_dissector_handle(dissect_dhcpv6_upstream,
	proto_dhcpv6);
  dissector_add("udp.port", UDP_PORT_DHCPV6_UPSTREAM, dhcpv6_handle);
}
