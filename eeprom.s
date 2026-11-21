; C-callable glue for the Atari cartridge EEPROM library (ee93c46.s).
; 93C46-class part: 64 x 16-bit words, present on retail carts, emulated
; by BigPEmu and the RetroHQ Jaguar Game Drive.
;
;   int eeprom_read(int addr);             returns the 16-bit word
;   int eeprom_write(int addr, int val);   0 = verified ok, nonzero = fail

		.globl	_eeprom_read
		.globl	_eeprom_write
		.extern	eeReadWord
		.extern	eeWriteWord

		.text

_eeprom_read:
		move.l	4(sp),d1
		bsr	eeReadWord
		and.l	#$FFFF,d0
		rts

_eeprom_write:
		move.l	4(sp),d1
		move.l	8(sp),d0
		bsr	eeWriteWord
		and.l	#$FFFF,d0
		rts
