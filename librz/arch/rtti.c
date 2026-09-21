// SPDX-FileCopyrightText: 2009-2018 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2009-2018 maijin <maijin21@gmail.com>
// SPDX-FileCopyrightText: 2009-2018 thestr4ng3r <info@florianmaerkl.de>
// SPDX-License-Identifier: LGPL-3.0-only

#include "analysis_private.h"

RZ_API char *rz_analysis_rtti_demangle_class_name(RzAnalysis *analysis, const char *name) {
	RVTableContext context;
	rz_analysis_vtable_begin(analysis, &context);
	if (context.abi == RZ_ANALYSIS_CPP_ABI_MSVC) {
		return rz_analysis_rtti_msvc_demangle_class_name(&context, name);
	}
	return rz_analysis_rtti_itanium_demangle_class_name(&context, name);
}

RZ_API void rz_analysis_rtti_print_at_vtable(RzAnalysis *analysis, ut64 addr, RzOutputMode mode, RZ_NONNULL RzStrBuf *sb) {
	bool use_json = mode == RZ_OUTPUT_MODE_JSON;
	if (use_json) {
		rz_strbuf_append(sb, "[");
	}

	RVTableContext context;
	rz_analysis_vtable_begin(analysis, &context);
	if (context.abi == RZ_ANALYSIS_CPP_ABI_MSVC) {
		rz_analysis_rtti_msvc_print_at_vtable(&context, addr, mode, false, sb);
	} else {
		rz_analysis_rtti_itanium_print_at_vtable(&context, addr, mode, sb);
	}

	if (use_json) {
		rz_strbuf_append(sb, "]\n");
	}
}

RZ_API void rz_analysis_rtti_print_all(RzAnalysis *analysis, RzOutputMode mode, RZ_NONNULL RzStrBuf *sb) {
	RVTableContext context;
	rz_analysis_vtable_begin(analysis, &context);

	bool use_json = mode == RZ_OUTPUT_MODE_JSON;
	if (use_json) {
		rz_strbuf_append(sb, "[");
	}

	rz_interrupt_break_push(analysis->intr, NULL, NULL);
	RzList *vtables = rz_analysis_vtable_search(&context);
	RzListIter *vtableIter;
	RVTableInfo *table;

	if (vtables) {
		bool comma = false;
		bool success = false;
		rz_list_foreach (vtables, vtableIter, table) {
			if (rz_interrupt_is_breaked(analysis->intr)) {
				break;
			}
			if (use_json && success) {
				rz_strbuf_append(sb, ",");
				comma = true;
			}
			if (context.abi == RZ_ANALYSIS_CPP_ABI_MSVC) {
				success = rz_analysis_rtti_msvc_print_at_vtable(&context, table->saddr, mode, true, sb);
			} else {
				success = rz_analysis_rtti_itanium_print_at_vtable(&context, table->saddr, mode, sb);
			}
			if (success) {
				comma = false;
				if (!use_json) {
					rz_strbuf_append(sb, "\n");
				}
			}
		}
		if (use_json && !success && comma && rz_strbuf_length(sb) > 0) {
			// drop last comma if necessary
			rz_strbuf_slice(sb, 0, rz_strbuf_length(sb) - 1);
		}
	}
	rz_list_free(vtables);

	if (use_json) {
		rz_strbuf_append(sb, "]\n");
	}

	rz_interrupt_break_pop(analysis->intr);
}

RZ_API void rz_analysis_rtti_recover_all(RzAnalysis *analysis) {
	RzBinObject *bin_obj = rz_bin_cur_object(analysis->binb.bin);
	if (!bin_obj) {
		return;
	}
	switch (bin_obj->lang) {
	case RZ_BIN_LANGUAGE_SWIFT:
		rz_analysis_rtti_swift(analysis);
		break;
	case RZ_BIN_LANGUAGE_OBJC:
		rz_analysis_rtti_objc(analysis);
		// fallthrough
	default: {
		RVTableContext context;
		rz_analysis_vtable_begin(analysis, &context);

		rz_interrupt_break_push(analysis->intr, NULL, NULL);
		RzList *vtables = rz_analysis_vtable_search(&context);
		if (vtables) {
			if (context.abi == RZ_ANALYSIS_CPP_ABI_MSVC) {
				rz_analysis_rtti_msvc_recover_all(&context, vtables);
			} else {
				rz_analysis_rtti_itanium_recover_all(&context, vtables);
			}
			rz_analysis_no_rtti_analysis(&context, vtables);
		}
		rz_list_free(vtables);
		rz_interrupt_break_pop(analysis->intr);
	}
	}
}
