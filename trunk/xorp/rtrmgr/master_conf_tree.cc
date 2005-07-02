// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001-2005 International Computer Science Institute
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

#ident "$XORP: xorp/rtrmgr/master_conf_tree.cc,v 1.47 2005/03/25 02:54:35 pavlin Exp $"


#include <sys/stat.h>
#include <grp.h>

#include "rtrmgr_module.h"

#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"
#include "libxorp/utils.hh"

#include "master_conf_tree.hh"
#include "module_command.hh"
#include "rtrmgr_error.hh"
#include "template_commands.hh"
#include "master_template_tree.hh"
#include "master_template_tree_node.hh"
#include "util.hh"

//
// The strings that are used to add and delete a load or save file, to
// access the content of the loaded file, etc.
//
// XXX: sigh, hard-coding the strings here is bad...
static string RTRMGR_CONFIG_NODE = "rtrmgr";
static string RTRMGR_CONFIG = "rtrmgr {\n}\n";
static string RTRMGR_SAVE_FILE_CONFIG = "rtrmgr {\n    save %s\n}\n";
static string RTRMGR_LOAD_FILE_CONFIG = "rtrmgr {\n    load %s\n}\n";
static string RTRMGR_CONFIG_FILENAME_VARNAME = "$(rtrmgr.CONFIG_FILENAME)";


/*************************************************************************
 * Master Config Tree class
 *************************************************************************/

