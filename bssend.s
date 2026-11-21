; BSS end marker — MUST stay the last object on the link line. startup.s
; zeroes [listbuf, _bss_end_marker) at boot: the COF load path does not
; clear BSS.

		.globl	_bss_end_marker

		.bss

_bss_end_marker:
		.ds.l	1

		.end
