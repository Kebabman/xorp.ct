// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001,2002 International Computer Science Institute
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

#ident "$XORP: xorp/bgp/path_attribute.cc,v 1.11 2003/01/29 00:38:56 rizzo Exp $"

// #define DEBUG_LOGGING
#define DEBUG_PRINT_FUNCTION_NAME

#include "bgp_module.h"
#include "config.h"
#include "libxorp/debug.h"
#include "libxorp/xlog.h"

#include "path_attribute.hh"
#include "packet.hh"

#include <stdio.h>
#include <netinet/in.h>
#include <vector>

#ifdef DEBUG_LOGGING
inline static void
dump_bytes(const uint8_t* d, uint8_t l)
{
    for (uint16_t i = 0; i < l; i++) {
	debug_msg("%3u 0x%02x\n", i, d[i]);
    }
}
#else
inline static void dump_bytes(const uint8_t*, uint8_t) {}
#endif /* DEBUG_LOGGING */

/*
 * Flags values crib:
 *
 * Well-known mandatory: optional = false, transitive = true
 * Well-known discretionary: optional = false, transitive = true
 * Optional transitive: optional = true, transitive = true,
 * Optional nontransitive: optional = true, transitive = false
 *
 * Yes, you really can't tell the difference between well-known
 * mandatory and well-known discretionary from the flags.  They're
 * well-known, so you should know :-)
 */

void
PathAttribute::decode()
{
    debug_msg("PathAttribute::decode called\n");
    dump_bytes(_data, _length);
    const uint8_t *data = _data;
    set_flags(*data);

    data += 2;	// skip fixed header fields

    if (extended()) {
	debug_msg("Extended\n");
	_attribute_length = ntohs((uint16_t &)(*data));
	data += 2;
    } else {
	debug_msg("Not Extended\n");
	_attribute_length = (uint8_t &)(*data);
	data++;
    }
}

void
PathAttribute::add_hash(MD5_CTX *context)
{
    encode();
    MD5Update(context, _data, _length);
}

const uint8_t *
PathAttribute::encode_and_get_data()
{
    debug_msg("PathAttribute get_data() called\n");
    encode();
    return (const uint8_t *)_data;
}

void
PathAttribute::dump()
{
    debug_msg(str().c_str());
}

string
PathAttribute::str() const
{
    string s = "Path attribute of type ";
    switch (type()) {
    case ORIGIN:
	s += "ORIGIN";
	break;

    case AS_PATH:
	s += "AS_PATH";
	break;

    case NEXT_HOP:
	s += "NEXT_HOP";
	break;

    case MED:
	s += "MED";
	break;

    case LOCAL_PREF:
	s += "LOCAL_PREF";
	break;

    case ATOMIC_AGGREGATE:
	s += "ATOMIC_AGGREGATOR";
	break;

    case AGGREGATOR:
	s += "AGGREGATOR";
	break;

    case COMMUNITY:
	s += "COMMUNITY";
	break;
    default:
	s += c_format("UNKNOWN(%d)", type());
    }
    return s;
}

bool
PathAttribute::operator<(const PathAttribute& him) const
{
    debug_msg("Comparing %s %s\n", str().c_str(), him.str().c_str());

    if (sorttype() < him.sorttype())
	return true;
    if (sorttype() > him.sorttype())
	return false;
    switch (type()) {
    case ORIGIN:
	return ((OriginAttribute&)*this) < ((OriginAttribute&)him);
    case AS_PATH:
	return ((ASPathAttribute&)*this) < ((ASPathAttribute&)him);
    case NEXT_HOP:
	if (dynamic_cast<const NextHopAttribute<IPv4>*>(this) != NULL) {
	    return (((NextHopAttribute<IPv4>&)*this)
		    < ((NextHopAttribute<IPv4>&)him));
	} else {
	    return (((NextHopAttribute<IPv6>&)*this)
		    < ((NextHopAttribute<IPv6>&)him));
	}
    case MED:
	return ((MEDAttribute&)*this) < ((MEDAttribute&)him);
    case LOCAL_PREF:
	return ((LocalPrefAttribute&)*this) < ((LocalPrefAttribute&)him);
    case ATOMIC_AGGREGATE:
	return ((AtomicAggAttribute&)*this) < ((AtomicAggAttribute&)him);
    case AGGREGATOR:
	return ((AggregatorAttribute&)*this) < ((AggregatorAttribute&)him);
    case COMMUNITY:
	return ((CommunityAttribute&)*this) < ((CommunityAttribute&)him);
    case UNKNOWN:
	return ((UnknownAttribute&)*this) < ((UnknownAttribute&)him);
    }
    abort();
}