MasterConfigTree::MasterConfigTree(const string& config_file,
				   MasterTemplateTree* tt,
				   ModuleManager& mmgr,
				   XorpClient& xclient,
				   bool global_do_exec,
				   bool verbose) throw (InitError)
    : ConfigTree(tt, verbose),
      _root_node(verbose),
      _commit_in_progress(false),
      _config_failed(false),
      _rtrmgr_config_node_found(false)
{
    _current_node = &_root_node;
    _task_manager = new TaskManager(*this, mmgr, xclient, 
				    global_do_exec, verbose);

    string configuration;
    string errmsg;

    if (read_file(configuration, config_file, errmsg) != true) {
	xorp_throw(InitError, errmsg);
    }

    if (parse(configuration, config_file, errmsg) != true) {
	xorp_throw(InitError, errmsg);
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    add_default_children();

    if (root_node().check_config_tree(errmsg) != true) {
	xorp_throw(InitError, errmsg);
    }

    //
    // If we got this far, it looks like we have a good bootfile.
    // The bootfile is now stored in the ConfigTree.
    // Now do the second pass, starting processes and configuring things.
    //
    execute();
}

MasterConfigTree::MasterConfigTree(TemplateTree* tt, bool verbose)
    : ConfigTree(tt, verbose),
      _root_node(verbose),
      _task_manager(NULL)
{
    _current_node = &_root_node;
}

MasterConfigTree::~MasterConfigTree()
{
    delete _task_manager;
}

MasterConfigTree&
MasterConfigTree::operator=(const MasterConfigTree& orig_tree)
{
    master_root_node().clone_subtree(orig_tree.const_master_root_node());
    return *this;
}

ConfigTree* MasterConfigTree::create_tree(TemplateTree *tt, bool verbose)
{
    MasterConfigTree *mct;
    mct = new MasterConfigTree(tt, verbose);
    return mct;
}


bool
MasterConfigTree::read_file(string& configuration,
			    const string& config_file,
			    string& errmsg)
{
    FILE* file = fopen(config_file.c_str(), "r");

    if (file == NULL) {
	errmsg = c_format("Failed to open config file: %s\n",
			  config_file.c_str());
	return false;
    }
    static const uint32_t MCT_READBUF = 8192;
    char buf[MCT_READBUF + 1];

    while (feof(file) == 0) {
	size_t bytes = fread(buf, 1, sizeof(buf) - 1, file);
	// NUL terminate it.
	buf[bytes] = '\0';
	if (bytes > 0) {
	    configuration += buf;
	}
    }
    fclose(file);
    return true;
}

bool
MasterConfigTree::parse(const string& configuration,
			const string& config_file,
			string& errmsg)
{
    if (ConfigTree::parse(configuration, config_file, errmsg) != true)
	return false;

    string s = show_tree();
    debug_msg("== MasterConfigTree::parse yields ==\n%s\n"
	      "====================================\n", s.c_str());

    return true;
}

void
MasterConfigTree::execute()
{
    debug_msg("##############################################################\n");
    debug_msg("MasterConfigTree::execute\n");

    list<string> changed_modules = find_changed_modules();
    list<string>::const_iterator iter;
    string s;
    for (iter = changed_modules.begin();
	 iter != changed_modules.end();
	 ++iter) {
	if (! s.empty())
	    s += ", ";
	s += (*iter);
    }
    XLOG_INFO("Changed modules: %s", s.c_str());

    _commit_cb = callback(this, &MasterConfigTree::config_done);
    commit_changes_pass2();
}

void
MasterConfigTree::config_done(bool success, string errmsg)
{
    if (success)
	debug_msg("Configuration done: success\n");
    else
	XLOG_ERROR("Configuration failed: %s", errmsg.c_str());

    _config_failed = !success;

    if (! success) {
	_config_failed = true;
	_config_failed_msg = errmsg;
	return;
    }

    string errmsg2;
    if (check_commit_status(errmsg2) == false) {
	XLOG_ERROR("%s", errmsg2.c_str());
	_config_failed = true;
	_config_failed_msg = errmsg2;
	return;
    }
    debug_msg("MasterConfigTree::config_done returning\n");
}

ConfigTreeNode*
MasterConfigTree::create_node(const string& segment, const string& path,
			      const TemplateTreeNode* ttn, 
			      ConfigTreeNode* parent_node, 
			      uid_t user_id, bool verbose)
{
    MasterConfigTreeNode *ctn, *parent;
    parent = dynamic_cast<MasterConfigTreeNode *>(parent_node);
    if (parent_node != NULL)
	XLOG_ASSERT(parent != NULL);
    ctn = new MasterConfigTreeNode(segment, path, ttn, parent, 
				   user_id, verbose);
    return reinterpret_cast<ConfigTreeNode*>(ctn);
}

list<string>
MasterConfigTree::find_changed_modules() const
{
    debug_msg("Find changed modules\n");

    set<string> changed_modules;
    const_master_root_node().find_changed_modules(changed_modules);

    list<string> ordered_modules;
    order_module_list(changed_modules, ordered_modules);

    return ordered_modules;
}

list<string>
MasterConfigTree::find_active_modules() const
{
    debug_msg("Find active modules\n");

    set<string> active_modules;
    const_master_root_node().find_active_modules(active_modules);

    list<string> ordered_modules;
    order_module_list(active_modules, ordered_modules);

    return ordered_modules;
}

list<string>
MasterConfigTree::find_inactive_modules() const
{
    debug_msg("Find inactive modules\n");

    set<string> all_modules;
    list<string> ordered_all_modules;

    debug_msg("All modules:\n");
    const_master_root_node().find_all_modules(all_modules);
    order_module_list(all_modules, ordered_all_modules);

    set<string> active_modules;
    list<string> ordered_active_modules;
    debug_msg("Active modules:\n");
    master_root_node().find_active_modules(active_modules);
    order_module_list(active_modules, ordered_active_modules);

    // Remove things that are common to both lists
    while (!ordered_active_modules.empty()) {
	list<string>::iterator iter;
	bool found = false;
	for (iter = ordered_all_modules.begin();
	     iter != ordered_all_modules.end();
	     ++iter) {
	    if (*iter == ordered_active_modules.front()) {
		ordered_all_modules.erase(iter);
		ordered_active_modules.pop_front();
		found = true;
		break;
	    }
	}
	XLOG_ASSERT(found == true);
    }

    debug_msg("Inactive Module Order: ");
    list<string>::const_iterator final_iter;
    for (final_iter = ordered_all_modules.begin();
	 final_iter != ordered_all_modules.end();
	 ++final_iter) {
	debug_msg("%s ", final_iter->c_str());
    }
    debug_msg("\n");

    return ordered_all_modules;
}

void
MasterConfigTree::order_module_list(const set<string>& module_set,
				    list<string>& ordered_modules) const
{
    multimap<string, string> depends;	// first depends on second
    set<string> no_info;		// modules we found no info about
    set<string> satisfied;		// modules we found no info about
    set<string> additional_modules;	// modules that we depend on that
					// aren't in module_set

    //
    // We've found the list of modules that have changed that need to
    // be applied.  Now we need to sort them so that the modules to be
    // performed first are at the beginning of the list.
    //

    // Handle the degenerate cases simply
    if (module_set.empty())
	return;

    set<string>::const_iterator iter;
    for (iter = module_set.begin(); iter != module_set.end(); ++iter) {
	ModuleCommand* mc = _template_tree->find_module(*iter);

	if (mc == NULL) {
	    no_info.insert(*iter);
	    debug_msg("%s has no module info\n", (*iter).c_str());
	    continue;
	}
	if (mc->depends().empty()) {
	    //
	    // This module doesn't depend on anything else, so it can
	    // be started early in the sequence.
	    //
	    ordered_modules.push_back(*iter);
	    satisfied.insert(*iter);
	    debug_msg("%s has no dependencies\n", (*iter).c_str());
	    continue;
	}
	list<string>::const_iterator di;
	for (di = mc->depends().begin(); di != mc->depends().end(); ++di) {
	    debug_msg("%s depends on %s\n", iter->c_str(), di->c_str());
	    depends.insert(pair<string,string>(*iter, *di));
	    // Check that the dependency is already in our list of modules.
	    if (module_set.find(*di)==module_set.end()) {
		// If not, add it.
		additional_modules.insert(*di);
	    }
	}
    }

    debug_msg("doing additional modules\n");
    // Figure out the dependencies for all the additional modules
    set<string> additional_done;
    while (!additional_modules.empty()) {
	iter = additional_modules.begin();
	ModuleCommand* mc = _template_tree->find_module(*iter);
	if (mc == NULL) {
	    debug_msg("%s has no info\n", (*iter).c_str());
	    additional_done.insert(*iter);
	    additional_modules.erase(iter);
	    ordered_modules.push_back(*iter);
	    satisfied.insert(*iter);
	    continue;
	}
	if (mc->depends().empty()) {
	    debug_msg("%s has no dependencies\n", (*iter).c_str());
	    additional_done.insert(*iter);
	    ordered_modules.push_back(*iter);
	    satisfied.insert(*iter);
	    additional_modules.erase(iter);
	    continue;
	}

	list<string>::const_iterator di;
	for (di = mc->depends().begin(); di != mc->depends().end(); ++di) {
	    depends.insert(pair<string,string>(*iter, *di));
	    debug_msg("%s depends on %s\n", iter->c_str(), di->c_str());
	    // Check that the dependency is already in our list of modules.
	    if (module_set.find(*di) == module_set.end()
		&& additional_modules.find(*di) == additional_modules.end()
		&& additional_done.find(*di) == additional_done.end()) {
		// If not, add it.
		additional_modules.insert(*di);
	    }
	}
	additional_done.insert(*iter);
	additional_modules.erase(iter);
    }
    debug_msg("done additional modules\n");

    multimap<string,string>::iterator curr_iter, next_iter;
    while (!depends.empty()) {
	bool progress_made = false;
	curr_iter = depends.begin();
	while (curr_iter != depends.end()) {
	    next_iter = curr_iter;
	    ++next_iter;
	    debug_msg("searching for dependency for %s on %s\n",
		   curr_iter->first.c_str(), curr_iter->second.c_str());
	    if (satisfied.find(curr_iter->second) != satisfied.end()) {
		// Rule is now satisfied.
		string module = curr_iter->first;
		depends.erase(curr_iter);
		progress_made = true;
		debug_msg("dependency of %s on %s satisfied\n",
		       module.c_str(), curr_iter->second.c_str());
		if (depends.find(module) == depends.end()) {
		    // This was the last dependency
		    satisfied.insert(module);
		    ordered_modules.push_back(module);
		    debug_msg("dependencies for %s now satisfied\n",
			   module.c_str());
		}
	    }
	    curr_iter = next_iter;
	}
	if (progress_made == false) {
	    XLOG_FATAL("Module dependencies cannot be satisfied");
	}
    }

    // finally add the modules for which we have no information
    for (iter = no_info.begin(); iter != no_info.end(); ++iter) {
	ordered_modules.push_back(*iter);
    }

    debug_msg("Module Order: ");
    list<string>::const_iterator final_iter;
    for (final_iter = ordered_modules.begin();
	 final_iter != ordered_modules.end();
	 ++final_iter) {
	debug_msg("%s ", final_iter->c_str());
    }
    debug_msg("\n");
}

void
MasterConfigTree::commit_changes_pass1(CallBack cb)
{
    string result;

    debug_msg("##############################################################\n");
    debug_msg("MasterConfigTree::commit_changes_pass1\n");

    _commit_in_progress = true;

    list<string> changed_modules = find_changed_modules();
    list<string> inactive_modules = find_inactive_modules();
    list<string>::const_iterator iter;
    debug_msg("Changed modules:\n");
    for (iter = changed_modules.begin();
	 iter != changed_modules.end();
	 ++iter) {
	debug_msg("%s ", (*iter).c_str());
    }
    debug_msg("\n");

    //
    // Two passes: the first checks for errors.  If no errors are
    // found, attempt the actual commit.
    //

    /*******************************************************************/
    /* Pass 1: check for errors without actually doing anything        */
    /*******************************************************************/

    _task_manager->reset();
    _task_manager->set_do_exec(false);
    _commit_cb = cb;

    master_root_node().initialize_commit();

    if (root_node().check_config_tree(result) == false) {
	// Something went wrong - return the error message.
	_commit_in_progress = false;
	cb->dispatch(false, result);
	return;
    }

    // Sort the changes in order of module dependencies
    for (iter = changed_modules.begin();
	 iter != changed_modules.end();
	 ++iter) {
	if (!module_config_start(*iter, result)) {
	    _commit_in_progress = false;
	    cb->dispatch(false, result);
	    return;
	}
    }

    bool needs_update = false;
    if (master_root_node().commit_changes(*_task_manager,
					  /* do_commit = */ false,
					  0, 0,
					  result,
					  needs_update)
	== false) {
	// Something went wrong - return the error message.
	_commit_in_progress = false;
	cb->dispatch(false, result);
	return;
    }

#if 0
    //
    // XXX: don't shutdown any modules yet, because in this stage
    // we are checking only for errors.
    //
    for (iter = inactive_modules.begin();
	 iter != inactive_modules.end(); ++iter) {
	_task_manager->shutdown_module(*iter);
    }
#endif // 0

    _task_manager->run(callback(this, &MasterConfigTree::commit_pass1_done));
}

void
MasterConfigTree::commit_pass1_done(bool success, string result)
{
    debug_msg("##############################################################\n");
    debug_msg("## commit_pass1_done\n");

    if (success) {
	commit_changes_pass2();
    } else {
	string msg = "Commit pass 1 failed: " + result;
	XLOG_ERROR("%s", msg.c_str());
	_commit_cb->dispatch(false, result);
    }
}

void
MasterConfigTree::commit_changes_pass2()
{
    string result;

    debug_msg("##############################################################\n");
    debug_msg("## commit_changes_pass2\n");

    _commit_in_progress = true;

    if (root_node().check_config_tree(result) == false) {
	XLOG_ERROR("Configuration tree error: %s", result.c_str());
	_commit_in_progress = false;
	_commit_cb->dispatch(false, result);
	return;
    }

    /*******************************************************************/
    /* Pass 2: implement the changes                                   */
    /*******************************************************************/

    list<string> changed_modules = find_changed_modules();
    list<string> inactive_modules = find_inactive_modules();
    list<string>::const_iterator iter;

    _task_manager->reset();
    _task_manager->set_do_exec(true);

    master_root_node().initialize_commit();
    // Sort the changes in order of module dependencies
    for (iter = changed_modules.begin();
	 iter != changed_modules.end();
	 ++iter) {
	if (!module_config_start(*iter, result)) {
	    XLOG_ERROR("Commit failed in deciding startups");
	    _commit_in_progress = false;
	    _commit_cb->dispatch(false, result);
	    return;
	}
    }

    bool needs_update = false;
    if (!master_root_node().commit_changes(*_task_manager,
				   /* do_commit = */ true,
				   0, 0,
				   result,
				   needs_update)) {
	// Abort the commit
	XLOG_ERROR("Commit failed in config tree");
	_commit_in_progress = false;
	_commit_cb->dispatch(false, result);
	return;
    }

    for (iter = inactive_modules.begin();
	 iter != inactive_modules.end();
	 ++iter) {
	_task_manager->shutdown_module(*iter);
    }

    _task_manager->run(callback(this, &MasterConfigTree::commit_pass2_done));
}

void
MasterConfigTree::commit_pass2_done(bool success, string result)
{
    debug_msg("##############################################################\n");
    debug_msg("## commit_pass2_done\n");

    if (success)
	debug_msg("## commit seems successful\n");
    else
	XLOG_ERROR("Commit failed: %s", result.c_str());

    _commit_cb->dispatch(success, result);
    _commit_in_progress = false;
}

bool
MasterConfigTree::check_commit_status(string& result)
{
    debug_msg("check_commit_status\n");
    bool success = master_root_node().check_commit_status(result);

    if (success) {
	// If the commit was successful, clear all the temporary state.
	debug_msg("commit was successful, finalizing...\n");
	master_root_node().finalize_commit();
	debug_msg("finalizing done\n");
    }
    return success;
}

string
MasterConfigTree::discard_changes()
{
    debug_msg("##############################################################\n");
    debug_msg("MasterConfigTree::discard_changes\n");
    string result = root_node().discard_changes(0, 0);
    debug_msg("##############################################################\n");
    return result;
}

string
MasterConfigTree::mark_subtree_for_deletion(const list<string>& path_segments,
					    uid_t user_id)
{
    ConfigTreeNode *found = find_node(path_segments);

    if (found == NULL)
	return "ERROR";

    if ((found->parent() != NULL)
	&& found->parent()->is_tag()
	&& found->parent()->children().size()==1) {
	found = found->parent();
    }

    found->mark_subtree_for_deletion(user_id);
    return string("OK");
}

void
MasterConfigTree::delete_entire_config()
{
    root_node().mark_subtree_for_deletion(0);
    root_node().undelete();

    // We don't need a verification pass - this isn't allowed to fail.
    commit_changes_pass2();
}

bool
MasterConfigTree::lock_node(const string& /* node */, uid_t /* user_id */,
			    uint32_t /* timeout */,
			    uint32_t& /* holder */)
{
    // TODO: XXX: not implemented yet
    return true;
}

bool
MasterConfigTree::unlock_node(const string& /* node */, uid_t /* user_id */)
{
    // TODO: XXX: not implemented yet
    return true;
}

bool
MasterConfigTree::save_to_file(const string& filename,
			       uid_t user_id,
			       const string& save_hook,
			       string& errmsg)
{
    errmsg = "";

    //
    // TODO: there are lots of hard-coded values below. Fix this!
    //

    // XXX: set the effective group to "xorp"
    struct group* grp = getgrnam("xorp");
    if (grp == NULL) {
	errmsg = "ERROR: config files are saved as group \"xorp\".";
	errmsg = "Group \"xorp\" does not exist on this system.\n";
	return false;
    }

    gid_t gid, orig_gid;
    orig_gid = getgid();
    gid = grp->gr_gid;
    if (setegid(gid) < 0) {
	errmsg = c_format("Failed to seteuid to group \"xorp\", gid %u\n",
			  XORP_UINT_CAST(gid));
	return false;
    }

    // Set effective user ID to the uid of the user that requested the save
    uid_t orig_uid;
    orig_uid = getuid();
    if (seteuid(user_id) < 0) {
	errmsg = c_format("Failed to seteuid to uid %u\n",
			  XORP_UINT_CAST(user_id));
	setegid(orig_gid);
	return false;
    }

    // Set a umask of 664, to allow sharing of config files between
    // users in group "xorp".
    mode_t orig_mask = umask(S_IWOTH);

    FILE* file;
    struct stat sb;
    if (stat(filename.c_str(), &sb) == 0) {
	if (sb.st_mode & S_IFREG == 0) {
	    if (((sb.st_mode & S_IFMT) == S_IFCHR) ||
		((sb.st_mode & S_IFMT) == S_IFBLK)) {
		errmsg = c_format("File %s is a special device.\n",
				  filename.c_str());
	    } else if ((sb.st_mode & S_IFMT) == S_IFIFO) {
		errmsg = c_format("File %s is a named pipe.\n",
				  filename.c_str());
	    } else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
		errmsg = c_format("File %s is a directory.\n",
				  filename.c_str());
	    }
	    seteuid(orig_uid);
	    setegid(orig_gid);
	    umask(orig_mask);
	    return false;
	}

	file = fopen(filename.c_str(), "r");
	if (file != NULL) {
	    // We've been asked to overwrite a file
	    char line[80];
	    if (fgets(line, sizeof(line) - 1, file) == NULL) {
		errmsg = "File " + filename + " exists, but an error occured when trying to check that it was OK to overwrite it\n";
		errmsg += "File was NOT overwritten\n";
		fclose(file);
		seteuid(orig_uid);
		setegid(orig_gid);
		umask(orig_mask);
		return false;
	    }
	    if (strncmp(line, "/*XORP", 6) != 0) {
		errmsg = "File " + filename + " exists, but it is not an existing XORP config file.\n";
		errmsg += "File was NOT overwritten\n";
		fclose(file);
		seteuid(orig_uid);
		setegid(orig_gid);
		umask(orig_mask);
		return false;
	    }
	    fclose(file);
	}
	// It seems OK to overwrite this file
	if (unlink(filename.c_str()) < 0) {
	    errmsg = "File " + filename + " exists, and can not be overwritten.\n";
	    errmsg += strerror(errno);
	    fclose(file);
	    seteuid(orig_uid);
	    setegid(orig_gid);
	    umask(orig_mask);
	    return false;
	}
    }

    file = fopen(filename.c_str(), "w");
    if (file == NULL) {
	errmsg = "Could not create file \"" + filename + "\"";
	errmsg += strerror(errno);
	seteuid(orig_uid);
	setegid(orig_gid);
	umask(orig_mask);
	return false;
    }

    // Write the file header
    string header = "/*XORP Configuration File, v1.0*/\n";
    size_t bytes;
    bytes = fwrite(header.c_str(), 1, header.size(), file);
    if (bytes < header.size()) {
	fclose(file);
	errmsg = "Error writing to file \"" + filename + "\"\n";
	// We couldn't even write the header - clean up if we can
	if (unlink(filename.c_str()) == 0) {
	    errmsg += "Save aborted; truncated file has been removed\n";
	} else {
	    errmsg += "Save aborted; truncated file may exist\n";
	}
	seteuid(orig_uid);
	setegid(orig_gid);
	umask(orig_mask);
	return false;
    }

    // Write the config to the file
    string config = show_unannotated_tree();
    bytes = fwrite(config.c_str(), 1, config.size(), file);
    if (bytes < config.size()) {
	fclose(file);
	errmsg = "Error writing to file \"" + filename + "\"\n";
	errmsg += strerror(errno);
	errmsg += "\n";
	//we couldn't write the config - clean up if we can
	if (unlink(filename.c_str()) == 0) {
	    errmsg += "Save aborted; truncated file has been removed\n";
	} else {
	    errmsg += "Save aborted; truncated file may exist\n";
	}
	seteuid(orig_uid);
	setegid(orig_gid);
	umask(orig_mask);
	return false;
    }

    // Set file group correctly
    if (fchown(fileno(file), user_id, gid) < 0) {
	// This shouldn't be able to happen, but if it does, it
	// shouldn't be fatal.
	errmsg = "WARNING: failed to set saved file to be group \"xorp\"\n";
    }

    // Close properly and clean up
    if (fclose(file) != 0) {
	errmsg = "Error closing file \"" + filename + "\"\n";
	errmsg += strerror(errno);
	errmsg += "\nFile may not have been written correctly\n";
	seteuid(orig_uid);
	setegid(orig_gid);
	return false;
    }

    run_save_hook(user_id, save_hook, filename);

    errmsg += "Save complete\n";
    seteuid(orig_uid);
    setegid(orig_gid);
    umask(orig_mask);
    return true;
}

