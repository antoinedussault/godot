/**************************************************************************/
/*  editor_fs_trace.h                                                     */
/**************************************************************************/
/* Temporary instrumentation for diagnosing FileSystem / save latency.    */
/* Enable at runtime with TUNA_FS_TRACE=1. Follows the same convention  */
/* as TUNA_CELL_TRACE in modules/cell_system.                           */
/**************************************************************************/

#ifndef EDITOR_FS_TRACE_H
#define EDITOR_FS_TRACE_H

#include "core/os/os.h"
#include "core/print_string.h"
#include "core/ustring.h"

class FSTraceScope {
	const char *label;
	uint64_t start;

public:
	static bool is_enabled() {
		static int enabled = -1;
		if (enabled < 0) {
			enabled = (OS::get_singleton() && OS::get_singleton()->get_environment("TUNA_FS_TRACE") == "1") ? 1 : 0;
		}
		return enabled == 1;
	}

	FSTraceScope(const char *p_label) :
			label(p_label), start(0) {
		if (is_enabled()) {
			start = OS::get_singleton()->get_ticks_usec();
		}
	}

	~FSTraceScope() {
		if (start) {
			double ms = (OS::get_singleton()->get_ticks_usec() - start) / 1000.0;
			print_line(String("[fs-trace] ") + label + ": " + rtos(ms) + " ms");
		}
	}
};

#define _FS_TRACE_CAT2(a, b) a##b
#define _FS_TRACE_CAT(a, b) _FS_TRACE_CAT2(a, b)
#define FS_TRACE(m_label) FSTraceScope _FS_TRACE_CAT(_fs_trace_scope_, __LINE__)(m_label)

#endif // EDITOR_FS_TRACE_H
