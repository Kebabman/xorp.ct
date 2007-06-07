/*
 * Copyright (c) 2001-2007 International Computer Science Institute
 * See LICENSE file for licensing, conditions, and warranties on use.
 *
 * DO NOT EDIT THIS FILE - IT IS PROGRAMMATICALLY GENERATED
 *
 * Generated by 'tgt-gen'.
 *
 * $XORP: xorp/xrl/targets/ribclient_base.hh,v 1.19 2007/05/23 12:12:55 pavlin Exp $
 */


#ifndef __XRL_TARGETS_RIBCLIENT_BASE_HH__
#define __XRL_TARGETS_RIBCLIENT_BASE_HH__

#undef XORP_LIBRARY_NAME
#define XORP_LIBRARY_NAME "XrlRibclientTarget"

#include "libxorp/xlog.h"
#include "libxipc/xrl_cmd_map.hh"

class XrlRibclientTargetBase {
protected:
    XrlCmdMap* _cmds;

public:
    /**
     * Constructor.
     *
     * @param cmds an XrlCmdMap that the commands associated with the target
     *		   should be added to.  This is typically the XrlRouter
     *		   associated with the target.
     */
    XrlRibclientTargetBase(XrlCmdMap* cmds = 0);

    /**
     * Destructor.
     *
     * Dissociates instance commands from command map.
     */
    virtual ~XrlRibclientTargetBase();

    /**
     * Set command map.
     *
     * @param cmds pointer to command map to associate commands with.  This
     * argument is typically a pointer to the XrlRouter associated with the
     * target.
     *
     * @return true on success, false if cmds is null or a command map has
     * already been supplied.
     */
    bool set_command_map(XrlCmdMap* cmds);

    /**
     * Get Xrl instance name associated with command map.
     */
    const string& name() const { return _cmds->name(); }

    /**
     * Get version string of instance.
     */
    const char* version() const { return "ribclient/0.0"; }

protected:

    /**
     *  Pure-virtual function that needs to be implemented to:
     *
     *  Route Info Changed route_info_changed is called by the RIB on the RIB
     *  client (typically a routing protocol) that had registered an interest
     *  in the routing of an address. This can be because the metric and/or
     *  nexthop changed.
     *
     *  @param addr base address of the subnet that was registered
     *
     *  @param prefix_len prefix length of the subnet that was registered
     *
     *  @param metric the routing metric toward the address.
     *
     *  @param admin_distance the administratively defined distance toward the
     *  address.
     *
     *  @param protocol_origin the name of the protocol that originated this
     *  routing entry.
     */
    virtual XrlCmdError rib_client_0_1_route_info_changed4(
	// Input values,
	const IPv4&	addr,
	const uint32_t&	prefix_len,
	const IPv4&	nexthop,
	const uint32_t&	metric,
	const uint32_t&	admin_distance,
	const string&	protocol_origin) = 0;

    virtual XrlCmdError rib_client_0_1_route_info_changed6(
	// Input values,
	const IPv6&	addr,
	const uint32_t&	prefix_len,
	const IPv6&	nexthop,
	const uint32_t&	metric,
	const uint32_t&	admin_distance,
	const string&	protocol_origin) = 0;

    /**
     *  Pure-virtual function that needs to be implemented to:
     *
     *  Route Info Invalid route_info_invalid is called by the RIB on the RIB
     *  client (typically a routing protocol) that had registere d an interest
     *  in the routing of an address. This can be because the information
     *  previously reported as applying no longer applies for any number of
     *  reasons. When the RIB sends this message, it has automatically
     *  de-registered interest in the route, and the client will normally need
     *  to send a register_interest request again.
     */
    virtual XrlCmdError rib_client_0_1_route_info_invalid4(
	// Input values,
	const IPv4&	addr,
	const uint32_t&	prefix_len) = 0;

    virtual XrlCmdError rib_client_0_1_route_info_invalid6(
	// Input values,
	const IPv6&	addr,
	const uint32_t&	prefix_len) = 0;

private:
    const XrlCmdError handle_rib_client_0_1_route_info_changed4(const XrlArgs& in, XrlArgs* out);

    const XrlCmdError handle_rib_client_0_1_route_info_changed6(const XrlArgs& in, XrlArgs* out);

    const XrlCmdError handle_rib_client_0_1_route_info_invalid4(const XrlArgs& in, XrlArgs* out);

    const XrlCmdError handle_rib_client_0_1_route_info_invalid6(const XrlArgs& in, XrlArgs* out);

    void add_handlers();
    void remove_handlers();
};

#endif // __XRL_TARGETS_RIBCLIENT_BASE_HH__