void
MasterConfigTree::run_save_hook(uid_t userid, const string& save_hook,
				const string& filename)
{
    if (save_hook.empty())
	return;
    debug_msg("run_save_hook: %s %s\n", save_hook.c_str(), filename.c_str());
    vector<string> argv;
    argv.reserve(2);
    argv.push_back(save_hook);
    argv.push_back(filename);
    _task_manager->shell_execute(userid, argv, 
           callback(this, &MasterConfigTree::save_hook_complete));
}

void
MasterConfigTree::save_hook_complete(bool success, const string errmsg) const
{
    if (success)
	debug_msg("save hook completed successfully\n");
    else
	XLOG_ERROR("save hook completed with error %s", errmsg.c_str());
}

bool
MasterConfigTree::remove_tmp_config_file(string& errmsg)
{
    string tmp_config_filename_value;

    if (root_node().expand_variable(RTRMGR_CONFIG_FILENAME_VARNAME,
				    tmp_config_filename_value)
	!= true) {
	errmsg = c_format("internal variable %s not found",
			  RTRMGR_CONFIG_FILENAME_VARNAME.c_str());
	errmsg = c_format("Failed to remove temporary configuration file: %s",
			  errmsg.c_str());
	return false;
    }

    // Unlink the file
    if (! tmp_config_filename_value.empty())
	unlink(tmp_config_filename_value.c_str());

    return true;
}

