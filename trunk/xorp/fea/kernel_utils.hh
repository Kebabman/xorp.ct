// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001-2006 International Computer Science Institute
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software")
// to deal in the Software without restriction, subject to the conditions
// listed in the XORP LICENSE file. These conditions include: you must
// preserve this copyright notice, and you cannot mention the copyright
// holders in advertising related to the Software without their permission.
// The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
// notice is a summary of the XORP LICENSE file; the license in that file is
// legally binding.

// $XORP: xorp/fea/kernel_utils.hh,v 1.4 2006/03/16 00:03:57 pavlin Exp $

#ifndef __FEA_KERNEL_UTILS_HH__
#define __FEA_KERNEL_UTILS_HH__


#include "libxorp/xorp.h"
#include "libxorp/ipvx.hh"


//
// XXX: In case of KAME the local interface index (also the link-local
// scope_id) is encoded in the third and fourth octet of an IPv6
// address (for link-local unicast/multicast addresses or
// interface-local multicast addresses only).
// E.g., see the sa6_recoverscope() implementation inside file
// "sys/netinet6/scope6.c" on KAME-derived IPv6 stack.
//
#ifdef HAVE_IPV6
inline IPv6
kernel_ipv6_adjust(const IPv6& ipv6)
{
#ifdef IPV6_STACK_KAME
    in6_addr in6_addr;
    ipv6.copy_out(in6_addr);
    if (IN6_IS_ADDR_LINKLOCAL(&in6_addr)
	|| IN6_IS_ADDR_MC_LINKLOCAL(&in6_addr)
	|| IN6_IS_ADDR_MC_NODELOCAL(&in6_addr)) {
	in6_addr.s6_addr[2] = 0;
	in6_addr.s6_addr[3] = 0;
	return IPv6(in6_addr);
    }
#endif // IPV6_STACK_KAME
    return ipv6;
}
#endif // HAVE_IPV6

inline IPvX
kernel_ipvx_adjust(const IPvX& ipvx)
{
#ifndef HAVE_IPV6
    return ipvx;
#else
    if (! ipvx.is_ipv6())
	return ipvx;
    return (kernel_ipv6_adjust(ipvx.get_ipv6()));
#endif // HAVE_IPV6
}

#endif // __FEA_KERNEL_UTILS_HH__
