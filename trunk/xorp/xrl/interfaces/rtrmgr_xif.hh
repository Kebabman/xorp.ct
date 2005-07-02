/*
 * Copyright (c) 2001-2005 International Computer Science Institute
 * See LICENSE file for licensing, conditions, and warranties on use.
 *
 * DO NOT EDIT THIS FILE - IT IS PROGRAMMATICALLY GENERATED
 *
 * Generated by 'clnt-gen'.
 *
 * $XORP: xorp/xrl/interfaces/rtrmgr_xif.hh,v 1.11 2005/03/25 02:54:55 pavlin Exp $
 */

#ifndef __XRL_INTERFACES_RTRMGR_XIF_HH__
#define __XRL_INTERFACES_RTRMGR_XIF_HH__

#undef XORP_LIBRARY_NAME
#define XORP_LIBRARY_NAME "XifRtrmgr"

#include "libxorp/xlog.h"
#include "libxorp/callback.hh"

#include "libxipc/xrl.hh"
#include "libxipc/xrl_error.hh"
#include "libxipc/xrl_sender.hh"


class XrlRtrmgrV0p1Client {
public:
    XrlRtrmgrV0p1Client(XrlSender* s) : _sender(s) {}
    virtual ~XrlRtrmgrV0p1Client() {}

    typedef XorpCallback2<void, const XrlError&, const uint32_t*>::RefPtr GetPidCB;

    bool send_get_pid(
	const char*	target_name,
	const GetPidCB&	cb
    );

    typedef XorpCallback3<void, const XrlError&, const string*, const uint32_t*>::RefPtr RegisterClientCB;
    /**
     *  Send Xrl intended to:
     *
     *  Register a user and client process with the rtrmgr.
     *
     *  @param tgt_name Xrl Target name
     *
     *  @param clientname name of xrl entity supporting rtrmgr_client.xif
     *  methods.
     */
    bool send_register_client(
	const char*	target_name,
	const uint32_t&	userid,
	const string&	clientname,
	const RegisterClientCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr UnregisterClientCB;

    bool send_unregister_client(
	const char*	target_name,
	const string&	token,
	const UnregisterClientCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr AuthenticateClientCB;

    bool send_authenticate_client(
	const char*	target_name,
	const uint32_t&	userid,
	const string&	clientname,
	const string&	token,
	const AuthenticateClientCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr EnterConfigModeCB;

    bool send_enter_config_mode(
	const char*	target_name,
	const string&	token,
	const bool&	exclusive,
	const EnterConfigModeCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr LeaveConfigModeCB;

    bool send_leave_config_mode(
	const char*	target_name,
	const string&	token,
	const LeaveConfigModeCB&	cb
    );

    typedef XorpCallback2<void, const XrlError&, const XrlAtomList*>::RefPtr GetConfigUsersCB;

    bool send_get_config_users(
	const char*	target_name,
	const string&	token,
	const GetConfigUsersCB&	cb
    );

    typedef XorpCallback3<void, const XrlError&, const bool*, const string*>::RefPtr GetRunningConfigCB;

    bool send_get_running_config(
	const char*	target_name,
	const string&	token,
	const GetRunningConfigCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr ApplyConfigChangeCB;

    bool send_apply_config_change(
	const char*	target_name,
	const string&	token,
	const string&	target,
	const string&	deltas,
	const string&	deletions,
	const ApplyConfigChangeCB&	cb
    );

    typedef XorpCallback3<void, const XrlError&, const bool*, const uint32_t*>::RefPtr LockConfigCB;

    bool send_lock_config(
	const char*	target_name,
	const string&	token,
	const uint32_t&	timeout,
	const LockConfigCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr UnlockConfigCB;

    bool send_unlock_config(
	const char*	target_name,
	const string&	token,
	const UnlockConfigCB&	cb
    );

    typedef XorpCallback3<void, const XrlError&, const bool*, const uint32_t*>::RefPtr LockNodeCB;

    bool send_lock_node(
	const char*	target_name,
	const string&	token,
	const string&	node,
	const uint32_t&	timeout,
	const LockNodeCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr UnlockNodeCB;

    bool send_unlock_node(
	const char*	target_name,
	const string&	token,
	const string&	node,
	const UnlockNodeCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr SaveConfigCB;

    bool send_save_config(
	const char*	target_name,
	const string&	token,
	const string&	target,
	const string&	filename,
	const SaveConfigCB&	cb
    );

    typedef XorpCallback1<void, const XrlError&>::RefPtr LoadConfigCB;

    bool send_load_config(
	const char*	target_name,
	const string&	token,
	const string&	target,
	const string&	filename,
	const LoadConfigCB&	cb
    );

protected:
    XrlSender* _sender;

private:
    void unmarshall_get_pid(
	const XrlError&	e,
	XrlArgs*	a,
	GetPidCB		cb
    );

    void unmarshall_register_client(
	const XrlError&	e,
	XrlArgs*	a,
	RegisterClientCB		cb
    );

    void unmarshall_unregister_client(
	const XrlError&	e,
	XrlArgs*	a,
	UnregisterClientCB		cb
    );

    void unmarshall_authenticate_client(
	const XrlError&	e,
	XrlArgs*	a,
	AuthenticateClientCB		cb
    );

    void unmarshall_enter_config_mode(
	const XrlError&	e,
	XrlArgs*	a,
	EnterConfigModeCB		cb
    );

    void unmarshall_leave_config_mode(
	const XrlError&	e,
	XrlArgs*	a,
	LeaveConfigModeCB		cb
    );

    void unmarshall_get_config_users(
	const XrlError&	e,
	XrlArgs*	a,
	GetConfigUsersCB		cb
    );

    void unmarshall_get_running_config(
	const XrlError&	e,
	XrlArgs*	a,
	GetRunningConfigCB		cb
    );

    void unmarshall_apply_config_change(
	const XrlError&	e,
	XrlArgs*	a,
	ApplyConfigChangeCB		cb
    );

    void unmarshall_lock_config(
	const XrlError&	e,
	XrlArgs*	a,
	LockConfigCB		cb
    );

    void unmarshall_unlock_config(
	const XrlError&	e,
	XrlArgs*	a,
	UnlockConfigCB		cb
    );

    void unmarshall_lock_node(
	const XrlError&	e,
	XrlArgs*	a,
	LockNodeCB		cb
    );

    void unmarshall_unlock_node(
	const XrlError&	e,
	XrlArgs*	a,
	UnlockNodeCB		cb
    );

    void unmarshall_save_config(
	const XrlError&	e,
	XrlArgs*	a,
	SaveConfigCB		cb
    );

    void unmarshall_load_config(
	const XrlError&	e,
	XrlArgs*	a,
	LoadConfigCB		cb
    );

};

#endif /* __XRL_INTERFACES_RTRMGR_XIF_HH__ */