bool
MasterConfigTree::save_config(const string& filename, uid_t user_id,
			      const string& save_hook, string& errmsg,
			      ConfigSaveCallBack cb)
{
    string tmp_config_filename;
    string save_file_config;

    XLOG_TRACE(_verbose, "Saving to file %s", filename.c_str());

    //
    // Create the string that is used to temporary add a save file to the
    // configuration.
    //
    save_file_config = c_format(RTRMGR_SAVE_FILE_CONFIG.c_str(),
				filename.c_str());

    //
    // Test whether the file name matches an entry in the template tree.
    // If yes, then add a save file to the configuration so the appropriate
    // external program will be invoked.
    // Otherwise, assume that the user tries to save to a local file.
    //
    do {
	MasterConfigTree new_tree(_template_tree, _verbose);
	if (new_tree.parse(save_file_config, "", errmsg) == true) {
	    //
	    // The file name is recognizable by the parser, hence
	    // invoke the corresponding external methods for processing.
	    //
	    break;
	}

	//
	// Assume that the user tries to save to a local file
	//
	if (save_to_file(filename, user_id, save_hook, errmsg) != true) {
	    XLOG_TRACE(_verbose, "Failed to save file %s: %s",
		       filename.c_str(), errmsg.c_str());
	    return false;
	}

	XLOG_TRACE(_verbose, "Saved file %s", filename.c_str());

	//
	// Schedule a timer to dispatch immediately the callback
	//
	EventLoop& eventloop = xorp_client().eventloop();
	_save_config_completed_timer = eventloop.new_oneoff_after(
	    TimeVal::ZERO(),
	    callback(this,
		     &MasterConfigTree::save_config_done_cb,
		     true,		// success
		     string(""),	// errmsg
		     cb));
	return true;
    } while (false);

    //
    // Create a temporary file that would be used by the external
    // program to copy the configuration from.
    //
    do {
	xsock_t s;

	s = xorp_make_temporary_file("", "xorp_rtrmgr_tmp_config_file",
				     tmp_config_filename, errmsg);
	if (s == XORP_BAD_SOCKET) {
	    errmsg = c_format("Cannot save the configuration file: "
			      "cannot create a temporary filename: %s",
			      errmsg.c_str());
	    return false;
	}

	//
	// Save the current configuration
	//
	string config = show_unannotated_tree();
	if (write(s, config.c_str(), config.size())
	    != static_cast<ssize_t>(config.size())) {
	    errmsg = c_format("Cannot save the configuration file: "
			      "error writing to a temporary file: %s",
			      strerror(errno));
	    // Close and unlink the file
	    close(s);
	    unlink(tmp_config_filename.c_str());
	    return false;
	}

	//
	// Close the file descriptor, because we don't need it.
	// Note, that the created file remains on the filesystem,
	// so it is guaranteed to be unique when we need to use it again.
	//
	close(s);
	break;
    } while (false);

    //
    // Check whether the current configuration already contains
    // rtrmgr configuration.
    //
    list<string> path;
    path.push_back(RTRMGR_CONFIG_NODE);
    if (find_node(path) != NULL) {
	_rtrmgr_config_node_found = true;
    } else {
	_rtrmgr_config_node_found = false;
    }

    //
    // Add the temporary save file to the configuration
    //
    if (apply_deltas(user_id, save_file_config,
		     /* provisional change */ true, errmsg)
	!= true) {
	errmsg = c_format("Cannot save the configuration file: %s",
			  errmsg.c_str());
	discard_changes();
	unlink(tmp_config_filename.c_str());
	return false;
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    add_default_children();

    // Save the name of the temporary file in the local variable
    if (root_node().set_variable(RTRMGR_CONFIG_FILENAME_VARNAME,
				 tmp_config_filename.c_str())
	!= true) {
	errmsg = c_format("Cannot save the configuration file: cannot store "
			  "temporary filename %s in internal variable %s",
			  tmp_config_filename.c_str(),
			  RTRMGR_CONFIG_FILENAME_VARNAME.c_str());
	unlink(tmp_config_filename.c_str());
	return false;
    }

    //
    // Specify the callback that will be invoked after we have saved the new
    // configuration file so we can continue from there.
    //
    CallBack save_cb;
    save_cb = callback(this, &MasterConfigTree::save_config_file_sent_cb,
		       filename, user_id, cb);
    commit_changes_pass1(save_cb);
    if (config_failed()) {
	errmsg = config_failed_msg();
	discard_changes();
	unlink(tmp_config_filename.c_str());
	return false;
    }

    return true;
}

void
MasterConfigTree::save_config_file_sent_cb(bool success,
					   string errmsg,
					   string filename,
					   uid_t user_id,
					   ConfigSaveCallBack cb)
{
    string dummy_errmsg;

    //
    // Remove the temporary file with the configuration
    //
    remove_tmp_config_file(dummy_errmsg);

    if (! success) {
	XLOG_TRACE(_verbose, "Failed to save file %s: %s",
		   filename.c_str(), errmsg.c_str());
	discard_changes();
	cb->dispatch(success, errmsg);
	return;
    }

    //
    // Check everything really worked, and finalize the commit when
    // the file was saved.
    //
    if (check_commit_status(errmsg) == false) {
	XLOG_TRACE(_verbose, "Check commit status indicates failure: %s",
		   errmsg.c_str());
	discard_changes();
	cb->dispatch(false, errmsg);
	return;
    }

    XLOG_TRACE(_verbose, "Saved file %s", filename.c_str());

    //
    // Create the string that is used to delete the temporary added
    // save file from the configuration.
    //
    string delete_save_file_config;
    if (_rtrmgr_config_node_found) {
	delete_save_file_config = c_format(RTRMGR_SAVE_FILE_CONFIG.c_str(),
					   filename.c_str());
    } else {
	delete_save_file_config = RTRMGR_CONFIG;
    }
    if (apply_deletions(user_id, delete_save_file_config,
			/* provisional change */ true, errmsg)
	!= true) {
	errmsg = c_format("Cannot save the configuration file because of "
			  "internal error: %s", errmsg.c_str());
	discard_changes();
	cb->dispatch(false, errmsg);
	return;
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    add_default_children();

    //
    // Specify the callback that will be invoked after we have cleaned-up
    // so we can continue from there.
    //
    bool orig_success = success;
    string orig_errmsg = errmsg;
    CallBack cleanup_cb;
    cleanup_cb = callback(this, &MasterConfigTree::save_config_file_cleanup_cb,
			  orig_success, orig_errmsg, filename, user_id, cb);
    commit_changes_pass1(cleanup_cb);
    if (config_failed()) {
	errmsg = config_failed_msg();
	discard_changes();
	cb->dispatch(false, errmsg);
	return;
    }
}

void
MasterConfigTree::save_config_file_cleanup_cb(bool success,
					      string errmsg,
					      bool orig_success,
					      string orig_errmsg,
					      string filename,
					      uid_t user_id,
					      ConfigSaveCallBack cb)
{
    if (! orig_success) {
	XLOG_TRACE(_verbose, "Failed to save file %s: %s",
		   filename.c_str(), orig_errmsg.c_str());
	discard_changes();
	cb->dispatch(orig_success, orig_errmsg);
	return;
    }

    if (! success) {
	XLOG_TRACE(_verbose, "Failed to save file %s: %s",
		   filename.c_str(), errmsg.c_str());
	discard_changes();
	cb->dispatch(success, errmsg);
	return;
    }

    //
    // Check everything really worked, and finalize the commit when
    // we cleaned up the file that was temporarily added.
    //
    if (check_commit_status(errmsg) == false) {
	XLOG_TRACE(_verbose, "Check commit status indicates failure: %s",
		   errmsg.c_str());
	discard_changes();
	cb->dispatch(false, errmsg);
	return;
    }

    XLOG_TRACE(_verbose, "Cleanup completed after saving file %s",
	       filename.c_str());

    save_config_done_cb(success, errmsg, cb);

    UNUSED(user_id);
}

void
MasterConfigTree::save_config_done_cb(bool success, string errmsg,
				      ConfigSaveCallBack cb)
{
    cb->dispatch(success, errmsg);
}

bool
MasterConfigTree::load_config(const string& filename, uid_t user_id,
			      string& errmsg, ConfigLoadCallBack cb)
{
    string tmp_config_filename;

    XLOG_TRACE(_verbose, "Loading file %s", filename.c_str());

    //
    // Create the string that is used to temporary add a load file to the
    // configuration.
    //
    string load_file_config = c_format(RTRMGR_LOAD_FILE_CONFIG.c_str(),
				       filename.c_str());

    //
    // Test whether the file name matches an entry in the template tree.
    // If yes, then add a load file to the configuration so the appropriate
    // external program will be invoked.
    // Otherwise, assume that the user tries to load from a local file.
    //
    do {
	MasterConfigTree new_tree(_template_tree, _verbose);
	if (new_tree.parse(load_file_config, "", errmsg) == true) {
	    //
	    // The file name is recognizable by the parser, hence
	    // invoke the corresponding external methods for processing.
	    //
	    break;
	}

	//
	// Assume that the user tries to load from a local file
	//
	string deltas, deletions;
	if (load_from_file(filename, user_id, errmsg, deltas, deletions)
	    != true) {
	    XLOG_TRACE(_verbose, "Failed to load file %s: %s",
		       filename.c_str(), errmsg.c_str());
	    return false;
	}

	XLOG_TRACE(_verbose, "Loading file %s", filename.c_str());

	//
	// Commit the changes
	//
	CallBack cb2 = callback(this,
				&MasterConfigTree::load_config_commit_changes_cb,
				deltas, deletions, cb);
	commit_changes_pass1(cb2);
	if (config_failed()) {
	    errmsg = config_failed_msg();
	    discard_changes();
	    return false;
	}

	return true;
    } while (false);

    //
    // Create a temporary file that would be used by the external
    // program to copy the configuration to.
    //
    do {
	xsock_t s;

	s = xorp_make_temporary_file("", "xorp_rtrmgr_tmp_config_file",
				     tmp_config_filename, errmsg);
	if (s == XORP_BAD_SOCKET) {
	    errmsg = c_format("Cannot load the configuration file: "
			      "cannot create a temporary filename: %s",
			      errmsg.c_str());
	    return false;
	}

	//
	// Close the file descriptor, because we don't need it.
	// Note, that the created file remains on the filesystem,
	// so it is guaranteed to be unique when we need to use it again.
	//
	close(s);
	break;
    } while (false);

    //
    // Check whether the current configuration already contains
    // rtrmgr configuration.
    //
    list<string> path;
    path.push_back(RTRMGR_CONFIG_NODE);
    if (find_node(path) != NULL) {
	_rtrmgr_config_node_found = true;
    } else {
	_rtrmgr_config_node_found = false;
    }

    //
    // Add the temporary load file to the configuration
    //
    if (apply_deltas(user_id, load_file_config,
		     /* provisional change */ true, errmsg)
	!= true) {
	errmsg = c_format("Cannot load the configuration file: %s",
			  errmsg.c_str());
	discard_changes();
	unlink(tmp_config_filename.c_str());
	return false;
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    add_default_children();

    // Save the name of the temporary file in the local variable
    if (root_node().set_variable(RTRMGR_CONFIG_FILENAME_VARNAME,
				 tmp_config_filename.c_str())
	!= true) {
	errmsg = c_format("Cannot load the configuration file: cannot store "
			  "temporary filename %s in internal variable %s",
			  tmp_config_filename.c_str(),
			  RTRMGR_CONFIG_FILENAME_VARNAME.c_str());
	unlink(tmp_config_filename.c_str());
	return false;
    }

    //
    // Specify the callback that will be invoked after we have read the new
    // configuration file so we can continue from there.
    //
    CallBack load_cb;
    load_cb = callback(this, &MasterConfigTree::load_config_file_received_cb,
		       filename, user_id, cb);
    commit_changes_pass1(load_cb);
    if (config_failed()) {
	errmsg = config_failed_msg();
	discard_changes();
	unlink(tmp_config_filename.c_str());
	return false;
    }

    return true;
}

void
MasterConfigTree::load_config_file_received_cb(bool success,
					       string errmsg,
					       string filename,
					       uid_t user_id,
					       ConfigLoadCallBack cb)
{
    string dummy_errmsg, dummy_deltas, dummy_deletions;

    if (! success) {
	XLOG_TRACE(_verbose, "Failed to load file %s: %s",
		   filename.c_str(), errmsg.c_str());
	remove_tmp_config_file(dummy_errmsg);
	discard_changes();
	cb->dispatch(success, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    //
    // Check everything really worked, and finalize the commit when
    // the file was loaded.
    //
    if (check_commit_status(errmsg) == false) {
	XLOG_TRACE(_verbose, "Check commit status indicates failure: %s",
		   errmsg.c_str());
	remove_tmp_config_file(dummy_errmsg);
	discard_changes();
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    XLOG_TRACE(_verbose, "Received file %s", filename.c_str());

    //
    // Get the string with the name of the temporary file with the
    // new configuration.
    //
    string tmp_config_filename;
    if (root_node().expand_variable(RTRMGR_CONFIG_FILENAME_VARNAME,
				    tmp_config_filename)
	!= true) {
	success = false;
	errmsg = c_format("internal variable %s not found",
			  RTRMGR_CONFIG_FILENAME_VARNAME.c_str());
	XLOG_TRACE(_verbose, "Failed to load file %s: %s",
		   filename.c_str(), errmsg.c_str());
	errmsg = c_format("Cannot load configuration file %s because of "
			  "internal error: %s",
			  filename.c_str(), errmsg.c_str());
	remove_tmp_config_file(dummy_errmsg);
	discard_changes();
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    //
    // Read the configuration from the temporary file
    //
    string rtrmgr_config_value;
    if (read_file(rtrmgr_config_value, tmp_config_filename, errmsg) != true) {
	success = false;
	XLOG_TRACE(_verbose, "Failed to load file %s: %s",
		   filename.c_str(), errmsg.c_str());
	errmsg = c_format("Cannot load configuration file %s because cannot "
			  "read temporary file %s with the configuration: %s",
			  filename.c_str(), tmp_config_filename.c_str(),
			  errmsg.c_str());
	remove_tmp_config_file(dummy_errmsg);
	discard_changes();
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    //
    // Remove the temporary file with the configuration
    //
    remove_tmp_config_file(dummy_errmsg);

    //
    // Create the string that is used to delete the temporary added
    // load file from the configuration.
    //
    string delete_load_file_config;
    if (_rtrmgr_config_node_found) {
	delete_load_file_config = c_format(RTRMGR_LOAD_FILE_CONFIG.c_str(),
					   filename.c_str());
    } else {
	delete_load_file_config = RTRMGR_CONFIG;
    }
    if (apply_deletions(user_id, delete_load_file_config,
			/* provisional change */ true, errmsg)
	!= true) {
	errmsg = c_format("Cannot load the configuration file because of "
			  "internal error: %s", errmsg.c_str());
	discard_changes();
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    add_default_children();

    //
    // Specify the callback that will be invoked after we have cleaned-up
    // so we can continue from there.
    //
    bool orig_success = success;
    string orig_errmsg = errmsg;
    CallBack cleanup_cb;
    cleanup_cb = callback(this, &MasterConfigTree::load_config_file_cleanup_cb,
			  orig_success, orig_errmsg, rtrmgr_config_value,
			  filename, user_id, cb);
    commit_changes_pass1(cleanup_cb);
    if (config_failed()) {
	errmsg = config_failed_msg();
	discard_changes();
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }
}

void
MasterConfigTree::load_config_file_cleanup_cb(bool success,
					      string errmsg,
					      bool orig_success,
					      string orig_errmsg,
					      string rtrmgr_config_value,
					      string filename,
					      uid_t user_id,
					      ConfigLoadCallBack cb)
{
    string deltas, deletions;
    string dummy_deltas, dummy_deletions;

    if (! orig_success) {
	XLOG_TRACE(_verbose, "Failed to load file %s: %s",
		   filename.c_str(), orig_errmsg.c_str());
	discard_changes();
	cb->dispatch(orig_success, orig_errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    if (! success) {
	XLOG_TRACE(_verbose, "Failed to load file %s: %s",
		   filename.c_str(), errmsg.c_str());
	discard_changes();
	cb->dispatch(success, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    //
    // Check everything really worked, and finalize the commit when
    // we cleaned up the file that was temporarily added.
    //
    if (check_commit_status(errmsg) == false) {
	XLOG_TRACE(_verbose, "Check commit status indicates failure: %s",
		   errmsg.c_str());
	discard_changes();
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    XLOG_TRACE(_verbose, "Cleanup completed after receiving file %s",
	       filename.c_str());

    //
    // Test out parsing the config on a new config tree to detect any
    // parse errors before we reconfigure ourselves with the new config.
    //
    MasterConfigTree new_tree(_template_tree, _verbose);
    if (new_tree.parse(rtrmgr_config_value, filename, errmsg) != true) {
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    new_tree.add_default_children();

    //
    // Ok, so the new config parses.  Now we need to figure out how it
    // differs from the existing config so we don't need to modify
    // anything that hasn't changed.
    //
    MasterConfigTree delta_tree(_template_tree, _verbose);
    MasterConfigTree deletion_tree(_template_tree, _verbose);
    diff_configs(new_tree, delta_tree, deletion_tree);

    string response;
    if (! root_node().merge_deltas(user_id, delta_tree.const_root_node(),
				  /* provisional change */ true, response)) {
	discard_changes();
	errmsg = response;
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }
    if (! root_node().merge_deletions(user_id, deletion_tree.const_root_node(),
				      /* provisional change */ true,
				      response)) {
	discard_changes();
	errmsg = response;
	cb->dispatch(false, errmsg, dummy_deltas, dummy_deletions);
	return;
    }

    // Pass these back out so we can notify other users of the change
    deltas = delta_tree.show_unannotated_tree();
    deletions = deletion_tree.show_unannotated_tree();

    //
    // Commit the changes
    //
    CallBack cb2 = callback(this,
			    &MasterConfigTree::load_config_commit_changes_cb,
			    deltas, deletions, cb);
    commit_changes_pass1(cb2);
    if (config_failed()) {
	errmsg = config_failed_msg();
	discard_changes();
	cb->dispatch(false, errmsg, deltas, deletions);
	return;
    }
}

void
MasterConfigTree::load_config_commit_changes_cb(bool success,
						string errmsg,
						string deltas,
						string deletions,
						ConfigLoadCallBack cb)
{
    cb->dispatch(success, errmsg, deltas, deletions);
}

bool
MasterConfigTree::load_from_file(const string& filename, uid_t user_id,
				 string& errmsg, string& deltas,
				 string& deletions)
{
    //
    // We run load_from_file as the UID of the user making the request
    // and as group xorp.  This prevents users using the rtrmgr to
    // attempt to load files they wouldn't normally have had the
    // permission to read.  Otherwise it's possible that they could
    // load configs they shouldn't have access to, or get parts of
    // protected files reported to them in error messages.
    //

    // Set the effective group to "xorp"
    struct group *grp = getgrnam("xorp");
    if (grp == NULL) {
	errmsg = "ERROR: config files are saved as group \"xorp\".";
	errmsg = "Group \"xorp\" does not exist on this system.\n";
	return false;
    }
    gid_t gid, orig_gid;
    orig_gid = getgid();
    gid = grp->gr_gid;
    if (setegid(gid) < 0) {
	errmsg = c_format("Failed to seteuid to group \"xorp\", gid %u\n",
			  XORP_UINT_CAST(gid));
	return false;
    }

    // Set effective user ID to the user_id of the user that requested the load
    uid_t orig_uid;
    orig_uid = getuid();
    if (seteuid(user_id) < 0) {
	errmsg = c_format("Failed to seteuid to user_id %u\n",
			  XORP_UINT_CAST(user_id));
	setegid(orig_gid);
	return false;
    }

    string configuration;
    if (! read_file(configuration, filename, errmsg)) {
	seteuid(orig_uid);
	setegid(orig_gid);
	return false;
    }

    // Revert UID and GID now we've done reading the file
    seteuid(orig_uid);
    setegid(orig_gid);

    //
    // Test out parsing the config on a new config tree to detect any
    // parse errors before we reconfigure ourselves with the new config.
    //
    MasterConfigTree new_tree(_template_tree, _verbose);
    if (new_tree.parse(configuration, filename, errmsg) != true) {
	return false;
    }

    //
    // Go through the config tree, and create nodes for any defaults
    // specified in the template tree that aren't already configured.
    //
    new_tree.add_default_children();
    

    //
    // Ok, so the new config parses.  Now we need to figure out how it
    // differs from the existing config so we don't need to modify
    // anything that hasn't changed.
    //
    MasterConfigTree delta_tree(_template_tree, _verbose);
    MasterConfigTree deletion_tree(_template_tree, _verbose);
    diff_configs(new_tree, delta_tree, deletion_tree);

    string response;
    if (! root_node().merge_deltas(user_id, delta_tree.const_root_node(),
				  /* provisional change */ true, response)) {
	discard_changes();
	errmsg = response;
	return false;
    }
    if (! root_node().merge_deletions(user_id, deletion_tree.const_root_node(),
				     /* provisional change */ true,
				     response)) {
	discard_changes();
	errmsg = response;
	return false;
    }

    // Pass these back out so we can notify other users of the change
    deltas = delta_tree.show_unannotated_tree();
    deletions = deletion_tree.show_unannotated_tree();

    //
    // The config is loaded.  We haven't yet committed it, but that
    // happens elsewhere so we can handle the callbacks correctly.
    //
    return true;
}

void
MasterConfigTree::diff_configs(const MasterConfigTree& new_tree,
			       MasterConfigTree& delta_tree,
			       MasterConfigTree& deletion_tree)
{
    //
    // Clone the existing config tree into the delta tree.
    // Clone the new config tree into the delta tree.
    //
    deletion_tree = *((MasterConfigTree*)(this));
    delta_tree = new_tree;

    deletion_tree.retain_different_nodes(new_tree, false);
    delta_tree.retain_different_nodes(*((ConfigTree*)(this)), true);

    debug_msg("=========================================================\n");
    debug_msg("ORIG:\n");
    debug_msg("%s", tree_str().c_str());
    debug_msg("=========================================================\n");
    debug_msg("NEW:\n");
    debug_msg("%s", new_tree.tree_str().c_str());
    debug_msg("=========================================================\n");
    debug_msg("=========================================================\n");
    debug_msg("DELTAS:\n");
    debug_msg("%s", delta_tree.tree_str().c_str());
    debug_msg("=========================================================\n");
    debug_msg("DELETIONS:\n");
    debug_msg("%s", deletion_tree.tree_str().c_str());
    debug_msg("=========================================================\n");

}

bool
MasterConfigTree::module_config_start(const string& module_name,
				      string& result)
{
    ModuleCommand *cmd = _template_tree->find_module(module_name);

    if (cmd == NULL) {
	result = "Module " + module_name + " is not registered with the TemplateTree, but is needed to satisfy a dependency\n";
	return false;
    }
    _task_manager->add_module(*cmd);
    return true;
}

#if 0	// TODO
bool
MasterConfigTree::module_shutdown(const string& module_name,
				  string& result)
{
    ModuleCommand *cmd = _template_tree->find_module(module_name);

    if (cmd == NULL) {
	result = "Module " + module_name + " is not registered with the TemplateTree, but is needed to satisfy a dependency\n";
	return false;
    }
    _task_manager->shutdown_module(*cmd);
    return true;
}

#endif // 0