bool
PathAttribute::operator==(const PathAttribute& him) const
{
    if (type() != him.type())
	return false;

    switch (type()) {
    case ORIGIN:
	return ((OriginAttribute&)*this) == ((OriginAttribute&)him);
    case AS_PATH:
	return ((ASPathAttribute&)*this) == ((ASPathAttribute&)him);
    case NEXT_HOP:
	if (dynamic_cast<const NextHopAttribute<IPv4>*>(this) != NULL) {
	    return (((NextHopAttribute<IPv4>&)*this)
		    == ((NextHopAttribute<IPv4>&)him));
	} else {
	    return (((NextHopAttribute<IPv6>&)*this)
		    == ((NextHopAttribute<IPv6>&)him));
	}
    case MED:
	return ((MEDAttribute&)*this) == ((MEDAttribute&)him);
    case LOCAL_PREF:
	return ((LocalPrefAttribute&)*this) == ((LocalPrefAttribute&)him);
    case ATOMIC_AGGREGATE:
	return ((AtomicAggAttribute&)*this) == ((AtomicAggAttribute&)him);
    case AGGREGATOR:
	return ((AggregatorAttribute&)*this) == ((AggregatorAttribute&)him);
    case COMMUNITY:
	return ((CommunityAttribute&)*this) == ((CommunityAttribute&)him);
    case UNKNOWN:
	return ((UnknownAttribute&)*this) == ((UnknownAttribute&)him);
    }
    abort();
}

/* ************ OriginAttribute ************* */

OriginAttribute::OriginAttribute(OriginType t)
    : PathAttribute(Transitive), _origin(t)
{
    XLOG_ASSERT(t == IGP || t == EGP || t == INCOMPLETE);

    _length = 4;
    _data = new uint8_t[_length];
}

OriginAttribute::OriginAttribute(const OriginAttribute& att)
    : PathAttribute(att.flags())
{
    _origin = att._origin;
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
}

OriginAttribute::OriginAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(0)
{
    dump_bytes(d, l);

    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _length = l;
    _data = data;
    debug_msg("Origin length %d\n", l);
    decode();
}

//encode is const because we don't change the semantic value of the
// class instance - we only re-encode it.

void
OriginAttribute::encode()
{
    delete[] _data;

    _length = 4;	// this attribute has static size of 4 bytes.
    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = type();
    data[2] = 1; // length of data
    data[3] = _origin;
    _data = data;
}

