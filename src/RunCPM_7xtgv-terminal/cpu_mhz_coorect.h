#ifndef XTGV_CPU_MHZ_COORECT_H
#define XTGV_CPU_MHZ_COORECT_H

#include <stdint.h>

// 7xtgv corrected copy of cpu_mhz.h; original retained unchanged.
// The original table assigns 7 T-states to LD A,B / LD A,D and HALT,
// which actually take 4. Its loop estimate is consequently inflated.
// Use explicit timings for this fixed test instead of a general opcode table.
// Arduino: 13,015,509 T-states (original estimate: 14,517,012).
// Z80run already receives the ESP32 IRAM attribute through the HAL;
// this timing/output wrapper does not need its own IRAM placement.

/* Run a small Z80 code and measure the time to estimate emulated clock.
   This will load a small z80 code into RAM, run it until halt, then compute the
   estimated clock at which the CPU is running */
void xtgvZ80estimateClock(void) {
	const uint8 testCode[] = {
#ifdef ARDUINO
		0x11, 0xF4, 0x01, // LD DE, 500
		0x01, 0xE8, 0x03, // LD BC, 1000
#else
		0x11, 0xE8, 0x03, // LD DE, 1000
		0x01, 0x10, 0x27, // LD BC, 10000
#endif
		0x0B,             // DEC BC
		0x78,             // LD A, B
		0xB1,             // OR C
		0x20, 0xFB,       // JR NZ, -5
		0x1B,             // DEC DE
		0x7A,             // LD A, D
		0xB3,             // OR E
		0x20, 0xF3,       // JR NZ, -13
		0x76              // HALT
	};

	uint64 time_start = 0;
	uint64 time_now = 0;

	// Load test code into RAM at address 0x0000
	for (uint16 addr = 0; addr < sizeof(testCode); addr++) {
		PUT_BYTE(addr, testCode[addr]);
	}

	// Reset CPU
	Z80reset();

	// Start timing
	time_start = millis();

	// Run until HALT
	while (Status == STATUS_RUNNING) {
		Z80run(0);
	}

	// End timing
	time_now = millis();

	// Calculate total T-states executed
	const uint8 t_ld_de = 10;
	const uint8 t_ld_bc = 10;
	const uint8 t_dec = 6; // DEC BC / DEC DE
	const uint8 t_ld_a = 4; // LD A,B / LD A,D
	const uint8 t_or = 4; // OR C / OR E
	const uint8 t_jr = 12; // JR NZ taken; not taken is 7
	const uint8 t_halt = 4;

#ifdef ARDUINO
	uint16 outer_iters = 500;
	uint16 inner_iters = 1000;
#else
	uint16 outer_iters = 1000;
	uint16 inner_iters = 10000;
#endif

	uint64 inner_body = t_dec + t_ld_a + t_or + t_jr;
	uint64 inner_exit = t_dec + t_ld_a + t_or + 7; // JR not taken
	uint64 total_inner = t_ld_bc + (inner_iters - 1) * inner_body + inner_exit;

	uint64 outer_body = total_inner + t_dec + t_ld_a + t_or + t_jr;
	uint64 outer_exit = total_inner + t_dec + t_ld_a + t_or + 7; // JR not taken
	uint64 total_outer = t_ld_de + (outer_iters - 1) * outer_body + outer_exit;

	// Final T-states count (full count, not in millions)
    uint64 tstates = total_outer + t_halt;

    // Calculate elapsed time in milliseconds
    uint64 elapsedTime = time_now - time_start;

    // Estimate clock speed in Hz
    if (elapsedTime == 0) elapsedTime = 1; // Prevent division by zero
    uint64 estimatedHz = (tstates * 1000) / elapsedTime;

    // Convert to MHz
    uint32 estimatedMHz = (uint32)(estimatedHz / 1000000);
    char buffer[64];
    sprintf(buffer, "%llu T-states in %llu ms\r\n", tstates, elapsedTime);
    _puts(buffer);
    sprintf(buffer, "Estimated Z80 clock speed: %u MHz\r\n", estimatedMHz);
    _puts(buffer);

	// Reset CPU
	Z80reset();
}

#endif // XTGV_CPU_MHZ_COORECT_H
