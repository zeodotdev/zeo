/*
 * Bridge protocol constants for JS↔C++ communication.
 *
 * All action names for messages from JavaScript to C++, and
 * JS function names for updates pushed from C++ to JavaScript.
 */

#ifndef BRIDGE_PROTOCOL_H
#define BRIDGE_PROTOCOL_H

namespace BridgeAction
{
    // Chat input actions (from input area)
    constexpr const char* SUBMIT             = "submit";
    constexpr const char* ATTACH_CLICK       = "attach_click";
    constexpr const char* TEXT_CHANGED        = "text_changed";
    constexpr const char* INPUT_RESIZE       = "input_resize";
    constexpr const char* EDIT_QUEUED        = "edit_queued";

    // Chat display actions (from chat area)
    constexpr const char* LINK_CLICK         = "link_click";
    constexpr const char* COPY               = "copy";
    constexpr const char* COPY_IMAGE         = "copy_image";
    constexpr const char* PREVIEW_IMAGE      = "preview_image";
    constexpr const char* PREVIEW_FILE       = "preview_file";
    constexpr const char* THINKING_TOGGLED   = "thinking_toggled";
    constexpr const char* TOOLRESULT_TOGGLED = "toolresult_toggled";
    constexpr const char* SCROLL_ACTIVITY    = "scroll_activity";

    // Top bar actions
    constexpr const char* NEW_CHAT           = "new_chat";
    constexpr const char* HISTORY_OPEN       = "history_open";
    constexpr const char* HISTORY_SELECT     = "history_select";
    constexpr const char* HISTORY_SEARCH     = "history_search";
    constexpr const char* HISTORY_CLOSE      = "history_close";

    // Control row actions
    constexpr const char* MODEL_CHANGE       = "model_change";
    constexpr const char* SEND_CLICK         = "send_click";
    constexpr const char* STOP_CLICK         = "stop_click";
    constexpr const char* SELECTION_PILL_CLICK = "selection_pill_click";
    constexpr const char* PLAN_TOGGLE          = "plan_toggle";
    constexpr const char* PLAN_APPROVE         = "plan_approve";

    // Pending changes actions
    constexpr const char* PENDING_CHANGES_TOGGLE       = "pending_changes_toggle";
    constexpr const char* PENDING_CHANGES_ACCEPT_ALL   = "pending_changes_accept_all";
    constexpr const char* PENDING_CHANGES_REJECT_ALL   = "pending_changes_reject_all";
    constexpr const char* PENDING_CHANGES_VIEW         = "pending_changes_view";
    constexpr const char* PENDING_CHANGES_ACCEPT_SHEET = "pending_changes_accept_sheet";
    constexpr const char* PENDING_CHANGES_REJECT_SHEET = "pending_changes_reject_sheet";

    // Auth actions
    constexpr const char* SIGN_IN_CLICK      = "sign_in_click";

    // Lifecycle
    constexpr const char* PAGE_READY         = "page_ready";

    // Debug
    constexpr const char* DEBUG              = "debug";
}

#endif // BRIDGE_PROTOCOL_H