void
OriginAttribute::decode()
{
    PathAttribute::decode();

    const uint8_t *d = _data;
    if (extended()) {
	d += 4;
    } else {
	d += 3;
    }

    _origin = (OriginType)(*d);
    debug_msg("Origin Attribute: ");
    switch (_origin) {
    case IGP:
	debug_msg("IGP\n");
	break;
    case EGP:
	debug_msg("EGP\n");
	break;
    case INCOMPLETE:
	debug_msg("INCOMPLETE\n");
	break;
    default:
	debug_msg("UNKNOWN (%d)\n", _origin);
	dump_bytes(d, _length);
	xorp_throw(CorruptMessage,
		   c_format("Unknown Origin Type %d", _origin),
		   UPDATEMSGERR, INVALORGATTR);
    }
    if (!well_known() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in Origin attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    debug_msg("Origin Attribute decode() finished \n");
}

void
OriginAttribute::add_hash(MD5_CTX *context)
{
    MD5Update(context, (const uint8_t*)&_origin, sizeof(_origin));
}

string
OriginAttribute::str() const
{
    string s = "Origin Path Attribute - ";
    switch (_origin) {
    case IGP:
	s += "IGP";
	break;
    case EGP:
	s += "EGP";
	break;
    case INCOMPLETE:
	s += "INCOMPLETE";
	break;
    default:
	s += "UNKNOWN";
    }
    return s;
}

#if 0
void
OriginAttribute::set_origintype(OriginType t)
{
    if (t == IGP || t == EGP || t == INCOMPLETE) {
	_origin = t;
    } else {
	debug_msg("Invalid Origin type (%d)\n", t);
	abort();
    }
}
#endif

/* ************ ASPathAttribute ************* */

ASPathAttribute::ASPathAttribute(const AsPath& init_as_path)
    : PathAttribute(Transitive), _as_path(init_as_path)
{
    _length = (uint8_t)0;
    _data = NULL;
}

ASPathAttribute::ASPathAttribute(const ASPathAttribute& att)
    : PathAttribute(att.flags()), _as_path(att._as_path)
{
    _as_path_len = att._as_path_len;
    _as_path_ordered = att._as_path_ordered;

    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
}

ASPathAttribute::ASPathAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    debug_msg("ASPathAttribute(%x, %d) constructor called\n",
	      (uint)d, l);
    uint8_t* data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
ASPathAttribute::encode()
{
    debug_msg("ASPathAttribute encode()\n");
    size_t l = _as_path.wire_length();
    if (l > 255)
	_flags |= Extended;
    size_t hl = extended() ? 4 : 3;	// header length
    delete[] _data;
    uint8_t *d = new uint8_t[l + hl];	// allocate a full buffer.

    _as_path.encode(l, d + hl);		// encode into the buffer
    debug_msg("ASPath length is %u\n", (uint32_t)l);
    debug_msg("Length %u\n", (uint32_t)l);

    d[0] = flags();
    d[1] = AS_PATH;

    if (extended()) {
	d[2] = (l >> 8) & 0xff;
	d[3] = l & 0xff;
    } else {
	d[2] = l & 0xff;
    }
    _length = l + hl;
    debug_msg("ASPath encode: length is %d\n", _length);
    _data = d;
}

void
ASPathAttribute::decode()
{
    PathAttribute::decode();
    debug_msg("AS Path Attribute:: decode\n");
    int temp_len;
    int as_len;

    const uint8_t *d = _data;
    int length = _attribute_length;

    if (extended()) {
	debug_msg("Extended AS Path!\n");
	d += 4;
    } else {
	d += 3;
    }

    while (length > 0) {
	dump_bytes(d, length);
	// XXX this assumes a 16 bit AS number
	as_len = d[1];	// number 
	temp_len = as_len*2+2;
	assert(temp_len <= length);

	debug_msg("temp_len %d, length %d, as_len %d\n",
		temp_len, length, as_len);
	int t = static_cast<ASPathSegType>(*d);
	switch (t) {
	case AS_SET:
	    debug_msg("Decoding AS Set\n");
	    _as_path.add_segment(AsSegment(d));
	    break;
	case AS_SEQUENCE:
	    debug_msg("Decoding AS Sequence\n");
	    _as_path.add_segment(AsSegment(d));
	    break;
	default:
	    debug_msg("Wrong sequence type %d\n", t);
	    xorp_throw(CorruptMessage,
		   c_format("Wrong sequence type %d", t),
		       UPDATEMSGERR, MALASPATH);
	}
	length = length - temp_len;
	d += temp_len; // 2 bytes of header
    }
    if (!well_known() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in AS Path attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    debug_msg("after: >%s<\n", _as_path.str().c_str());
}

string
ASPathAttribute::str() const
{
    string s = "AS Path Attribute " + _as_path.str();
    return s;
}

/* ************ NextHopAttribute ************* */

template <class A>
NextHopAttribute<A>::NextHopAttribute<A>(const A& nh)
    :  PathAttribute(Transitive), _next_hop(nh)
{
    _length = 3 + A::addr_size();
    _data = new uint8_t[_length];
}

template <class A>
NextHopAttribute<A>::NextHopAttribute<A>(const NextHopAttribute<A>& att)
    : PathAttribute(att.flags())
{
    _next_hop = att._next_hop;
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
}

template <class A>
NextHopAttribute<A>::NextHopAttribute<A>(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
NextHopAttribute<IPv4>::encode()
{
    delete[] _data;

    // this attribute has static size of 7 bytes
    _length = 7;
    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = NEXT_HOP;
    data[2] = 4; // length of data
    _next_hop.copy_out(data+3);
    _data = data;
}

void
NextHopAttribute<IPv6>::encode()
{
    // XXXX A true nexthop attribute in BGP can only be IPv4.  Need to
    // figure out the IPv6 stuff later.
    abort();
}

template <class A>
void
NextHopAttribute<A>::decode()
{
    PathAttribute::decode();

    const uint8_t *d = _data;
    if (extended()) {
	d += 4;
    } else {
	d += 3;
    }
    if (!well_known() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in NextHop attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    _next_hop = A(d);

    // check that the nexthop address is syntactically correct
    if (!_next_hop.is_unicast()) {
	xorp_throw(CorruptMessage,
		   c_format("NextHop %s is not a unicast address",
			    _next_hop.str().c_str()),
		   UPDATEMSGERR, INVALNHATTR,
		   data(), size())
    }
    debug_msg("Next Hop Attribute \n");
}

template <class A>
void
NextHopAttribute<A>::add_hash(MD5_CTX *context) 
{
    MD5Update(context, (const uint8_t*)&_next_hop, sizeof(_next_hop));
}

template <class A>
string
NextHopAttribute<A>::str() const
{
    string s = "Next Hop Attribute " + _next_hop.str();
    return s;
}


template class NextHopAttribute<IPv4>;
template class NextHopAttribute<IPv6>;


/* ************ MEDAttribute ************* */

MEDAttribute::MEDAttribute(uint32_t med)
    : PathAttribute(Optional), _multiexitdisc(med)
{
    _length = 7;
    _data = 0;
}

MEDAttribute::MEDAttribute(const MEDAttribute& att)
    : PathAttribute(att.flags()), _multiexitdisc(att._multiexitdisc)
{
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    } else {
	_length = 0;
	_data = NULL;
    }
    _attribute_length = att._attribute_length;
}

MEDAttribute::MEDAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _length = l;
    _data = data;
    decode();
}

void
MEDAttribute::encode()
{
    delete[] _data;

    // this attribute has static size of 7 bytes
    _length = 7;
    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = MED;
    data[2] = 4; // length of data
    uint32_t med = htonl(_multiexitdisc);
    memcpy(data+3, &med, 4);
    _data = data;
}

void
MEDAttribute::decode()
{
    PathAttribute::decode();

    const uint8_t *d = _data;
    if (extended()) {
	d += 4;
    } else {
	d += 3;
    }
    if (!optional() || transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in MED attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    debug_msg("Multi_exit discriminator Attribute\n");
    uint32_t med;
    memcpy(&med, d, 4);
    _multiexitdisc = htonl(med);
    debug_msg("Multi_exit desc %d\n", _multiexitdisc);
}

string
MEDAttribute::str() const
{
    return c_format("Multiple Exit Descriminator Attribute: MED=%d",
		    _multiexitdisc);
}
/* ************ LocalPrefAttribute ************* */

LocalPrefAttribute::LocalPrefAttribute(uint32_t localpref)
    : PathAttribute(Transitive), _localpref(localpref)
{
    //    _type = LOCAL_PREF;
    _length = 7;
    _data = NULL;
}

LocalPrefAttribute::LocalPrefAttribute(const LocalPrefAttribute& att)
    : PathAttribute(att.flags()), _localpref(att._localpref)
{
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
}

LocalPrefAttribute::LocalPrefAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
LocalPrefAttribute::encode()
{
    delete[] _data;

    // this attribute has static size of 7 bytes
    _length = 7;
    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = LOCAL_PREF;
    data[2] = 4; // length of data
    debug_msg("Encoding Local Pref of %u\n", _localpref);
    uint32_t localpref = htonl(_localpref);
    memcpy(data+3, &localpref, 4);

    _data = data;
}

void
LocalPrefAttribute::decode()
{
    PathAttribute::decode();

    const uint8_t *d = _data;
    if (extended()) {
	d += 4;
    } else {
	d += 3;
    }
    if (!well_known() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in LocalPref attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    debug_msg("Local Preference Attribute\n");
    uint32_t localpref;
    memcpy(&localpref, d, 4);
    _localpref = ntohl(localpref);
    debug_msg("Local Preference Attribute %d\n", _localpref);
}

string
LocalPrefAttribute::str() const
{
    return c_format("Local Preference Attribute - %d", _localpref);
}

/* ************ AtomicAggAttribute ************* */

AtomicAggAttribute::AtomicAggAttribute()
    : PathAttribute(Transitive)
{
    _data = 0;
    _length = 3;
}


AtomicAggAttribute::AtomicAggAttribute(const AtomicAggAttribute& att)
    : PathAttribute(att.flags())
{
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
}

AtomicAggAttribute::AtomicAggAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
AtomicAggAttribute::encode()
{
    delete[] _data;

    // this attribute has static size of 3 bytes
    _length = 3;
    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = ATOMIC_AGGREGATE;
    data[2] = 0;
    _data = data;
}

void
AtomicAggAttribute::decode()
{
    PathAttribute::decode();
    if (!well_known() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in AtomicAggregate attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    debug_msg("Atomic Aggregate Attribute\n");
}

string
AtomicAggAttribute::str() const
{
    string s;
    s = "Atomic Aggregator Attribute";
    return s;
}
/* ************ AggregatorAttribute ************* */

AggregatorAttribute::AggregatorAttribute(const IPv4& routeaggregator,
		       const AsNum& aggregatoras)
    : PathAttribute(Optional | Transitive),
      _routeaggregator(routeaggregator), _aggregatoras(aggregatoras)
{
    _length = 9;
    _data = NULL;
}

AggregatorAttribute::AggregatorAttribute(const AggregatorAttribute& att)
    : PathAttribute(att.flags()),
    _routeaggregator(att._routeaggregator),
    _aggregatoras(att._aggregatoras)
{
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
}

AggregatorAttribute::AggregatorAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags), _aggregatoras(AsNum::AS_INVALID)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
AggregatorAttribute::encode()
{
    delete[] _data;

    // this attribute has static size of 9 bytes;
    _length = 9;
    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = AGGREGATOR;
    data[2] = 6;
    _aggregatoras.copy_out(data+3);
    // This defined to be an IPv4 address in RFC 2858 (line 46)
    _routeaggregator.copy_out(data+5);
    _data = data;
}

void
AggregatorAttribute::decode()
{
    PathAttribute::decode();

    debug_msg("Aggregator Attribute\n");

    const uint8_t *d = _data;
    if (extended()) {
	d += 4;
    } else {
	d += 3;
    }
    // The internet draft sets this AS Number to be 2 octets (section 4.3)
    _aggregatoras = AsNum(d);
    _routeaggregator = IPv4(d+2);
    if (!optional() || !transitive())
	xorp_throw(CorruptMessage,
		   "Bad Flags in Aggregator attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
}

string AggregatorAttribute::str() const
{
    string s = "Aggregator Attribute " + _aggregatoras.str() + " "
		+ _routeaggregator.str();
    return s;
}


/* ************ CommunityAttribute ************* */

CommunityAttribute::CommunityAttribute()
    : PathAttribute(Optional | Transitive)
{
}


CommunityAttribute::CommunityAttribute(const CommunityAttribute& att)
    : PathAttribute(att.flags())
{
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
    set <uint32_t>::const_iterator iter = att._communities.begin();
    while (iter != att._communities.end()) {
	_communities.insert(*iter);
	++iter;
    }
}

CommunityAttribute::CommunityAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
CommunityAttribute::add_community(uint32_t community)
{
    _communities.insert(community);
}

void
CommunityAttribute::encode()
{
    delete[] _data;

    _length = 3 + (4 * _communities.size());
    assert(_length < 256);

    uint8_t *data = new uint8_t[_length];
    data[0] = flags();
    data[1] = COMMUNITY;
    data[2] = 4*_communities.size();
    set <uint32_t>::const_iterator iter = _communities.begin();
    uint8_t *current = data + 3;
    while (iter != _communities.end()) {
	uint32_t value;
	value = htonl(*iter);
	memcpy(current, &value, 4);
	current += 4;
	++iter;
    }
    _data = data;
}

void
CommunityAttribute::decode()
{
    PathAttribute::decode();
    const uint8_t *d = _data;
    if (extended()) {
	uint16_t len;
	memcpy(&len, d+2, 2);
	_length = htons(len);
	d += 4;
    } else {
	_length = d[2];
	d += 3;
    }
    if (!optional() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in Community attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    int remaining = _length;
    while (remaining > 0) {
	uint32_t value;
	memcpy(&value, d, 4);
	_communities.insert(htonl(value));
	d += 4;
	remaining -= 4;
    }

    debug_msg("Community Attribute\n");
}

string
CommunityAttribute::str() const
{
    string s = "Community Attribute ";
    set <uint32_t>::const_iterator iter = _communities.begin();
    while (iter != _communities.end()) {
	s += c_format("%u ", *iter);
	++iter;
    }

    return s;
}

bool
CommunityAttribute::operator<(const CommunityAttribute& him) const
{
    if (_communities.size() < him.community_set().size())
	return true;
    if (him.community_set().size() < _communities.size())
	return false;
    set <uint32_t>::const_iterator his_i, my_i;
    my_i = _communities.begin();
    his_i = him.community_set().begin();
    while (my_i != _communities.end()) {
	if (*my_i < *his_i)
	    return true;
	if (*his_i < *my_i)
	    return false;
	his_i++;
	my_i++;
    }
    return false;
}

bool
CommunityAttribute::operator==(const CommunityAttribute& him) const
{
    if (_communities.size() != him.community_set().size())
	return false;
    if (_communities == him.community_set())
	return true;
    return false;
}

/* ************ UnknownAttribute ************* */

UnknownAttribute::UnknownAttribute()
    : PathAttribute(Optional | Transitive)
{
}


UnknownAttribute::UnknownAttribute(const UnknownAttribute& att)
    : PathAttribute(att.flags())
{
    if (att._data != NULL) {
	_length = att._length;
	uint8_t *p = new uint8_t[_length];
	memcpy(p, att._data, _length);
	_data = p;
    }
    _attribute_length = att._attribute_length;
    _type = att._type;
}

UnknownAttribute::UnknownAttribute(const uint8_t* d, uint16_t l)
    : PathAttribute(NoFlags)
{
    uint8_t *data = new uint8_t[l];
    memcpy(data, d, l);
    _data = data;
    _length = l;
    decode();
}

void
UnknownAttribute::encode()
{
}

void
UnknownAttribute::decode()
{
    PathAttribute::decode();
    const uint8_t *d = _data;
    _type = d[1];

    switch (_type) {
    case ORIGIN:
    case AS_PATH:
    case NEXT_HOP:
    case MED:
    case LOCAL_PREF:
    case ATOMIC_AGGREGATE:
    case AGGREGATOR:
    case COMMUNITY:
	//we got called by mistake
	abort();
    default:
	break;
    }

    if (extended()) {
	uint16_t len;
	memcpy(&len, d+2, 2);
	_length = htons(len);
	d += 4;
    } else {
	_length = d[2];
	d += 3;
    }
    if (!optional() || !transitive()) {
	xorp_throw(CorruptMessage,
		   "Bad Flags in Unknown attribute",
		   UPDATEMSGERR, ATTRFLAGS,
		   data(), size());
    }
    debug_msg("Unknown Attribute\n");
}

string
UnknownAttribute::str() const
{
    string s = "Unknown Attribute ";
    for (int i=0; i< _length; i++) {
	s += c_format("%x ", _data[i]);
    }
    return s;
}

bool
UnknownAttribute::operator<(const UnknownAttribute& him) const
{
    if (size() < him.size())
	return true;
    if (size() > him.size())
	return false;

    //lengths are equal
    if (memcmp(data(), him.data(), size())<0)
	return true;
    return false;
}

bool
UnknownAttribute::operator==(const UnknownAttribute& him) const
{
    return (size() == him.size() &&
	    (memcmp(data(), him.data(), size()) == 0) );
}

